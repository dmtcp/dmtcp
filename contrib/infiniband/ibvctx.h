/****************************************************************************
 *   Copyright (C) 2011-2015 by Greg Kerr, Jiajun Cao, Kapil Arya, and      *
 *   Gene Cooperman                                                         *
 *   kerrgi@gmail.com, jiajun@ccs.neu.edu, kapil@ccs.neu.edu, and           *
 *   gene@ccs.neu.edu                                                       *
 *                                                                          *
 *   This file is part of the infiniband plugin for DMTCP                   *
 *   (DMTCP:plugin/infiniband).                                             *
 *                                                                          *
 *  DMTCP:plugin/infiniband is free software: you can redistribute it and/or*
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP:plugin/infininband is distributed in the hope that it will be     *
 *  useful, but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:plugin/infiniband.  If not, see                *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#define _GNU_SOURCE
#include <dlfcn.h>
#include <infiniband/verbs.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// libibverbs.so includes two versions for each symbol, 1.1 for new one, which
// is the default, and 1.0 for the old. We use dlvsym to call the new one.
#define NEXT_IBV_FNC(func)                                            \
  ({                                                                  \
    static __typeof__(&func)_real_ ## func = (__typeof__(&func)) - 1; \
    if (_real_ ## func == (__typeof__(&func)) - 1) {                  \
      _real_ ## func =                                                \
        (__typeof__(&func))dlvsym(RTLD_NEXT, # func, "IBVERBS_1.1");  \
    }                                                                 \
    _real_ ## func;                                                   \
  })

// For some reason, ibv_create_comp_channel() and ibv_destroy_comp_channel()
// have version 1.0 only
#define NEXT_IBV_COMP_CHANNEL(func)                                   \
  ({                                                                  \
    static __typeof__(&func)_real_ ## func = (__typeof__(&func)) - 1; \
    if (_real_ ## func == (__typeof__(&func)) - 1) {                  \
      _real_ ## func =                                                \
        (__typeof__(&func))dlvsym(RTLD_NEXT, # func, "IBVERBS_1.0");  \
    }                                                                 \
    _real_ ## func;                                                   \
  })

void pre_checkpoint(void);
int _fork_init(void);
struct ibv_device **_get_device_list(int *num_devices);
const char *_get_device_name(struct ibv_device *device);
void _free_device_list(struct ibv_device **list);
struct ibv_context *_open_device(struct ibv_device *device);
int _query_device(struct ibv_context *context,
                  struct ibv_device_attr *device_attr);
int _query_port(struct ibv_context *context,
                uint8_t port_num,
                struct ibv_port_attr *port_attr);
int _query_pkey(struct ibv_context *context,
                uint8_t port_num,
                int index,
                uint16_t *pkey);
int _query_gid(struct ibv_context *context,
               uint8_t port_num,
               int index,
               union ibv_gid *gid);
uint64_t _get_device_guid(struct ibv_device *dev);
struct ibv_comp_channel *_create_comp_channel(struct ibv_context *context);
int _destroy_comp_channel(struct ibv_comp_channel *channel);
int _close_device(struct ibv_context *ctx);
int _req_notify_cq(struct ibv_cq *cq, int solicited_only);
int _get_cq_event(struct ibv_comp_channel *channel,
                  struct ibv_cq **cq,
                  void **cq_context);
int _get_async_event(struct ibv_context *context,
                     struct ibv_async_event *event);
void _ack_async_event(struct ibv_async_event *event);
struct ibv_pd *_alloc_pd(struct ibv_context *context);
struct ibv_mr *_reg_mr(struct ibv_pd *pd, void *addr, size_t length, int flag);
struct ibv_cq *_create_cq(struct ibv_context *context,
                          int cqe,
                          void *cq_context,
                          struct ibv_comp_channel *channel,
                          int comp_vector);
struct ibv_srq *_create_srq(struct ibv_pd *pd,
                            struct ibv_srq_init_attr *srq_init_attr);
int _modify_srq(struct ibv_srq *srq, struct ibv_srq_attr *attr, int attr_mask);
int _query_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr);

struct ibv_qp *_create_qp(struct ibv_pd *pd,
                          struct ibv_qp_init_attr *qp_init_attr);
int _modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask);
int _resize_cq(struct ibv_cq *cq, int cqe);
int _query_qp(struct ibv_qp *qp,
              struct ibv_qp_attr *attr,
              int attr_mask,
              struct ibv_qp_init_attr *init_attr);
int _post_recv(struct ibv_qp *qp,
               struct ibv_recv_wr *wr,
               struct ibv_recv_wr **bad_wr);
int _post_srq_recv(struct ibv_srq *srq,
                   struct ibv_recv_wr *wr,
                   struct ibv_recv_wr **bad_wr);
int _post_send(struct ibv_qp *qp,
               struct ibv_send_wr *wr,
               struct ibv_send_wr **bad_wr);
int _poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc);
int _destroy_cq(struct ibv_cq *cq);
int _destroy_srq(struct ibv_srq *srq);
int _destroy_qp(struct ibv_qp *qp);
int _dereg_mr(struct ibv_mr *mr);
int _dealloc_pd(struct ibv_pd *pd);
void _ack_cq_events(struct ibv_cq *cq, unsigned int nevents);
struct ibv_ah *_create_ah(struct ibv_pd *pd, struct ibv_ah_attr *attr);
int _destroy_ah(struct ibv_ah *ah);
struct ibv_mw *_alloc_mw(struct ibv_pd *pd, enum ibv_mw_type type);
int _bind_mw(struct ibv_qp *qp, struct ibv_mw *mw, struct ibv_mw_bind *mw_bind);
int _dealloc_mw(struct ibv_mw *mw);
