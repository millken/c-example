#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>

#define MAX_LINE 1024
#ifndef EPOLLRDHUP
#define EPOLLRDHUP 0x2000
#endif
static struct config {
    char *path;
    char *file; 
    int port;
    int threads;
    bool dynamic;
} cfg;

typedef struct event_ptr {
    int loop_fd;
    int fd;
    char addr[16];
}event_ptr;

static struct epoll_event events[4096], ev;

static void usage() {
    printf("Usage: httpv <options> <url> \n"
           " Options: \n"
           " -p, --path <P> path of url \n"
           " -P, --port <P> port of host \n"
           " -t, --threads <N> Number of threads to use \n"
           " \n"
           " -f, --file <F> Load ip file \n"
           " -H, --header <H> Add header to request \n"
           " \n");
}
static int parse_args(struct config *cfg, char **headers, int argc, char* argv[])
{
    char **header = headers;
    int c;
    memset(cfg, 0, sizeof(struct config));
    cfg->path = "/";
    cfg->port = 80;
    cfg->file = NULL;
    cfg->dynamic = false;
    cfg->threads = 1000;

    while ((c = getopt(argc, argv, "f:P:p:t:H:d:h?")) != -1) {
        switch (c) {
        case 'f':
            cfg->file = optarg;
            break;
        case 'P': cfg->port = atoi(optarg); break;
        case 't': cfg->threads = atoi(optarg); break;        
        case 'p': cfg->path = optarg; break;
        case 'H':
            *header++ = optarg;
            break;
        case 'h':
        case '?':
        default:
            return -1;
        }
    }
    *header = NULL;
    return 0;
}

char *substr(char *haystack, char *begin, char *end)
{
   char *ret, *r;
   char *b = strstr(haystack, begin);
   if (b) {
    char *e = strstr(b, end);
      if(e) {
        int offset = e - b;
        int retlen = offset - strlen(begin);
          if ((ret = malloc(retlen + 1)) == NULL)
            return NULL;        
          strncpy(ret, b + strlen(begin), retlen);
        return ret;
      }
   }
   return NULL;
}

bool setnonblocking(int sockfd) {    
    int opts;    
   
   opts = fcntl(sockfd, F_GETFL);    
    if(opts < 0) {    
        perror("fcntl(F_GETFL)\n");    
        return false;    
    }    
    opts = (opts | O_NONBLOCK);    
    if(fcntl(sockfd, F_SETFL, opts) < 0) {    
        perror("fcntl(F_SETFL)\n");    
        return false;  
    }    
    return true;
}

static int connected( char *host)
{
    struct hostent *hp;
    struct sockaddr_in addr;

    // epoll mask that contain the list of epoll events attached to a network socket
    static struct epoll_event event;
    
    int sock;
    int on = 1;
    
    if((hp = gethostbyname(host)) == NULL) {
       fprintf(stderr,"[NetTools] Invalid server name: %s\n", host);
       return -1;
    }
    memset(&addr, 0, sizeof (addr));
    bcopy(hp->h_addr, &addr.sin_addr, hp->h_length);
    addr.sin_port = htons(cfg.port);
    addr.sin_family = AF_INET;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
      fprintf(stderr, "create socket fail");
      return -5;
    }
    // set socket to non blocking and allow port reuse
    if ( (setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(int)) ||
      fcntl(sock, F_SETFL, O_NONBLOCK)) == -1)
    {
      fprintf(stderr, "setsockopt || fcntl");
      return -2;
    }    
    int res = connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr));
    if (res < 0) {
        if (errno == EINPROGRESS) {
            return -7;
        }
        return -3;
    }
   return sock;    
}

int socket_check(int fd)
{
   int ret;
   int code;
   int len = sizeof(int);

   ret = getsockopt(fd, SOL_SOCKET, SO_ERROR, &code, &len);

   if ((ret || code)!= 0)
      return 1;

   return 0;
}
int se_create(int event_size)
{
    return epoll_create(event_size);
}

event_ptr *se_add(int loop_fd, int fd)
{
    event_ptr *ptr = malloc(sizeof(event_ptr));

    if(!ptr) {
        return ptr;
    }

    ptr->loop_fd = loop_fd;
    ptr->fd = fd;

    ev.data.ptr = ptr;
    ev.events = EPOLLPRI;

    int ret = epoll_ctl(loop_fd, EPOLL_CTL_ADD, fd, &ev);

    if(ret < 0) {
        free(ptr);
        ptr = NULL;
    }
    return ptr;
}

int se_delete(event_ptr *ptr)
{
    if(!ptr) {
        return -1;
    }

    if(epoll_ctl(ptr->loop_fd, EPOLL_CTL_DEL, ptr->fd, &ev) < 0) {
        return -1;
    }

    free(ptr);

    return 0;
}

int se_loop(int loop_fd, int waitout)
{
    int n = 0, i = 0;
    event_ptr *ptr = NULL;

    while(1) {

        n = epoll_wait(loop_fd, events, 4096, waitout);

        for(i = 0; i < n; i++) {
            ptr = events[i].data.ptr;

            if(events[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR)) {
                printf("%s\n", "EPOLLIN");

            } else if(events[i].events & (EPOLLOUT | EPOLLHUP | EPOLLERR)) {
                printf("%s\n", "EPOLLOUT");
            }
        }

        if(n == -1 && errno != EINTR) {
            break;
        }
    }

    return 0;
}

void main(int argc, char* argv[])
{
    char *url, **headers;
    int epfd;
    char header[1024];

    sprintf(header, "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", "/", "localhost");
    headers = malloc((argc / 2) * sizeof(char *));
    
    if (parse_args(&cfg, headers, argc, argv)) {
        usage();
        exit(1);
    }
    if (cfg.file) {
        FILE *fp;
        char buf[MAX_LINE];  /*缓冲区*/
        int n, len ;
        fp = fopen(cfg.file, "r");
        if (fp == NULL) {
            printf("file not exist :%s\n", cfg.file);
            exit(1);
        }
        n = 0;
        epfd = se_create(1024);
        while(fgets(buf, MAX_LINE, fp) != NULL) {
            len = strlen(buf);
            buf[len-1] = '\0'; /* 去掉换行符 */
              if(strlen(buf) >= 7)
              {
                int sockfd = connected(buf);
                if (sockfd > 0)
                {
                    event_ptr *ptr = se_add(epfd, sockfd);
                    strcpy(ptr->addr, buf);
                }
                fprintf (stderr, "create and connect : %s=%d=%d\n", buf, sockfd, socket_check(sockfd));
              }         
            ++n;
        }
        se_loop(epfd, 5);
    }
}
