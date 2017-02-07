#ifndef IBVID_H
#define IBVID_H
#include <stdint.h>
#include "lib/list.h"

typedef struct ibv_qp_id {
  uint32_t qpn;
  uint16_t lid;
  uint32_t psn;
} ibv_qp_id_t;

typedef struct {
  uint32_t qpn;
  uint16_t lid;
} ibv_qp_pd_id_t, ibv_ud_qp_id_t;

struct ibv_rkey_id {
  uint32_t pd_id;
  uint32_t rkey;
};

struct ibv_rkey_pair {
  struct ibv_rkey_id orig_rkey;
  uint32_t new_rkey;
  struct list_elem elem;
};

struct ibv_ud_qp_id_pair {
  ibv_ud_qp_id_t orig_id;
  ibv_ud_qp_id_t curr_id;
  struct list_elem elem;
};

typedef struct qp_num_mapping {
  uint32_t virtual_qp_num;
  uint32_t real_qp_num;
  struct list_elem elem;
} qp_num_mapping_t;

ibv_qp_id_t * create_ibv_id(int qpn, int lid, void * buffer, int size);
#endif
