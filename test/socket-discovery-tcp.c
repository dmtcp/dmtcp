#define _GNU_SOURCE

#include <arpa/inet.h>
#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

typedef int (*socket_fn)(int, int, int);
typedef int (*bind_fn)(int, const struct sockaddr *, socklen_t);
typedef int (*listen_fn)(int, int);
typedef int (*connect_fn)(int, const struct sockaddr *, socklen_t);
typedef int (*accept_fn)(int, struct sockaddr *, socklen_t *);

static void
set_and_check_options(int fd)
{
  int enabled = 1;
  assert(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                    &enabled, sizeof(enabled)) == 0);
}

static void
check_options(int fd)
{
  int value = 0;
  socklen_t length = sizeof(value);
  assert(getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &value, &length) == 0);
  assert(value == 1);
}

int
main(void)
{
  void *libc = dlopen("libc.so.6", RTLD_NOW | RTLD_LOCAL);
  assert(libc != NULL);
  socket_fn real_socket = (socket_fn)dlsym(libc, "socket");
  bind_fn real_bind = (bind_fn)dlsym(libc, "bind");
  listen_fn real_listen = (listen_fn)dlsym(libc, "listen");
  connect_fn real_connect = (connect_fn)dlsym(libc, "connect");
  accept_fn real_accept = (accept_fn)dlsym(libc, "accept");
  assert(real_socket != NULL && real_bind != NULL &&
         real_listen != NULL && real_connect != NULL &&
         real_accept != NULL);

  int listener = real_socket(AF_INET, SOCK_STREAM, 0);
  assert(listener >= 0);
  int reuseAddress = 1;
  assert(setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                    &reuseAddress, sizeof(reuseAddress)) == 0);
  struct sockaddr_in address = {
    .sin_family = AF_INET,
    .sin_addr.s_addr = htonl(INADDR_ANY),
  };
  assert(real_bind(listener, (struct sockaddr *)&address,
                   sizeof(address)) == 0);
  assert(real_listen(listener, 4) == 0);
  socklen_t addressLength = sizeof(address);
  assert(getsockname(listener, (struct sockaddr *)&address,
                     &addressLength) == 0);
  struct sockaddr_in connectAddress = address;
  connectAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  enum { CLIENT_COUNT = 2 };
  for (unsigned int clientId = 0; clientId < CLIENT_COUNT; ++clientId) {
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
      close(listener);
      int client = real_socket(AF_INET, SOCK_STREAM, 0);
      assert(client >= 0);
      set_and_check_options(client);
      assert(setsockopt(client, SOL_SOCKET, SO_REUSEADDR,
                        &reuseAddress, sizeof(reuseAddress)) == 0);

      struct sockaddr_in clientAddress = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
      };
      assert(real_bind(client, (struct sockaddr *)&clientAddress,
                       sizeof(clientAddress)) == 0);
      socklen_t clientAddressLength = sizeof(clientAddress);
      assert(getsockname(client, (struct sockaddr *)&clientAddress,
                         &clientAddressLength) == 0);
      assert(real_connect(client, (struct sockaddr *)&connectAddress,
                          sizeof(connectAddress)) == 0);
      assert(send(client, &clientId, sizeof(clientId), 0) ==
             sizeof(clientId));

      for (;;) {
        struct sockaddr_in currentAddress = {};
        socklen_t currentAddressLength = sizeof(currentAddress);
        assert(getsockname(client, (struct sockaddr *)&currentAddress,
                           &currentAddressLength) == 0);
        assert(currentAddress.sin_port == clientAddress.sin_port);
        check_options(client);
        unsigned int value = 0;
        assert(recv(client, &value, sizeof(value), MSG_WAITALL) ==
               sizeof(value));
        value += clientId + 1;
        assert(send(client, &value, sizeof(value), 0) == sizeof(value));
      }
    }
  }

  int peers[CLIENT_COUNT] = { -1, -1 };
  for (unsigned int index = 0; index < CLIENT_COUNT; ++index) {
    int peer = real_accept(listener, NULL, NULL);
    assert(peer >= 0);
    set_and_check_options(peer);
    unsigned int clientId = CLIENT_COUNT;
    assert(recv(peer, &clientId, sizeof(clientId), MSG_WAITALL) ==
           sizeof(clientId));
    assert(clientId < CLIENT_COUNT);
    assert(peers[clientId] == -1);
    assert(fcntl(peer, F_SETOWN, getpid()) == 0);
    peers[clientId] = peer;
  }

  int owner = fcntl(peers[0], F_GETOWN);
  assert(owner > 0);
  for (unsigned int iteration = 0;; ++iteration) {
    for (unsigned int clientId = 0; clientId < CLIENT_COUNT; ++clientId) {
      assert(fcntl(peers[clientId], F_GETOWN) == owner);
      check_options(peers[clientId]);
      assert(send(peers[clientId], &iteration, sizeof(iteration), 0) ==
             sizeof(iteration));
      unsigned int response = 0;
      assert(recv(peers[clientId], &response, sizeof(response), MSG_WAITALL) ==
             sizeof(response));
      assert(response == iteration + clientId + 1);
    }
    printf("socket discovery TCP iteration %u\n", iteration);
    fflush(stdout);
    sleep(1);
  }
}
