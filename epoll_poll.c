#include<fcntl.h>
#include<cstdio>
#include<unistd.h>
#include<cstdlib>
#include<sys/socket.h>
#include<sys/epoll.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<errno.h>
#include<cstring>
#include<pthread.h>

const int EPOLL_SIZE=5000;
const int EVENT_ARR=5000;
const int PORT=8002;
const int BUF_SIZE=5000;
const int BACK_QUEUE=100;
const int THREAD_MAX=100;
static unsigned int s_thread_para[THREAD_MAX][8];   //线程参数
static pthread_t s_tid[THREAD_MAX];                 //线程ID
pthread_mutex_t s_mutex[THREAD_MAX]; //线程锁
int epFd;                            //epoll
struct epoll_event ev,evs[EVENT_ARR];

char *get_type(char *url,char *buf)
{

   const char *t=url+strlen(url);
   char type[64];
   for(;t>=url&&*t!='.';--t)  ;

   strcpy(type,t+1);
   if(strcmp(type,"html")==0||strcmp(type,"htm")==0)
           sprintf(buf,"text/%s",type);
   else if(strcmp(type,"gif")==0||
         strcmp(type,"jpg")==0||
         strcmp(type,"jpeg")==0||
         strcmp(type,"png")==0)
         sprintf(buf,"image/%s",type);
  else if(strcmp(type,"/")==0)
      sprintf(buf,"text/html");
  else if(strcmp(type,"css")==0)
        sprintf(buf,"text/css");
  else if(strcmp(type,"js")==0)
     sprintf(buf,"application/x-javascript");
  else
      {
     
      sprintf( buf, "unknown" );
      }

      return buf;

}
 void* http_server(int thread_para[])
{
      int pool_index;           //thread pool ID
      int clientFd;             //client socket
      char buf[BUF_SIZE];
      pthread_detach(pthread_self());
      pool_index=thread_para[7];

 wait_unlock:
         pthread_mutex_lock(s_mutex+pool_index); //wait for thread unlock
         clientFd=thread_para[1];                //client socket ID
                              //先进行试探性读取
           int len=read(clientFd,buf,BUF_SIZE);
              printf("%s",buf);
              if(len>0)
                           {
             char *token=strtok(buf," ");          //GET
           printf("token:%s",token);
             char type[64];
           char *url=strtok(NULL," ");           //URL
           while(*url=='.'||*url=='/')++url;
           printf("url:%s",url);
           char file[1280000];

           sprintf(file,"%s",url);
           printf("file:%s",file);

             FILE *fp=fopen(file,"rb");

             if(fp==0)
                {
                char response[]="HTTP/1.1 404 NOT FOUND\r\n\r\n";
                printf("HTTP/1.1 404 NOT FOUND\r\n\r\n");
                write(clientFd,response,strlen(response));
                }

             else
             {

            int file_size;
            char *content;
            char *response;
            fseek(fp,0,SEEK_END);
            file_size=ftell(fp);
            fseek(fp,0,SEEK_SET);
            content=(char *)malloc(file_size+1);
            response=(char*)malloc(200);
            fread(content,file_size,1,fp);
              content[file_size]=0;
              sprintf(response,"HTTP/1.1 200 OK\r\nContent-Length:%d\r\nContent-Type:%s\r\n\r\n",file_size,get_type(url,type));
             // printf("HTTP/1.1 200 OK\r\nContent-Type:%s\r\nContent-Length:%d\r\n\r\n%s",get_type(url,type),file_size,content);
              write(clientFd,response,strlen(response));
              write(clientFd,content,file_size);
              free(content);
              free(response);

                              }
                           }
              else if(len==0)
                                               {
                                                 //触发了EPOLL事件，却没有读取，表示断线
                   //printf("Client closed at %s\n",inet_ntoa(clientAddr.sin_addr));
                   epoll_ctl(epFd,EPOLL_CTL_DEL,clientFd,&ev);
                   close(clientFd);
                   int i=thread_para[3];
                   evs[i].data.fd=-1;

                                               }
               else if(len==EAGAIN)
                                            {
                 printf("socket huan cun man le!\n");

                                            }
               else
                                            {
                  //client读取出错
                  printf("Client read failed!\n");
                                            }
       thread_para[0] = 0;//设置线程占用标志为"空闲"
       goto wait_unlock;

       printf("pthread exit!\n");
       pthread_exit(NULL);

}

static int init_thread_pool(void)
{
    int i,rc;
    for(i=0;i<THREAD_MAX;i++)
    {
     s_thread_para[i][0]=0;  //idle
     s_thread_para[i][7]=i;   //thread pool ID
     pthread_mutex_lock(s_mutex+i);   //thread lock
    }
    // create thread pool
    for(i=0;i<THREAD_MAX;i++)
    {  rc=pthread_create(s_tid+i,0,(void *(*)(void*))&http_server,(void *)(s_thread_para[i]));
       if(0!=rc)
        { fprintf(stderr,"Create thread failed!\n");
           return -1;
                    }
           }
    return 0;
}

void setnoblock(int sockFd)         //设置非阻塞模式
{
int opt;
if((opt=fcntl(sockFd,F_GETFL))<0)  //获取原来的flag;
    {
    printf("GET FL failed!\n");
    exit(-1);
     }
 opt|=O_NONBLOCK;
  if(fcntl(sockFd,F_SETFL,opt)<0)
 printf("SET FL failed!\n");


}

int main()
{
  int serverFd,j;
  serverFd=socket(AF_INET,SOCK_STREAM,0); //创建服务器fd
    setnoblock(serverFd);                   //设置为非阻塞模式
    unsigned int        optval;
    struct linger        optval1;
    //设置SO_REUSEADDR选项(服务器快速重起)
      optval = 0x1;
      setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &optval, 4);

     // 设置SO_LINGER选项(防范CLOSE_WAIT挂住所有套接字)
     optval1.l_onoff = 1;
     optval1.l_linger = 60;
     setsockopt(serverFd, SOL_SOCKET, SO_LINGER, &optval1, sizeof(struct linger));

          //创建epoll，并将serverFd放入监听队列
    epFd=epoll_create(EPOLL_SIZE);

    ev.data.fd=serverFd;
    ev.events=EPOLLIN|EPOLLET;
    epoll_ctl(epFd,EPOLL_CTL_ADD,serverFd,&ev);

         //绑定服务器端口
   struct sockaddr_in serverAddr;
   socklen_t serverlen=sizeof(struct sockaddr_in);
   serverAddr.sin_addr.s_addr=htonl(INADDR_ANY);
   serverAddr.sin_port=htons(PORT);
    if(bind(serverFd,(struct sockaddr*)&serverAddr,serverlen))
          {
       printf("BIND failed!\n");
       exit(-1);
          }

    //线程池初始化
    int rc = init_thread_pool();
    if (0 != rc) exit(-1);



    //打开监听
    if(listen(serverFd,BACK_QUEUE))
          {
          printf("Listen failed!\n");
          exit(-1);
          }

        //服务处理
    int clientFd;
    sockaddr_in clientAddr;
    socklen_t clientlen;

    for(;;)
        {
     //等待epoll事件到来，最多取EVENT_ARR个事件
     int nfds=epoll_wait(epFd,evs,EVENT_ARR,-1);
     //处理事件
     for(int i=0;i<nfds;i++)
      {
      if(evs[i].data.fd==serverFd&&evs[i].events&EPOLLIN)
            {
              //如果是serverFd，表明有新连接连入
          if((clientFd=accept(serverFd,(struct sockaddr*)&clientAddr,&clientlen))<0)
                               {
             printf("ACCEPT  failed\n");
                               }
          printf("Connect from %s:%d\n",inet_ntoa(clientAddr.sin_addr),htons(clientAddr.sin_port));
        setnoblock(clientFd);
                  //注册accept()到的连接
        ev.data.fd=clientFd;
        ev.events=EPOLLIN|EPOLLET;
        epoll_ctl(epFd,EPOLL_CTL_ADD,clientFd,&ev);
            }
      else if(evs[i].events&EPOLLIN)
      {
                  //如果不是serverFd,则是client的可读
      printf("client can write!\n");
      if((clientFd=evs[i].data.fd)>0)
           {
             //查询空闲线程对

          for(j = 0; j < THREAD_MAX; j++) {
              if (0 == s_thread_para[j][0]) break;
          }
          if (j >= THREAD_MAX) {
              fprintf(stderr, "线程池已满, 连接将被放弃\r\n");
              shutdown(clientFd, SHUT_RDWR);
              close(clientFd);
              continue;
          }
          //复制有关参数
          s_thread_para[j][0] = 1;//设置活动标志为"活动"
          s_thread_para[j][1] = clientFd;//客户端连接
          s_thread_para[j][2] = serverFd;//服务索引
          s_thread_para[j][3]=i;             //epoll event id;
          //线程解锁
          pthread_mutex_unlock(s_mutex + j);  }
      else printf("other error!\n");
      }
         }
        }
    return 0;
}
