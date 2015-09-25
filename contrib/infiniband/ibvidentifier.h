#ifndef IBVID_H
#define IBVID_H
#include <stdint.h>

typedef struct ibv_qp_id {
  uint32_t qpn;
  uint16_t lid;
  uint32_t psn;
} ibv_qp_id;

typedef struct {
  uint32_t qpn;
  uint32_t lid;
} ibv_qp_pd_id, ibv_ud_qp_id;

struct ibv_rkey_id {
  int pd_id;
  uint32_t rkey;
};

ibv_qp_id * create_ibv_id(int qpn, int lid, void * buffer, int size);
#endif
