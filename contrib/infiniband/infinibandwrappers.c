#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "dmtcp.h"

#include "ibvctx.h"
#include "debug.h"
#include <infiniband/verbs.h>

void *dlopen(const char *filename, int flag) {
  if (filename) {
    if (strstr(filename, "libibverbs.so")) {
      return RTLD_DEFAULT;
    }
  }
  return NEXT_FNC(dlopen)(filename, flag);
}

int ibv_fork_init(void)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("****** WRAPPER for ibv_fork_init ******\n");
  int rslt = _fork_init();
  dmtcp_plugin_enable_ckpt();
  return rslt;
}

struct ibv_device **ibv_get_device_list(int *num_devices)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("****** WRAPPER for ibv_get_device_list\n");

  struct ibv_device ** result = _get_device_list(num_devices);

  dmtcp_plugin_enable_ckpt();
  return result;
}

const char *ibv_get_device_name(struct ibv_device * dev)
{
//  dmtcp_plugin_disable_ckpt();
//  PDEBUG("****** WRAPPER for ibv_get_device_name\n");

  const char * rslt = _get_device_name(dev);

//  dmtcp_plugin_enable_ckpt();
  return rslt;
}

struct ibv_context *ibv_open_device(struct ibv_device *dev)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("******* WRAPPER for begin of ibv_open_device\n");

  struct ibv_context * user_copy = _open_device(dev);

  dmtcp_plugin_enable_ckpt();
  return user_copy;
}

int ibv_query_device(struct ibv_context *context, struct ibv_device_attr *device_attr)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("******* WRAPPER for begin of ibv_query_device\n");

  int rslt = _query_device(context,device_attr);

  dmtcp_plugin_enable_ckpt();
  return rslt;
}

int ibv_query_pkey(struct ibv_context *context, uint8_t port_num,  int index, uint16_t *pkey)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("******* WRAPPER for begin of ibv_query_pkey\n");

  int rslt = _query_pkey(context,port_num, index, pkey);

  dmtcp_plugin_enable_ckpt();
  return rslt;
}

int ibv_query_gid(struct ibv_context *context, uint8_t port_num, int index, union ibv_gid *gid)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("******* WRAPPER for begin of ibv_query_gid\n");

  int rslt = _query_gid(context,port_num, index, gid);

  dmtcp_plugin_enable_ckpt();
  return rslt;
}

uint64_t ibv_get_device_guid(struct ibv_device *device)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("******* WRAPPER for ibv_get_device_guid\n");

  uint64_t rslt = _get_device_guid(device);

  dmtcp_plugin_enable_ckpt();
  return rslt;
}

struct ibv_comp_channel * ibv_create_comp_channel(struct ibv_context
                                                                *context)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("******* WRAPPER for ibv_create_comp_channel\n");

  struct ibv_comp_channel * rslt = _create_comp_channel(context);

  dmtcp_plugin_enable_ckpt();
  return rslt;
}

int ibv_destroy_comp_channel(struct ibv_comp_channel * channel)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("****** WRAPPER for ibv_destroy_comp_channel\n");

  int rslt = _destroy_comp_channel(channel);

  dmtcp_plugin_enable_ckpt();
  return rslt;
}

struct ibv_pd *ibv_alloc_pd(struct ibv_context *context)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("******* WRAPPER FOR ibv_alloc_pd\n");

  struct ibv_pd * user_copy = _alloc_pd(context);

  dmtcp_plugin_enable_ckpt();
  return user_copy;
}

struct ibv_mr *ibv_reg_mr(struct ibv_pd *pd, void *addr,
                          size_t length,
                          int access) //int access)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("******** WRAPPER for ibv_reg_mr\n");

  struct ibv_mr * user_copy = _reg_mr(pd, addr, length, access);

  dmtcp_plugin_enable_ckpt();
  return user_copy;
}

struct ibv_cq *ibv_create_cq(struct ibv_context *context, int cqe,
                             void *cq_context,
                             struct ibv_comp_channel *channel,
                             int comp_vector)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("******** WRAPPER for ibv_create_cq\n");

  struct ibv_cq * user_copy = _create_cq(context, cqe, cq_context, channel, comp_vector);

  dmtcp_plugin_enable_ckpt();
  return user_copy;
}

struct ibv_srq *ibv_create_srq(struct ibv_pd * pd, struct ibv_srq_init_attr * srq_init_attr)
{
  dmtcp_plugin_disable_ckpt();
 // PDEBUG("******** WRAPPER for ibv_create_srq\n");

  struct ibv_srq * user_copy = _create_srq(pd, srq_init_attr);

  dmtcp_plugin_enable_ckpt();
  return user_copy;
}

int ibv_modify_srq(struct ibv_srq *srq,
                   struct ibv_srq_attr *srq_attr,
                   int srq_attr_mask)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("******** WRAPPER for ibv_modify_srq\n");
  int rslt = _modify_srq(srq, srq_attr, srq_attr_mask);
  dmtcp_plugin_enable_ckpt();
  return rslt;
}

int ibv_query_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("******** WRAPPER for ibv_query_srq\n");
  int rslt = _query_srq(srq, srq_attr);
  dmtcp_plugin_enable_ckpt();
  return rslt;
}

int ibv_destroy_srq(struct ibv_srq *srq)
{
  dmtcp_plugin_disable_ckpt();
 // PDEBUG("******** WRAPPER for ibv_destroy_srq\n");
  int rslt = _destroy_srq(srq);
  dmtcp_plugin_enable_ckpt();
  return rslt;
}

struct ibv_qp *ibv_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("******** WRAPPER for ibv_create_qp\n");

  struct ibv_qp * user_copy = _create_qp(pd, qp_init_attr);

  dmtcp_plugin_enable_ckpt();
  return user_copy;
}

int ibv_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
                   int attr_mask) //int attr_mask)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("********* WRAPPER for ibv_modify_qp\n");

  int rslt = _modify_qp(qp, attr, attr_mask);

  dmtcp_plugin_enable_ckpt();
  return rslt;
}

int ibv_get_cq_event(struct ibv_comp_channel *channel, struct ibv_cq **cq,
                        void **cq_context)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("******** WRAPPER for ibv_get_cq_event");

  int rslt = _get_cq_event(channel, cq, cq_context);

  dmtcp_plugin_enable_ckpt();
  return rslt;
}

int ibv_query_qp(struct ibv_qp * qp, struct ibv_qp_attr * attr,
                 int attr_mask, struct ibv_qp_init_attr *init_attr)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("******** WRAPPER FOR ibv_query_qp\n");

  int rslt = _query_qp(qp, attr, attr_mask, init_attr);

  dmtcp_plugin_enable_ckpt();
  return rslt;
}

int ibv_get_async_event(struct ibv_context *context, struct ibv_async_event *event)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("******** WRAPPER FOR ibv_get_async_event\n");

  int rslt = _get_async_event(context, event);

  dmtcp_plugin_enable_ckpt();
  return rslt;
}

void ibv_ack_async_event(struct ibv_async_event *event)
{
  dmtcp_plugin_disable_ckpt();
 // PDEBUG("******* WRAPPER FOR ibv_ack_async_event\n");

  _ack_async_event(event);

  dmtcp_plugin_enable_ckpt();
}

int ibv_resize_cq(struct ibv_cq *cq, int cqe)
{

  dmtcp_plugin_disable_ckpt();
  //PDEBUG("****** WRAPPER for ibv_resize_cq\n");

  int rslt = _resize_cq(cq,cqe);

  dmtcp_plugin_enable_ckpt();
  return rslt;
}

int ibv_destroy_cq(struct ibv_cq *cq)
{
  dmtcp_plugin_disable_ckpt();
 // PDEBUG("****** WRAPPER for ibv_destroy_cq\n");

  int rslt = _destroy_cq(cq);

  dmtcp_plugin_enable_ckpt();
  return rslt;
}

int ibv_destroy_qp(struct ibv_qp *qp)
{
  dmtcp_plugin_disable_ckpt();
  //PDEBUG("****** WRAPPER for ibv_destroy qp\n");

  int rslt = _destroy_qp(qp);

  dmtcp_plugin_enable_ckpt();
  return rslt;
}

int ibv_dereg_mr(struct ibv_mr *mr)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("****** WRAPPER for ibv_dereg_mr\n");

  int rslt = _dereg_mr(mr);

  dmtcp_plugin_enable_ckpt();
  return rslt;
}

int ibv_dealloc_pd(struct ibv_pd *pd)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("****** WRAPPER for ibv_dealloc_pd\n");

  int rslt = _dealloc_pd(pd);

  dmtcp_plugin_enable_ckpt();
  return rslt;
}

int ibv_close_device(struct ibv_context *context)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("***** WRAPPER for ibv_close_device\n");

  int rslt = _close_device(context);

  dmtcp_plugin_enable_ckpt();
  return rslt;
}

void ibv_free_device_list(struct ibv_device **list)
{
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("********* WRAPPER for ibv_free_device_list\n");

  _free_device_list(list);

  dmtcp_plugin_enable_ckpt();
}

void ibv_ack_cq_events(struct ibv_cq * cq, unsigned int nevents)
{
  dmtcp_plugin_disable_ckpt();
 // PDEBUG("******** WRAPPER for ibv_ack_cq_events\n");

  _ack_cq_events(cq, nevents);

  dmtcp_plugin_enable_ckpt();
}

struct ibv_ah * ibv_create_ah(struct ibv_pd *pd, struct ibv_ah_attr *attr){
  dmtcp_plugin_disable_ckpt();
//  PDEBUG("******** WRAPPER for ibv_create_ah\n");

  struct ibv_ah *rslt = _create_ah(pd, attr);

  dmtcp_plugin_enable_ckpt();
  return rslt;
}
