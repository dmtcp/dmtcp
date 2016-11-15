#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include "ibrun_common.h"

#define MAX_LEN 128

int
main()
{
  char *port = getenv("IBRUN_PORT");
  char *server = getenv("IBRUN_HOST");

  if (!port || !server) {
    fprintf(stderr, "env var is not set\n");
    exit(1);
  }

  int sockfd;
  struct addrinfo hints, *servinfo, *p;
  int ret;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  if ((ret = getaddrinfo(server, port, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
    exit(1);
  }

  for (p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype,
                         p->ai_protocol)) == -1) {
      perror("socket");
      continue;
    }

    if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      perror("connect");
      continue;
    }

    break;
  }

  if (p == NULL) {
    fprintf(stderr, "Failed to connect.\n");
    exit(2);
  }

  freeaddrinfo(servinfo);

  unique_id id = {
    .node_id = 0,
    .process_id = 0
  };

  if (read(sockfd, &id, sizeof id) == -1) {
    perror("read");
    exit(3);
  }

  printf("export SLURM_NODEID=%d ; export SLURM_LOCALID=%d\n",
         id.node_id, id.process_id);

  exit(0);
}
