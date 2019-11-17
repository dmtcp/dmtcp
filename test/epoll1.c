#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdio.h>

#define MAX_EVENTS 10
/* Code to set up listening socket, with 'listen_sockfd',
   (socket(), bind(), listen()) omitted */

struct sockaddr_in sockaddr;
int listen_sockfd;
int use_epoll_create1 = 0;

void server()
{
  struct epoll_event ev, events[MAX_EVENTS];
  int conn_sock, nfds, epollfd;

  if (use_epoll_create1 == 1) {
#if __GLIBC_PREREQ(2, 9)
    epollfd = epoll_create1(EPOLL_CLOEXEC);
#else
    epollfd = epoll_create(5);
#endif
  } else {
    epollfd = epoll_create(5);
  }

  if (epollfd == -1) {
    perror("epoll_create1");
    exit(EXIT_FAILURE);
  }

  ev.events = EPOLLIN;
  ev.data.fd = listen_sockfd;
  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sockfd, &ev) == -1) {
    perror("epoll_ctl: listen_sockfd");
    exit(EXIT_FAILURE);
  }

  for (;;) {
    nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
    if (nfds == -1) {
      perror("epoll_wait");
      exit(EXIT_FAILURE);
    }

    socklen_t addrlen = sizeof(struct sockaddr);
    for (size_t n = 0; n < nfds; ++n) {
      if (events[n].data.fd == listen_sockfd) {
        conn_sock = accept(listen_sockfd,
                           (struct sockaddr *) &sockaddr, &addrlen);
        if (conn_sock == -1) {
          perror("accept");
          exit(EXIT_FAILURE);
        }
        //setnonblocking(conn_sock);
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = conn_sock;
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock,
                      &ev) == -1) {
          perror("epoll_ctl: conn_sock");
          exit(EXIT_FAILURE);
        }
      } else {
        char byte;
        while(read(events[n].data.fd, &byte, 1) != 1) {}
        while(write(events[n].data.fd, &byte, 1) != 1) {}
      }
    }
  }
}

void client()
{
  int sd;
  int i = 0;
  char byte;
  close(listen_sockfd);
  sd = socket(AF_INET, SOCK_STREAM, 0); /* create connection socket */
  connect(sd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
  while (1) { /* client writes and then reads */
    if (i++ % 1000 == 0) {
      printf("."); fflush(stdout);
    }
    if (i % 50000 == 0) {
      printf("\n");
    }
    while (write(sd, &byte, 1) != 1) {}
    while (read(sd, &byte, 1) != 1) {}
  }
}

int
main(int argc, char *argv[])
{
  int port = 6500; // We will search for a free port, starting here.
  int rc;
  pid_t pid;

  if (argc == 2 && strcmp(argv[1], "--use-epoll-create1") == 0) {
    use_epoll_create1 = 1;
  }

  memset(&sockaddr, 0, sizeof(sockaddr));
  sockaddr.sin_family = AF_INET;
  inet_aton("127.0.0.1", &sockaddr.sin_addr);

  // sockaddr.sin_addr.s_addr = INADDR_LOOPBACK;
  sockaddr.sin_port = htons(port);

  listen_sockfd = socket(AF_INET, SOCK_STREAM, 0);
  do {
    sockaddr.sin_port = htons(ntohs(sockaddr.sin_port) + 1);
    rc = bind(listen_sockfd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
  } while (rc == -1 && errno == EADDRINUSE);
  if (rc == -1) {
    perror("bind");
    exit(1);
  }
  listen(listen_sockfd, 5);

  // This is informational only; it could be deleted.
  struct sockaddr_in sockaddr_tmp;
  socklen_t addrlen;
  rc = getsockname(listen_sockfd, (struct sockaddr *)&sockaddr_tmp, &addrlen);
  if (rc == -1) {
    perror("getsockname");
    exit(1);
  }
  printf("Listening on port:  %d\n", (int)ntohs(sockaddr.sin_port));

  pid = fork();
  if (pid) { /* if parent process */
    client();
  } else { /* else child process */
    server();
  }
}
