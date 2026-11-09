/* Non-blocking connect() calls that never complete, with the sockets kept
 * open across checkpoints: one created with SOCK_NONBLOCK, one made
 * non-blocking with fcntl(). Both connect to a local port that is bound but
 * not listening, so the connection is refused.
 *
 * The socket plugin records such a socket as TCP_CONNECT_IN_PROGRESS. It must
 * classify it (here: as external) before the peer-information exchange, which
 * calls getpeername(); on an unconnected socket that fails with ENOTCONN.
 */
#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Start a non-blocking connect() and require it to be pending. Only a
 * connect() that returns -1 with EINPROGRESS is recorded as
 * TCP_CONNECT_IN_PROGRESS; an immediate result would not test the fix.
 */
static void
connect_pending(int sd, struct sockaddr_in *addr)
{
  if (connect(sd, (struct sockaddr *)addr, sizeof(*addr)) == 0) {
    fprintf(stderr, "connect: completed immediately\n");
    exit(1);
  }
  if (errno != EINPROGRESS) {
    perror("connect: expected EINPROGRESS");
    exit(1);
  }
}

int
main()
{
  struct sockaddr_in sockaddr;
  socklen_t len = sizeof(sockaddr);
  int bound_sd, sd1, sd2, flags;
  int i = 0;

  /* A bound port without a listener refuses connections. */
  memset(&sockaddr, 0, sizeof(sockaddr));
  sockaddr.sin_family = AF_INET;
  inet_aton("127.0.0.1", &sockaddr.sin_addr);
  bound_sd = socket(AF_INET, SOCK_STREAM, 0);
  if (bound_sd == -1) {
    perror("socket");
    return 1;
  }
  if (bind(bound_sd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) == -1 ||
      getsockname(bound_sd, (struct sockaddr *)&sockaddr, &len) == -1) {
    perror("bind");
    return 1;
  }

  sd1 = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (sd1 == -1) {
    perror("socket");
    return 1;
  }
  connect_pending(sd1, &sockaddr);

  sd2 = socket(AF_INET, SOCK_STREAM, 0);
  if (sd2 == -1) {
    perror("socket");
    return 1;
  }
  flags = fcntl(sd2, F_GETFL);
  if (flags == -1 || fcntl(sd2, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("fcntl");
    return 1;
  }
  connect_pending(sd2, &sockaddr);

  while (1) {
    if (i++ % 10 == 0) {
      printf("."); fflush(stdout);
    }
    usleep(100000);
  }
}
