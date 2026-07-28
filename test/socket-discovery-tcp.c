#define _GNU_SOURCE

#include <arpa/inet.h>
#include <assert.h>
#include <dlfcn.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

typedef int (*socket_fn)(int, int, int);
typedef int (*bind_fn)(int, const struct sockaddr *, socklen_t);
typedef int (*listen_fn)(int, int);
typedef int (*connect_fn)(int, const struct sockaddr *, socklen_t);
typedef int (*accept_fn)(int, struct sockaddr *, socklen_t *);

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
  struct sockaddr_in address = {
    .sin_family = AF_INET,
    .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
  };
  assert(real_bind(listener, (struct sockaddr *)&address,
                   sizeof(address)) == 0);
  assert(real_listen(listener, 4) == 0);
  socklen_t addressLength = sizeof(address);
  assert(getsockname(listener, (struct sockaddr *)&address,
                     &addressLength) == 0);

  pid_t child = fork();
  assert(child >= 0);
  if (child == 0) {
    close(listener);
    int client = real_socket(AF_INET, SOCK_STREAM, 0);
    assert(client >= 0);
    assert(real_connect(client, (struct sockaddr *)&address,
                        sizeof(address)) == 0);
    for (;;) {
      unsigned int value = 0;
      assert(recv(client, &value, sizeof(value), MSG_WAITALL) ==
             sizeof(value));
      ++value;
      assert(send(client, &value, sizeof(value), 0) == sizeof(value));
    }
  }

  int peer = real_accept(listener, NULL, NULL);
  assert(peer >= 0);
  close(listener);
  for (unsigned int iteration = 0;; ++iteration) {
    assert(send(peer, &iteration, sizeof(iteration), 0) ==
           sizeof(iteration));
    unsigned int response = 0;
    assert(recv(peer, &response, sizeof(response), MSG_WAITALL) ==
           sizeof(response));
    assert(response == iteration + 1);
    printf("socket discovery TCP iteration %u\n", iteration);
    fflush(stdout);
    sleep(1);
  }
}
