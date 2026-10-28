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
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void
connect_nonblocking(int sd, struct sockaddr_in *addr)
{
  if (connect(sd, (struct sockaddr *)addr, sizeof(*addr)) == 0 ||
      (errno != EINPROGRESS && errno != ECONNREFUSED)) {
    perror("connect");
  }
}

int
main()
{
  struct sockaddr_in sockaddr;
  socklen_t len = sizeof(sockaddr);
  int bound_sd, sd1, sd2;
  int i = 0;

  /* A bound port without a listener refuses connections. */
  memset(&sockaddr, 0, sizeof(sockaddr));
  sockaddr.sin_family = AF_INET;
  inet_aton("127.0.0.1", &sockaddr.sin_addr);
  bound_sd = socket(AF_INET, SOCK_STREAM, 0);
  if (bind(bound_sd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) == -1 ||
      getsockname(bound_sd, (struct sockaddr *)&sockaddr, &len) == -1) {
    perror("bind");
    return 1;
  }

  sd1 = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  connect_nonblocking(sd1, &sockaddr);

  sd2 = socket(AF_INET, SOCK_STREAM, 0);
  fcntl(sd2, F_SETFL, fcntl(sd2, F_GETFL) | O_NONBLOCK);
  connect_nonblocking(sd2, &sockaddr);

  while (1) {
    if (i++ % 10 == 0) {
      printf("."); fflush(stdout);
    }
    usleep(100000);
  }
}
