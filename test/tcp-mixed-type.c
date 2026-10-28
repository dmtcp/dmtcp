/* A TCP connection whose two ends were created with different type flags:
 * the server's listener with plain SOCK_STREAM, the client's socket with
 * SOCK_STREAM | SOCK_CLOEXEC, as Python and the XRootD client do (#1298).
 *
 * The socket plugin must compare the base type, not the raw type, when it
 * exchanges peer information before the drain. If it doesn't, the
 * SOCK_CLOEXEC end never sends its peer information, the plain end marks the
 * connection external, and the checkpoint waits forever to drain the other
 * end.
 */
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

int
main(int argc, char *argv[])
{
  int port = (argc == 2 ? atoi(argv[1]) : 6600);
  struct sockaddr_in sockaddr;
  int listener_sd, rc;
  pid_t pid;

  memset(&sockaddr, 0, sizeof(sockaddr));
  sockaddr.sin_family = AF_INET;
  inet_aton("127.0.0.1", &sockaddr.sin_addr);

  listener_sd = socket(AF_INET, SOCK_STREAM, 0);
  do {
    sockaddr.sin_port = htons(++port);
    rc = bind(listener_sd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
  } while (rc == -1 && errno == EADDRINUSE);
  if (rc == -1) {
    perror("bind");
    return 1;
  }
  listen(listener_sd, 5);

  pid = fork();
  if (pid) { /* parent: client, socket created with SOCK_CLOEXEC */
    int sd;
    int i = 0;
    char byte = 0;
    close(listener_sd);
    sd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (connect(sd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) == -1) {
      perror("connect");
      return 1;
    }
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
  } else { /* child: server, accepted from a plain SOCK_STREAM listener */
    int sd;
    char byte;
    sd = accept(listener_sd, NULL, NULL);
    while (1) { /* server reads and then writes */
      while (read(sd, &byte, 1) != 1) {}
      while (write(sd, &byte, 1) != 1) {}
    }
  }
}
