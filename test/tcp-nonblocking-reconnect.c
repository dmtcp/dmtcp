#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dmtcp.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define CHECK(x)  \
  do {            \
    if (!(x)) {   \
      perror(#x); \
      exit(2);    \
    }             \
  } while (0)

static void
ready(int fd, int events)
{
  struct pollfd p = { fd, events, 0 };
  int result;
  do {
    result = poll(&p, 1, 10000);
  } while (result < 0 && errno == EINTR);
  CHECK(result == 1);
}

static void
send_byte(int fd, char value)
{
  for (;;) {
    ssize_t n = send(fd, &value, 1, MSG_NOSIGNAL);
    if (n == 1)
      return;
    if (n < 0 && errno == EINTR)
      continue;
    CHECK(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
    ready(fd, POLLOUT);
  }
}

static char
receive_byte(int fd)
{
  char value;
  for (;;) {
    ssize_t n = recv(fd, &value, 1, 0);
    if (n == 1)
      return value;
    if (n < 0 && errno == EINTR)
      continue;
    CHECK(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
    ready(fd, POLLIN);
  }
}

int
main(int argc, char **argv)
{
  CHECK(argc == 4);
  int nonblock = strcmp(argv[1], "nonblock") == 0;
  int server = strcmp(argv[2], "server") == 0;
  struct sockaddr_in address = { 0 };
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int fd = socket(AF_INET, SOCK_STREAM | (nonblock ? SOCK_NONBLOCK : 0), 0);
  CHECK(fd >= 0);
  if (server) {
    CHECK(bind(fd, (void *)&address, sizeof(address)) == 0);
    CHECK(listen(fd, 4) == 0);
    socklen_t size = sizeof(address);
    CHECK(getsockname(fd, (void *)&address, &size) == 0);
    FILE *file = fopen(argv[3], "w");
    CHECK(file != NULL);
    fprintf(file, "%u\n", ntohs(address.sin_port));
    CHECK(fclose(file) == 0);
    ready(fd, POLLIN);
    int accepted = accept4(fd, NULL, NULL, nonblock ? SOCK_NONBLOCK : 0);
    CHECK(accepted >= 0);
    CHECK(close(fd) == 0);
    fd = accepted;
  } else {
    unsigned port = 0;
    for (int i = 0; i < 1000 && port == 0; ++i) {
      FILE *file = fopen(argv[3], "r");
      if (file != NULL) {
        if (fscanf(file, "%u", &port) != 1)
          port = 0;
        fclose(file);
      }
      if (port == 0)
        usleep(10000);
    }
    CHECK(port > 0 && port < 65536);
    address.sin_port = htons(port);
    int result = connect(fd, (void *)&address, sizeof(address));
    CHECK(result == 0 || (nonblock && errno == EINPROGRESS));
    ready(fd, POLLOUT);
    int error;
    socklen_t length = sizeof(error);
    CHECK(getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 &&
          error == 0);
  }
  unsigned long count = 0;
  for (;;) {
    int checkpoints_before = 0, restarts_before = 0;
    dmtcp_get_local_status(&checkpoints_before, &restarts_before);
    int flags = fcntl(fd, F_GETFL);
    CHECK(flags >= 0 && !!(flags & O_NONBLOCK) == nonblock);
    if (server) {
      char value = receive_byte(fd);
      send_byte(fd, value);
    } else {
      char value = (char)(count % 127);
      send_byte(fd, value);
      CHECK(receive_byte(fd) == value);
    }
    int checkpoints = 0, restarts = 0;
    dmtcp_get_local_status(&checkpoints, &restarts);
    // Do not label pre-checkpoint work as post-restart validation.
    if (restarts_before != restarts)
      continue;
    printf("PROBE server=%d restarts=%d count=%lu\n", server, restarts,
           ++count);
    fflush(stdout);
    usleep(100000);
  }
}
