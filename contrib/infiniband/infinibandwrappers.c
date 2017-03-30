#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dmtcp.h"

#include <infiniband/verbs.h>
#include "debug.h"
#include "ibvctx.h"

void *
dlopen(const char *filename, int flag)
{
  if (filename) {
    if (strstr(filename, "libibverbs.so")) {
      void *handle = NEXT_FNC(dlopen)("libdmtcp_infiniband.so", flag);
      if (handle == NULL) {
        IBV_WARNING("\n*** Please either add $DMTCP_PATH$/lib/dmtcp "
                    "to LD_LIBRARY_PATH,\n"
                    "*** or else include an absolute pathname "
                    "for libdmtcp_infiniband.so\n\n");
      }
      return handle;
    }
  }
  return NEXT_FNC(dlopen)(filename, flag);
}

int
ibv_fork_init(void)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("****** WRAPPER for ibv_fork_init ******\n");
  int rslt = _fork_init();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

struct ibv_device **
ibv_get_device_list(int *num_devices)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("****** WRAPPER for ibv_get_device_list\n");

  struct ibv_device **result = _get_device_list(num_devices);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return result;
}

const char *
ibv_get_device_name(struct ibv_device *dev)
{
  // DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("****** WRAPPER for ibv_get_device_name\n");

  const char *rslt = _get_device_name(dev);

  // DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

struct ibv_context *
ibv_open_device(struct ibv_device *dev)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("******* WRAPPER for ibv_open_device\n");

  struct ibv_context *user_copy = _open_device(dev);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return user_copy;
}

int
ibv_query_device(struct ibv_context *context,
                 struct ibv_device_attr *device_attr)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("******* WRAPPER for ibv_query_device\n");

  int rslt = _query_device(context, device_attr);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

// ibv_query_port is defined as a macro in verbs.h
#undef ibv_query_port
int ibv_query_port(struct ibv_context *context, uint8_t port_num,
                   struct ibv_port_attr *port_attr)
{
  DMTCP_PLUGIN_DISABLE_CKPT();

  int rslt = _query_port(context, port_num, port_attr);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

int
ibv_query_pkey(struct ibv_context *context,
               uint8_t port_num,
               int index,
               uint16_t *pkey)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("******* WRAPPER for ibv_query_pkey\n");

  int rslt = _query_pkey(context, port_num, index, pkey);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

int
ibv_query_gid(struct ibv_context *context,
              uint8_t port_num,
              int index,
              union ibv_gid *gid)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("******* WRAPPER for ibv_query_gid\n");

  int rslt = _query_gid(context, port_num, index, gid);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

uint64_t
ibv_get_device_guid(struct ibv_device *device)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("******* WRAPPER for ibv_get_device_guid\n");

  uint64_t rslt = _get_device_guid(device);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

struct ibv_comp_channel *
ibv_create_comp_channel(struct ibv_context *context)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("******* WRAPPER for ibv_create_comp_channel\n");

  struct ibv_comp_channel *rslt = _create_comp_channel(context);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

int
ibv_destroy_comp_channel(struct ibv_comp_channel *channel)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("****** WRAPPER for ibv_destroy_comp_channel\n");

  int rslt = _destroy_comp_channel(channel);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

struct ibv_pd *
ibv_alloc_pd(struct ibv_context *context)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("******* WRAPPER FOR ibv_alloc_pd\n");

  struct ibv_pd *user_copy = _alloc_pd(context);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return user_copy;
}

struct ibv_mr *
ibv_reg_mr(struct ibv_pd *pd, void *addr, size_t length, int access) // int
                                                                     // access)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("******** WRAPPER for ibv_reg_mr\n");

  struct ibv_mr *user_copy = _reg_mr(pd, addr, length, access);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return user_copy;
}

struct ibv_cq *
ibv_create_cq(struct ibv_context *context,
              int cqe,
              void *cq_context,
              struct ibv_comp_channel *channel,
              int comp_vector)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("******** WRAPPER for ibv_create_cq\n");

  struct ibv_cq *user_copy = _create_cq(context, cqe, cq_context,
                                        channel, comp_vector);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return user_copy;
}

struct ibv_srq *
ibv_create_srq(struct ibv_pd *pd, struct ibv_srq_init_attr *srq_init_attr)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("******** WRAPPER for ibv_create_srq\n");

  struct ibv_srq *user_copy = _create_srq(pd, srq_init_attr);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return user_copy;
}

int
ibv_modify_srq(struct ibv_srq *srq,
               struct ibv_srq_attr *srq_attr,
               int srq_attr_mask)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("******** WRAPPER for ibv_modify_srq\n");
  int rslt = _modify_srq(srq, srq_attr, srq_attr_mask);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

int
ibv_query_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("******** WRAPPER for ibv_query_srq\n");
  int rslt = _query_srq(srq, srq_attr);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

int
ibv_destroy_srq(struct ibv_srq *srq)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("******** WRAPPER for ibv_destroy_srq\n");
  int rslt = _destroy_srq(srq);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

struct ibv_qp *
ibv_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("******** WRAPPER for ibv_create_qp\n");

  struct ibv_qp *user_copy = _create_qp(pd, qp_init_attr);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return user_copy;
}

int
ibv_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask) // int
                                                                          // attr_mask)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("********* WRAPPER for ibv_modify_qp\n");

  int rslt = _modify_qp(qp, attr, attr_mask);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

int
ibv_get_cq_event(struct ibv_comp_channel *channel,
                 struct ibv_cq **cq,
                 void **cq_context)
{
  IBV_DEBUG("******** WRAPPER for ibv_get_cq_event");

  int rslt = _get_cq_event(channel, cq, cq_context);

  return rslt;
}

int
ibv_query_qp(struct ibv_qp *qp,
             struct ibv_qp_attr *attr,
             int attr_mask,
             struct ibv_qp_init_attr *init_attr)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("******** WRAPPER FOR ibv_query_qp\n");

  int rslt = _query_qp(qp, attr, attr_mask, init_attr);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

int
ibv_get_async_event(struct ibv_context *context, struct ibv_async_event *event)
{
  IBV_DEBUG("******** WRAPPER FOR ibv_get_async_event\n");

  int rslt = _get_async_event(context, event);

  return rslt;
}

void
ibv_ack_async_event(struct ibv_async_event *event)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("******* WRAPPER FOR ibv_ack_async_event\n");

  _ack_async_event(event);

  DMTCP_PLUGIN_ENABLE_CKPT();
}

int
ibv_resize_cq(struct ibv_cq *cq, int cqe)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("****** WRAPPER for ibv_resize_cq\n");

  int rslt = _resize_cq(cq, cqe);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

int
ibv_destroy_cq(struct ibv_cq *cq)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("****** WRAPPER for ibv_destroy_cq\n");

  int rslt = _destroy_cq(cq);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

int
ibv_destroy_qp(struct ibv_qp *qp)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("****** WRAPPER for ibv_destroy qp\n");

  int rslt = _destroy_qp(qp);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

int
ibv_dereg_mr(struct ibv_mr *mr)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("****** WRAPPER for ibv_dereg_mr\n");

  int rslt = _dereg_mr(mr);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

int
ibv_dealloc_pd(struct ibv_pd *pd)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("****** WRAPPER for ibv_dealloc_pd\n");

  int rslt = _dealloc_pd(pd);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

int
ibv_close_device(struct ibv_context *context)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("***** WRAPPER for ibv_close_device\n");

  int rslt = _close_device(context);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

void
ibv_free_device_list(struct ibv_device **list)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("********* WRAPPER for ibv_free_device_list\n");

  _free_device_list(list);

  DMTCP_PLUGIN_ENABLE_CKPT();
}

void
ibv_ack_cq_events(struct ibv_cq *cq, unsigned int nevents)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("******** WRAPPER for ibv_ack_cq_events\n");

  _ack_cq_events(cq, nevents);

  DMTCP_PLUGIN_ENABLE_CKPT();
}

struct ibv_ah *
ibv_create_ah(struct ibv_pd *pd, struct ibv_ah_attr *attr)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  IBV_DEBUG("******** WRAPPER for ibv_create_ah\n");

  struct ibv_ah *rslt = _create_ah(pd, attr);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

int
ibv_destroy_ah(struct ibv_ah *ah)
{
  int rslt;

  IBV_DEBUG("******** WRAPPER for ibv_destroy_ah\n");
  DMTCP_PLUGIN_DISABLE_CKPT();

  rslt = _destroy_ah(ah);

  DMTCP_PLUGIN_ENABLE_CKPT();

  return rslt;
}

/*
 * The following are some unimplemented functionalities, including:
 *
 * Reregistering memory regions
 *
 * Multicast support
 *
 * TODO: Adding XRC (eXtended Reliable Connected) functionalities
 *
 */

int ibv_rereg_mr(struct ibv_mr *mr, int flags,
                 struct ibv_pd *pd, void *addr,
                 size_t length, int access)
{
  IBV_WARNING("Not implemented.\n");
  return NEXT_IBV_FNC(ibv_rereg_mr)(mr, flags,
                                    pd, addr,
                                    length, access);
}

int ibv_attach_mcast(struct ibv_qp *qp, const union ibv_gid *gid,
                     uint16_t lid)
{
  IBV_WARNING("Not implemented.\n");
  return NEXT_IBV_FNC(ibv_attach_mcast)(qp, gid, lid);
}

int ibv_detach_mcast(struct ibv_qp *qp, const union ibv_gid *gid,
                     uint16_t lid)
{
  IBV_WARNING("Not implemented.\n");
  return NEXT_IBV_FNC(ibv_detach_mcast)(qp, gid, lid);
}
