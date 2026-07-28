#define _GNU_SOURCE

#include <assert.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <dmtcp.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <stdbool.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <netinet/in.h>
#include <stddef.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

typedef int (*socketpair_fn)(int, int, int, int[2]);
typedef int (*socket_fn)(int, int, int);
typedef int (*bind_fn)(int, const struct sockaddr *, socklen_t);
typedef int (*listen_fn)(int, int);
typedef int (*connect_fn)(int, const struct sockaddr *, socklen_t);
typedef int (*accept_fn)(int, struct sockaddr *, socklen_t *);

static void
run_echo_server(int listener)
{
  int peer = accept(listener, NULL, NULL);
  assert(peer >= 0);
  close(listener);
  char buffer[64];
  ssize_t length;
  while ((length = recv(peer, buffer, sizeof(buffer), 0)) > 0) {
    assert(send(peer, buffer, length, MSG_NOSIGNAL) == length);
  }
}

static void
run_unix_server(const char *path)
{
  int listener = socket(AF_UNIX, SOCK_STREAM, 0);
  assert(listener >= 0);
  struct sockaddr_un address = {
    .sun_family = AF_UNIX,
  };
  assert(strlen(path) < sizeof(address.sun_path));
  strcpy(address.sun_path, path);
  unlink(path);
  assert(bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0);
  assert(listen(listener, 1) == 0);
  char readyPath[128];
  snprintf(readyPath, sizeof(readyPath), "%s.ready", path);
  FILE *ready = fopen(readyPath, "w");
  assert(ready != NULL);
  assert(fclose(ready) == 0);
  run_echo_server(listener);
}

static void
run_tcp_server(const char *portFile)
{
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  assert(listener >= 0);
  struct sockaddr_in address = {
    .sin_family = AF_INET,
    .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
  };
  assert(bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0);
  assert(listen(listener, 1) == 0);
  socklen_t addressLength = sizeof(address);
  assert(getsockname(listener, (struct sockaddr *)&address,
                     &addressLength) == 0);
  char tempFile[160];
  snprintf(tempFile, sizeof(tempFile), "%s.tmp", portFile);
  FILE *file = fopen(tempFile, "w");
  assert(file != NULL);
  assert(fprintf(file, "%u\n", ntohs(address.sin_port)) > 0);
  assert(fclose(file) == 0);
  assert(rename(tempFile, portFile) == 0);
  run_echo_server(listener);
}

static void
start_external_server(const char *program,
                      const char *mode,
                      const char *endpoint)
{
  pid_t child = fork();
  assert(child >= 0);
  if (child == 0) {
    execl("bin/dmtcp_nocheckpoint", "dmtcp_nocheckpoint",
          program, mode, endpoint, NULL);
    _exit(127);
  }
}

static void
wait_for_path(const char *path)
{
  for (int attempt = 0; attempt < 500; ++attempt) {
    if (access(path, F_OK) == 0) {
      return;
    }
    usleep(10000);
  }
  assert(0 && "external socket server did not become ready");
}

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

static void
make_unix_connection(socket_fn real_socket,
                     bind_fn real_bind,
                     listen_fn real_listen,
                     connect_fn real_connect,
                     accept_fn real_accept,
                     const struct sockaddr_un *address,
                     socklen_t addressLength,
                     int sockets[2])
{
  int listener = real_socket(AF_UNIX, SOCK_STREAM, 0);
  assert(listener >= 0);
  assert(real_bind(listener, (const struct sockaddr *)address,
                   addressLength) == 0);
  assert(real_listen(listener, 1) == 0);
  sockets[0] = real_socket(AF_UNIX, SOCK_STREAM, 0);
  assert(sockets[0] >= 0);
  assert(real_connect(sockets[0], (const struct sockaddr *)address,
                      addressLength) == 0);
  sockets[1] = real_accept(listener, NULL, NULL);
  assert(sockets[1] >= 0);
  close(listener);
}

int
main(int argc, char **argv)
{
  if (argc == 3 && strcmp(argv[1], "--unix-server") == 0) {
    assert(prctl(PR_SET_PDEATHSIG, SIGTERM) == 0);
    run_unix_server(argv[2]);
    return 0;
  }
  if (argc == 3 && strcmp(argv[1], "--tcp-server") == 0) {
    assert(prctl(PR_SET_PDEATHSIG, SIGTERM) == 0);
    run_tcp_server(argv[2]);
    return 0;
  }

  void *libc = dlopen("libc.so.6", RTLD_NOW | RTLD_LOCAL);
  assert(libc != NULL);

  socketpair_fn real_socketpair =
    (socketpair_fn)dlsym(libc, "socketpair");
  socket_fn real_socket = (socket_fn)dlsym(libc, "socket");
  bind_fn real_bind = (bind_fn)dlsym(libc, "bind");
  listen_fn real_listen = (listen_fn)dlsym(libc, "listen");
  connect_fn real_connect = (connect_fn)dlsym(libc, "connect");
  accept_fn real_accept = (accept_fn)dlsym(libc, "accept");
  assert(real_socketpair != NULL && real_socket != NULL &&
         real_bind != NULL &&
         real_listen != NULL && real_connect != NULL &&
         real_accept != NULL);

  int sockets[2] = { -1, -1 };
  assert(real_socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  int alias = dup(sockets[0]);
  assert(alias >= 0);
  assert(fcntl(alias, F_SETFD, FD_CLOEXEC) == 0);

  char externalUnixPath[sizeof(((struct sockaddr_un *)0)->sun_path)];
  snprintf(externalUnixPath, sizeof(externalUnixPath),
           "/tmp/dmtcp-socket-external-%ld.sock", (long)getpid());
  char externalUnixReady[128];
  snprintf(externalUnixReady, sizeof(externalUnixReady),
           "%s.ready", externalUnixPath);
  unlink(externalUnixPath);
  unlink(externalUnixReady);
  start_external_server(argv[0], "--unix-server", externalUnixPath);
  wait_for_path(externalUnixReady);
  int externalUnix = real_socket(AF_UNIX, SOCK_STREAM, 0);
  assert(externalUnix >= 0);
  struct sockaddr_un externalUnixAddress = {
    .sun_family = AF_UNIX,
  };
  strcpy(externalUnixAddress.sun_path, externalUnixPath);
  assert(real_connect(externalUnix,
                      (struct sockaddr *)&externalUnixAddress,
                      sizeof(externalUnixAddress)) == 0);
  unlink(externalUnixPath);
  unlink(externalUnixReady);

  char externalPortFile[128];
  snprintf(externalPortFile, sizeof(externalPortFile),
           "/tmp/dmtcp-socket-external-%ld.port", (long)getpid());
  unlink(externalPortFile);
  start_external_server(argv[0], "--tcp-server", externalPortFile);
  wait_for_path(externalPortFile);
  FILE *portFile = fopen(externalPortFile, "r");
  assert(portFile != NULL);
  unsigned int externalPort = 0;
  assert(fscanf(portFile, "%u", &externalPort) == 1);
  assert(fclose(portFile) == 0);
  int externalTcp = real_socket(AF_INET, SOCK_STREAM, 0);
  assert(externalTcp >= 0);
  struct sockaddr_in externalAddress = {
    .sin_family = AF_INET,
    .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    .sin_port = htons(externalPort),
  };
  assert(real_connect(externalTcp,
                      (struct sockaddr *)&externalAddress,
                      sizeof(externalAddress)) == 0);
  unlink(externalPortFile);
  pid_t originalRealPid = dmtcp_pid_virtual_to_real(getpid());

  struct sockaddr_un pathnameAddress = {
    .sun_family = AF_UNIX,
  };
  snprintf(pathnameAddress.sun_path, sizeof(pathnameAddress.sun_path),
           "/tmp/dmtcp-socket-discovery-%ld.sock", (long)getpid());
  unlink(pathnameAddress.sun_path);
  socklen_t pathnameLength =
    offsetof(struct sockaddr_un, sun_path) +
    strlen(pathnameAddress.sun_path) + 1;
  int pathnameSockets[2] = { -1, -1 };
  make_unix_connection(real_socket, real_bind, real_listen, real_connect,
                       real_accept, &pathnameAddress, pathnameLength,
                       pathnameSockets);
  unlink(pathnameAddress.sun_path);

  struct sockaddr_un abstractAddress = {
    .sun_family = AF_UNIX,
  };
  int abstractNameLength =
    snprintf(abstractAddress.sun_path + 1,
             sizeof(abstractAddress.sun_path) - 1,
             "dmtcp-socket-discovery-%ld", (long)getpid());
  assert(abstractNameLength > 0);
  socklen_t abstractLength =
    offsetof(struct sockaddr_un, sun_path) + 1 + abstractNameLength;
  int abstractSockets[2] = { -1, -1 };
  make_unix_connection(real_socket, real_bind, real_listen, real_connect,
                       real_accept, &abstractAddress, abstractLength,
                       abstractSockets);

  int seqpacketSockets[2] = { -1, -1 };
  assert(real_socketpair(AF_UNIX, SOCK_SEQPACKET, 0,
                         seqpacketSockets) == 0);
  int seqpacketAlias = dup(seqpacketSockets[0]);
  assert(seqpacketAlias >= 0);
  assert(fcntl(seqpacketAlias, F_SETFD, FD_CLOEXEC) == 0);

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
    bool restarted =
      dmtcp_pid_virtual_to_real(getpid()) != originalRealPid;
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

    snprintf(message, sizeof(message), "pathname-%u", iteration);
    exchange(pathnameSockets[0], pathnameSockets[1], message);

    snprintf(message, sizeof(message), "abstract-%u", iteration);
    exchange(abstractSockets[0], abstractSockets[1], message);

    assert(fcntl(seqpacketAlias, F_GETFD) == FD_CLOEXEC);
    snprintf(message, sizeof(message), "seqpacket-%u", iteration);
    exchange(seqpacketAlias, seqpacketSockets[1], message);

    if (restarted) {
      errno = 0;
      assert(send(externalUnix, message, sizeof(message),
                  MSG_NOSIGNAL) == -1);
      assert(errno == EPIPE);
      errno = 0;
      assert(send(externalTcp, message, sizeof(message),
                  MSG_NOSIGNAL) == -1);
      assert(errno == EPIPE);
    } else {
      snprintf(message, sizeof(message), "external-unix-%u", iteration);
      exchange(externalUnix, externalUnix, message);
      snprintf(message, sizeof(message), "external-tcp-%u", iteration);
      exchange(externalTcp, externalTcp, message);
    }

    printf("socket discovery iteration %u\n", iteration);
    fflush(stdout);
    sleep(1);
  }
}
