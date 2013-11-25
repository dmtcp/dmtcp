#ifndef IBVID_H
#define IBVID_H
#include <sys/types.h>
#include <infiniband/verbs.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>

struct ibv_qp_id {
  uint32_t qpn;
  uint16_t lid;
  uint32_t psn;
};

struct ibv_qp_pd_id {
  uint32_t qpn;
  uint32_t lid;
};

struct ibv_rkey_id {
  int pd_id;
  uint32_t rkey;
};

struct ibv_qp_id * create_ibv_id(int qpn, int lid, void * buffer, int size);
#endif
