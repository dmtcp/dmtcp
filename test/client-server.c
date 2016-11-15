#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdio.h>

int
main(int argc, char *argv[])
{
  int port = (argc == 2 ? atoi(argv[1]) : 6500);
  struct sockaddr_in sockaddr;
  int listener_sd, rc;
  pid_t pid;

  memset(&sockaddr, 0, sizeof(sockaddr));
  sockaddr.sin_family = AF_INET;
  inet_aton("127.0.0.1", &sockaddr.sin_addr);

  // sockaddr.sin_addr.s_addr = INADDR_LOOPBACK;
  sockaddr.sin_port = htons(port);

  listener_sd = socket(AF_INET, SOCK_STREAM, 0);
  do {
    sockaddr.sin_port = htons(ntohs(sockaddr.sin_port) + 1);
    rc = bind(listener_sd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
  } while (rc == -1 && errno == EADDRINUSE);
  if (rc == -1) {
    perror("bind");
  }
  listen(listener_sd, 5);

  pid = fork();
  if (pid) { /* if parent process */
    int sd;
    int i = 0;
    char byte;
    close(listener_sd);
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
  } else { /* else child process */
    int sd;
    char byte;
    sd = accept(listener_sd, NULL, NULL);
    while (1) { /* server reads and then writes */
      while (read(sd, &byte, 1) != 1) {}
      while (write(sd, &byte, 1) != 1) {}
    }
  }
}
