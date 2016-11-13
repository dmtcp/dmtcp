#define __USE_GNU
#define _GNU_SOURCE
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
#include "infinibandreals.h"

/* This macro requires a static local declaration of "next_fnc". */
#define NEXT_FNC(symbol) \
  (next_fnc ? *next_fnc : *(next_fnc = dlsym(RTLD_NEXT, # symbol)))


// : REMOVE THS
#undef JNOTE
#define JNOTE(X) printf(X); printf("\n");

struct ibv_device **
ibv_get_device_list(int *num_devices)
{
  JNOTE("****** WRAPPER for ibv_get_device_list");

  return _real_ibv_get_device_list(num_devices);
}

const char *
ibv_get_device_name(struct ibv_device *dev)
{
  JNOTE("****** WRAPPER for ibv_get_device_name");

  return _real_ibv_get_device_name(dev);
}

struct ibv_context *
ibv_open_device(struct ibv_device *dev)
{
  JNOTE("******* WRAPPER for begin of ibv_open_device");

  return _real_ibv_open_device(dev);
}

int
ibv_query_device(struct ibv_context *context,
                 struct ibv_device_attr *device_attr)
{
  JNOTE("******* WRAPPER for begin of ibv_query_device");

  return _real_ibv_query_device(context, device_attr);
}

int
ibv_query_pkey(struct ibv_context *context,
               uint8_t port_num,
               int index,
               uint16_t *pkey)
{
  JNOTE("******* WRAPPER for begin of ibv_query_pkey");

  return ibv_query_pkey(context, port_num, index, pkey);
}

int
ibv_query_gid(struct ibv_context *context,
              uint8_t port_num,
              int index,
              union ibv_gid *gid)
{
  JNOTE("******* WRAPPER for begin of ibv_query_gid");

  return _real_ibv_query_gid(context, port_num, index, gid);
}

uint64_t
ibv_get_device_guid(struct ibv_device *device)
{
  JNOTE("******* WRAPPER for ibv_get_device_guid");

  return _real_ibv_get_device_guid(device);
}

struct ibv_comp_channel *
ibv_create_comp_channel(struct ibv_context
                        *context)
{
  JNOTE("******* WRAPPER for ibv_create_comp_channel");

  return _real_ibv_create_comp_channel(context);
}

int
ibv_destroy_comp_channel(struct ibv_comp_channel *channel)
{
  JNOTE("****** WRAPPER for ibv_destroy_comp_channel");

  return _real_ibv_destroy_comp_channel(channel);
}

struct ibv_pd *
ibv_alloc_pd(struct ibv_context *context)
{
  JNOTE("******* WRAPPER FOR ibv_alloc_pd");

  return _real_ibv_alloc_pd(context);
}

struct ibv_mr *
ibv_reg_mr(struct ibv_pd *pd, void *addr, size_t length, int access) // int
                                                                     // access)
{
  JNOTE("******** WRAPPER for ibv_reg_mr");

  return _real_ibv_reg_mr(pd, addr, length, access);
}

struct ibv_cq *
ibv_create_cq(struct ibv_context *context,
              int cqe,
              void *cq_context,
              struct ibv_comp_channel *channel,
              int comp_vector)
{
  JNOTE("******** WRAPPER for ibv_create_cq");

  return _real_ibv_create_cq(context, cqe, cq_context, channel, comp_vector);
}

struct ibv_qp *
ibv_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr)
{
  JNOTE("******** WRAPPER for ibv_create_qp");

  return _real_ibv_create_qp(pd, qp_init_attr);
}

int
ibv_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask) // int
                                                                          // attr_mask)
{
  JNOTE("********* WRAPPER for ibv_modify_qp");
  attr->rq_psn = attr->rq_psn & 0xffffff;
  attr->sq_psn = attr->sq_psn & 0xffffff;
  if (attr_mask & IBV_QP_RQ_PSN) {
    printf("1RQ_PSN is %x\n", attr->rq_psn);
  }

  if (attr_mask & IBV_QP_SQ_PSN) {
    printf("1SQ_PSN is %x\n", attr->sq_psn);
  }

  if (attr_mask & IBV_QP_STATE) {
    if (attr->qp_state == IBV_QPS_RTR) {
      printf("RQ_PSN is %x\n", attr->rq_psn);
    } else if (attr->qp_state == IBV_QPS_RTS) {
      printf("SQ_PSN is %x\n", attr->sq_psn);
    }
  }

  int rslt = _real_ibv_modify_qp(qp, attr, attr_mask);


  struct ibv_qp_attr query_attr;
  struct ibv_qp_init_attr init_attr;
  int foo = _real_ibv_query_qp(qp, &query_attr, attr_mask, &init_attr);

  if (attr_mask & IBV_QP_RQ_PSN) {
    printf("attr.rq_psn is %x\n", query_attr.rq_psn);
  }

  if (attr_mask & IBV_QP_SQ_PSN) {
    printf("attr.sq_psn is %x\n", query_attr.sq_psn);
  }

  if (attr_mask & IBV_QP_STATE) {
    printf("qp state set attr.sq_psn is %x\n", query_attr.sq_psn);
    printf("qp state set attr.rq_psn is %x\n", query_attr.rq_psn);
  }

  return rslt;
}

int
ibv_get_cq_event(struct ibv_comp_channel *channel,
                 struct ibv_cq **cq,
                 void **cq_context)
{
  // JNOTE("******** WRAPPER for ibv_get_cq_event");

  return _real_ibv_get_cq_event(channel, cq, cq_context);
}

int
ibv_query_qp(struct ibv_qp *qp,
             struct ibv_qp_attr *attr,
             int attr_mask,
             struct ibv_qp_init_attr *init_attr)
{
  JNOTE("******** WRAPPER FOR ibv_query_qp");

  return _real_ibv_query_qp(qp, attr, attr_mask, init_attr);
}

int
ibv_get_async_event(struct ibv_context *context, struct ibv_async_event *event)
{
  JNOTE("******** WRAPPER FOR ibv_get_async_event");

  return _real_ibv_get_async_event(context, event);
}

void
ibv_ack_async_event(struct ibv_async_event *event)
{
  JNOTE("******* WRAPPER FOR ibv_ack_async_event");

  _real_ibv_ack_async_event(event);
}

int
ibv_resize_cq(struct ibv_cq *cq, int cqe)
{
  JNOTE("****** WRAPPER for ibv_resize_cq");

  return _real_ibv_resize_cq(cq, cqe);
}

int
ibv_destroy_cq(struct ibv_cq *cq)
{
  JNOTE("****** WRAPPER for ibv_destroy_cq");

  return _real_ibv_destroy_cq(cq);
}

int
ibv_destroy_qp(struct ibv_qp *qp)
{
  JNOTE("****** WRAPPER2 for ibv_destroy qp");

  return _real_ibv_destroy_qp(qp);
}

int
ibv_dereg_mr(struct ibv_mr *mr)
{
  JNOTE("****** WRAPPER for ibv_dereg_mr");

  return _real_ibv_dereg_mr(mr);
}

int
ibv_dealloc_pd(struct ibv_pd *pd)
{
  JNOTE("****** WRAPPER for ibv_dealloc_pd");

  return _real_ibv_dealloc_pd(pd);
}

int
ibv_close_device(struct ibv_context *context)
{
  JNOTE("***** WRAPPER for ibv_close_device");

  return _real_ibv_close_device(context);
}

void
ibv_free_device_list(struct ibv_device **list)
{
  JNOTE("********* WRAPPER for ibv_free_device_list");

  _real_ibv_free_device_list(list);
}

void
ibv_ack_cq_events(struct ibv_cq *cq, unsigned int nevents)
{
  JNOTE("******** WRAPPER for ibv_ack_cq_events");

  _real_ibv_ack_cq_events(cq, nevents);
}
