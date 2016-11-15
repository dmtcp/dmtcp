#include <arpa/inet.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/un.h>

/* According to earlier standards */
#include <errno.h>
#include <infiniband/verbs.h>
#include <sys/types.h>
#include <unistd.h>
#include "jassert.h"
#include "dmtcp.h"
#include "ib2tcp.h"
#include "ib2tcp.h"
#include "ibwrappers.h"

using namespace dmtcp;

static int ib2t_post_send(struct ibv_qp *qp,
                          struct ibv_send_wr *wr,
                          struct ibv_send_wr **bad_wr);
static int ib2t_post_recv(struct ibv_qp *qp,
                          struct ibv_recv_wr *wr,
                          struct ibv_recv_wr **bad_wr);
static int ib2t_post_srq_recv(struct ibv_srq *srq,
                              struct ibv_recv_wr *wr,
                              struct ibv_recv_wr **bad_wr);
static int ib2t_poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc);
static int ib2t_req_notify_cq(struct ibv_cq *cq, int solicited_only);

#define DECL_FPTR(func)                                 \
  static __typeof__(&ib2t_ ## func)_real_ibv_ ## func = \
    (__typeof__(&ib2t_ ## func))NULL

#define UPDATE_FUNC_ADDR(func, addr) \
  do {                               \
    _real_ibv_ ## func = addr;       \
    addr = ib2t_ ## func;            \
  } while (0)

DECL_FPTR(post_recv);
DECL_FPTR(post_srq_recv);
DECL_FPTR(post_send);
DECL_FPTR(poll_cq);
DECL_FPTR(req_notify_cq);

extern int isVirtIB;

/**************************************************************/
/**************************************************************/
/**************************************************************/

#if 0
extern "C"
int
ibv_fork_init(void)
{
  JASSERT(!isVirtIB);
  return _real_ibv_fork_init();
}

extern "C"
struct ibv_device **
ibv_get_device_list(int *num_devices)
{
  JASSERT(!isVirtIB);
  return _real_ibv_get_device_list(num_devices);
}

extern "C"
const char *ibv_get_device_name(struct ibv_device *dev)
{
  JASSERT(!isVirtIB);
  return _real_ibv_get_device_name(dev);
}

extern "C"
int
ibv_query_device(struct ibv_context *context,
                 struct ibv_device_attr *device_attr)
{
  JASSERT(!isVirtIB);
  return _real_ibv_query_device(context, device_attr);
}

extern "C"
int
ibv_query_pkey(struct ibv_context *context,
               uint8_t port_num,
               int index,
               uint16_t *pkey)
{
  JASSERT(!isVirtIB);
  return _real_ibv_query_pkey(context, port_num, index, pkey);
}

extern "C"
int
ibv_query_gid(struct ibv_context *context,
              uint8_t port_num,
              int index,
              union ibv_gid *gid)
{
  JASSERT(!isVirtIB);
  return _real_ibv_query_gid(context, port_num, index, gid);
}

extern "C"
uint64_t
ibv_get_device_guid(struct ibv_device *device)
{
  JASSERT(!isVirtIB);
  return _real_ibv_get_device_guid(device);
}

extern "C"
struct ibv_comp_channel *
ibv_create_comp_channel(struct ibv_context *context)
{
  JASSERT(!isVirtIB);
  return _real_ibv_create_comp_channel(context);
}

extern "C"
int
ibv_destroy_comp_channel(struct ibv_comp_channel *channel)
{
  JASSERT(!isVirtIB);
  return _real_ibv_destroy_comp_channel(channel);
}

extern "C"
int
ibv_close_device(struct ibv_context *context)
{
  JASSERT(!isVirtIB);
  return _real_ibv_close_device(context);
}

extern "C"
void
ibv_free_device_list(struct ibv_device **list)
{
  JASSERT(!isVirtIB);
  _real_ibv_free_device_list(list);
}

extern "C"
int
ibv_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask)
{
  JASSERT(!isVirtIB);
  return _real_ibv_modify_qp(qp, attr, attr_mask);
}

extern "C"
int
ibv_query_qp(struct ibv_qp *qp,
             struct ibv_qp_attr *attr,
             int attr_mask,
             struct ibv_qp_init_attr *init_attr)
{
  JASSERT(!isVirtIB);
  return _real_ibv_query_qp(qp, attr, attr_mask, init_attr);
}

extern "C"
int
ibv_resize_cq(struct ibv_cq *cq, int cqe)
{
  JASSERT(!isVirtIB);
  return _real_ibv_resize_cq(cq, cqe);
}

extern "C"
int
ibv_modify_srq(struct ibv_srq *srq,
               struct ibv_srq_attr *srq_attr,
               int srq_attr_mask)
{
  JASSERT(!isVirtIB);
  return _real_ibv_modify_srq(srq, srq_attr, srq_attr_mask);
}

extern "C"
int
ibv_query_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr)
{
  JASSERT(!isVirtIB);
  return _real_ibv_query_srq(srq, srq_attr);
}

extern "C"
struct ibv_mr *
ibv_reg_mr(struct ibv_pd *pd, void *addr, size_t length, int access) // int
                                                                     // access)
{
  JASSERT(!isVirtIB);
  return _real_ibv_reg_mr(pd, addr, length, access);
}

extern "C"
int
ibv_dereg_mr(struct ibv_mr *mr)
{
  JASSERT(!isVirtIB);
  return _real_ibv_dereg_mr(mr);
}

extern "C"
struct ibv_cq *
ibv_create_cq(struct ibv_context *context,
              int cqe,
              void *cq_context,
              struct ibv_comp_channel *channel,
              int comp_vector)
{
  JASSERT(!isVirtIB);
  return _real_ibv_create_cq(context, cqe, cq_context, channel, comp_vector);
}

extern "C"
int
ibv_destroy_cq(struct ibv_cq *cq)
{
  JASSERT(!isVirtIB);
  return _real_ibv_destroy_cq(cq);
}

extern "C"
int
ibv_create_xrc_rcv_qp(struct ibv_qp_init_attr *init_attr, uint32_t *xrc_rcv_qpn)
{
  JASSERT(false);
  return 0;
}

extern "C"
struct ibv_srq *
ibv_create_xrc_srq(struct ibv_pd *pd,
                   struct ibv_xrc_domain *xrc_domain,
                   struct ibv_cq *xrc_cq,
                   struct ibv_srq_init_attr *srq_init_attr)
{
  JASSERT(false);
  return NULL;
}

extern "C"
struct ibv_srq *
ibv_create_srq(struct ibv_pd *pd, struct ibv_srq_init_attr *srq_init_attr)
{
  JASSERT(!isVirtIB);
  struct ibv_srq *qp = _real_ibv_create_srq(pd, srq_init_attr);

  if (qp != NULL) {
    IB2TCP::createSRQ(qp);
  }
  return qp;
}

extern "C"
int ibv_modify_srq(struct ibv_srq *srq,
                   struct ibv_srq_attr *srq_attr,
                   int srq_attr_mask);
{
  int rslt = _real_ibv_modify_srq(srq, srq_attr, srq_attr_mask);
  if (rslt == 0) {
    IB2TCP::modifySRQ(srq, srq_attr, srq_attr_mask);
  }
  return rslt;
}

extern "C"
int
ibv_destroy_srq(struct ibv_srq *srq)
{
  JASSERT(false);
  JASSERT(!isVirtIB);
  return _real_ibv_destroy_srq(srq);
}
#endif // if 0

extern "C"
struct ibv_context *
ibv_open_device(struct ibv_device *dev)
{
  JASSERT(!isVirtIB);

  struct ibv_context *ctx = _real_ibv_open_device(dev);

  if (ctx != NULL) {
    /* setup the trampolines */
    UPDATE_FUNC_ADDR(post_recv, ctx->ops.post_recv);
    UPDATE_FUNC_ADDR(post_srq_recv, ctx->ops.post_srq_recv);
    UPDATE_FUNC_ADDR(post_send, ctx->ops.post_send);
    UPDATE_FUNC_ADDR(poll_cq, ctx->ops.poll_cq);
    UPDATE_FUNC_ADDR(req_notify_cq, ctx->ops.req_notify_cq);
  }
  return ctx;
}

extern "C"
struct ibv_qp *
ibv_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr)
{
  JASSERT(!isVirtIB);
  struct ibv_qp *qp = _real_ibv_create_qp(pd, qp_init_attr);

  if (qp != NULL) {
    IB2TCP::createQP(qp, qp_init_attr);
  }
  return qp;
}

extern "C"
int
ibv_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask)
{
  int rslt = _real_ibv_modify_qp(qp, attr, attr_mask);

  if (rslt == 0) {
    IB2TCP::modifyQP(qp, attr, attr_mask);
  }
  return rslt;
}

extern "C"
int
ibv_destroy_qp(struct ibv_qp *qp)
{
  JASSERT(false);
  JASSERT(!isVirtIB);
  return _real_ibv_destroy_qp(qp);
}

/***********************************************************/
/***********************************************************/
/***********************************************************/
/***********************************************************/

extern "C"
void
ibv_ack_cq_events(struct ibv_cq *cq, unsigned int nevents)
{
  JASSERT(false);

  // FIXME:
  if (!isVirtIB) {
    _real_ibv_ack_cq_events(cq, nevents);
  }
}

extern "C"
int
ibv_get_cq_event(struct ibv_comp_channel *channel,
                 struct ibv_cq **cq,
                 void **cq_context)
{
  JASSERT(false);

  // FIXME:
  if (!isVirtIB) {
    return _real_ibv_get_cq_event(channel, cq, cq_context);
  }
}

extern "C"
int
ibv_get_async_event(struct ibv_context *context, struct ibv_async_event *event)
{
  JASSERT(false);

  // FIXME:
  if (!isVirtIB) {
    return _real_ibv_get_async_event(context, event);
  }
}

extern "C"
void
ibv_ack_async_event(struct ibv_async_event *event)
{
  JASSERT(false);

  // FIXME:
  if (!isVirtIB) {
    _real_ibv_ack_async_event(event);
  }
}

/***********************************************************/
/***********************************************************/
/***********************************************************/
/***********************************************************/

static int
ib2t_post_send(struct ibv_qp *qp,
               struct ibv_send_wr *wr,
               struct ibv_send_wr **bad_wr)
{
  int ret = 0;

  if (!isVirtIB) {
    ret = _real_ibv_post_send(qp, wr, bad_wr);
  }
  if (ret == 0) {
    IB2TCP::postSend(qp, wr, bad_wr);
  }
  return ret;
}

static int
ib2t_post_recv(struct ibv_qp *qp,
               struct ibv_recv_wr *wr,
               struct ibv_recv_wr **bad_wr)
{
  int ret = 0;

  if (!isVirtIB) {
    ret = _real_ibv_post_recv(qp, wr, bad_wr);
  }
  if (ret == 0) {
    IB2TCP::postRecv(qp, wr, bad_wr);
  }
  return ret;
}

static int
ib2t_post_srq_recv(struct ibv_srq *srq,
                   struct ibv_recv_wr *wr,
                   struct ibv_recv_wr **bad_wr)
{
  int ret = 0;

  if (!isVirtIB) {
    ret = _real_ibv_post_srq_recv(srq, wr, bad_wr);
  }
  if (ret == 0) {
    IB2TCP::postSrqRecv(srq, wr, bad_wr);
  }
  return ret;
}

static int
ib2t_poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc)
{
  // FIXME: Drain completion queue on ckpt.
  int ret = 0;

  if (isVirtIB) {
    ret = IB2TCP::pollCq(cq, num_entries, wc);
  } else {
    // if (ret < num_entries) {
    int r = _real_ibv_poll_cq(cq, num_entries, &wc[ret]);
    IB2TCP::postPollCq(cq, r, &wc[ret]);
    ret += r;
  }
  return ret;
}

static int
ib2t_req_notify_cq(struct ibv_cq *cq, int solicited_only)
{
  JASSERT(false);
  if (!isVirtIB) {
    return _real_ibv_req_notify_cq(cq, solicited_only);
  }
}
