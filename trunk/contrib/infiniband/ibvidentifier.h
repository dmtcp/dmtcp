#ifndef IBVID_H
#define IBVID_H
#include <sys/types.h>
#include <infiniband/verbs.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include "constants.h"

#ifdef IBV

struct ibv_qp_id {
  int       qpn;
  int       lid;
  uint32_t  psn;
};

struct ibv_qp_id * create_ibv_id(int qpn, int lid, void * buffer, int size);
#endif
#endif
