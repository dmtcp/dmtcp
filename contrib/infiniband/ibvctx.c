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

#include "ibvctx.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <infiniband/verbs.h>
#include <linux/types.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "config.h"
#include "dmtcp.h"
#include "ibv_internal.h"
#include "lib/list.h"

static bool is_restart = false;

// ! This flag is used to trace whether ibv_fork_init is called
static bool is_fork = false;

/* these lists will track the resources so they can be recreated
 * at restart time */

// ! This is the list of contexts
static struct list ctx_list = LIST_INITIALIZER(ctx_list);

// ! This is the list of protection domains
static struct list pd_list = LIST_INITIALIZER(pd_list);

// ! This is the list of memory regions
static struct list mr_list = LIST_INITIALIZER(mr_list);

// ! This is the list of completion queues
static struct list cq_list = LIST_INITIALIZER(cq_list);

// ! This is the list of queue pairs
static struct list qp_list = LIST_INITIALIZER(qp_list);

// ! This is the list of shared receive queues
static struct list srq_list = LIST_INITIALIZER(srq_list);

// ! This is the list of completion channels
static struct list comp_list = LIST_INITIALIZER(comp_list);

// Address Handler list
static struct list ah_list = LIST_INITIALIZER(ah_list);

// ! This is the list of rkey pairs
static struct list rkey_list = LIST_INITIALIZER(rkey_list);

// List to store the virtual-to-real qp_num
static struct list qp_num_list = LIST_INITIALIZER(qp_num_list);
static struct list lid_list = LIST_INITIALIZER(lid_list);

// Offsets used to generate unique ids
static uint32_t pd_id_offset = 0;
static uint32_t qp_num_offset = 0;
static uint16_t lid_offset = 0;

// Base lid, unique per node, initilized during launching and restart
static uint16_t base_lid = 0;
static bool lid_mapping_initialized = false;

#define LID_QUOTA 10 // Max number of virtual LIDs per node
#define DEFAULT_PSN 0

static pthread_mutex_t pd_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t qp_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t lid_mutex = PTHREAD_MUTEX_INITIALIZER;

static void send_qp_info(void);
static void query_qp_info(void);
static void send_lid_info(void);
static void query_lid_info(void);
static void send_rkey_info(void);
static void post_restart(void);
static void post_restart2(void);
static void nameservice_register_data(void);
static void nameservice_send_queries(void);
static void refill(void);
static void cleanup(void);

// Translate virtual IDs to real ones, first look up the local
// cache, if doesn't exist, query the coordinator, store the result
// in the cache.

qp_id_t translate_qp_num(uint32_t virtual_qp_num);
static uint16_t translate_lid(uint16_t virtual_lid);

#define DECL_FPTR(func)                                \
  static __typeof__(&ibv_ ## func)_real_ibv_ ## func = \
    (__typeof__(&ibv_ ## func))NULL

#define UPDATE_FUNC_ADDR(func, addr) \
  do {                               \
    _real_ibv_##func = addr;         \
    addr = _##func;                  \
  } while (0)

DECL_FPTR(post_recv);
DECL_FPTR(post_srq_recv);
DECL_FPTR(post_send);
DECL_FPTR(poll_cq);
DECL_FPTR(req_notify_cq);

DECL_FPTR(query_device);
DECL_FPTR(query_port);
DECL_FPTR(alloc_pd);
DECL_FPTR(dealloc_pd);
DECL_FPTR(reg_mr);
DECL_FPTR(dereg_mr);
// the function prototype is different the one in ibv_context_ops
// DECL_FPTR(create_cq);
DECL_FPTR(resize_cq);
DECL_FPTR(destroy_cq);
DECL_FPTR(create_srq);
DECL_FPTR(modify_srq);
DECL_FPTR(query_srq);
DECL_FPTR(destroy_srq);
DECL_FPTR(create_qp);
DECL_FPTR(query_qp);
DECL_FPTR(modify_qp);
DECL_FPTR(destroy_qp);
DECL_FPTR(create_ah);
DECL_FPTR(destroy_ah);

// Memory window operations are not supported
DECL_FPTR(alloc_mw);
DECL_FPTR(dealloc_mw);
// On some versions of Mellanox OFED, ibv_bind_mw() is not implemented
// DECL_FPTR(bind_mw);

/* These files are processed by sed at compile time */
#include "get_cq_from_pointer.ic"
#include "get_qp_from_pointer.ic"
#include "get_srq_from_pointer.ic"
/* keys.ic should be included before ibv_wr_ops_recv.ic, because it declares
 * a function sqe_update_lkey, which is used inside ibv_wr_ops_recv.ic. */
#include "keys.ic"
#include "ibv_wr_ops_recv.ic"
#include "ibv_wr_ops_send.ic"

int
dmtcp_infiniband_enabled(void) { return 1; }

static void
infiniband_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data) {
  switch (event) {
  case DMTCP_EVENT_INIT:
    {
      long hostid = gethostid();
      dmtcp_get_unique_id_from_coordinator("ib_blid",
          &hostid, sizeof(long),
          &base_lid, LID_QUOTA, sizeof(base_lid));
      break;
    }
    break;
  case DMTCP_EVENT_EXIT:
    cleanup();
    break;

  case DMTCP_EVENT_PRECHECKPOINT:
    pre_checkpoint();
    break;

  case DMTCP_EVENT_RESTART:
    post_restart();
    dmtcp_global_barrier("IB::restart");
    nameservice_register_data();
    dmtcp_global_barrier("IB::restart_nameservice_register_data");
    nameservice_send_queries();
    dmtcp_global_barrier("IB::restart_nameservice_send_queries");
    refill();
    break;

  default:
    break;
  }
}

DmtcpPluginDescriptor_t infiniband_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "infiniband",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "InfiniBand plugin",
  infiniband_event_hook
};

DMTCP_DECL_PLUGIN(infiniband_plugin);

static void
nameservice_register_data(void)
{
  send_qp_info();
  send_lid_info();
  send_rkey_info();
}

static void
nameservice_send_queries(void)
{
  // query_qp_info();
  query_lid_info();
  post_restart2();
}

/*! This will populate the coordinator with information about the new QPs */
void send_qp_info()
{
  struct list_elem *e;
  char hostname[128];

  gethostname(hostname,128);

  for (e = list_begin(&qp_list);
       e != list_end(&qp_list);
       e = list_next(e)) {
    struct internal_ibv_qp *internal_qp;
    qp_id_t cur_qp_id;

    internal_qp = list_entry(e, struct internal_ibv_qp, elem);
    cur_qp_id.qp_num = internal_qp->real_qp->qp_num;
    cur_qp_id.pd_id =
      ibv_pd_to_internal(internal_qp->user_qp.pd)->pd_id;

    IBV_DEBUG("Sending virtual qp_num: %lu, "
        "real qp_num: %lu, pd_id: %lu from %s\n",
        (unsigned long)internal_qp->user_qp.qp_num,
        (unsigned long)cur_qp_id.qp_num,
        (unsigned long)cur_qp_id.pd_id,
        hostname);

    dmtcp_send_key_val_pair_to_coordinator("ib_qp",
        &internal_qp->user_qp.qp_num,
        sizeof(internal_qp->user_qp.qp_num),
        &cur_qp_id, sizeof(cur_qp_id));
  }
}

/*! This will query the coordinator for information about the new QPs */
void query_qp_info()
{
  char hostname[128];
  struct list_elem *e;

  gethostname(hostname, 128);

  for (e = list_begin(&qp_num_list);
       e != list_end(&qp_num_list);
       e = list_next(e)) {
    struct list_elem *w;
    qp_num_mapping_t *mapping = list_entry(e, qp_num_mapping_t, elem);

    // First, check the local qp list, update the local cache
    for (w = list_begin(&qp_list);
         w != list_end(&qp_list);
         w = list_next(w)) {
      struct internal_ibv_qp *internal_qp =
        list_entry(w, struct internal_ibv_qp, elem);

      if (mapping->virtual_qp_num == internal_qp->user_qp.qp_num) {
        IBV_DEBUG("Querying local qp: virtual qp_num: %lu, "
            "real qp_num: %lu from %s\n",
            (unsigned long)mapping->virtual_qp_num,
            (unsigned long)internal_qp->real_qp->qp_num,
            hostname);
        mapping->real_qp_num = internal_qp->real_qp->qp_num;
        assert(mapping->pd_id ==
               ibv_pd_to_internal(internal_qp->user_qp.pd)->pd_id);
        break;
      }
    }

    // For remote qps, query the coordinator
    if (w == list_end(&qp_list)) {
      qp_id_t cur_qp_id;
      uint32_t size = sizeof(cur_qp_id);

      IBV_DEBUG("Querying remote qp: virtual qp_num: %lu from %s\n",
          (unsigned long)mapping->virtual_qp_num, hostname);
      dmtcp_send_query_to_coordinator("ib_qp",
          &mapping->virtual_qp_num, sizeof(mapping->virtual_qp_num),
          &cur_qp_id, &size);
      assert(size == sizeof(cur_qp_id));
      mapping->real_qp_num = cur_qp_id.qp_num;
      assert(mapping->pd_id == cur_qp_id.pd_id);
    }
  }
}

void send_lid_info() {
  struct list_elem *e;
  char hostname[128];

  gethostname(hostname,128);

  for (e = list_begin(&lid_list);
       e != list_end(&lid_list);
       e = list_next(e)) {
    lid_mapping_t *mapping = list_entry(e, lid_mapping_t, elem);

    // Local lids
    if (mapping->port != 0) {
      IBV_DEBUG("Sending virtual lid: 0x%04x, "
          "real lid: 0x%04x from %s\n",
          mapping->virtual_lid,
          mapping->real_lid,
          hostname);

      dmtcp_send_key_val_pair_to_coordinator("ib_lid",
          &mapping->virtual_lid,
          sizeof(mapping->virtual_lid),
          &mapping->real_lid,
          sizeof(mapping->real_lid));
    }
  }
}

void query_lid_info() {
  struct list_elem *e;
  char hostname[128];

  gethostname(hostname,128);

  for (e = list_begin(&lid_list);
       e != list_end(&lid_list);
       e = list_next(e)) {
    lid_mapping_t *mapping = list_entry(e, lid_mapping_t, elem);

    // Remote lids
    if (mapping->port == 0) {
      uint32_t size = sizeof(mapping->real_lid);

      IBV_DEBUG("Querying remote lid: virtual lid: 0x%04x from %s\n",
          mapping->virtual_lid,
          hostname);

      dmtcp_send_query_to_coordinator("ib_lid",
          &mapping->virtual_lid,
          sizeof(mapping->virtual_lid),
          &mapping->real_lid,
          &size);
      assert(size == sizeof(mapping->real_lid));
    }
  }
}

// Send the rkey info to the coordinator
// A tuple (virtual rkey, pd id) is used as the key, and the real rkey
// created after restart is used as the value.
// Virtual rkey combined with the pd id can globally identify a mr.
void send_rkey_info()
{
  struct list_elem *e;

  for (e = list_begin(&mr_list);
       e != list_end(&mr_list);
       e = list_next(e)) {
    struct internal_ibv_mr *internal_mr;
    struct internal_ibv_pd *internal_pd;
    rkey_id_t cur_rkey_id;

    internal_mr = list_entry(e, struct internal_ibv_mr, elem);
    internal_pd = ibv_pd_to_internal(internal_mr->user_mr.pd);
    cur_rkey_id.pd_id = internal_pd->pd_id;
    cur_rkey_id.rkey = internal_mr->user_mr.rkey;

    dmtcp_send_key_val_pair_to_coordinator("ib_rkey",
        &cur_rkey_id, sizeof(cur_rkey_id),
        &internal_mr->real_mr->rkey,
        sizeof(internal_mr->real_mr->rkey));
  }
}

/*! This will drain the given completion queue
 */
static void
drain_completion_queue(struct internal_ibv_cq *internal_cq)
{
  int ne;

  /* This loop will drain the completion queue and buffer the entries */
  do {
    struct ibv_wc_wrapper *wc = malloc(sizeof(struct ibv_wc_wrapper));
    if (wc == NULL) {
      IBV_ERROR("Unable to allocate memory for wc\n");
    }

    ne = _real_ibv_poll_cq(internal_cq->real_cq, 1, &wc->wc);

    if (ne > 0) {
      struct internal_ibv_qp *internal_qp;
      enum ibv_wc_opcode opcode;

      IBV_DEBUG("Found a completion on the queue.\n");
      list_push_back(&internal_cq->wc_queue, &wc->elem);
      internal_qp = qp_num_to_qp(&qp_list, wc->wc.qp_num);
      wc->wc.qp_num = internal_qp->user_qp.qp_num;
      opcode = wc->wc.opcode;

      if (wc->wc.status != IBV_WC_SUCCESS) {
        IBV_WARNING("Unsuccessful completion: %d.\n", opcode);
      } else {
        if (opcode & IBV_WC_RECV ||
            opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
          if (internal_qp->user_qp.srq) {
            struct internal_ibv_srq *internal_srq;

            internal_srq = ibv_srq_to_internal(internal_qp->user_qp.srq);
            internal_srq->recv_count++;
          }
          else {
            struct list_elem *e;
            struct ibv_post_recv_log *log;

            e = list_pop_front(&internal_qp->post_recv_log);
            log = list_entry(e, struct ibv_post_recv_log, elem);
            free(log);
          }
        } else if (opcode == IBV_WC_SEND ||
                   opcode == IBV_WC_RDMA_WRITE ||
                   opcode == IBV_WC_RDMA_READ ||
                   opcode == IBV_WC_COMP_SWAP ||
                   opcode == IBV_WC_FETCH_ADD) {
          if (internal_qp->init_attr.sq_sig_all) {
            struct list_elem *e;
            struct ibv_post_send_log *log;

            e = list_pop_front(&internal_qp->post_send_log);
            log = list_entry(e, struct ibv_post_send_log, elem);
            assert(log->magic == SEND_MAGIC);
            free(log);
          }
          else {
            while(1) {
              struct list_elem *e;
              struct ibv_post_send_log *log;

              e = list_pop_front(&internal_qp->post_send_log);
              log = list_entry(e, struct ibv_post_send_log, elem);

              if (log->wr.send_flags & IBV_SEND_SIGNALED) {
                assert(log->magic == SEND_MAGIC);
                free(log);
                break;
              }
              else {
                assert(log->magic == SEND_MAGIC);
                free(log);
              }
            }
          }
        } else if (opcode == IBV_WC_BIND_MW) {
          IBV_ERROR("opcode %d specifies unsupported operation.\n",
                    opcode);
        } else {
          IBV_ERROR("Unknown or invalid opcode.\n");
        }
      }
    } else {
      free(wc);
    }
  } while (ne > 0);
}

/*! This will first check the completion queue for any
 * left completions and remove them, saving them into a buffer
 * for later user. It will then close all the resources
 */
void
pre_checkpoint(void)
{
  struct list_elem *e;
  struct list_elem *w;

  IBV_DEBUG("Entering pre_checkpoint.\n");

  for (e = list_begin(&cq_list); e != list_end(&cq_list); e = list_next(e)) {
    struct internal_ibv_cq *internal_cq;

    internal_cq = list_entry(e, struct internal_ibv_cq, elem);
    drain_completion_queue(internal_cq);
  }

  for (e = list_begin(&qp_list); e != list_end(&qp_list); e = list_next(e)) {
    struct internal_ibv_qp *internal_qp;

    internal_qp = list_entry(e, struct internal_ibv_qp, elem);

    if (internal_qp->init_attr.sq_sig_all == 0) {
      if (list_size(&internal_qp->post_send_log) > 0) {
        w = list_begin(&internal_qp->post_send_log);
        while (w != list_end(&internal_qp->post_send_log)) {
          struct list_elem *t = w;
          struct ibv_post_send_log *log = list_entry(t,
                                                     struct ibv_post_send_log,
                                                     elem);
          if (!(log->wr.send_flags & IBV_SEND_SIGNALED)) {
            w = list_next(w);
            list_remove(&log->elem);
            free(log->wr.sg_list);
            free(log);
          }
        }
      }
    }
  }
  IBV_DEBUG("Exiting pre_checkpoint\n");
}

// TODO: Must handle case of modifying after checkpoint
void
post_restart(void)
{
  is_restart = true;
  lid_mapping_initialized = false;
  if (is_fork) {
    if (NEXT_IBV_FNC(ibv_fork_init)()) {
      IBV_ERROR("ibv_fork_init fails.\n");
    }
  }

  /* code to re-open device */
  int num = 0;
  struct ibv_device **real_dev_list;
  struct list_elem *e;

  // This is useful when IB is not used while the plugin is enabled.
  if (dlvsym(RTLD_NEXT, "ibv_get_device_list", "IBVERBS_1.1") != NULL) {
    real_dev_list = NEXT_IBV_FNC(ibv_get_device_list)(&num);
    if (!num) {
      IBV_ERROR("ibv_get_device_list() returned 0 devices.\n");
    }
    if (num > 1) {
      IBV_ERROR("IB plugin currently does not "
                "support more than one HCA\n");
    }
  }

  for (e = list_begin(&ctx_list); e != list_end(&ctx_list); e = list_next(e))
  {
    struct internal_ibv_ctx *internal_ctx;
    int i;

    internal_ctx = list_entry(e, struct internal_ibv_ctx, elem);

    for (i = 0; i < num; i++) {
      if (!strncmp(internal_ctx->user_ctx.device->dev_name,
                   real_dev_list[i]->dev_name,
                   strlen(real_dev_list[i]->dev_name))) {
        internal_ctx->real_ctx =
          NEXT_IBV_FNC(ibv_open_device)(real_dev_list[i]);

        if (!internal_ctx->real_ctx) {
          IBV_ERROR("Could not re-open the context.\n");
        }

        // here we need to make sure that recreated
        // contexts and old ones have
        // the same cmd_fd and async_fd
        if (internal_ctx->real_ctx->async_fd ==
            internal_ctx->user_ctx.cmd_fd) {
          if (internal_ctx->real_ctx->async_fd !=
              internal_ctx->user_ctx.async_fd) {
            if (dup2(internal_ctx->real_ctx->async_fd,
                     internal_ctx->user_ctx.async_fd) !=
                internal_ctx->user_ctx.async_fd) {
              IBV_ERROR("Could not duplicate async_fd.\n");
            }
            close(internal_ctx->real_ctx->async_fd);
            internal_ctx->real_ctx->async_fd = internal_ctx->user_ctx.async_fd;
          }
          if (internal_ctx->real_ctx->cmd_fd !=
              internal_ctx->user_ctx.cmd_fd) {
            if (dup2(internal_ctx->real_ctx->cmd_fd,
                     internal_ctx->user_ctx.cmd_fd) !=
                internal_ctx->user_ctx.cmd_fd) {
              IBV_ERROR("Could not duplicate cmd_fd.\n");
            }
            close(internal_ctx->real_ctx->cmd_fd);
            internal_ctx->real_ctx->cmd_fd = internal_ctx->user_ctx.cmd_fd;
          }
        } else {
          if (internal_ctx->real_ctx->cmd_fd !=
              internal_ctx->user_ctx.cmd_fd) {
            if (dup2(internal_ctx->real_ctx->cmd_fd,
                     internal_ctx->user_ctx.cmd_fd) !=
                internal_ctx->user_ctx.cmd_fd) {
              IBV_ERROR("Could not duplicate cmd_fd.\n");
            }
            close(internal_ctx->real_ctx->cmd_fd);
            internal_ctx->real_ctx->cmd_fd = internal_ctx->user_ctx.cmd_fd;
          }
          if (internal_ctx->real_ctx->async_fd !=
              internal_ctx->user_ctx.async_fd) {
            if (dup2(internal_ctx->real_ctx->async_fd,
                     internal_ctx->user_ctx.async_fd) !=
                internal_ctx->user_ctx.async_fd) {
              IBV_ERROR("Could not duplicate async_fd.\n");
            }
            close(internal_ctx->real_ctx->async_fd);
            internal_ctx->real_ctx->async_fd = internal_ctx->user_ctx.async_fd;
          }
        }

        break;
      }
    }

    if (!lid_mapping_initialized) {
      int ret;
      struct list_elem *w;

      lid_mapping_initialized = true;

      for (w = list_begin(&lid_list);
           w != list_end(&lid_list);
           w = list_next(w)) {
        lid_mapping_t *mapping = list_entry(w, lid_mapping_t, elem);
        struct ibv_port_attr port_attr;

        // Update local real lid
        if (mapping->port != 0) {
          ret = NEXT_IBV_FNC(ibv_query_port)(internal_ctx->real_ctx,
                                             mapping->port, &port_attr);
          if (ret != 0) {
            IBV_ERROR("Error getting device attributes.\n");
          }
          assert(port_attr.state == IBV_PORT_ARMED ||
                 port_attr.state == IBV_PORT_ACTIVE);
          mapping->real_lid = port_attr.lid;
        }
      }
    }
  }

  NEXT_IBV_FNC(ibv_free_device_list)(real_dev_list);

  /* end code to re-open device */

  /* code to alloc the protection domain */
  for (e = list_begin(&pd_list); e != list_end(&pd_list); e = list_next(e)) {
    struct internal_ibv_pd *internal_pd;
    struct internal_ibv_ctx *internal_ctx;

    internal_pd = list_entry(e, struct internal_ibv_pd, elem);
    internal_ctx = ibv_ctx_to_internal(internal_pd->user_pd.context);

    internal_pd->real_pd = NEXT_IBV_FNC(ibv_alloc_pd)(internal_ctx->real_ctx);

    if (!internal_pd->real_pd) {
      IBV_ERROR("Could not re-alloc the pd: %d\n", errno);
    }
  }

  /*end code to alloc the protection domain */

  /* code to register the memory region */
  for (e = list_begin(&mr_list); e != list_end(&mr_list); e = list_next(e)) {
    struct internal_ibv_mr *internal_mr;
    struct internal_ibv_pd *internal_pd;

    internal_mr = list_entry(e, struct internal_ibv_mr, elem);
    internal_pd = ibv_pd_to_internal(internal_mr->user_mr.pd);

    internal_mr->real_mr = NEXT_IBV_FNC(ibv_reg_mr)(internal_pd->real_pd,
                                                    internal_mr->user_mr.addr,
                                                    internal_mr->user_mr.length,
                                                    internal_mr->flags);

    if (!internal_mr->real_mr) {
      IBV_ERROR("Could not re-register the mr.\n");
    }
  }

  /* end code to register the memory region */

  /* code to register the completion channel */
  for (e = list_begin(&comp_list); e != list_end(&comp_list);
       e = list_next(e)) {
    struct internal_ibv_comp_channel *internal_comp;
    struct internal_ibv_ctx *internal_ctx;
    int flags;

    IBV_DEBUG("Recreating completion channel\n");
    internal_comp = list_entry(e, struct internal_ibv_comp_channel, elem);
    internal_ctx = ibv_ctx_to_internal(internal_comp->user_channel.context);

    internal_comp->real_channel = NEXT_IBV_COMP_CHANNEL(ibv_create_comp_channel)
        (internal_ctx->real_ctx);

    if (!internal_comp->real_channel) {
      IBV_ERROR("Could not re-create the comp channel.\n");
    }

    if (internal_comp->real_channel->fd != internal_comp->user_channel.fd)
    {
      if(dup2(internal_comp->real_channel->fd,
              internal_comp->user_channel.fd) == -1) {
        IBV_ERROR("Could not duplicate the file descriptor.\n");
      }
      close(internal_comp->real_channel->fd);
      internal_comp->real_channel->fd = internal_comp->user_channel.fd;
    }

    flags = NEXT_FNC(fcntl)(internal_comp->real_channel->fd, F_GETFL, NULL);
    if (!(flags & O_NONBLOCK)) {
      int rslt = NEXT_FNC(fcntl)(internal_comp->real_channel->fd,
                             F_SETFL, flags | O_NONBLOCK);
      if (rslt < 0) {
        IBV_ERROR("Failed to change file descriptor "
                  "of completion event channel: %d\n", errno);
      }
    }
  }

  /* end code to register the completion channel */

  /* code to register the completion queue */
  for (e = list_begin(&cq_list); e != list_end(&cq_list); e = list_next(e)) {
    struct internal_ibv_cq *internal_cq;
    struct internal_ibv_ctx *internal_ctx;
    struct ibv_comp_channel *real_channel = NULL;

    internal_cq = list_entry(e, struct internal_ibv_cq, elem);
    internal_ctx = ibv_ctx_to_internal(internal_cq->user_cq.context);
    if (internal_cq->user_cq.channel) {
      real_channel =
        ibv_comp_to_internal(internal_cq->user_cq.channel)->real_channel;
    }

    internal_cq->real_cq = NEXT_IBV_FNC(ibv_create_cq)
        (internal_ctx->real_ctx,
        internal_cq->user_cq.cqe,
        internal_cq->user_cq.cq_context,
        real_channel,
        internal_cq->comp_vector);

    if (!internal_cq->real_cq) {
      IBV_ERROR("Could not recreate the cq.\n");
    }

    struct list_elem *w;
    for (w = list_begin(&internal_cq->req_notify_log);
         w != list_end(&internal_cq->req_notify_log);
         w = list_next(w)) {
      struct ibv_req_notify_cq_log *log;

      log = list_entry(w, struct ibv_req_notify_cq_log, elem);
      if (_real_ibv_req_notify_cq(internal_cq->real_cq,
                                  log->solicited_only)) {
        IBV_ERROR("Could not repost req_notify_cq\n");
      }
    }
  }

  /* end code to register completion queue */


  /* code to re-create the shared receive queue */
  for (e = list_begin(&srq_list); e != list_end(&srq_list); e = list_next(e)) {
    struct internal_ibv_srq *internal_srq;
    struct ibv_srq_init_attr new_attr;

    internal_srq = list_entry(e, struct internal_ibv_srq, elem);
    new_attr = internal_srq->init_attr;

    internal_srq->real_srq =
      NEXT_IBV_FNC(ibv_create_srq)
        (ibv_pd_to_internal(internal_srq->user_srq.pd)->real_pd,
        &new_attr);
    if (!internal_srq->real_srq) {
      IBV_ERROR("Could not recreate the srq.\n");
    }
  }

  /* end code to re-create the shared receive queue */

  /* code to re-create the queue pairs */
  for (e = list_begin(&qp_list); e != list_end(&qp_list); e = list_next(e)) {
    struct internal_ibv_qp *internal_qp;
    struct ibv_qp_init_attr new_attr;

    internal_qp = list_entry(e, struct internal_ibv_qp, elem);
    new_attr = internal_qp->init_attr;
    new_attr.recv_cq =
      ibv_cq_to_internal(internal_qp->user_qp.recv_cq)->real_cq;
    new_attr.send_cq =
      ibv_cq_to_internal(internal_qp->user_qp.send_cq)->real_cq;

    if (new_attr.srq) {
      new_attr.srq = ibv_srq_to_internal(internal_qp->user_qp.srq)->real_srq;
    }

    internal_qp->real_qp =
      NEXT_IBV_FNC(ibv_create_qp)
        (ibv_pd_to_internal(internal_qp->user_qp.pd)->real_pd,
        &new_attr);

    if (!internal_qp->real_qp) {
      IBV_ERROR("Could not recreate the qp.\n");
    }
  }
  /* end code to re-create the queue pairs */

  // Reset the rkey list at each restart.
  e = list_begin(&rkey_list);
  while (e != list_end(&rkey_list)) {
    rkey_mapping_t *mapping;
    struct list_elem *w;

    w = e;
    mapping = list_entry(e, rkey_mapping_t, elem);
    e = list_next(e);
    list_remove(w);
    free(mapping);
  }

  // Reset the qp local cache on restart, keep only the local qp info.
  e = list_begin(&qp_num_list);
  while (e != list_end(&qp_num_list)) {
    qp_num_mapping_t *mapping;
    struct list_elem *w;

    mapping = list_entry(e, qp_num_mapping_t, elem);
    for (w = list_begin(&qp_list);
         w != list_end(&qp_list);
         w = list_next(w)) {
      struct internal_ibv_qp *internal_qp;

      internal_qp = list_entry(w, struct internal_ibv_qp, elem);
      // Local qp, update the mapping
      if (mapping->virtual_qp_num == internal_qp->user_qp.qp_num) {
        mapping->real_qp_num = internal_qp->real_qp->qp_num;
        break;
      }
    }

    // Remote qp, remove the mapping
    if (w == list_end(&qp_list)) {
      w = e;
      e = list_next(e);
      list_remove(w);
      free(mapping);
    }
    else {
      e = list_next(e);
    }
  }
}

void
post_restart2(void)
{
  struct list_elem *e;

  // Recreate the Address Handlers
  for (e = list_begin(&ah_list);
       e != list_end(&ah_list);
       e = list_next(e)) {
    struct ibv_ah_attr real_attr;
    struct internal_ibv_ah *internal_ah;

    internal_ah = list_entry(e, struct internal_ibv_ah, elem);
    real_attr = internal_ah->attr;
    real_attr.dlid = translate_lid(internal_ah->attr.dlid);

    internal_ah->real_ah =
      NEXT_IBV_FNC(ibv_create_ah)
        (ibv_pd_to_internal(internal_ah->user_ah.pd)->real_pd,
        &real_attr);
    if (internal_ah->real_ah == NULL) {
      IBV_ERROR("Fail to recreate the ah.\n");
    }
  }

  for (e = list_begin(&srq_list); e != list_end(&srq_list); e = list_next(e)) {
    struct internal_ibv_srq *internal_srq;
    struct list_elem *w;
    uint32_t i;

    internal_srq = list_entry(e, struct internal_ibv_srq, elem);
    for (i = 0; i < internal_srq->recv_count; i++) {
      w = list_pop_front(&internal_srq->post_srq_recv_log);
      struct ibv_post_srq_recv_log *log;

      log = list_entry(w, struct ibv_post_srq_recv_log, elem);
      free(log);
    }

    internal_srq->recv_count = 0;

    for (w = list_begin(&internal_srq->post_srq_recv_log);
         w != list_end(&internal_srq->post_srq_recv_log);
         w = list_next(w)) {
      struct ibv_post_srq_recv_log *log;
      struct ibv_recv_wr *copy_wr;
      struct ibv_recv_wr *bad_wr;

      log = list_entry(w, struct ibv_post_srq_recv_log, elem);
      copy_wr = copy_recv_wr(&log->wr);
      update_lkey_recv(copy_wr);

      int rslt = _real_ibv_post_srq_recv(internal_srq->real_srq,
                                         copy_wr, &bad_wr);
      if (rslt) {
        IBV_ERROR("Call to ibv_post_srq_recv failed: %d\n", errno);
      }
      delete_recv_wr(copy_wr);
    }

    for (w = list_rbegin(&internal_srq->modify_srq_log);
         w != list_rend(&internal_srq->modify_srq_log);
         w = list_prev(w)) {
      struct ibv_modify_srq_log *mod;
      struct ibv_srq_attr attr;

      mod = list_entry(w, struct ibv_modify_srq_log, elem);
      attr = mod->attr;

      if (mod->attr_mask & IBV_SRQ_MAX_WR){
	if(NEXT_IBV_FNC(ibv_modify_srq)
            (internal_srq->real_srq, &attr, IBV_SRQ_MAX_WR)){
	  IBV_WARNING("Could not modify srq properly.\n");
	}
	break;
      }
    }
  }

  for (e = list_begin(&qp_list); e != list_end(&qp_list); e = list_next(e)) {
    struct internal_ibv_qp *internal_qp;
    struct list_elem *w;

    internal_qp = list_entry(e, struct internal_ibv_qp, elem);
    for (w = list_begin(&internal_qp->modify_qp_log);
         w != list_end(&internal_qp->modify_qp_log);
         w = list_next(w)) {
      struct ibv_modify_qp_log *mod;
      struct ibv_qp_attr attr;

      mod = list_entry(w, struct ibv_modify_qp_log, elem);
      attr = mod->attr;

      // Use the translation table
      if (mod->attr_mask & IBV_QP_DEST_QPN) {
        qp_id_t rem_qp_id = translate_qp_num(attr.dest_qp_num);
        attr.dest_qp_num = rem_qp_id.qp_num;
        internal_qp->remote_pd_id = rem_qp_id.pd_id;
      }
      if (mod->attr_mask & IBV_QP_SQ_PSN) {
        attr.sq_psn = DEFAULT_PSN;
      }
      if (mod->attr_mask & IBV_QP_RQ_PSN) {
        attr.rq_psn = DEFAULT_PSN;
      }
      if (mod->attr_mask & IBV_QP_AV) {
        attr.ah_attr.dlid = translate_lid(attr.ah_attr.dlid);
      }

      if (NEXT_IBV_FNC(ibv_modify_qp)
           (internal_qp->real_qp, &attr, mod->attr_mask) != 0) {
        IBV_ERROR("Could not modify qp properly: %d\n", errno);
      }
    }
  }

  for (e = list_begin(&qp_list); e != list_end(&qp_list); e = list_next(e)) {
    struct internal_ibv_qp *internal_qp;
    struct list_elem *w;

    internal_qp = list_entry(e, struct internal_ibv_qp, elem);
    for (w = list_begin(&internal_qp->post_recv_log);
         w != list_end(&internal_qp->post_recv_log);
         w = list_next(w)) {
      struct ibv_post_recv_log *log;
      struct ibv_recv_wr *copy_wr;
      struct ibv_recv_wr *bad_wr;

      log = list_entry(w, struct ibv_post_recv_log, elem);
      copy_wr = copy_recv_wr(&log->wr);
      update_lkey_recv(copy_wr);
      assert(copy_wr->next == NULL);

      IBV_DEBUG("Reposting recv.\n");
      int rslt = _real_ibv_post_recv(internal_qp->real_qp, copy_wr, &bad_wr);
      if (rslt != 0) {
        IBV_ERROR("Repost recv failed.\n");
      }
      delete_recv_wr(copy_wr);
    }
  }
}

void
refill(void)
{
  struct list_elem *e;

  for (e = list_begin(&qp_list); e != list_end(&qp_list); e = list_next(e)) {
    struct internal_ibv_qp *internal_qp;
    struct list_elem *w;

    internal_qp = list_entry(e, struct internal_ibv_qp, elem);
    for (w = list_begin(&internal_qp->post_send_log);
         w != list_end(&internal_qp->post_send_log);
         w = list_next(w)) {
      struct ibv_post_send_log *log;
      struct ibv_send_wr *copy_wr;
      struct ibv_send_wr *bad_wr;

      log = list_entry(w, struct ibv_post_send_log, elem);
      IBV_DEBUG("log->magic : %x\n", log->magic);
      assert(log->magic == SEND_MAGIC);
      assert(&log->wr != NULL);

      copy_wr = copy_send_wr(&log->wr);
      update_lkey_send(copy_wr);
      switch (internal_qp->user_qp.qp_type) {
      case IBV_QPT_RC:
        update_rkey_send(copy_wr, internal_qp->remote_pd_id);
        break;
      case IBV_QPT_UD:
        assert(copy_wr->opcode == IBV_WR_SEND);
        update_ud_send_restart(copy_wr);
        break;
      default:
        IBV_ERROR("Warning: unsupported qp type: %d\n",
                  internal_qp->user_qp.qp_type);
      }

      IBV_DEBUG("Reposting send.\n");
      int rslt = _real_ibv_post_send(internal_qp->real_qp, copy_wr, &bad_wr);
      if (rslt) {
        IBV_ERROR("Repost recv failed.\n");
      }
      delete_send_wr(copy_wr);
    }
  }
}

void cleanup() {
  struct list_elem *e;
  struct list_elem *w;

  e = list_begin(&qp_num_list);
  while (e != list_end(&qp_num_list)) {
    qp_num_mapping_t *mapping;

    w = e;
    mapping = list_entry(e, qp_num_mapping_t, elem);
    e = list_next(e);
    list_remove(w);
    free(mapping);
  }

  e = list_begin(&lid_list);
  while (e != list_end(&lid_list)) {
    lid_mapping_t *mapping;

    w = e;
    mapping = list_entry(e, lid_mapping_t, elem);
    e = list_next(e);
    list_remove(w);
    free(mapping);
  }

  e = list_begin(&mr_list);
  while (e != list_end(&mr_list)) {
    struct internal_ibv_mr *internal_mr;

    w = e;
    internal_mr = list_entry(w, struct internal_ibv_mr, elem);
    e = list_next(e);
    list_remove(w);
    free(internal_mr);
  }

  e = list_begin(&rkey_list);
  while (e != list_end(&rkey_list)) {
    rkey_mapping_t *mapping;

    w = e;
    mapping = list_entry(e, rkey_mapping_t, elem);
    e = list_next(e);
    list_remove(w);
    free(mapping);
  }
}

int _fork_init() {
  is_fork = true;
  return NEXT_IBV_FNC(ibv_fork_init)();
}

// ! This performs the work of the _get_device_list_wrapper

/*!
  This function will open the real device list, store into real_dev_list
  and then copy the list, returning an image of the copy to the user
  */
struct ibv_device **
_get_device_list(int *num_devices)
{
  struct ibv_device **real_dev_list = NULL;
  int real_num_devices;
  struct dev_list_info *list_info;
  int i;
  struct ibv_device **user_list = NULL;

  real_dev_list = NEXT_IBV_FNC(ibv_get_device_list)(&real_num_devices);

  if (real_num_devices > 1) {
    IBV_ERROR("IB plugin currently does not "
              "support more than one HCA\n");
  }

  if (num_devices) {
    *num_devices = real_num_devices;
  }

  if (!real_dev_list) {
    return NULL;
  }

  user_list = calloc(real_num_devices + 1, sizeof(struct ibv_device *));
  list_info = (struct dev_list_info *)malloc(sizeof(struct dev_list_info));

  if (!user_list || !list_info) {
    IBV_ERROR("Could not allocate memory for _get_device_list.\n");
  }

  memset(user_list, 0, (real_num_devices + 1) * sizeof(struct ibv_device *));
  memset(list_info, 0, sizeof(struct dev_list_info));

  list_info->num_devices = real_num_devices;
  list_info->user_dev_list = user_list;
  list_info->real_dev_list = real_dev_list;

  for (i = 0; i < real_num_devices; i++) {
    struct internal_ibv_dev *dev;

    dev = (struct internal_ibv_dev *)malloc(sizeof(struct internal_ibv_dev));
    if (!dev) {
      IBV_ERROR("Could not allocate memory for _get_device_list.\n");
    }

    memset(dev, 0, sizeof(struct internal_ibv_dev));
    INIT_INTERNAL_IBV_TYPE(dev);
    memcpy(&dev->user_dev, real_dev_list[i], sizeof(struct ibv_device));
    dev->real_dev = real_dev_list[i];
    user_list[i] = &dev->user_dev;
    dev->list_info = list_info;
  }

  return user_list;
}

const char *
_get_device_name(struct ibv_device *device)
{
  if (!IS_INTERNAL_IBV_STRUCT(ibv_device_to_internal(device))) {
    return NEXT_IBV_FNC(ibv_get_device_name)(device);
  }
  return NEXT_IBV_FNC(ibv_get_device_name)
           (ibv_device_to_internal(device)->real_dev);
}

// TODO: I think the GUID could change and need to be translated
uint64_t
_get_device_guid(struct ibv_device *dev)
{
  struct internal_ibv_dev *internal_dev = ibv_device_to_internal(dev);

  if (!IS_INTERNAL_IBV_STRUCT(internal_dev)) {
    return NEXT_IBV_FNC(ibv_get_device_guid)(dev);
  }

  return NEXT_IBV_FNC(ibv_get_device_guid)(internal_dev->real_dev);
}

struct ibv_comp_channel *
_create_comp_channel(struct ibv_context *ctx)
{
  struct internal_ibv_ctx *internal_ctx;
  struct internal_ibv_comp_channel *internal_comp;

  internal_ctx = ibv_ctx_to_internal(ctx);
  assert(IS_INTERNAL_IBV_STRUCT(internal_ctx));

  internal_comp = malloc(sizeof(struct internal_ibv_comp_channel));

  if (!internal_comp) {
    IBV_ERROR("Could not alloc memory for comp channel\n");
  }

  internal_comp->real_channel =
    NEXT_IBV_COMP_CHANNEL(ibv_create_comp_channel)(internal_ctx->real_ctx);

  if (!internal_comp->real_channel) {
    IBV_ERROR("Completion channel could not be created.\n");
  }

  INIT_INTERNAL_IBV_TYPE(internal_comp);
  memcpy(&internal_comp->user_channel,
         internal_comp->real_channel,
         sizeof(struct ibv_comp_channel));
  internal_comp->user_channel.context = ctx;
  list_push_back(&comp_list, &internal_comp->elem);

  return &internal_comp->user_channel;
}

int
_destroy_comp_channel(struct ibv_comp_channel *channel)
{
  struct internal_ibv_comp_channel *internal_comp;
  int rslt;

  internal_comp = ibv_comp_to_internal(channel);
  assert(IS_INTERNAL_IBV_STRUCT(internal_comp));

  rslt = NEXT_IBV_COMP_CHANNEL(ibv_destroy_comp_channel)
      (internal_comp->real_channel);
  list_remove(&internal_comp->elem);
  free(internal_comp);

  return rslt;
}

int
_get_cq_event(struct ibv_comp_channel *channel,
              struct ibv_cq **cq,
              void **cq_context)
{
  struct internal_ibv_comp_channel *internal_channel;
  struct internal_ibv_cq *internal_cq;
  int rslt, flags;

  internal_channel = ibv_comp_to_internal(channel);
  assert(IS_INTERNAL_IBV_STRUCT(internal_channel));

  flags = NEXT_FNC(fcntl)(internal_channel->real_channel->fd,
                          F_GETFL, NULL);

  // We need to change the call to non-blocking mode
  // FIXME: we need to take care of the case where fd is already
  // changed to non-blocking mode by the user.
  if (!(flags & O_NONBLOCK)) {
    rslt = NEXT_FNC(fcntl)(internal_channel->real_channel->fd,
                           F_SETFL, flags | O_NONBLOCK);
    if (rslt < 0) {
      IBV_ERROR("Failed to change file descriptor "
                "of completion event channel: %d\n", errno);
    }
  }

  {
    int ms_timeout = 100;
    struct pollfd my_pollfd = {
      .fd = internal_channel->real_channel->fd,
      .events = POLLIN,
      .revents = 0
    };

    do {
      rslt = NEXT_FNC(poll)(&my_pollfd, 1, ms_timeout);
    } while (rslt == 0);

    if (rslt == -1) {
      IBV_ERROR("poll() in ibv_get_cq_event() failed\n");
    }
  }

  DMTCP_PLUGIN_DISABLE_CKPT();

  rslt = NEXT_IBV_FNC(ibv_get_cq_event)
                     (internal_channel->real_channel,
                      cq, cq_context);

  internal_cq = get_cq_from_pointer(*cq);
  assert(internal_cq != NULL);
  *cq = &internal_cq->user_cq;
  *cq_context = internal_cq->user_cq.context;

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

int
_get_async_event(struct ibv_context *ctx, struct ibv_async_event *event)
{
  struct internal_ibv_ctx *internal_ctx = ibv_ctx_to_internal(ctx);
  struct internal_ibv_qp *internal_qp;
  struct internal_ibv_cq *internal_cq;
  struct internal_ibv_srq *internal_srq;
  int rslt;

  assert(IS_INTERNAL_IBV_STRUCT(internal_ctx));

  int flags = NEXT_FNC(fcntl)(internal_ctx->real_ctx->async_fd,
                              F_GETFL, NULL);

  // We need to change the call to non-blocking mode
  if (!(flags & O_NONBLOCK)) {
    rslt  = NEXT_FNC(fcntl)(internal_ctx->real_ctx->async_fd,
                            F_SETFL, flags | O_NONBLOCK);
    if (rslt < 0) {
      IBV_ERROR("Failed to change file descriptor "
                "of async event queue: %d\n", errno);
    }
  }

  {
    int ms_timeout = 100;
    struct pollfd my_pollfd = {
      .fd = internal_ctx->real_ctx->async_fd,
      .events = POLLIN,
      .revents = 0
    };

    do {
      rslt = NEXT_FNC(poll)(&my_pollfd, 1, ms_timeout);
    } while (rslt == 0);

    if (rslt == -1) {
      IBV_ERROR("poll() in ibv_get_async_event() failed\n");
    }
  }

  DMTCP_PLUGIN_DISABLE_CKPT();

  rslt = NEXT_IBV_FNC(ibv_get_async_event)(internal_ctx->real_ctx, event);

  switch (event->event_type) {
  /* QP events */
  case IBV_EVENT_QP_FATAL:
  case IBV_EVENT_QP_REQ_ERR:
  case IBV_EVENT_QP_ACCESS_ERR:
  case IBV_EVENT_QP_LAST_WQE_REACHED:
  case IBV_EVENT_SQ_DRAINED:
  case IBV_EVENT_COMM_EST:
  case IBV_EVENT_PATH_MIG:
  case IBV_EVENT_PATH_MIG_ERR:
    internal_qp = get_qp_from_pointer(event->element.qp);
    assert(internal_qp != NULL);
    event->element.qp = &internal_qp->user_qp;
    break;

  /* CQ events */
  case IBV_EVENT_CQ_ERR:
    internal_cq = get_cq_from_pointer(event->element.cq);
    assert(internal_cq != NULL);
    event->element.cq = &internal_cq->user_cq;
    break;

  /*SRQ events */
  case IBV_EVENT_SRQ_ERR:
  case IBV_EVENT_SRQ_LIMIT_REACHED:
    internal_srq = get_srq_from_pointer(event->element.srq);
    assert(internal_srq != NULL);
    event->element.srq = &internal_srq->user_srq;
    break;
  case IBV_EVENT_PORT_ACTIVE:
  case IBV_EVENT_PORT_ERR:
  case IBV_EVENT_LID_CHANGE:
  case IBV_EVENT_PKEY_CHANGE:
  case IBV_EVENT_SM_CHANGE:
  case IBV_EVENT_CLIENT_REREGISTER:
  case IBV_EVENT_GID_CHANGE:
    break;

  /* CA events */
  case IBV_EVENT_DEVICE_FATAL:
    return rslt;
    break;
  default:
    IBV_WARNING("Warning: could not identify the ibv_event_type\n");
    break;
  }

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

void
_ack_async_event(struct ibv_async_event *event)
{
  struct internal_ibv_qp *internal_qp;
  struct internal_ibv_cq *internal_cq;
  struct internal_ibv_srq *internal_srq;

  switch (event->event_type) {
  /* QP events */
  case IBV_EVENT_QP_FATAL:
  case IBV_EVENT_QP_REQ_ERR:
  case IBV_EVENT_QP_ACCESS_ERR:
  case IBV_EVENT_QP_LAST_WQE_REACHED:
  case IBV_EVENT_SQ_DRAINED:
  case IBV_EVENT_COMM_EST:
  case IBV_EVENT_PATH_MIG:
  case IBV_EVENT_PATH_MIG_ERR:
    internal_qp = ibv_qp_to_internal(event->element.qp);
    assert(internal_qp != NULL);
    event->element.qp = internal_qp->real_qp;
    break;

  /* CQ events */
  case IBV_EVENT_CQ_ERR:
    internal_cq = ibv_cq_to_internal(event->element.cq);
    assert(internal_cq != NULL);
    event->element.cq = internal_cq->real_cq;
    break;

  /*SRQ events */
  case IBV_EVENT_SRQ_ERR:
  case IBV_EVENT_SRQ_LIMIT_REACHED:
    internal_srq = ibv_srq_to_internal(event->element.srq);
    assert(internal_srq != NULL);
    event->element.srq = internal_srq->real_srq;
    break;
  case IBV_EVENT_PORT_ACTIVE:
  case IBV_EVENT_PORT_ERR:
  case IBV_EVENT_LID_CHANGE:
  case IBV_EVENT_PKEY_CHANGE:
  case IBV_EVENT_SM_CHANGE:
  case IBV_EVENT_CLIENT_REREGISTER:
  case IBV_EVENT_GID_CHANGE:
    break;

  /* CA events */
  case IBV_EVENT_DEVICE_FATAL:
    break;
  default:
    IBV_WARNING("Warning: Could not identify the ibv_event_type\n");
    break;
  }

  NEXT_IBV_FNC(ibv_ack_async_event)(event);
}

// ! This performs the work of freeing the device list

/*! This function will free the real device list and then
 * delete the members of the device list copy
 */
void
_free_device_list(struct ibv_device **dev_list)
{
  struct ibv_device **real_dev_list;
  int i;
  bool all_device_free = true;
  struct dev_list_info *list_info;

  if (!dev_list) {
    return;
  }

  list_info = ibv_device_to_internal(dev_list[0])->list_info;
  real_dev_list = list_info->real_dev_list;

  NEXT_IBV_FNC(ibv_free_device_list)(real_dev_list);

  for (i = 0; i < list_info->num_devices; i++) {
    struct internal_ibv_dev *dev = ibv_device_to_internal(dev_list[i]);
    if (dev->in_use) {
      all_device_free = false;
      break;
    }
  }

  if (all_device_free) {
    for (i = 0; i < list_info->num_devices; i++) {
      struct internal_ibv_dev *dev = ibv_device_to_internal(dev_list[i]);
      free(dev);
    }
    free(dev_list);
    free(list_info);
  } else {
    list_info->in_free = true;
  }
}

/*
 * FIXME :if a checkpoint happens between ibv_get_device_list() and
 * ibv_open_device(), the current code doesn't work for restart.
 * We currently assume these work is done at initialization phase.
 * We need to recreate the device list(s) that have been created,
 * and not yet freed.
 */
struct ibv_context *
_open_device(struct ibv_device *device)
{
  struct internal_ibv_dev *dev = ibv_device_to_internal(device);
  struct internal_ibv_ctx *ctx;
  struct ibv_device_attr device_attr;
  int ret;
  uint8_t port;

  assert(IS_INTERNAL_IBV_STRUCT(dev));

  ctx = malloc(sizeof(struct internal_ibv_ctx));
  if (!ctx) {
    IBV_ERROR("Couldn't allocate memory for _open_device\n");
  }

  ctx->real_ctx = NEXT_IBV_FNC(ibv_open_device)(dev->real_dev);

  if (ctx->real_ctx == NULL) {
    IBV_ERROR("Could not allocate the real ctx.\n");
  }

  INIT_INTERNAL_IBV_TYPE(ctx);
  dev->in_use = true;

  // Initilizae local virtual-to-real lid mapping
  pthread_mutex_lock(&lid_mutex);
  if (lid_mapping_initialized) {
    pthread_mutex_unlock(&lid_mutex);
  } else {
    lid_mapping_initialized = true;
    pthread_mutex_unlock(&lid_mutex);
    ret = NEXT_IBV_FNC(ibv_query_device)(ctx->real_ctx, &device_attr);
    if (ret) {
      IBV_ERROR("Error getting device attributes.\n");
    }
    for (port = 1; port <= device_attr.phys_port_cnt; port++) {
      struct ibv_port_attr port_attr;

      ret = NEXT_IBV_FNC(ibv_query_port)(ctx->real_ctx, port, &port_attr);
      if (ret) {
        IBV_ERROR("Error getting port attributes.\n");
      }

      if (port_attr.state == IBV_PORT_ARMED ||
          port_attr.state == IBV_PORT_ACTIVE) {
        lid_mapping_t *mapping = malloc(sizeof(lid_mapping_t));

        if (!mapping) {
          IBV_ERROR("Unable to allocate memory for lid mapping.\n");
        }

        mapping->port = port;
        mapping->real_lid = port_attr.lid;
        pthread_mutex_lock(&lid_mutex);
        mapping->virtual_lid = base_lid + lid_offset;
        lid_offset++;
        if (lid_offset >= LID_QUOTA) {
          IBV_ERROR("Lid exceeds quota\n");
        }
        list_push_back(&lid_list, &mapping->elem);
        pthread_mutex_unlock(&lid_mutex);

        // Send the mapping to the coordinator.

        IBV_DEBUG("Sending virtual lid: 0x%04x, "
            "real lid: 0x%04x\n",
            mapping->virtual_lid,
            mapping->real_lid);

        dmtcp_send_key_val_pair_to_coordinator_sync("ib_lid",
            &mapping->virtual_lid, sizeof(mapping->virtual_lid),
            &mapping->real_lid, sizeof(mapping->real_lid));
      }
    }
  }

  memcpy(&ctx->user_ctx, ctx->real_ctx, sizeof(struct ibv_context));

  /* setup the trampolines */
  UPDATE_FUNC_ADDR(post_recv, ctx->user_ctx.ops.post_recv);
  UPDATE_FUNC_ADDR(post_srq_recv, ctx->user_ctx.ops.post_srq_recv);
  UPDATE_FUNC_ADDR(post_send, ctx->user_ctx.ops.post_send);
  UPDATE_FUNC_ADDR(poll_cq, ctx->user_ctx.ops.poll_cq);
  UPDATE_FUNC_ADDR(req_notify_cq, ctx->user_ctx.ops.req_notify_cq);

  UPDATE_FUNC_ADDR(query_device, ctx->user_ctx.ops.query_device);
  UPDATE_FUNC_ADDR(query_port, ctx->user_ctx.ops.query_port);
  UPDATE_FUNC_ADDR(alloc_pd, ctx->user_ctx.ops.alloc_pd);
  UPDATE_FUNC_ADDR(dealloc_pd, ctx->user_ctx.ops.dealloc_pd);
  UPDATE_FUNC_ADDR(reg_mr, ctx->user_ctx.ops.reg_mr);
  UPDATE_FUNC_ADDR(dereg_mr, ctx->user_ctx.ops.dereg_mr);
  // UPDATE_FUNC_ADDR(create_cq, ctx->user_ctx.ops.create_cq);
  UPDATE_FUNC_ADDR(resize_cq, ctx->user_ctx.ops.resize_cq);
  UPDATE_FUNC_ADDR(destroy_cq, ctx->user_ctx.ops.destroy_cq);
  UPDATE_FUNC_ADDR(create_srq, ctx->user_ctx.ops.create_srq);
  UPDATE_FUNC_ADDR(modify_srq, ctx->user_ctx.ops.modify_srq);
  UPDATE_FUNC_ADDR(query_srq, ctx->user_ctx.ops.query_srq);
  UPDATE_FUNC_ADDR(destroy_srq, ctx->user_ctx.ops.destroy_srq);
  UPDATE_FUNC_ADDR(create_qp, ctx->user_ctx.ops.create_qp);
  UPDATE_FUNC_ADDR(query_qp, ctx->user_ctx.ops.query_qp);
  UPDATE_FUNC_ADDR(modify_qp, ctx->user_ctx.ops.modify_qp);
  UPDATE_FUNC_ADDR(destroy_qp, ctx->user_ctx.ops.destroy_qp);
  UPDATE_FUNC_ADDR(create_ah, ctx->user_ctx.ops.create_ah);
  UPDATE_FUNC_ADDR(destroy_ah, ctx->user_ctx.ops.destroy_ah);

  UPDATE_FUNC_ADDR(alloc_mw, ctx->user_ctx.ops.alloc_mw);
  // UPDATE_FUNC_ADDR(bind_mw, ctx->user_ctx.ops.bind_mw);
  UPDATE_FUNC_ADDR(dealloc_mw, ctx->user_ctx.ops.dealloc_mw);

  ctx->user_ctx.device = device;

  list_push_back(&ctx_list, &ctx->elem);

  return &ctx->user_ctx;
}

int
_query_device(struct ibv_context *context, struct ibv_device_attr *device_attr)
{
  struct internal_ibv_ctx *internal_ctx = ibv_ctx_to_internal(context);

  if (!IS_INTERNAL_IBV_STRUCT(internal_ctx)) {
    return NEXT_IBV_FNC(ibv_query_device)(context, device_attr);
  }

  return NEXT_IBV_FNC(ibv_query_device)(internal_ctx->real_ctx, device_attr);
}

int
_query_port(struct ibv_context *context,
            uint8_t port_num,
            struct ibv_port_attr *port_attr)
{
  struct internal_ibv_ctx *internal_ctx = ibv_ctx_to_internal(context);
  int ret;

  IBV_DEBUG("******* WRAPPER for ibv_query_port\n");

  // This is found in some mellanox drivers, where ibv_modify_qp() calls
  // ibv_query_port() internally
  if (!IS_INTERNAL_IBV_STRUCT(internal_ctx)) {
    return NEXT_IBV_FNC(ibv_query_port)(context, port_num, port_attr);
  }

  ret = NEXT_IBV_FNC(ibv_query_port)(internal_ctx->real_ctx,
                                     port_num, port_attr);
  if (ret == 0 &&
      (port_attr->state == IBV_PORT_ARMED ||
       port_attr->state == IBV_PORT_ACTIVE)) {
    struct list_elem *e;

    pthread_mutex_lock(&lid_mutex);
    for (e = list_begin(&lid_list);
         e != list_end(&lid_list);
         e = list_next(e)) {
      lid_mapping_t *mapping = list_entry(e, lid_mapping_t, elem);

      if (mapping->port == port_num) {
        port_attr->lid = mapping->virtual_lid;
        break;
      }
    }
    assert(e != list_end(&lid_list));
    pthread_mutex_unlock(&lid_mutex);
  }

  return ret;
}

int
_query_pkey(struct ibv_context *context,
            uint8_t port_num,
            int index,
            uint16_t *pkey)
{
  struct internal_ibv_ctx *internal_ctx = ibv_ctx_to_internal(context);

  if (!IS_INTERNAL_IBV_STRUCT(internal_ctx)) {
    return NEXT_IBV_FNC(ibv_query_pkey)(context, port_num, index, pkey);
  }

  return NEXT_IBV_FNC(ibv_query_pkey)(internal_ctx->real_ctx,
                                      port_num, index, pkey);
}

int
_query_gid(struct ibv_context *context,
           uint8_t port_num,
           int index,
           union ibv_gid *gid)
{
  struct internal_ibv_ctx *internal_ctx = ibv_ctx_to_internal(context);

  if (!IS_INTERNAL_IBV_STRUCT(internal_ctx)) {
    return NEXT_IBV_FNC(ibv_query_gid)(context, port_num, index, gid);
  }

  return NEXT_IBV_FNC(ibv_query_gid)(internal_ctx->real_ctx,
                                     port_num, index, gid);
}

int
_close_device(struct ibv_context *ctx)
{
  struct internal_ibv_ctx *internal_ctx = ibv_ctx_to_internal(ctx);
  struct internal_ibv_dev *dev;
  struct dev_list_info *list_info;

  dev = ibv_device_to_internal(internal_ctx->user_ctx.device);
  assert(IS_INTERNAL_IBV_STRUCT(dev));
  dev->in_use = false;
  list_info = dev->list_info;

  int rslt = NEXT_IBV_FNC(ibv_close_device)(internal_ctx->real_ctx);

  list_remove(&internal_ctx->elem);
  free(internal_ctx);

  if (list_info->in_free) {
    int i;
    struct ibv_device **user_list;
    bool all_device_free = true;
    struct internal_ibv_dev *dev_elem;

    user_list = list_info->user_dev_list;
    for (i = 0; i < list_info->num_devices; i++) {
      dev_elem = ibv_device_to_internal(user_list[i]);
      if (dev_elem->in_use) {
        all_device_free = false;
        break;
      }
    }

    if (all_device_free) {
      for (i = 0; i < list_info->num_devices; i++) {
        dev_elem = ibv_device_to_internal(user_list[i]);
        free(dev_elem);
      }
      free(user_list);
      free(list_info);
    }
  }


  return rslt;
}

struct ibv_pd *
_alloc_pd(struct ibv_context *context)
{
  struct internal_ibv_ctx *internal_ctx = ibv_ctx_to_internal(context);
  struct internal_ibv_pd *pd;

  assert(IS_INTERNAL_IBV_STRUCT(internal_ctx));
  pd = malloc(sizeof(struct internal_ibv_pd));

  if (!pd) {
    IBV_ERROR("Cannot allocate memory for _alloc_pd\n");
  }

  pd->real_pd = NEXT_IBV_FNC(ibv_alloc_pd)(internal_ctx->real_ctx);

  if (!pd->real_pd) {
    IBV_ERROR("Cannot allocate pd\n");
  }

  INIT_INTERNAL_IBV_TYPE(pd);
  memcpy(&pd->user_pd, pd->real_pd, sizeof(struct ibv_pd));
  pd->user_pd.context = context;

  pthread_mutex_lock(&pd_mutex);
  pd->pd_id = (uint32_t)getpid() + pd_id_offset;
  pd_id_offset++;

  list_push_back(&pd_list, &pd->elem);

  pthread_mutex_unlock(&pd_mutex);

  return &pd->user_pd;
}

int
_dealloc_pd(struct ibv_pd *pd)
{
  struct internal_ibv_pd *internal_pd = ibv_pd_to_internal(pd);
  int rslt;

  assert(IS_INTERNAL_IBV_STRUCT(internal_pd));
  rslt = NEXT_IBV_FNC(ibv_dealloc_pd)(internal_pd->real_pd);

  list_remove(&internal_pd->elem);
  free(internal_pd);

  return rslt;
}

struct ibv_mr *
_reg_mr(struct ibv_pd *pd, void *addr, size_t length, int flag)
{
  struct internal_ibv_pd *internal_pd = ibv_pd_to_internal(pd);
  struct internal_ibv_mr *internal_mr;

  assert(IS_INTERNAL_IBV_STRUCT(internal_pd));
  internal_mr = malloc(sizeof(struct internal_ibv_mr));
  if (!internal_mr) {
    IBV_ERROR("Could not allocate memory for _reg_mr\n");
  }

  internal_mr->real_mr = NEXT_IBV_FNC(ibv_reg_mr)(internal_pd->real_pd,
                                                  addr, length, flag);
  if (!internal_mr->real_mr) {
    IBV_ERROR("Could not register mr.\n");
  }

  internal_mr->flags = flag;

  INIT_INTERNAL_IBV_TYPE(internal_mr);
  memcpy(&internal_mr->user_mr, internal_mr->real_mr,
         sizeof(struct ibv_mr));
  internal_mr->user_mr.context = internal_pd->user_pd.context;
  internal_mr->user_mr.pd = &internal_pd->user_pd;

  /*
  *  We need to check that memery regions created
  *  before checkpointing and after restarting will not
  *  have the same lkey/rkey.
  */
  if (is_restart) {
    struct list_elem *e;
    for (e = list_begin(&mr_list);
         e != list_end(&mr_list);
         e = list_next(e)) {
      struct internal_ibv_mr *mr;

      mr = list_entry(e, struct internal_ibv_mr, elem);
      if (mr->user_mr.rkey == internal_mr->user_mr.rkey ||
          mr->user_mr.lkey == internal_mr->user_mr.lkey) {
        IBV_ERROR("Duplicate lkey/rkey genereated after restart\n");
      }
    }
  }

  list_push_back(&mr_list, &internal_mr->elem);

  return &internal_mr->user_mr;
}

int
_dereg_mr(struct ibv_mr *mr)
{
  struct internal_ibv_mr *internal_mr = ibv_mr_to_internal(mr);
  struct list_elem *e;

  assert(IS_INTERNAL_IBV_STRUCT(internal_mr));
  int rslt = NEXT_IBV_FNC(ibv_dereg_mr)(internal_mr->real_mr);

  /*
   * The destruction of the internal mr structure is delayed till
   * the end of the application to handle the following case:
   *
   * - A mr is created before checkpoint.
   * - On restart, it's rkey mapping is propogated to the coordinator.
   * - The mr is deregistered after restart.
   * - A new mr is created. It is possible that this mr has the same
   *   lkey/rkey. We'd have to update the mapping to the coordinator,
   *   which will incur runtime overhead.
   *
   * Most applications tend to register mrs during initialization,
   * and to register them during finalization. Therefore, currently
   * we simply terminate the application if there's any duplication.
   * See the check in _reg_mr().
   *
   * TODO: we should update the mapping instead of terminating the
   * application.
   *
   */
  // list_remove(&internal_mr->elem);
  // free(internal_mr);

  return rslt;
}

int _req_notify_cq(struct ibv_cq *cq, int solicited_only)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  struct internal_ibv_cq *internal_cq = ibv_cq_to_internal(cq);
  int rslt;

  assert(IS_INTERNAL_IBV_STRUCT(internal_cq));
  rslt = _real_ibv_req_notify_cq(internal_cq->real_cq, solicited_only);

  if (rslt == 0) {
    struct ibv_req_notify_cq_log *log;

    log = malloc(sizeof(struct ibv_req_notify_cq_log));
    if (!log) {
      IBV_ERROR("Could not allocate memory for _req_notify_cq.\n");
    }

    log->solicited_only = solicited_only;
    list_push_back(&internal_cq->req_notify_log, &log->elem);
  }

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

struct ibv_cq *
_create_cq(struct ibv_context *context,
           int cqe,
           void *cq_context,
           struct ibv_comp_channel *channel,
           int comp_vector)
{
  struct internal_ibv_ctx *internal_ctx = ibv_ctx_to_internal(context);
  struct internal_ibv_cq *internal_cq;
  struct ibv_comp_channel *real_channel;

  assert(IS_INTERNAL_IBV_STRUCT(internal_ctx));
  if (channel) {
    assert(IS_INTERNAL_IBV_STRUCT(ibv_comp_to_internal(channel)));
    real_channel = ibv_comp_to_internal(channel)->real_channel;
  } else {
    real_channel = NULL;
  }

  internal_cq = malloc(sizeof(struct internal_ibv_cq));
  if (!internal_cq) {
    IBV_ERROR("Could not allocate memory for _create_cq\n");
  }

  /* set up the lists */
  list_init(&internal_cq->wc_queue);
  list_init(&internal_cq->req_notify_log);

  internal_cq->real_cq = NEXT_IBV_FNC(ibv_create_cq)(internal_ctx->real_ctx,
                                                     cqe, cq_context,
                                                     real_channel, comp_vector);

  INIT_INTERNAL_IBV_TYPE(internal_cq);
  internal_cq->comp_vector = comp_vector;

  memcpy(&internal_cq->user_cq, internal_cq->real_cq, sizeof(struct ibv_cq));

  internal_cq->user_cq.context = &internal_ctx->user_ctx;
  internal_cq->user_cq.channel = channel;

  list_push_back(&cq_list, &internal_cq->elem);
  return &internal_cq->user_cq;
}

int
_resize_cq(struct ibv_cq *cq, int cqe)
{
  struct internal_ibv_cq *internal_cq = ibv_cq_to_internal(cq);
  int rslt;

  if (!IS_INTERNAL_IBV_STRUCT(internal_cq)) {
    return NEXT_IBV_FNC(ibv_resize_cq)(cq, cqe);
  }

  rslt = NEXT_IBV_FNC(ibv_resize_cq)(internal_cq->real_cq, cqe);
  if (!rslt) {
    internal_cq->user_cq.cqe = internal_cq->real_cq->cqe;
  }

  return rslt;
}

int
_destroy_cq(struct ibv_cq *cq)
{
  struct internal_ibv_cq *internal_cq = ibv_cq_to_internal(cq);
  int rslt;

  assert(IS_INTERNAL_IBV_STRUCT(internal_cq));
  rslt = NEXT_IBV_FNC(ibv_destroy_cq)(internal_cq->real_cq);

  /* destroy the four lists */
  struct list_elem *e;

  e = list_begin(&internal_cq->wc_queue);
  while (e != list_end(&internal_cq->wc_queue)) {
    struct list_elem *w = e;
    struct ibv_wc_wrapper *wc = list_entry(e, struct ibv_wc_wrapper, elem);
    e = list_next(e);
    list_remove(w);
    free(wc);
  }

  e = list_begin(&internal_cq->req_notify_log);
  while (e != list_end(&internal_cq->req_notify_log)) {
    struct list_elem *w = e;
    struct ibv_req_notify_cq_log *log;
    log = list_entry(e, struct ibv_req_notify_cq_log, elem);
    e = list_next(e);
    list_remove(w);
    free(log);
  }

  /* end destroying the lists */

  list_remove(&internal_cq->elem);
  free(internal_cq);

  return rslt;
}

struct ibv_qp *
_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr)
{
  struct internal_ibv_pd *internal_pd = ibv_pd_to_internal(pd);
  struct ibv_qp_init_attr attr = *qp_init_attr;
  struct internal_ibv_qp *internal_qp;
  qp_num_mapping_t *mapping;

  assert(IS_INTERNAL_IBV_STRUCT(internal_pd));
  internal_qp = malloc(sizeof(struct internal_ibv_qp));
  mapping = malloc(sizeof(qp_num_mapping_t));
  if (!internal_qp || !mapping) {
    IBV_ERROR("Cannot allocate memory for _create_qp\n");
  }
  memset(internal_qp, 0, sizeof(struct internal_ibv_qp));
  memset(mapping, 0, sizeof(qp_num_mapping_t));

  /* fix up the qp_init_attr here */
  assert(IS_INTERNAL_IBV_STRUCT(ibv_cq_to_internal(qp_init_attr->recv_cq)));
  assert(IS_INTERNAL_IBV_STRUCT(ibv_cq_to_internal(qp_init_attr->send_cq)));
  attr.recv_cq = ibv_cq_to_internal(qp_init_attr->recv_cq)->real_cq;
  attr.send_cq = ibv_cq_to_internal(qp_init_attr->send_cq)->real_cq;
  if (attr.srq) {
    assert(IS_INTERNAL_IBV_STRUCT(ibv_srq_to_internal(qp_init_attr->srq)));
    attr.srq = ibv_srq_to_internal(qp_init_attr->srq)->real_srq;
  }

  internal_qp->real_qp = NEXT_IBV_FNC(ibv_create_qp)
                             (internal_pd->real_pd, &attr);
  if (!internal_qp->real_qp) {
    IBV_WARNING("Failed to create qp\n");
    free(internal_qp);
    return NULL;
  }

  INIT_INTERNAL_IBV_TYPE(internal_qp);
  memcpy(&internal_qp->user_qp,
         internal_qp->real_qp,
         sizeof(struct ibv_qp));

  list_init(&internal_qp->modify_qp_log);
  list_init(&internal_qp->post_send_log);
  list_init(&internal_qp->post_recv_log);
  internal_qp->user_qp.context = internal_pd->user_pd.context;
  internal_qp->user_qp.pd = &internal_pd->user_pd;
  internal_qp->user_qp.recv_cq = qp_init_attr->recv_cq;
  internal_qp->user_qp.send_cq = qp_init_attr->send_cq;
  internal_qp->user_qp.srq = qp_init_attr->srq;
  internal_qp->init_attr = *qp_init_attr;

  /*
   * We use virtual pid + offset to virtualize the qp_num, so that
   * all qp numbers are unique across the computation (since every
   * virtual pid is unique).
   * */
  pthread_mutex_lock(&qp_mutex);
  if (qp_num_offset >= 1000) {
    IBV_ERROR("IB plugin does not support more than 1000 "
              "queue pairs per process.\n");
  }
  internal_qp->user_qp.qp_num = (uint32_t)getpid() + qp_num_offset;
  qp_num_offset++;
  mapping->virtual_qp_num = internal_qp->user_qp.qp_num;
  mapping->real_qp_num = internal_qp->real_qp->qp_num;
  mapping->pd_id = internal_pd->pd_id;

  list_push_back(&qp_list, &internal_qp->elem);
  list_push_back(&qp_num_list, &mapping->elem);
  pthread_mutex_unlock(&qp_mutex);

  // Update the key-value database before returning.
  {
    qp_id_t cur_qp_id = {
      .qp_num = mapping->real_qp_num,
      .pd_id = mapping->pd_id
    };

    IBV_DEBUG("Sending virtual qp_num: %lu, "
        "real qp_num: %lu, pd_id: %lu\n",
        (unsigned long)internal_qp->user_qp.qp_num,
        (unsigned long)cur_qp_id.qp_num,
        (unsigned long)cur_qp_id.pd_id);

    dmtcp_send_key_val_pair_to_coordinator_sync("ib_qp",
        &internal_qp->user_qp.qp_num,
        sizeof(internal_qp->user_qp.qp_num),
        &cur_qp_id, sizeof(cur_qp_id));
  }

  return &internal_qp->user_qp;
}

int
_destroy_qp(struct ibv_qp *qp)
{
  struct internal_ibv_qp *internal_qp = ibv_qp_to_internal(qp);
  qp_num_mapping_t *mapping;
  int rslt;

  assert(IS_INTERNAL_IBV_STRUCT(internal_qp));
  rslt = NEXT_IBV_FNC(ibv_destroy_qp)(internal_qp->real_qp);
  struct list_elem *e = list_begin(&internal_qp->modify_qp_log);

  while (e != list_end(&internal_qp->modify_qp_log)) {
    struct list_elem *w = e;
    struct ibv_modify_qp_log *log;

    log = list_entry(e, struct ibv_modify_qp_log, elem);
    e = list_next(e);
    list_remove(w);
    free(log);
  }

  e = list_begin(&internal_qp->post_recv_log);
  while (e != list_end(&internal_qp->post_recv_log)) {
    struct list_elem *w = e;
    struct ibv_post_recv_log *log;

    log = list_entry(e, struct ibv_post_recv_log, elem);
    e = list_next(e);
    list_remove(w);
    free(log);
  }

  e = list_begin(&internal_qp->post_send_log);
  while (e != list_end(&internal_qp->post_send_log)) {
    struct list_elem *w = e;
    struct ibv_post_send_log *log;

    log = list_entry(e, struct ibv_post_send_log, elem);
    assert(log->magic == SEND_MAGIC);
    e = list_next(e);
    list_remove(w);
    free(log);
  }

  pthread_mutex_lock(&qp_mutex);
  e = list_begin(&qp_num_list);
  while (e != list_end(&qp_num_list)) {
    mapping = list_entry(e, qp_num_mapping_t, elem);
    if (mapping->virtual_qp_num == internal_qp->user_qp.qp_num) {
      break;
    }
    e = list_next(e);
  }

  assert(e != list_end(&qp_num_list));
  list_remove(&mapping->elem);
  list_remove(&internal_qp->elem);

  pthread_mutex_unlock(&qp_mutex);
  free(mapping);
  free(internal_qp);

  return rslt;
}

int
_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask)
{
  struct internal_ibv_qp *internal_qp = ibv_qp_to_internal(qp);
  struct ibv_modify_qp_log *log;
  struct ibv_qp_attr real_attr = *attr;
  int rslt;

  assert(IS_INTERNAL_IBV_STRUCT(internal_qp));
  log = malloc(sizeof(struct ibv_modify_qp_log));
  if (!log) {
    IBV_ERROR("Couldn't allocate memory for log.\n");
  }

  log->attr = *attr;
  log->attr_mask = attr_mask;
  list_push_back(&internal_qp->modify_qp_log, &log->elem);

  if (attr_mask & IBV_QP_DEST_QPN) {
    qp_id_t rem_qp_id = translate_qp_num(attr->dest_qp_num);
    real_attr.dest_qp_num = rem_qp_id.qp_num;
    internal_qp->remote_pd_id = rem_qp_id.pd_id;
  }

  if (attr_mask & IBV_QP_AV) {
    real_attr.ah_attr.dlid = translate_lid(attr->ah_attr.dlid);
  }

  if (attr_mask & IBV_QP_SQ_PSN) {
    real_attr.sq_psn = DEFAULT_PSN;
  }

  if (attr_mask & IBV_QP_RQ_PSN) {
    real_attr.rq_psn = DEFAULT_PSN;
  }

  rslt = NEXT_IBV_FNC(ibv_modify_qp)
             (internal_qp->real_qp, &real_attr, attr_mask);
  if (rslt) {
    IBV_WARNING("Failed to modify qp\n");
  }

  internal_qp->user_qp.state = internal_qp->real_qp->state;

  return rslt;
}

int
_query_qp(struct ibv_qp *qp,
          struct ibv_qp_attr *attr,
          int attr_mask,
          struct ibv_qp_init_attr *init_attr)
{
  struct internal_ibv_qp *internal_qp = ibv_qp_to_internal(qp);
  int rslt;
  struct list_elem *e;

  assert(IS_INTERNAL_IBV_STRUCT(internal_qp));
  rslt = NEXT_IBV_FNC(ibv_query_qp)(internal_qp->real_qp,
                                    attr, attr_mask, init_attr);

  assert(get_cq_from_pointer(init_attr->recv_cq) != NULL);
  assert(get_cq_from_pointer(init_attr->send_cq) != NULL);
  init_attr->recv_cq = &get_cq_from_pointer(init_attr->recv_cq)->user_cq;
  init_attr->send_cq = &get_cq_from_pointer(init_attr->send_cq)->user_cq;
  if (init_attr->srq) {
    assert(get_srq_from_pointer(init_attr->srq) != NULL);
    init_attr->srq = &get_srq_from_pointer(init_attr->srq)->user_srq;
  }

  if (attr_mask & IBV_QP_AV) {
    for (e = list_begin(&lid_list);
         e != list_end(&lid_list);
         e = list_next(e)) {
      lid_mapping_t *mapping = list_entry(e, lid_mapping_t, elem);

      if (mapping->real_lid == attr->ah_attr.dlid) {
        attr->ah_attr.dlid = mapping->virtual_lid;
        break;
      }
    }
    assert(e != list_end(&lid_list));
  }

  if (attr_mask & IBV_QP_DEST_QPN) {
    for (e = list_begin(&qp_num_list);
         e != list_end(&qp_num_list);
         e = list_next(e)) {
      qp_num_mapping_t *mapping =
        list_entry(e, qp_num_mapping_t, elem);

      if (mapping->real_qp_num == attr->dest_qp_num) {
        attr->dest_qp_num = mapping->virtual_qp_num;
        break;
      }
    }
    assert(e != list_end(&lid_list));
  }

  return rslt;
}

struct ibv_srq *
_create_srq(struct ibv_pd *pd, struct ibv_srq_init_attr *srq_init_attr)
{
  struct internal_ibv_pd *internal_pd;
  struct internal_ibv_srq *internal_srq;

  internal_pd = ibv_pd_to_internal(pd);

  assert(IS_INTERNAL_IBV_STRUCT(internal_pd));
  internal_srq = malloc(sizeof(struct internal_ibv_srq));

  if (internal_srq == NULL) {
    IBV_ERROR("Cannot allocate memory for _create_srq\n");
  }

  internal_srq->real_srq = NEXT_IBV_FNC(ibv_create_srq)(internal_pd->real_pd,
                                                        srq_init_attr);

  if (internal_srq->real_srq == NULL) {
    IBV_ERROR("_real_ibv_create_srq failed\n");
  }

  internal_srq->recv_count = 0;
  INIT_INTERNAL_IBV_TYPE(internal_srq);
  memcpy(&internal_srq->user_srq,
         internal_srq->real_srq,
         sizeof(struct ibv_srq));

  /* fix up the user_srq*/
  /* loop to find the right context the "dumb way" */
  /* This should probably just take the contxt from pd */
  struct list_elem *e;
  struct internal_ibv_ctx *rslt = NULL;
  for (e = list_begin(&ctx_list);
       e != list_end(&ctx_list);
       e = list_next(e)) {
    struct internal_ibv_ctx *ctx;

    ctx = list_entry(e, struct internal_ibv_ctx, elem);
    if (ctx->real_ctx == internal_srq->real_srq->context) {
      rslt = ctx;
      break;
    }
  }

  if (!rslt) {
    IBV_ERROR("Could not find context in _create_srq\n");
  }

  list_init(&internal_srq->modify_srq_log);
  list_init(&internal_srq->post_srq_recv_log);
  internal_srq->user_srq.context = &rslt->user_ctx;
  internal_srq->user_srq.pd = &internal_pd->user_pd;
  internal_srq->init_attr = *srq_init_attr;

  list_push_back(&srq_list, &internal_srq->elem);
  return &internal_srq->user_srq;
}

int
_destroy_srq(struct ibv_srq *srq)
{
  struct internal_ibv_srq *internal_srq = ibv_srq_to_internal(srq);
  int rslt;
  struct list_elem *e;

  assert(IS_INTERNAL_IBV_STRUCT(internal_srq));
  rslt = NEXT_IBV_FNC(ibv_destroy_srq)(internal_srq->real_srq);
  e = list_begin(&internal_srq->modify_srq_log);
  while (e != list_end(&internal_srq->modify_srq_log)) {
    struct list_elem *w = e;
    struct ibv_modify_srq_log *log;

    log = list_entry(e, struct ibv_modify_srq_log, elem);
    e = list_next(e);
    list_remove(w);
    free(log);
  }

  e = list_begin(&internal_srq->post_srq_recv_log);
  while (e != list_end(&internal_srq->post_srq_recv_log)) {
    struct list_elem *w = e;
    struct ibv_post_srq_recv_log *log;

    log = list_entry(e, struct ibv_post_srq_recv_log, elem);
    e = list_next(e);
    list_remove(w);
    free(log);
  }

  list_remove(&internal_srq->elem);
  free(internal_srq);

  return rslt;
}

int
_modify_srq(struct ibv_srq *srq, struct ibv_srq_attr *attr, int attr_mask)
{
  struct internal_ibv_srq *internal_srq = ibv_srq_to_internal(srq);
  struct ibv_modify_srq_log *log;

  assert(IS_INTERNAL_IBV_STRUCT(internal_srq));
  log = malloc(sizeof(struct ibv_modify_srq_log));
  if (!log) {
    IBV_ERROR("Couldn't allocate memory for log.\n");
  }

  log->attr = *attr;
  log->attr_mask = attr_mask;

  int rslt = NEXT_IBV_FNC(ibv_modify_srq)(internal_srq->real_srq,
                                          attr, attr_mask);
  if (!rslt) {
    list_push_back(&internal_srq->modify_srq_log, &log->elem);
  }
  return rslt;
}

int
_query_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr)
{
  if (!IS_INTERNAL_IBV_STRUCT(ibv_srq_to_internal(srq))) {
    return NEXT_IBV_FNC(ibv_query_srq)(srq, srq_attr);
  }

  return NEXT_IBV_FNC(ibv_query_srq)(ibv_srq_to_internal(srq)->real_srq,
                                     srq_attr);
}

int _post_recv(struct ibv_qp *qp, struct ibv_recv_wr *wr,
                   struct ibv_recv_wr **bad_wr) {
  struct internal_ibv_qp *internal_qp = ibv_qp_to_internal(qp);
  struct ibv_recv_wr *copy_wr;
  struct ibv_recv_wr *copy_wr1;

  DMTCP_PLUGIN_DISABLE_CKPT();

  assert(IS_INTERNAL_IBV_STRUCT(internal_qp));
  copy_wr = copy_recv_wr(wr);
  int rslt;
  update_lkey_recv(copy_wr);

  rslt = _real_ibv_post_recv(internal_qp->real_qp, copy_wr, bad_wr);

  delete_recv_wr(copy_wr);
  copy_wr = copy_recv_wr(wr);
  copy_wr1 = copy_wr;
  while (copy_wr1) {
    struct ibv_post_recv_log *log = malloc(sizeof(struct ibv_post_recv_log));
    struct ibv_recv_wr *tmp;

    if (!log) {
      IBV_ERROR("Could not allocate memory for log.\n");
    }
    log->wr = *copy_wr1;
    log->wr.next = NULL;

    list_push_back(&internal_qp->post_recv_log, &log->elem);

    tmp = copy_wr1;
    copy_wr1 = copy_wr1->next;
    free(tmp);
  }

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

int _post_srq_recv(struct ibv_srq *srq, struct ibv_recv_wr *wr,
                       struct ibv_recv_wr **bad_wr) {
  struct internal_ibv_srq *internal_srq = ibv_srq_to_internal(srq);
  struct ibv_recv_wr *copy_wr;
  int rslt;
  struct ibv_recv_wr *copy_wr1;

  DMTCP_PLUGIN_DISABLE_CKPT();

  assert(IS_INTERNAL_IBV_STRUCT(internal_srq));
  copy_wr = copy_recv_wr(wr);
  update_lkey_recv(copy_wr);

  rslt = _real_ibv_post_srq_recv(internal_srq->real_srq, copy_wr, bad_wr);
  if (rslt) {
    IBV_ERROR("_ibv_post_srq_recv failed!\n");
  }

  delete_recv_wr(copy_wr);

  copy_wr = copy_recv_wr(wr);
  copy_wr1 = copy_wr;
  while (copy_wr1) {
    struct ibv_post_srq_recv_log *log;
    struct ibv_recv_wr *tmp;

    log = malloc(sizeof(struct ibv_post_srq_recv_log));

    if (!log) {
      IBV_ERROR("Could not allocate memory for log.\n");
    }
    log->wr = *copy_wr1;
    log->wr.next = NULL;

    list_push_back(&internal_srq->post_srq_recv_log, &log->elem);

    tmp = copy_wr1;
    copy_wr1 = copy_wr1->next;
    free(tmp);
  }

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

int _post_send(struct ibv_qp *qp, struct ibv_send_wr *wr, struct
                   ibv_send_wr **bad_wr) {
  struct internal_ibv_qp *internal_qp = ibv_qp_to_internal(qp);
  struct ibv_send_wr *copy_wr;
  int rslt;
  struct ibv_send_wr *copy_wr1;

  DMTCP_PLUGIN_DISABLE_CKPT();

  assert(IS_INTERNAL_IBV_STRUCT(internal_qp));
  copy_wr = copy_send_wr(wr);

  if (is_restart) {
    update_lkey_send(copy_wr);
  }

  switch (internal_qp->user_qp.qp_type) {
  case IBV_QPT_RC:
    if (is_restart) {
      assert(internal_qp->remote_pd_id != 0);
      update_rkey_send(copy_wr, internal_qp->remote_pd_id);
    }
    break;
  case IBV_QPT_UD:
    assert(copy_wr->opcode == IBV_WR_SEND);
    update_ud_send(copy_wr);
    break;
  default:
    IBV_ERROR("Unsupported qp type: %d\n",
              internal_qp->user_qp.qp_type);
  }

  rslt = _real_ibv_post_send(internal_qp->real_qp, copy_wr, bad_wr);

  delete_send_wr(copy_wr);

  copy_wr = copy_send_wr(wr);
  copy_wr1 = copy_wr;
  while (copy_wr1) {
    struct ibv_post_send_log *log = malloc(sizeof(struct ibv_post_send_log));
    struct ibv_send_wr *tmp;

    if (!log) {
      IBV_ERROR("Could not allocate memory for log.\n");
    }
    log->magic = SEND_MAGIC;
    log->wr = *copy_wr1;
    log->wr.next = NULL;
    list_push_back(&internal_qp->post_send_log, &log->elem);
    tmp = copy_wr1;
    copy_wr1 = copy_wr1->next;
    free(tmp);
  }

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

int _poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc)
{
  int rslt = 0;
  struct internal_ibv_cq *internal_cq = ibv_cq_to_internal(cq);
  int size, i;

  DMTCP_PLUGIN_DISABLE_CKPT();

  assert(IS_INTERNAL_IBV_STRUCT(internal_cq));
  size = list_size(&internal_cq->wc_queue);
  if (size > 0) {
    struct list_elem *e = list_front(&internal_cq->wc_queue);

    for (i = 0; (i < size) && (i < num_entries); i++) {
      struct list_elem *w = e;

      IBV_DEBUG("Polling completion from internal buffer\n");
      wc[i] = list_entry(e, struct ibv_wc_wrapper, elem)->wc;
      e = list_next(e);
      list_remove(w);
      rslt++;
    }

    if (size < num_entries) {
      int ne = _real_ibv_poll_cq(internal_cq->real_cq,
                                 num_entries - size, wc + size);

      if (ne < 0) {
	IBV_ERROR("poll_cq() error!\n");
      }
      rslt += ne;
    }
  } else {
    rslt = _real_ibv_poll_cq(internal_cq->real_cq, num_entries, wc);
    if (rslt < 0) {
      IBV_ERROR("poll_cq() error!\n");
    }
  }

  for (i = 0; i < rslt; i++) {
    if (i >= size) {
      if (wc[i].status != IBV_WC_SUCCESS) {
        IBV_WARNING("Unsuccessful completion: %d.\n", wc[i].opcode);
      } else {
        struct internal_ibv_qp *internal_qp =
          qp_num_to_qp(&qp_list, wc[i].qp_num);
        enum ibv_wc_opcode opcode = wc[i].opcode;

        wc[i].qp_num = internal_qp->user_qp.qp_num;
        if (opcode & IBV_WC_RECV ||
            opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
          if (internal_qp->user_qp.srq) {
            struct internal_ibv_srq *internal_srq;
            internal_srq = ibv_srq_to_internal(internal_qp->user_qp.srq);
            internal_srq->recv_count++;
          }
          else {
            struct list_elem *e;
            struct ibv_post_recv_log *log;

            e = list_pop_front(&internal_qp->post_recv_log);
            log = list_entry(e, struct ibv_post_recv_log, elem);

            free(log->wr.sg_list);
            free(log);
          }
        } else if (opcode == IBV_WC_SEND ||
                   opcode == IBV_WC_RDMA_WRITE ||
                   opcode == IBV_WC_RDMA_READ ||
                   opcode == IBV_WC_COMP_SWAP ||
                   opcode == IBV_WC_FETCH_ADD) {
          if (internal_qp->init_attr.sq_sig_all) {
            struct list_elem *e;
            struct ibv_post_send_log *log;

            e = list_pop_front(&internal_qp->post_send_log);
            log = list_entry(e, struct ibv_post_send_log, elem);

            assert(log->magic == SEND_MAGIC);
            free(log);
          }
          else {
            while(1) {
              struct list_elem *e;
              struct ibv_post_send_log *log;

              e = list_pop_front(&internal_qp->post_send_log);
              log = list_entry(e, struct ibv_post_send_log, elem);

              if (log->wr.send_flags & IBV_SEND_SIGNALED) {
                assert(log->magic == SEND_MAGIC);
                free(log->wr.sg_list);
                free(log);
                break;
              }
              else {
                assert(log->magic == SEND_MAGIC);
                free(log->wr.sg_list);
                free(log);
              }
            }
          }
        } else if (opcode == IBV_WC_BIND_MW) {
          IBV_ERROR("opcode %d specifies unsupported operation.\n",
                    opcode);
        } else {
          IBV_ERROR("Unknown or invalid opcode: %d\n", opcode);
        }
      }
    }
  }

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

void
_ack_cq_events(struct ibv_cq *cq, unsigned int nevents)
{
  struct internal_ibv_cq *internal_cq = ibv_cq_to_internal(cq);

  assert(IS_INTERNAL_IBV_STRUCT(internal_cq));

  return NEXT_IBV_FNC(ibv_ack_cq_events)(internal_cq->real_cq, nevents);
}

struct ibv_ah *
_create_ah(struct ibv_pd *pd, struct ibv_ah_attr *attr)
{
  struct internal_ibv_pd *internal_pd = ibv_pd_to_internal(pd);
  struct internal_ibv_ah *internal_ah;
  struct ibv_ah_attr real_attr = *attr;

  assert(IS_INTERNAL_IBV_STRUCT(internal_pd));
  internal_ah = malloc(sizeof(struct internal_ibv_ah));
  if (internal_ah == NULL) {
    IBV_ERROR("Unable to allocate memory for _create_ah\n");
  }
  memset(internal_ah, 0, sizeof(struct internal_ibv_ah));
  internal_ah->attr = *attr;

  real_attr.dlid = translate_lid(attr->dlid);

  internal_ah->real_ah = NEXT_IBV_FNC(ibv_create_ah)(internal_pd->real_pd,
                                                     &real_attr);

  if (internal_ah->real_ah == NULL) {
    IBV_ERROR("_real_ibv_create_ah failed\n");
  }

  INIT_INTERNAL_IBV_TYPE(internal_ah);
  memcpy(&internal_ah->user_ah,
         internal_ah->real_ah,
         sizeof(struct ibv_ah));
  internal_ah->user_ah.context = internal_pd->user_pd.context;
  internal_ah->user_ah.pd = &internal_pd->user_pd;

  list_push_back(&ah_list, &internal_ah->elem);

  return &internal_ah->user_ah;
}

int
_destroy_ah(struct ibv_ah *ah)
{
  struct internal_ibv_ah *internal_ah;
  int rslt;

  internal_ah = ibv_ah_to_internal(ah);
  assert(IS_INTERNAL_IBV_STRUCT(internal_ah));
  rslt = NEXT_IBV_FNC(ibv_destroy_ah)(internal_ah->real_ah);

  list_remove(&internal_ah->elem);
  free(internal_ah);

  return rslt;
}

struct ibv_mw *_alloc_mw(struct ibv_pd *pd,
                            enum ibv_mw_type type)
{
  IBV_WARNING("Not implemented.\n");
  return NEXT_IBV_FNC(ibv_alloc_mw)(pd, type);
}

/*
int _bind_mw(struct ibv_qp *qp, struct ibv_mw *mw,
                struct ibv_mw_bind *mw_bind)
{
  IBV_WARNING("Not implemented.\n");
  return NEXT_IBV_FNC(ibv_bind_mw)(qp, mw, mw_bind);
}
*/

int _dealloc_mw(struct ibv_mw *mw)
{
  IBV_WARNING("Not implemented.\n");
  return NEXT_IBV_FNC(ibv_dealloc_mw)(mw);
}

qp_id_t translate_qp_num(uint32_t virtual_qp_num) {
  struct list_elem *e;
  qp_num_mapping_t *mapping = NULL;
  qp_id_t rslt;

  pthread_mutex_lock(&qp_mutex);
  for (e = list_begin(&qp_num_list);
       e != list_end(&qp_num_list);
       e = list_next(e)) {
    mapping = list_entry(e, qp_num_mapping_t, elem);
    if (mapping->virtual_qp_num == virtual_qp_num) {
      rslt.qp_num = mapping->real_qp_num;
      rslt.pd_id = mapping->pd_id;
      break;
    }
  }

  // destination qp_num not found, query the coordinator,
  // add the mapping to the cache
  if (e == list_end(&qp_num_list)) {
    uint32_t size = sizeof(rslt);

    pthread_mutex_unlock(&qp_mutex);

    IBV_DEBUG("Querying remote qp: virtual qp_num: %lu\n",
        (unsigned long)virtual_qp_num);

    dmtcp_send_query_to_coordinator("ib_qp",
        &virtual_qp_num, sizeof(virtual_qp_num),
        &rslt, &size);
    assert(size == sizeof(rslt));

    mapping = malloc(sizeof(qp_num_mapping_t));
    if (!mapping) {
      IBV_ERROR("Cannot allocate memory for translate_qp_num\n");
    }
    mapping->virtual_qp_num = virtual_qp_num;
    mapping->real_qp_num = rslt.qp_num;
    mapping->pd_id = rslt.pd_id;
    pthread_mutex_lock(&qp_mutex);
    list_push_back(&qp_num_list, &mapping->elem);
  }

  pthread_mutex_unlock(&qp_mutex);
  return rslt;
}

uint16_t translate_lid(uint16_t virtual_lid) {
  struct list_elem *e;
  lid_mapping_t *mapping = NULL;
  uint16_t real_lid;

  pthread_mutex_lock(&lid_mutex);
  for (e = list_begin(&lid_list);
       e != list_end(&lid_list);
       e = list_next(e)) {
    mapping = list_entry(e, lid_mapping_t, elem);
    if (mapping->virtual_lid == virtual_lid) {
      real_lid = mapping->real_lid;
      break;
    }
  }
  // translation not found, query the coordinator,
  // add the mapping to the local cache
  if (e == list_end(&lid_list)) {
    uint32_t size = sizeof(real_lid);

    pthread_mutex_unlock(&lid_mutex);

    IBV_DEBUG("Querying remote lid: virtual lid: 0x%04x\n",
        virtual_lid);

    dmtcp_send_query_to_coordinator("ib_lid",
        &virtual_lid, sizeof(virtual_lid),
        &real_lid, &size);
    assert(size == sizeof(real_lid));

    mapping = malloc(sizeof(lid_mapping_t));
    if (!mapping) {
      IBV_ERROR("Cannot allocate memory for translate_lid\n");
    }
    mapping->port = 0;
    mapping->virtual_lid = virtual_lid;
    mapping->real_lid = real_lid;

    pthread_mutex_lock(&lid_mutex);
    list_push_back(&lid_list, &mapping->elem);
  }

  pthread_mutex_unlock(&lid_mutex);
  return real_lid;
}
