/*
 * myepoll.cpp
 *
 * Created on: 2013-06-03
 * Author: liuxiaoxian
 * To improve the MS concurrency research: sent to the client's data sent over
 */

#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <iostream>

#include "lxx_net.h"

using namespace std;
#define MAX_EPOLL_SIZE 500
#define MAX_CLIENT_SIZE 500
#define MAX_IP_LEN      16
#define MAX_CLIENT_BUFF_LEN 1024
#define QUEUE_LEN 500
#define BUFF_LEN 1024
int fd_epoll = -1;
int fd_listen = -1;

// The customer end connection
typedef struct {
  int fd;                           // The connection handle
  char host[MAX_IP_LEN];           // IP address
  int port;                         // Port
  int len;                          // Data buffer size
  char buff[MAX_CLIENT_BUFF_LEN];   // Buffer data
  bool  status;                     // State
} client_t;

client_t *ptr_cli = NULL;

// Join epoll
int epoll_add(int fd_epoll, int fd, struct epoll_event *ev) {
  if (fd_epoll <0 || fd <0 || ev == NULL) {
    return -1;
  }

  if (epoll_ctl(fd_epoll, EPOLL_CTL_ADD, fd, ev) <0) {
    fprintf(stderr, "epoll_add failed(epoll_ctl)[fd_epoll:%d,fd:%d][%s]\n",
          fd_epoll, fd, strerror(errno));
    return -1;
  }

  fprintf(stdout, "epoll_add success[fd_epoll:%d,fd:%d]\n", fd_epoll, fd);
  return 0;
}

int epoll_del(int fd_epoll, int fd) {
  if (fd_epoll <0 || fd <0) {
    return -1;
  }

  struct epoll_event ev_del;
  if (epoll_ctl(fd_epoll, EPOLL_CTL_DEL, fd, &ev_del) <0) {
    fprintf(stderr, "epoll_del failed(epoll_ctl)[fd_epoll:%d,fd:%d][%s]\n",
          fd_epoll, fd, strerror(errno));
    return -1;
  }
  close(fd);
  fprintf(stdout, "epoll_del success[epoll_fd:%d,fd:%d]\n", fd_epoll, fd);
  return 0;
}

// Receive data
void do_read_data(int idx) {
  if (idx >= MAX_CLIENT_SIZE) {
    return;
  }
  int n;
  size_t pos = ptr_cli[idx].len;

  if ((n = recv(ptr_cli[idx].fd, ptr_cli[idx].buff+pos, MAX_CLIENT_BUFF_LEN-pos, 0))) { // Buffer data have been received
    fprintf(stdout, "[IP:%s,port:%d], data:%s\n", ptr_cli[idx].host, ptr_cli[idx].port, ptr_cli[idx].buff);
    send(ptr_cli[idx].fd, ptr_cli[idx].buff, pos+1, 0);
  } else if (n > 0) { // Buffer zone and data readability
    ptr_cli[idx].len += n;
  } else if (errno != EAGAIN) {  // To end the connection is closed
    fprintf(stdout, "The Client closed(read)[IP:%s,port:%d]\n", ptr_cli[idx].host, ptr_cli[idx].port);
    epoll_del(fd_epoll, ptr_cli[idx].fd);
    ptr_cli[idx].status = false;
  }
}

// Accept new connections
static void do_accept_client() {
  struct epoll_event ev;
  struct sockaddr_in cliaddr;
  socklen_t cliaddr_len = sizeof(cliaddr);

  int conn_fd = lxx_net_accept(fd_listen, (struct sockaddr *)&cliaddr, &cliaddr_len);
  if (conn_fd >= 0) {
    if (lxx_net_set_socket(conn_fd, false) != 0) {
      close(conn_fd);
      fprintf(stderr, "do_accept_client failed(setnonblock)[%s:%d]\n",
      inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port);
      return;
    }

    int i = 0;
    bool flag = true;

    // Looking for the right.
    for (i = 0; i <MAX_CLIENT_SIZE; i++) {
      if (!ptr_cli[i].status) {
        ptr_cli[i].port = cliaddr.sin_port;
        snprintf(ptr_cli[i].host, sizeof(ptr_cli[i].host), inet_ntoa(cliaddr.sin_addr));
        ptr_cli[i].len = 0;
        ptr_cli[i].fd = conn_fd;
        ptr_cli[i].status = true;
        flag = false;
        break;
      }
    }

    if (flag) {// No connection available
      close(conn_fd);
      fprintf(stderr, "do_accept_client failed(not found unuse client)[%s:%d]\n",
                    inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port);
    } else {
      ev.events = EPOLLIN;
      ev.data.u32 = i | 0x10000000;
      if (epoll_add(fd_epoll, conn_fd, &ev) <0) {
        ptr_cli[i].status = false;
        close(ptr_cli[i].fd);
        fprintf(stderr, "do_accept_client failed(epoll_add)[%s:%d]",
        inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port);
        return;
      }

      fprintf(stdout, "do_accept_client success[%s:%d]",
            inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port);
    }
  }
}


int main(int argc, char **argv) {
  unsigned short port = 12345;
  if(argc == 2){
      port = atoi(argv[1]);
  }
  if ((fd_listen = lxx_net_listen(port, QUEUE_LEN)) <0) {
    fprintf(stderr, "listen port failed[%d]", port);
    return -1;
  }

  fd_epoll = epoll_create(MAX_EPOLL_SIZE);
  if (fd_epoll <0) {
    fprintf(stderr, "create epoll failed.%d\n", fd_epoll);
    close(fd_listen);
    return -1;
  }

  // The monitor connected to join the event collection
  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = fd_listen;

  if (epoll_add(fd_epoll, fd_listen, &ev) <0) {
    close(fd_epoll);
    close(fd_listen);
    fd_epoll = -1;
    fd_listen = -1;
    return -1;
  }

  ptr_cli = new client_t[MAX_CLIENT_SIZE];
  struct epoll_event events[MAX_EPOLL_SIZE];
  for (;;) {
    int nfds = epoll_wait(fd_epoll, events, MAX_EPOLL_SIZE, 10);

    if (nfds <0) {
      int err = errno;
      if (err != EINTR) {
        fprintf(stderr, "epoll_wait failed[%s]", strerror(err));
      }
      continue;
    }

    for (int i = 0; i <nfds; i++) {
      if (events[i].data.u32 & 0x10000000) {
        // Receive data
        do_read_data(events[i].data.u32 & 0x0FFFFFFF);
      } else if(events[i].data.fd == fd_listen) {
        // Accept new connections
        do_accept_client();
      }
    }
  }
  return 0;
}
