#define _GNU_SOURCE

#include <assert.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

typedef int (*socketpair_fn)(int, int, int, int[2]);

static void
exchange(int sender, int receiver, const char *message)
{
  char buffer[64] = {};
  size_t length = strlen(message) + 1;

  assert(send(sender, message, length, 0) == (ssize_t)length);
  assert(recv(receiver, buffer, sizeof(buffer), 0) == (ssize_t)length);
  assert(strcmp(buffer, message) == 0);
}

static socklen_t
get_netlink_memberships(int fd, uint32_t *memberships, socklen_t capacity)
{
  socklen_t length = capacity;
  assert(getsockopt(fd, SOL_NETLINK, NETLINK_LIST_MEMBERSHIPS,
                    memberships, &length) == 0);
  return length;
}

int
main(void)
{
  void *libc = dlopen("libc.so.6", RTLD_NOW | RTLD_LOCAL);
  assert(libc != NULL);

  socketpair_fn real_socketpair =
    (socketpair_fn)dlsym(libc, "socketpair");
  assert(real_socketpair != NULL);

  int sockets[2] = { -1, -1 };
  assert(real_socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  int alias = dup(sockets[0]);
  assert(alias >= 0);
  assert(fcntl(alias, F_SETFD, FD_CLOEXEC) == 0);

  int netlink = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
  assert(netlink >= 0);
  unsigned int netlinkGroup = 3;
  assert(setsockopt(netlink, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
                    &netlinkGroup, sizeof(netlinkGroup)) == 0);
  uint32_t expectedMemberships[8] = {};
  socklen_t expectedMembershipLength =
    get_netlink_memberships(netlink, expectedMemberships,
                            sizeof(expectedMemberships));

  int listener = socket(AF_INET, SOCK_STREAM, 0);
  assert(listener >= 0);
  int enabled = 1;
  int requestedBuffer = 64 * 1024;
  assert(setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                    &enabled, sizeof(enabled)) == 0);
  assert(setsockopt(listener, SOL_SOCKET, SO_RCVBUF,
                    &requestedBuffer, sizeof(requestedBuffer)) == 0);
  struct sockaddr_in address = {
    .sin_family = AF_INET,
    .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
  };
  assert(bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0);
  assert(listen(listener, 7) == 0);
  int expectedBuffer = 0;
  socklen_t optionLength = sizeof(expectedBuffer);
  assert(getsockopt(listener, SOL_SOCKET, SO_RCVBUF,
                    &expectedBuffer, &optionLength) == 0);

  for (unsigned int iteration = 0;; ++iteration) {
    assert(fcntl(alias, F_GETFD) == FD_CLOEXEC);
    int value = 0;
    optionLength = sizeof(value);
    assert(getsockopt(netlink, SOL_SOCKET, SO_DOMAIN,
                      &value, &optionLength) == 0);
    assert(value == AF_NETLINK);
    uint32_t memberships[8] = {};
    socklen_t membershipLength =
      get_netlink_memberships(netlink, memberships, sizeof(memberships));
    assert(membershipLength == expectedMembershipLength);
    assert(memcmp(memberships, expectedMemberships,
                  membershipLength) == 0);
    optionLength = sizeof(value);
    assert(getsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                      &value, &optionLength) == 0);
    assert(value == enabled);
    optionLength = sizeof(value);
    assert(getsockopt(listener, SOL_SOCKET, SO_RCVBUF,
                      &value, &optionLength) == 0);
    assert(value == expectedBuffer);

    char message[64];
    snprintf(message, sizeof(message), "forward-%u", iteration);
    exchange(sockets[0], sockets[1], message);

    snprintf(message, sizeof(message), "reverse-%u", iteration);
    exchange(sockets[1], alias, message);

    printf("socket discovery iteration %u\n", iteration);
    fflush(stdout);
    sleep(1);
  }
}
