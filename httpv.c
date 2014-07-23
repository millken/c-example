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



static struct config {
	char *path;
	char *file;	
    int port;
    int threads;
    bool dynamic;
} cfg;

typedef struct event_ptr {
	int fd;
	char addr[16];
}event_ptr;

static void usage() {
    printf("Usage: httpv <options> <url> \n"
           " Options: \n"
           " -p, --path <P> path of url \n"
           " -P, --port <P> port of host \n"
           " -t, --threads <N> Number of threads to use \n"
           " \n"
           " -f, --file <F> Load ip file \n"
           " -H, --header <H> Add header to request \n"
           " --latency Print latency statistics \n"
           " --timeout <T> Socket/request timeout \n"
           " -v, --version Print version details \n"
           " \n"
           " Numeric arguments may include a SI unit (1k, 1M, 1G)\n"
           " Time arguments may include a time unit (2s, 2m, 2h)\n");
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

static int create_and_connect( char *host , int *epfd)
{
	struct hostent *hp;
	struct sockaddr_in addr;

	// epoll mask that contain the list of epoll events attached to a network socket
	static struct epoll_event event;
   	
	int sock;
	int on = 1;
	
    if((hp = gethostbyname(host)) == NULL) {
       fprintf(stderr,"[NetTools] Invalid server name: %s\n", host);
    }
    bcopy(hp->h_addr, &addr.sin_addr, hp->h_length);
    addr.sin_port = htons(cfg.port);
    addr.sin_family = AF_INET;
    sock = socket(AF_INET, SOCK_STREAM, 0);
	// set socket to non blocking and allow port reuse
	if ( (setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(int)) ||
	  fcntl(sock, F_SETFL, O_NONBLOCK)) == -1)
	{
	  perror("setsockopt || fcntl");
	  exit(1);
	}    
   if( connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr)) == -1
      && errno != EINPROGRESS)
   {
      // connect doesn't work, are we running out of available ports ? if yes, destruct the socket
      if (errno == EAGAIN)
      {
         perror("connect is EAGAIN");
         close(sock);
         exit(1);
      }
   }
   else
   {
      /* epoll will wake up for the following events :
       *
       * EPOLLIN : The associated file is available for read(2) operations.
       *
       * EPOLLOUT : The associated file is available for write(2) operations.
       *
       * EPOLLRDHUP : Stream socket peer closed connection, or shut down writing 
       * half of connection. (This flag is especially useful for writing simple 
       * code to detect peer shutdown when using Edge Triggered monitoring.)
       *
       * EPOLLERR : Error condition happened on the associated file descriptor. 
       * epoll_wait(2) will always wait for this event; it is not necessary to set it in events.
       */
      event.events = EPOLLOUT | EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET ;
      //Edgvent.events = EPOLLOUT | EPOLLIN | EPOLLRDHUP | EPOLLERR;
      
      event.data.ptr = malloc(sizeof(event_ptr));
      
      struct event_ptr *ptr = event.data.ptr;
      bzero(ptr, sizeof(event_ptr));
      ptr->fd = sock;
      strcpy(ptr->addr, host);
      //ptr->addr[strlen(host)] = '\0';
      //printf("%s=%d\n", ptr->addr, strlen(host));

      //event.data.fd = ptr.fd;
      //event.data.ptr = &ptr;

      // add the socket to the epoll file descriptors
      if(epoll_ctl((int)*epfd, EPOLL_CTL_ADD, sock, &event) != 0)
      {
         perror("epoll_ctl, adding socket\n");
         exit(1);
      }
   }

   return 0;	
}

/* reading waiting errors on the socket
 * return 0 if there's no, 1 otherwise
 */
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

void stringlink(char *s, char *t)
{
    while (*s != '\0')
    {
        s++;
    }
    while (*t != '\0')
    {
        *s++ = *t++;
    }
    *s = '\0';
}

void main(int argc, char* argv[])
{
	char *url, **headers;
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
		int epfd;
		static struct epoll_event *events;
		static struct epoll_event event_mask;

		// create the special epoll file descriptor
		epfd = epoll_create(cfg.threads);

		// allocate enough memory to store all the events in the "events" structure
		if (NULL == (events = calloc(cfg.threads, sizeof(struct epoll_event))))
		{
		  perror("calloc events");
		  exit(1);
		};
   				
		while(fgets(buf, MAX_LINE, fp) != NULL) {
			len = strlen(buf);
			buf[len-1] = '\0'; /* 去掉换行符 */
			printf("%s %d\n", buf, len -1);
			  if(create_and_connect(buf, &epfd) != 0)
			  {
				 perror("create and connect");
				 exit(1);
			  }			
			++n;
		}
		
		
  
    char **h;
	char header_name[32];
	char header_value[200];
	char header[1024];
	char header_tmp[232];
  event_ptr * ptr;
	
	sprintf(header, "GET %s HTTP/1.1\r\n", cfg.path);
	
	for (h = headers; *h; h++) {
		char *p = strchr(*h, ':');
		if (p && p[1] == ' ') {
			bzero(header_name, 32);
			bzero(header_value, 200);
			bzero(header_tmp, 232);
			strncpy(header_name, *h, strlen(*h) - strlen(p));
			strcpy(header_value, p + 2);
			sprintf(header_tmp, "%s: %s\r\n", header_name, header_value);
			stringlink(header, header_tmp);
			//printf("p=%p(%d), %s => %s\n", p, strlen(p), header_name, header_value);
		}
	}
	stringlink(header, "\r\n");
	printf("header: \n%s\n", header);
	int header_len = strlen(header);
	
   char buffer[1025];
   int buffersize = 1024;
   int count, i, datacount;	
	
		while(1) {
			count = epoll_wait(epfd, events, cfg.threads, 3000);
			if(count == 0) break;
		for(i=0;i<count;i++) {	
 		ptr = events[i].data.ptr;
		printf("fd=%d ,ip=%s\n", ptr->fd, ptr->addr);
	  if ((events[i].events & EPOLLERR) ||  (events[i].events & EPOLLHUP))
	    {
              /* An error has occured on this fd, or the socket is not
                 ready for reading (why were we notified then?) */
	      fprintf (stderr, "epoll error %d\n", ptr->fd);
	      close (ptr->fd);
	      continue;
	    }				
         if (events[i].events & EPOLLOUT) //socket is ready for writing
         {
            // verify the socket is connected and doesn't return an error
            if(socket_check(ptr->fd) != 0)
            {
               perror("write socket_check");
               continue;
            }
            else
            {
               if((datacount = send(ptr->fd, header, header_len, 0)) < 0)
               {
                  perror("send failed");
                  continue;
               }
               else
               {
                  /* we just wrote on this socket, we don't want to write on it anymore
                   * but we still want to read on it, so we modify the event mask to
                   * remove EPOLLOUT from the events list
                   */
                  event_mask.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET;
                  event_mask.data.fd = ptr->fd;

                  if(epoll_ctl(epfd, EPOLL_CTL_MOD, ptr->fd, &event_mask) != 0)
                  {
                     perror("epoll_ctl, modify socket\n");
                     exit(1);
                  }

               }
            }
         }

         if (events[i].events & EPOLLIN) //socket is ready for reading
         {
            // verify the socket is connected and doesn't return an error
            if(socket_check(ptr->fd) != 0)
            {
               perror("read socket_check");
               continue;
            }
            else 
            {
               memset(buffer,0x0,buffersize);

               if((datacount = recv(ptr->fd, buffer, buffersize, 0)) < 0)
               {
                  perror("recv failed");
                  continue;
               }
               printf("%s\n", buffer);
            }
         }
         free(ptr);
         }			
		}
	}
	
    //printf("headers: %s\npath: %s\nport: %d\n", *headers, cfg.path, cfg.port);
}
