//https://jve.linuxwall.info/ressources/code/sockevent.c
/* sockevent.c
 * create TCP socket and store then in a epoll file descriptor
 * build with : gcc -O3 -g -falign-functions=4 -falign-jumps -falign-loops -Wall -o sockevent sockevent.c
 */


#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <pthread.h>


/* catch SIGINT and set stop to signal_id
 */
int stop;
void interrupt(int signal_id)
{
   stop = signal_id;
}

   // init struct for statistics
struct statistics
{
   int reqsent;
   int bytessent;
   int reprecv;
   int bytesrecv;
   int error;
   int nbsock;
};

void printstats(struct statistics *stats)
{
   struct statistics previous;
   previous.reqsent = 0;
   previous.reprecv = 0;
   int banner = 0;

   printf("\nreprecv\tbytes\t^hit\treqsent\tbytes\t^req\tErrors\tActive\n");

   for(;;)
      {
         sleep(1);
         if(banner == 10)
            {
               printf("\nreprecv\tbytes\t^hit\treqsent\tbytes\t^req\tErrors\tActive\n");
               banner = 0;
            }
         else
            banner++;


         printf("%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", stats->reprecv, stats->bytesrecv, stats->reprecv - previous.reprecv,stats->reqsent, stats->bytessent, stats->reqsent - previous.reqsent, stats->error, stats->nbsock);

         previous.reqsent = stats->reqsent;
         previous.reprecv = stats->reprecv;
      }

}


/* WT (inject29) function to convert a ip:port chain into a sockaddr struct */
struct sockaddr_in str2sa(char *str)
{
   static struct sockaddr_in sa;
   char *c;
   int port;

   bzero(&sa, sizeof(sa));
   str=strdup(str);
   if ((c=strrchr(str,':')) != NULL) {
      *c++=0;
      port=atol(c);
   }
   else
      port=0;

   if (!inet_aton(str, &sa.sin_addr)) {
      struct hostent *he;

      if ((he = gethostbyname(str)) == NULL)
         fprintf(stderr,"[NetTools] Invalid server name: %s\n",str);
      else
         sa.sin_addr = *(struct in_addr *) *(he->h_addr_list);
   }
   sa.sin_port=htons(port);
   sa.sin_family=AF_INET;

   free(str);
   return sa;
}

/* create a TCP socket with non blocking options and connect it to the target
* if succeed, add the socket in the epoll list and exit with 0
*/
int create_and_connect( struct sockaddr_in target , int *epfd)
{
   int yes = 1;
   int sock;

   // epoll mask that contain the list of epoll events attached to a network socket
   static struct epoll_event Edgvent;


   if( (sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
   {
      perror("socket");
      exit(1);
   }

   // set socket to non blocking and allow port reuse
   if ( (setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int)) ||
      fcntl(sock, F_SETFL, O_NONBLOCK)) == -1)
   {
      perror("setsockopt || fcntl");
      exit(1);
   }

   if( connect(sock, (struct sockaddr *)&target, sizeof(struct sockaddr)) == -1
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
      Edgvent.events = EPOLLOUT | EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET ;
      //Edgvent.events = EPOLLOUT | EPOLLIN | EPOLLRDHUP | EPOLLERR;

      Edgvent.data.fd = sock;

      // add the socket to the epoll file descriptors
      if(epoll_ctl((int)epfd, EPOLL_CTL_ADD, sock, &Edgvent) != 0)
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
   size_t len = sizeof(int);

   ret = getsockopt(fd, SOL_SOCKET, SO_ERROR, &code, &len);

   if ((ret || code)!= 0)
      return 1;

   return 0;
}




int main(int argc, char *argv[])
{
   
   if(argc!=3)
   {
      printf("gatlinject <ip:port> <num socket>\n");
      exit(1);
   }

   struct sockaddr_in target = str2sa((char *) argv[1]); // convert target information
   int maxconn = atoi(argv[2]); //number of sockets to connect to the target

   // internal variables definition
   int i, count, datacount;

   char message[] = "hello\n\n";
   int messagelength = strlen(message);

   char buffer[1500];
   int buffersize = strlen(buffer);

   struct statistics stats;
   memset(&stats,0x0,6 * sizeof(int));
   pthread_t Statsthread;
   

   // time
   struct timeval start;
   struct timeval current;
   float elapsedtime;

   // catch SIGINT to exit in a clean way
   struct sigaction sa;
   memset(&sa, 0, sizeof(struct sigaction *));
   sa.sa_handler = interrupt;
   sa.sa_flags = 0;
   sigemptyset (&(sa.sa_mask));
   if(sigaction (SIGINT, &sa, NULL)!= 0)
   {
      perror("sigaction failed");
      exit(1);
   }

   // the epoll file descriptor
   int epfd;

   // epoll structure that will contain the current network socket and event when epoll wakes up
   static struct epoll_event *events;
   static struct epoll_event event_mask;

   // create the special epoll file descriptor
   epfd = epoll_create(maxconn);

   // allocate enough memory to store all the events in the "events" structure
   if (NULL == (events = calloc(maxconn, sizeof(struct epoll_event))))
   {
      perror("calloc events");
      exit(1);
   };

   // create and connect as much as needed
   for(i=0;i<maxconn;i++)
      if(create_and_connect(target, (int *) epfd) != 0)
      {
         perror("create and connect");
         exit(1);
      }
      else
         stats.nbsock++;

   // start the thread that prints the statistics
   if( 0!= pthread_create (&Statsthread, NULL, (void *)printstats, &stats) ){
      perror("stats thread");
      exit(1);
   }

   gettimeofday(&start, NULL);

   do
   {
      /* wait for events on the file descriptors added into epfd
       *
       * if one of the socket that's contained into epfd is available for reading, writing,
       * is closed or have an error, this socket will be return in events[i].data.fd
       * and events[i].events will be set to the corresponding event
       *
       * count contain the number of returned events
       */
      count = epoll_wait(epfd, events, maxconn, 1000);

      for(i=0;i<count;i++)
      {
         if (events[i].events & EPOLLOUT) //socket is ready for writing
         {
            // verify the socket is connected and doesn't return an error
            if(socket_check(events[i].data.fd) != 0)
            {
               perror("write socket_check");
               continue;
            }
            else
            {
               if((datacount = send(events[i].data.fd, message, messagelength, 0)) < 0)
               {
                  stats.error++;
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
                  event_mask.data.fd = events[i].data.fd;

                  if(epoll_ctl(epfd, EPOLL_CTL_MOD, events[i].data.fd, &event_mask) != 0)
                  {
                     perror("epoll_ctl, modify socket\n");
                     exit(1);
                  }

                  stats.reqsent++;
                  stats.bytessent += datacount;
               }
            }
         }

         if (events[i].events & EPOLLIN) //socket is ready for writing
         {
            // verify the socket is connected and doesn't return an error
            if(socket_check(events[i].data.fd) != 0)
            {
               perror("read socket_check");
               continue;
            }
            else 
            {
               memset(buffer,0x0,buffersize);

               if((datacount = recv(events[i].data.fd, buffer, buffersize, 0)) < 0)
               {
                  stats.error++;
                  perror("recv failed");
                  continue;
               }
               else
               {
                  stats.bytesrecv += datacount;
                  stats.reprecv++;

               }
            }
         }

         if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) //socket closed, delete and create a new one
         {
            // socket is closed, remove the socket from epoll and create a new one
            epoll_ctl(epfd, EPOLL_CTL_DEL, events[i].data.fd, NULL);

            if(close(events[i].data.fd)!=0)
            {
               perror("close");
               continue;
            }
            else
               stats.nbsock--;

            if(create_and_connect(target, (int *) epfd) != 0)
            {
               perror("create and connect");
               continue;
            }
            else
               stats.nbsock++;
         }
         if (events[i].events & EPOLLERR)
         {
            perror("epoll");
            continue;
         }
      }
   } while(!stop);

   gettimeofday(&current, NULL);

   elapsedtime = (current.tv_sec * 1000000 + current.tv_usec) -  (start.tv_sec * 1000000 + start.tv_usec);

   printf("\n\nTime: %4.6f\nRequests sent: %d\nBytes sent: %d\nReponses received: %d\nBytes received: %d\nRates out: %4.6freq/s, %4.6fbytes/s\nRates in : %4.6frep/s, %4.6fbytes/s\nErrors: %d\n", elapsedtime/1000000, stats.reqsent, stats.bytessent, stats.reprecv, stats.bytesrecv, stats.reqsent/(elapsedtime/1000000), stats.bytessent/(elapsedtime/1000000), stats.reprecv/(elapsedtime/1000000), stats.bytesrecv/(elapsedtime/1000000), stats.error);

   return 0;
}

















