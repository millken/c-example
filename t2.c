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
#include <signal.h>

#define MAX_LINE 1024
#ifndef EPOLLRDHUP
#define EPOLLRDHUP 0x2000
#endif

#ifdef DEBUG

void *
debug_malloc(size_t size, const char *file, int line, const char *func)
{
        void *p;

        p = malloc(size);
        printf("%s:%d:%s:malloc(%ld): p=0x%lx\n",
            file, line, func, size, (unsigned long)p);
        return p;
}

#define malloc(s) debug_malloc(s, __FILE__, __LINE__, __func__)
#define free(p)  do {                                                   \
        printf("%s:%d:%s:free(0x%lx)\n", __FILE__, __LINE__,            \
            __func__, (unsigned long)p);                                \
        free(p);                                                        \
} while (0)

#endif
static struct config {
    char *body;
    char *file; 
    char *servername;
    char *title;
    int port;
    int threads;
    bool dynamic;
} cfg;

typedef struct se_ptr_s se_ptr_t;
typedef int (*se_rw_proc_t)(se_ptr_t *ptr);

struct se_ptr_s {
    int loop_fd;
    int fd;
    se_rw_proc_t rfunc;
    se_rw_proc_t wfunc;    
    char addr[16];
};

static struct epoll_event events[4096], ev;


static void usage() {
    printf("Usage: httpv <options> \n"
           " Options: \n"
           " -b, --body <b> data for socket, default <'GET / HTTP/1.1\\r\\nHost: localhost\\r\\n\\r\\n'> \n"
           " -P, --port <P> port of host \n"
           " -t, --threads <N> Number of threads to use \n"
           " -f, --file <F> Load ip file \n"
           " -0, --title <T> match title \n"
           " -1, --servername <S> match server \n"
           " \n");
}
static int parse_args(struct config *cfg,  int argc, char* argv[])
{
    int c;
    memset(cfg, 0, sizeof(struct config));
    cfg->body = "GET / HTTP/1.1\r\nAccept-Encoding: deflate\r\nHost: www.baidu.com\r\n\r\n";
    cfg->port = 80;
    cfg->file = NULL;
    cfg->dynamic = false;
    cfg->threads = 1000;
    cfg->servername = NULL;
    cfg->title = NULL;

    while ((c = getopt(argc, argv, "0:1:f:P:b:t:d:h?")) != -1) {
        switch (c) {
        case 'f':
            cfg->file = optarg;
            break;
        case 'P': cfg->port = atoi(optarg); break;
        case 't': cfg->threads = atoi(optarg); break;        
        case 'b': cfg->body = optarg; break;
        case '0': cfg->title = optarg; break;
        case '1': cfg->servername = optarg; break;
        case 'h':
        case '?':
        default:
            return -1;
        }
    }
    return 0;
}

char* substring(const char* str, size_t begin, size_t len)
{
  if (str == 0 || strlen(str) == 0 || strlen(str) < begin || strlen(str) < (begin+len))
    return 0;

  return strndup(str + begin, len);
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
            ret[retlen] = '\0';
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
      !setnonblocking(sock)))
    {
      fprintf(stderr, "setsockopt || fcntl");
      return -2;
    }    
    int res = connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr));

    if (res < 0) {
        if (errno == EINPROGRESS) {
            return sock;
        }
        close(sock);
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

se_ptr_t *se_add(int loop_fd, int fd)
{
    se_ptr_t *ptr = malloc(sizeof(se_ptr_t));

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

int se_delete(se_ptr_t *ptr)
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
    se_ptr_t *ptr = NULL;

    while(1) {

        n = epoll_wait(loop_fd, events, 4096, waitout);
        if(n == 0) break;
        for(i = 0; i < n; i++) {
            ptr = events[i].data.ptr;

            if (events[i].events & ( EPOLLHUP | EPOLLERR))
            {
                se_delete(ptr);
            } else if(events[i].events & (EPOLLIN) && ptr->rfunc) {
                //printf("rfunc\n");
                ptr->rfunc(ptr);

            } else if(events[i].events & (EPOLLOUT) && ptr->wfunc) {
                //printf("wfunc\n");
                ptr->wfunc(ptr);
            }
        }

        if(n == -1 && errno != EINTR) {
            printf("exit\n");
            break;
        }
    }

    return 0;
}

int se_be_read(se_ptr_t *ptr, se_rw_proc_t func)
{
    ptr->rfunc = func;
    ptr->wfunc = NULL;

    ev.data.ptr = ptr;
    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR | EPOLLONESHOT;

    return epoll_ctl(ptr->loop_fd, EPOLL_CTL_MOD, ptr->fd, &ev);
}

int se_be_write(se_ptr_t *ptr, se_rw_proc_t func)
{
    ptr->rfunc = NULL;
    ptr->wfunc = func;

    ev.data.ptr = ptr;
    ev.events = EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR | EPOLLET;

    return epoll_ctl(ptr->loop_fd, EPOLL_CTL_MOD, ptr->fd, &ev);
}

int se_be_pri(se_ptr_t *ptr, se_rw_proc_t func)
{
    ptr->rfunc = func;
    ptr->wfunc = NULL;

    ev.data.ptr = ptr;
    ev.events = EPOLLPRI;

    return epoll_ctl(ptr->loop_fd, EPOLL_CTL_MOD, ptr->fd, &ev);
}

int se_be_rw(se_ptr_t *ptr, se_rw_proc_t rfunc, se_rw_proc_t wfunc)
{
    ptr->rfunc = rfunc;
    ptr->wfunc = wfunc;

    ev.data.ptr = ptr;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR;

    return epoll_ctl(ptr->loop_fd, EPOLL_CTL_MOD, ptr->fd, &ev);
}

int network_be_read(se_ptr_t *ptr)
{
    char *response = NULL;
    char buffer[1500];
    int read_size , total_size = 0;
    if (socket_check(ptr->fd) == 1)
    {
        fprintf (stderr, " [ %s->%d] read socket_check : [%d]%s\n", ptr->addr, ptr->fd, errno, strerror(errno));
        //se_be_read(ptr, network_be_read);
        return 1;
    }
    bzero(buffer, 1500);
	while( (read_size = recv(ptr->fd , buffer , sizeof(buffer) , 0) ))
	{
		if((errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)) {
			fprintf (stderr, " recv[%d] : [%d]%s\n",read_size, errno, strerror(errno));
			continue;
		}
		response = realloc(response , read_size + total_size);
		if(response == NULL)
		{
			printf("realloc failed");
			exit(1);
		}
		memcpy((response + total_size) , buffer , read_size);
		total_size += read_size;
	}
	response = realloc(response , total_size + 1);
	*(response + total_size) = '\0';
     if(total_size > 0) {
        int m0 = 0;
        int m1 = 0;
        //printf("read %d=%d: %s\n", n, nread, buffer);
        char *http_status = substring(buffer, 9, 3);
        char *http_servername = substr(buffer, "Server: ", "\r\n");
        char *http_title = substr(buffer, "<title>", "</title>");
        if (cfg.title != NULL) {
            ++m0;
            if (http_title != NULL && strstr(http_title, cfg.title) != NULL)++m1;
        }
        if (cfg.servername != NULL) {
            ++m0;
            if (http_servername != NULL && strstr(http_servername, cfg.servername) != NULL)++m1;
        }  

        if( m0 == m1)fprintf(stdout, "%s\t%s\t%s\t%s\n", ptr->addr, http_status, http_servername, http_title);
        if(http_status != NULL) free(http_status);
        if(http_servername != NULL) free(http_servername);
        if(http_title != NULL) free(http_title);     
    }
    if(response != NULL) free(response);
    close(ptr->fd);
    se_delete(ptr);
    return total_size;

}
int network_be_write(se_ptr_t *ptr)
{
    int rs;

    if (socket_check(ptr->fd) == 1)
    {
        fprintf (stderr, " [ %s->%d] write socket_check : [%d]%s\n", ptr->addr, ptr->fd, errno, strerror(errno));
        se_delete(ptr);
        //se_be_write(ptr, network_be_write);
        return 1;
    }
    rs = send(ptr->fd, cfg.body, strlen(cfg.body), 0);
    if (rs < 0) {
        printf("send error [%d]%s\n", errno, strerror(errno));
        se_delete(ptr);
        return 1;
    }
    se_be_read(ptr, network_be_read);

}
void main(int argc, char* argv[])
{
    int epfd;
    signal(SIGPIPE, SIG_IGN); //oops strace 
    if (parse_args(&cfg, argc, argv)) {
        usage();
        exit(1);
    }
    if (cfg.file) {
        FILE *fp;
        char buf[MAX_LINE];
        int n, len ;
        fp = fopen(cfg.file, "r");
        if (fp == NULL) {
            printf("file not exist :%s\n", cfg.file);
            exit(1);
        }
        n = 0;
        epfd = se_create(1024);
        int rn = 0;
master_worker:
        while(fgets(buf, MAX_LINE, fp) != NULL) {
            len = strlen(buf);
            buf[len-1] = '\0';
              if(strlen(buf) >= 7)
              {
                int sockfd = connected(buf);
                if (sockfd > 0)
                {
                    se_ptr_t *ptr = se_add(epfd, sockfd);
                    strcpy(ptr->addr, buf);
                    se_be_write(ptr, network_be_write);
                }
                //fprintf (stderr, "create and connect : %s=%d\n", buf, sockfd);
              }         
            ++n;
            ++rn;
            if(rn > cfg.threads) goto epoll_worker;
        }
        if (rn == 0) exit(0);
 epoll_worker:
        fprintf (stderr, "work line: %d - %d\n", (n-cfg.threads) < 0 ? 1 : (n-cfg.threads), n);   
        rn = 0;    
        se_loop(epfd, 4000);
        goto master_worker;
    }
    fprintf (stderr, "work done\n");
}
