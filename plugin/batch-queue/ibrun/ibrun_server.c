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

#define BACKLOG 128

typedef struct {
  char hostname[16];
  int cur;
  int max;
} slots;

static int number_of_ckpts = 0;
static int number_of_hosts = 0;

int
main()
{
  char *port = getenv("IBRUN_PORT");

  if (!port) {
    fprintf(stderr, "env var is not set\n");
    exit(1);
  }

  struct sockaddr_in sockaddr;
  int listener_sd, rc;

  memset(&sockaddr, 0, sizeof sockaddr);
  sockaddr.sin_family = AF_INET;
  sockaddr.sin_addr.s_addr = INADDR_ANY;
  sockaddr.sin_port = htons(atoi(port));

  if ((listener_sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket");
    exit(2);
  }

  if ((bind(listener_sd, (struct sockaddr *)&sockaddr,
            sizeof sockaddr)) == -1) {
    perror("bind");
    exit(2);
  }

  if ((listen(listener_sd, BACKLOG)) == -1) {
    perror("listen");
    exit(2);
  }

  number_of_ckpts = atoi(getenv("NUM_CKPTS"));
  number_of_hosts = atoi(getenv("DMTCP_REMLAUNCH_NODES"));

  slots *slots_list = (slots *)malloc(number_of_hosts * sizeof(slots));
  memset(slots_list, 0, number_of_hosts * sizeof(slots));

  int slots_per_host = number_of_ckpts / number_of_hosts;
  int remaining_slots = number_of_ckpts % number_of_hosts;

  int i = 0;
  for (; i < number_of_hosts; ++i) {
    slots_list[i].max = slots_per_host;
    if (remaining_slots > 0) {
      ++slots_list[i].max;
      --remaining_slots;
    }
  }

  int next_id = 0;
  int client_sd;
  unique_id id;
  struct sockaddr_in client;
  size_t size;

  for (; next_id < number_of_ckpts; ++next_id) {
    size = sizeof(client);
    if ((client_sd = accept(listener_sd,
                            (struct sockaddr *)&client,
                            (socklen_t *)&size)) == -1) {
      perror("accept");
      continue;
    }

    for (i = 0; i < number_of_hosts; ++i) {
      char *client_name = inet_ntoa(client.sin_addr);
      if (slots_list[i].cur == 0) {
        strcpy(slots_list[i].hostname, client_name);
        ++slots_list[i].cur;
        id.node_id = i;
        id.process_id = 0;
        break;
      } else if (strcmp(slots_list[i].hostname, client_name) == 0) {
        id.node_id = i;
        id.process_id = slots_list[i].cur++;
        break;
      }
    }

    if (i >= number_of_hosts) {
      fprintf(stderr, "Error during allocating id\n");
      exit(3);
    }

    if (write(client_sd, &id, sizeof id) == -1) {
      perror("write");
      exit(3);
    }
  }

  free(slots_list);
  exit(0);
}
