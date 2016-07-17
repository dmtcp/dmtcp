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
#include <infiniband/verbs.h>
#include <linux/types.h>
#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>
#include "ibv_internal.h"
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "lib/list.h"
#include "dmtcp.h"
#include "config.h"
#include <pthread.h>
#include <errno.h>

static bool is_restart = false;

//! This flag is used to trace whether ibv_fork_init is called
static bool is_fork = false;

/* these lists will track the resources so they can be recreated
 * at restart time */
//! This is the list of contexts
static struct list ctx_list = LIST_INITIALIZER(ctx_list);
//! This is the list of protection domains
static struct list pd_list = LIST_INITIALIZER(pd_list);
//! This is the list of memory regions
static struct list mr_list = LIST_INITIALIZER(mr_list);
//! This is the list of completion queues
static struct list cq_list = LIST_INITIALIZER(cq_list);
//! This is the list of queue pairs
static struct list qp_list = LIST_INITIALIZER(qp_list);
//! This is the list of shared receive queues
static struct list srq_list = LIST_INITIALIZER(srq_list);
//! This is the list of completion channels
static struct list comp_list = LIST_INITIALIZER(comp_list);
// Address Handler list
static struct list ah_list = LIST_INITIALIZER(ah_list);
//! This is the list of rkey pairs
static struct list rkey_list;

static uint32_t pd_id_count = 0;

static void send_qp_info(void);
static void query_qp_info(void);
static void send_qp_pd_info(void);
static void query_qp_pd_info(void);
static void send_rkey_info(void);
static void post_restart(void);
static void post_restart2(void);
static void nameservice_register_data(void);
static void nameservice_send_queries(void);
static void refill(void);

int _ibv_post_send(struct ibv_qp * qp, struct ibv_send_wr * wr,
                   struct ibv_send_wr ** bad_wr);
int _ibv_poll_cq(struct ibv_cq * cq, int num_entries,
                 struct ibv_wc * wc);
int _ibv_post_srq_recv(struct ibv_srq * srq, struct ibv_recv_wr * wr,
                       struct ibv_recv_wr ** bad_wr);
int _ibv_post_recv(struct ibv_qp * qp, struct ibv_recv_wr * wr,
                   struct ibv_recv_wr ** bad_wr);
int _ibv_req_notify_cq(struct ibv_cq * cq, int solicited_only);

#define DECL_FPTR(func) \
    static __typeof__(&ibv_##func) _real_ibv_##func = \
                                   (__typeof__(&ibv_##func)) NULL

#define UPDATE_FUNC_ADDR(func, addr) \
  do {                               \
    _real_ibv_##func = addr;         \
    addr = _ibv_##func;              \
  } while (0)

DECL_FPTR(post_recv);
DECL_FPTR(post_srq_recv);
DECL_FPTR(post_send);
DECL_FPTR(poll_cq);
DECL_FPTR(req_notify_cq);

/* These files are processed by sed at compile time */
#include "keys.ic"
#include "ibv_wr_ops_send.ic"
#include "ibv_wr_ops_recv.ic"
#include "get_qp_from_pointer.ic"
#include "get_cq_from_pointer.ic"
#include "get_srq_from_pointer.ic"

int dmtcp_infiniband_enabled(void) { return 1; }

static DmtcpBarrier infinibandBarriers[] = {
  {DMTCP_GLOBAL_BARRIER_PRE_CKPT, pre_checkpoint, "checkpoint"},

  {DMTCP_GLOBAL_BARRIER_RESTART, post_restart, "restart"},
  {DMTCP_GLOBAL_BARRIER_RESTART, nameservice_register_data, "restart_nameservice_register_data"},
  {DMTCP_GLOBAL_BARRIER_RESTART, nameservice_send_queries, "restart_nameservice_send_queries"},
  {DMTCP_GLOBAL_BARRIER_RESTART, refill, "restart_refill"}
};

DmtcpPluginDescriptor_t infiniband_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "infiniband",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "InfiniBand plugin",
  DMTCP_DECL_BARRIERS(infinibandBarriers),
  NULL
};

DMTCP_DECL_PLUGIN(infiniband_plugin);

static void nameservice_register_data(void)
{
  send_qp_info();
  send_qp_pd_info();
  send_rkey_info();
}


static void nameservice_send_queries(void)
{
  query_qp_info();
  query_qp_pd_info();
  post_restart2();
}


/*! This will populate the coordinator with information about the new QPs */
static void send_qp_info(void)
{
  //TODO: BREAK REPLAYING OF MODIFY_QP LOG INTO TWO STAGES.
  //GO AHEAD AND MOVE QP INTO INIT STATE BEFORE DOING THIS.
  // IF A QP WAS NEVER MOVED INTO RTR THEN IT WON'T HAVE A CORRESPONDING QP
  struct list_elem *e;
  char hostname[128];

  gethostname(hostname,128);
  size_t count = 0;
  for (e = list_begin(&qp_list); e != list_end(&qp_list); e = list_next(e)) {
    struct internal_ibv_qp *internal_qp =
      list_entry(e, struct internal_ibv_qp, elem);
    if (internal_qp->user_qp.state != IBV_QPS_INIT) {
      count++;
    }
  }

  typedef struct {
    uint16_t key;
    uint16_t val;
  } LidInfoKVPair;

  typedef struct {
    ibv_qp_id_t key;
    ibv_qp_id_t val;
  } RCQPInfoKVPair;

  typedef struct {
    ibv_ud_qp_id_t key;
    ibv_ud_qp_id_t val;
  } UDQPInfoKVPair;

  LidInfoKVPair *lidInfoKVPairs =
    (LidInfoKVPair*) malloc(sizeof(LidInfoKVPair) * count);

  RCQPInfoKVPair *rcQPInfoKVPairs =
    (RCQPInfoKVPair*) malloc(sizeof(RCQPInfoKVPair) * count);

  UDQPInfoKVPair *udQPInfoKVPairs =
    (UDQPInfoKVPair*) malloc(sizeof(UDQPInfoKVPair) * count);

  size_t lidKVCount = 0;
  size_t rcQPInfoKVCount = 0;
  size_t udQPInfoKVCount = 0;
  /*
   * For RC QP, we need to send (qpn, lid, psn)
   * For UD QP, we need to send (qpn, lid)
   */
  for (e = list_begin(&qp_list); e != list_end(&qp_list); e = list_next(e)) {
    struct internal_ibv_qp * internal_qp =
      list_entry(e, struct internal_ibv_qp, elem);

    if (internal_qp->user_qp.state != IBV_QPS_INIT) {
      lidInfoKVPairs[lidKVCount].key = internal_qp->original_id.lid;
      lidInfoKVPairs[lidKVCount].val = internal_qp->current_id.lid;
      lidKVCount++;

      switch (internal_qp->user_qp.qp_type) {
        case IBV_QPT_RC:
          PDEBUG("RC QP: Sending over original_id: "
                 "0x%06x 0x%04x 0x%06x and current_id: "
                 "0x%06x 0x%04x 0x%06x from %s\n",
                 internal_qp->original_id.qpn, internal_qp->original_id.lid,
                 internal_qp->original_id.psn, internal_qp->current_id.qpn,
                 internal_qp->current_id.lid, internal_qp->current_id.psn,
                 hostname);

          rcQPInfoKVPairs[rcQPInfoKVCount].key = internal_qp->original_id;
          rcQPInfoKVPairs[rcQPInfoKVCount].key = internal_qp->current_id;
          rcQPInfoKVCount++;

          break;

        case IBV_QPT_UD:
          PDEBUG("UD QP: Sending over original_id: "
                 "0x%06x 0x%04x and current_id: "
                 "0x%06x 0x%04x from %s\n",
                 internal_qp->original_id.qpn, internal_qp->original_id.lid,
                 internal_qp->current_id.qpn, internal_qp->current_id.lid,
                 hostname);

          // Reuse original_id and current_id structure here, excluding psn
          udQPInfoKVPairs[udQPInfoKVCount].key.qpn =
            internal_qp->original_id.qpn;
          udQPInfoKVPairs[udQPInfoKVCount].key.lid =
            internal_qp->original_id.lid;

          udQPInfoKVPairs[udQPInfoKVCount].val.qpn =
            internal_qp->current_id.qpn;
          udQPInfoKVPairs[udQPInfoKVCount].val.lid =
            internal_qp->current_id.lid;
          udQPInfoKVCount++;

          break;

        default:
          fprintf(stderr, "Warning: unsupported qp type: %d\n",
                  internal_qp->user_qp.qp_type);
          exit(1);
      }
    }
  }
  dmtcp_send_key_val_pairs_to_coordinator("lidInfo",
                                          sizeof(lidInfoKVPairs->key),
                                          sizeof(lidInfoKVPairs->val),
                                          lidKVCount,
                                          lidInfoKVPairs);
  dmtcp_send_key_val_pairs_to_coordinator("rc_qp_info",
                                          sizeof(rcQPInfoKVPairs->key),
                                          sizeof(rcQPInfoKVPairs->val),
                                          rcQPInfoKVCount,
                                          rcQPInfoKVPairs);
  dmtcp_send_key_val_pairs_to_coordinator("ud_qp_info",
                                          sizeof(udQPInfoKVPairs->key),
                                          sizeof(udQPInfoKVPairs->val),
                                          udQPInfoKVCount,
                                          udQPInfoKVPairs);
  free(lidInfoKVPairs);
  free(rcQPInfoKVPairs);
  free(udQPInfoKVPairs);
}

/*! This will query the coordinator for information about the new QPs */
static void query_qp_info(void)
{
  char hostname[128];
  struct list_elem *e;

  gethostname(hostname, 128);

  for (e = list_begin(&qp_list); e != list_end(&qp_list); e = list_next(e)) {
    struct internal_ibv_qp * internal_qp;

    internal_qp = list_entry(e, struct internal_ibv_qp, elem);
    if (internal_qp->user_qp.qp_type == IBV_QPT_RC) {
      uint32_t size = sizeof(internal_qp->current_remote);

      PDEBUG("Querying for remote_id: 0x%06x 0x%04x 0x%06x from %s\n",
             internal_qp->remote_id.qpn, internal_qp->remote_id.lid,
             internal_qp->remote_id.psn, hostname);

      dmtcp_send_query_to_coordinator("qp_info",
                                      &internal_qp->remote_id,
                                      sizeof(internal_qp->remote_id),
                                      &internal_qp->current_remote,
          			      &size);

      assert(size == sizeof(ibv_qp_id_t));
    }
  }
}

static void send_qp_pd_info(void) {
  struct list_elem *e;

  typedef struct {
    ibv_qp_pd_id_t key;
    uint32_t       val;
  } PDInfoKVPair;

  PDInfoKVPair *pairs =
    (PDInfoKVPair*) malloc(list_size(&qp_list) * sizeof(PDInfoKVPair));

  size_t count = 0;
  for (e = list_begin(&qp_list); e != list_end(&qp_list); e = list_next(e)) {
    struct internal_ibv_qp * internal_qp;
    struct internal_ibv_pd * internal_pd;

    internal_qp = list_entry(e, struct internal_ibv_qp, elem);
    internal_pd = ibv_pd_to_internal(internal_qp->user_qp.pd);

    assert(sizeof(internal_qp->local_qp_pd_id) == sizeof(pairs[count].key));
    pairs[count].key = internal_qp->local_qp_pd_id;
    pairs[count].val = internal_pd->pd_id;
    count++;
  }

  dmtcp_send_key_val_pairs_to_coordinator("pd_info",
                                          sizeof(pairs->key),
                                          sizeof(pairs->val),
                                          count,
                                          pairs);
  free(pairs);
}

static void query_qp_pd_info(void) {
  struct list_elem *e;
  uint32_t size;
  int ret;

  for (e = list_begin(&qp_list); e != list_end(&qp_list); e = list_next(e)) {
    struct internal_ibv_qp * internal_qp;

    internal_qp = list_entry(e, struct internal_ibv_qp, elem);
    if (internal_qp->user_qp.qp_type == IBV_QPT_RC) {
      size = sizeof(internal_qp->remote_pd_id);
      ret = dmtcp_send_query_to_coordinator("pd_info",
                                            &internal_qp->remote_qp_pd_id,
                                            sizeof(ibv_qp_pd_id_t),
                                            &internal_qp->remote_pd_id,
          			            &size);
      assert(size == sizeof(int));
      assert(ret != 0);
    }
  }
}

/*! This will populate the coordinator with information about the new rkeys */
static void send_rkey_info(void)
{
  typedef struct {
    struct ibv_rkey_id key;
    uint32_t           val;
  } MRInfoKVPair;

  MRInfoKVPair *pairs =
    (MRInfoKVPair*) malloc(list_size(&mr_list) * sizeof(MRInfoKVPair));

  size_t count = 0;
  struct list_elem *e;
  for (e = list_begin(&mr_list); e != list_end(&mr_list); e = list_next(e)) {
      struct internal_ibv_mr * internal_mr;
      struct internal_ibv_pd * internal_pd;

      internal_mr = list_entry(e, struct internal_ibv_mr, elem);
      internal_pd = ibv_pd_to_internal(internal_mr->user_mr.pd);

      pairs[count].key.pd_id = internal_pd->pd_id;
      pairs[count].key.rkey = internal_mr->user_mr.rkey;
      pairs[count].val = internal_mr->real_mr->rkey,
      count++;
  }

  dmtcp_send_key_val_pairs_to_coordinator("mr_info",
                                          sizeof(pairs->key),
                                          sizeof(pairs->val),
                                          count,
                                          pairs);
  free(pairs);
}

/*! This will drain the given completion queue
 */
static void drain_completion_queue(struct internal_ibv_cq * internal_cq)
{
  int ne;
  /* This loop will drain the completion queue and buffer the entries */
  do {
    struct ibv_wc_wrapper * wc = malloc(sizeof(struct ibv_wc_wrapper));
    if (wc == NULL) {
      fprintf(stderr, "Unable to allocate memory for wc\n");
      exit(1);
    }

    ne = _real_ibv_poll_cq(internal_cq->real_cq, 1, &wc->wc);

    if (ne > 0) {
      struct internal_ibv_qp * internal_qp;
      enum ibv_wc_opcode opcode;

      PDEBUG("Found a completion on the queue.\n");
      list_push_back(&internal_cq->wc_queue, &wc->elem);
      internal_qp = qp_num_to_qp(&qp_list, wc->wc.qp_num);
      wc->wc.qp_num = internal_qp->user_qp.qp_num;
      opcode = wc->wc.opcode;

      if (opcode & IBV_WC_RECV ||
          opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
	if (internal_qp->user_qp.srq) {
	  struct internal_ibv_srq * internal_srq;

	  internal_srq = ibv_srq_to_internal(internal_qp->user_qp.srq);
	  internal_srq->recv_count++;
	}
	else {
          struct list_elem * e;
          struct ibv_post_recv_log * log;

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
          struct list_elem * e;
          struct ibv_post_send_log * log;

          e = list_pop_front(&internal_qp->post_send_log);
          log = list_entry(e, struct ibv_post_send_log, elem);
          assert(log->magic == SEND_MAGIC);
          free(log);
	}
	else {
	  while(1) {
            struct list_elem * e;
            struct ibv_post_send_log * log;

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
        fprintf(stderr,
                "Error: opcode %d specifies unsupported operation.\n", opcode);
        exit(1);
      } else {
        fprintf(stderr, "Unknown or invalid opcode.\n");
        exit(1);
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
void pre_checkpoint(void)
{
  struct list_elem * e;
  struct list_elem * w;

  PDEBUG("I made it into pre_checkpoint.\n");

  for (e = list_begin(&cq_list); e != list_end(&cq_list); e = list_next(e)) {
    struct internal_ibv_cq * internal_cq;

    internal_cq = list_entry(e, struct internal_ibv_cq, elem);
    drain_completion_queue(internal_cq);
  }

  for (e = list_begin(&qp_list); e != list_end(&qp_list); e = list_next(e)) {
    struct internal_ibv_qp * internal_qp;

    internal_qp = list_entry(e, struct internal_ibv_qp, elem);

    if (internal_qp->init_attr.sq_sig_all == 0) {
      if (list_size(&internal_qp->post_send_log) > 0) {
        w = list_begin(&internal_qp->post_send_log);
        while (w != list_end(&internal_qp->post_send_log)) {
          struct list_elem *t = w;
          struct ibv_post_send_log *log = list_entry(t, struct ibv_post_send_log, elem);
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
  PDEBUG("Made it out of pre_checkpoint\n");
}


// TODO: Must handle case of modifying after checkpoint
void post_restart(void)
{
  list_init(&rkey_list);
  is_restart = true;
  if (is_fork) {
    if (NEXT_IBV_FNC(ibv_fork_init)()) {
      fprintf(stderr, "ibv_fork_init fails.\n");
      exit(1);
    }
  }
  /* code to re-open device */
  int num = 0;
  struct ibv_device ** real_dev_list;

  // This is useful when IB is not used while the plugin is enabled.
  if (dlvsym(RTLD_NEXT, "ibv_get_device_list", "IBVERBS_1.1") != NULL) {

    real_dev_list = NEXT_IBV_FNC(ibv_get_device_list)(&num);
    if (!num)
    {
      fprintf(stderr, "Error: ibv_get_device_list returned 0 devices.\n");
      exit(1);
    }
  }

  struct list_elem *e;
  for (e = list_begin(&ctx_list); e != list_end(&ctx_list); e = list_next(e))
  {
    struct internal_ibv_ctx * internal_ctx;
    int i;

    internal_ctx = list_entry(e, struct internal_ibv_ctx, elem);

    for (i = 0; i < num; i++) {
      if (!strncmp(internal_ctx->user_ctx.device->dev_name,
                   real_dev_list[i]->dev_name,
                   strlen(real_dev_list[i]->dev_name))) {

          internal_ctx->real_ctx = NEXT_IBV_FNC(ibv_open_device)(real_dev_list[i]);

          if (!internal_ctx->real_ctx)
          {
            fprintf(stderr, "Error: Could not re-open the context.\n");
            exit(1);
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
                fprintf(stderr, "Error: Could not duplicate async_fd.\n");
	        exit(1);
              }
	      close(internal_ctx->real_ctx->async_fd);
	      internal_ctx->real_ctx->async_fd = internal_ctx->user_ctx.async_fd;
	    }
	    if (internal_ctx->real_ctx->cmd_fd !=
                internal_ctx->user_ctx.cmd_fd) {
              if (dup2(internal_ctx->real_ctx->cmd_fd,
                       internal_ctx->user_ctx.cmd_fd) !=
                  internal_ctx->user_ctx.cmd_fd) {
                fprintf(stderr, "Error: Could not duplicate cmd_fd.\n");
	        exit(1);
              }
	      close(internal_ctx->real_ctx->cmd_fd);
	      internal_ctx->real_ctx->cmd_fd = internal_ctx->user_ctx.cmd_fd;
	    }
	  }
	  else {
	    if (internal_ctx->real_ctx->cmd_fd !=
                internal_ctx->user_ctx.cmd_fd) {
              if (dup2(internal_ctx->real_ctx->cmd_fd,
                       internal_ctx->user_ctx.cmd_fd) !=
                  internal_ctx->user_ctx.cmd_fd) {
                fprintf(stderr, "Error: Could not duplicate cmd_fd.\n");
	        exit(1);
              }
	      close(internal_ctx->real_ctx->cmd_fd);
	      internal_ctx->real_ctx->cmd_fd = internal_ctx->user_ctx.cmd_fd;
	    }
	    if (internal_ctx->real_ctx->async_fd !=
                internal_ctx->user_ctx.async_fd) {
              if (dup2(internal_ctx->real_ctx->async_fd,
                       internal_ctx->user_ctx.async_fd) !=
                  internal_ctx->user_ctx.async_fd) {
                fprintf(stderr, "Error: Could not duplicate async_fd.\n");
	        exit(1);
              }
	      close(internal_ctx->real_ctx->async_fd);
	      internal_ctx->real_ctx->async_fd = internal_ctx->user_ctx.async_fd;
	    }
	  }

          break;
      }
    }
  }
  NEXT_IBV_FNC(ibv_free_device_list)(real_dev_list);
  /* end code to re-open device */

  /* code to alloc the protection domain */
  for (e = list_begin(&pd_list); e != list_end(&pd_list); e = list_next(e))
  {
    struct internal_ibv_pd * internal_pd;
    struct internal_ibv_ctx * internal_ctx;

    internal_pd = list_entry(e, struct internal_ibv_pd, elem);
    internal_ctx = ibv_ctx_to_internal(internal_pd->user_pd.context);

    internal_pd->real_pd = NEXT_IBV_FNC(ibv_alloc_pd)(internal_ctx->real_ctx);

    if (!internal_pd->real_pd)
    {
      fprintf(stderr, "Error: Could not re-alloc the pd.\n");
      fprintf(stderr, "Error number is %d.\n", errno);
      exit(1);
    }

  }
  /*end code to alloc the protection domain */

  /* code to register the memory region */
  for (e = list_begin(&mr_list); e != list_end(&mr_list); e = list_next(e))
  {
    struct internal_ibv_mr * internal_mr;
    struct internal_ibv_pd * internal_pd;

    internal_mr = list_entry(e, struct internal_ibv_mr, elem);
    internal_pd = ibv_pd_to_internal(internal_mr->user_mr.pd);

    internal_mr->real_mr = NEXT_IBV_FNC(ibv_reg_mr)(internal_pd->real_pd,
                                                    internal_mr->user_mr.addr,
                                                    internal_mr->user_mr.length,
                                                    internal_mr->flags);

    if (!internal_mr->real_mr)
    {
      fprintf(stderr, "Error: Could not re-reg the mr.\n");
      exit(1);
    }
  }
  /* end code to register the memory region */

  /* code to register the completion channel */
  for (e = list_begin(&comp_list); e != list_end(&comp_list); e = list_next(e))
  {
    struct internal_ibv_comp_channel * internal_comp;
    struct internal_ibv_ctx * internal_ctx;

    PDEBUG("About to deal with completion channels\n");
    internal_comp = list_entry(e, struct internal_ibv_comp_channel, elem);
    internal_ctx = ibv_ctx_to_internal(internal_comp->user_channel.context);

    internal_comp->real_channel = NEXT_IBV_COMP_CHANNEL(ibv_create_comp_channel)
                                    (internal_ctx->real_ctx);

    if (!internal_comp->real_channel)
    {
      fprintf(stderr, "Error: Could not re-create the comp channel.\n");
      exit(1);
    }

    if (internal_comp->real_channel->fd != internal_comp->user_channel.fd)
    {
      if(dup2(internal_comp->real_channel->fd,
              internal_comp->user_channel.fd) == -1) {
        fprintf(stderr, "Error: could not duplicate the file descriptor.\n");
        exit(1);
      }
      close(internal_comp->real_channel->fd);
      internal_comp->real_channel->fd = internal_comp->user_channel.fd;
    }
  }
  /* end code to register the completion channel */

  /* code to register the completion queue */
  for (e = list_begin(&cq_list); e != list_end(&cq_list); e = list_next(e))
  {
    struct internal_ibv_cq * internal_cq;
    struct internal_ibv_ctx * internal_ctx;

    internal_cq = list_entry(e, struct internal_ibv_cq, elem);
    internal_ctx = ibv_ctx_to_internal(internal_cq->user_cq.context);

    internal_cq->real_cq = NEXT_IBV_FNC(ibv_create_cq)
                                         (internal_ctx->real_ctx,
                                          internal_cq->user_cq.cqe,
                                          internal_cq->user_cq.cq_context, NULL,
                                          internal_cq->comp_vector);

    if (!internal_cq->real_cq)
    {
      fprintf(stderr, "Error: could not recreate the cq.\n");
      exit(1);
    }

    struct list_elem *w;
    for (w = list_begin(&internal_cq->req_notify_log);
         w != list_end(&internal_cq->req_notify_log);
         w = list_next(w))
    {
      struct ibv_req_notify_cq_log * log;

      log = list_entry(w, struct ibv_req_notify_cq_log, elem);
      if (_real_ibv_req_notify_cq(internal_cq->real_cq, log->solicited_only))
      {
        fprintf(stderr, "Error: Could not repost req_notify_cq\n");
        exit(1);
      }
    }
  }

  /* end code to register completion queue */


  /* code to re-create the shared receive queue */
  for (e = list_begin(&srq_list); e != list_end(&srq_list); e = list_next(e))
  {
    struct internal_ibv_srq * internal_srq;
    struct ibv_srq_init_attr new_attr;

    internal_srq = list_entry(e, struct internal_ibv_srq, elem);
    new_attr = internal_srq->init_attr;

    internal_srq->real_srq =
    NEXT_IBV_FNC(ibv_create_srq)
                  (ibv_pd_to_internal(internal_srq->user_srq.pd)->real_pd,
                   &new_attr);
    if (!internal_srq->real_srq)
    {
      fprintf(stderr, "Error: Could not recreate the srq.\n");
      exit(1);
    }
  }
  /* end code to re-create the shared receive queue */

  /* code to re-create the queue pairs */
  for (e = list_begin(&qp_list); e != list_end(&qp_list); e = list_next(e))
  {
    struct internal_ibv_qp * internal_qp;
    struct ibv_qp_init_attr new_attr;
    uint32_t psn;

    internal_qp = list_entry(e, struct internal_ibv_qp, elem);
    new_attr = internal_qp->init_attr;
    new_attr.recv_cq = ibv_cq_to_internal(internal_qp->user_qp.recv_cq)->real_cq;
    new_attr.send_cq = ibv_cq_to_internal(internal_qp->user_qp.send_cq)->real_cq;

    if(new_attr.srq) {
      new_attr.srq = ibv_srq_to_internal(internal_qp->user_qp.srq)->real_srq;
    }

    internal_qp->real_qp =
    NEXT_IBV_FNC(ibv_create_qp)
                  (ibv_pd_to_internal(internal_qp->user_qp.pd)->real_pd,
                   &new_attr);

    if (!internal_qp->real_qp)
    {
      fprintf(stderr, "Error: Could not recreate the qp.\n");
      exit(1);
    }

    // SETTING PORT NUM TO 1 IN THIS CALL IS POTENTIALLY UNSAFE
    // THIS CAN BE FIXED IN THE CALL TO IBV_MODIFY_QP
    /* get the LID */
    struct ibv_port_attr attr2;
    if (NEXT_IBV_FNC(ibv_query_port)
         (internal_qp->real_qp->context, internal_qp->port_num, &attr2))
    {
      fprintf(stderr, "Call to ibv_query_port failed.\n");
      exit(1);
    }

    /* generate the new PSN */
    srand48(getpid() * time(NULL));
    psn = lrand48() & 0xffffff;

    internal_qp->current_id.qpn = internal_qp->real_qp->qp_num;
    internal_qp->current_id.lid = attr2.lid;

    /* must handle LMC*/
    struct list_elem * w;
    for (w = list_begin(&internal_qp->modify_qp_log);
         w != list_end(&internal_qp->modify_qp_log);
         w = list_next(w)) {
      struct ibv_modify_qp_log * mod;

      mod = list_entry(w, struct ibv_modify_qp_log, elem);
      if (mod->attr_mask & IBV_QP_AV) {
        internal_qp->current_id.lid |= mod->attr.ah_attr.src_path_bits;
      }
    }
    internal_qp->current_id.psn = psn;
    /* end code to populate the ID */
  }
  /* end code to re-create the queue pairs */
}

void post_restart2(void)
{
  struct list_elem * e;

  // Recreate the Address Handlers
  for (e = list_begin(&ah_list); e != list_end(&ah_list); e = list_next(e)) {
    uint32_t size;
    struct ibv_ah_attr real_attr;
    struct internal_ibv_ah *internal_ah;

    internal_ah = list_entry(e, struct internal_ibv_ah, elem);
    real_attr = internal_ah->attr;

    dmtcp_send_query_to_coordinator("lidInfo",
                                    &internal_ah->attr.dlid,
                                    sizeof(internal_ah->attr.dlid),
                                    &real_attr.dlid,
                                    &size);

    assert(size == sizeof(internal_ah->attr.dlid));

    internal_ah->real_ah =
      NEXT_IBV_FNC(ibv_create_ah)
                  (ibv_pd_to_internal(internal_ah->user_ah.pd)->real_pd,
                  &real_attr);
    if (internal_ah->real_ah == NULL) {
      fprintf(stderr, "Fail to recreate the ah.\n");
      exit(1);
    }
  }

  for (e = list_begin(&srq_list); e != list_end(&srq_list); e = list_next(e))
  {
    struct internal_ibv_srq * internal_srq;
    struct list_elem * w;
    uint32_t i;

    internal_srq = list_entry(e, struct internal_ibv_srq, elem);
    for (i = 0; i < internal_srq->recv_count; i++) {
      w = list_pop_front(&internal_srq->post_srq_recv_log);
      struct ibv_post_srq_recv_log * log;

      log = list_entry(w, struct ibv_post_srq_recv_log, elem);
      free(log);
    }

    internal_srq->recv_count = 0;

    for (w = list_begin(&internal_srq->post_srq_recv_log);
         w != list_end(&internal_srq->post_srq_recv_log);
         w = list_next(w)) {
      struct ibv_post_srq_recv_log * log;
      struct ibv_recv_wr * copy_wr;
      struct ibv_recv_wr * bad_wr;

      log = list_entry(w, struct ibv_post_srq_recv_log, elem);
      copy_wr = copy_recv_wr(&log->wr);
      update_lkey_recv(copy_wr);

      int rslt = _real_ibv_post_srq_recv(internal_srq->real_srq,
                                         copy_wr, &bad_wr);
      if (rslt) {
        fprintf(stderr, "Call to ibv_post_srq_recv failed.\n");
        fprintf(stderr, "error number is %d\n", errno);
	exit(1);
      }
      delete_recv_wr(copy_wr);
    }

    for (w = list_rbegin(&internal_srq->modify_srq_log);
	 w != list_rend(&internal_srq->modify_srq_log);
	 w = list_prev(w))
    {
      struct ibv_modify_srq_log * mod;
      struct ibv_srq_attr attr;

      mod = list_entry(w, struct ibv_modify_srq_log, elem);
      attr = mod->attr;

      if (mod->attr_mask & IBV_SRQ_MAX_WR){
	if(NEXT_IBV_FNC(ibv_modify_srq)
            (internal_srq->real_srq, &attr, IBV_SRQ_MAX_WR)){
	  fprintf(stderr, "Error: Cound not modify srq properly.\n");
//	  exit(1);
	}
	break;
      }
    }
  }

  for (e = list_begin(&qp_list); e != list_end(&qp_list); e = list_next(e)) {
    struct internal_ibv_qp * internal_qp;
    struct list_elem * w;

    internal_qp = list_entry(e, struct internal_ibv_qp, elem);
    for (w = list_begin(&internal_qp->modify_qp_log);
         w != list_end(&internal_qp->modify_qp_log);
         w = list_next(w)) {
      struct ibv_modify_qp_log * mod;
      struct ibv_qp_attr attr;

      mod = list_entry(w, struct ibv_modify_qp_log, elem);
      attr = mod->attr;

      if (mod->attr_mask & IBV_QP_DEST_QPN) {
        attr.dest_qp_num = internal_qp->current_remote.qpn;
      }
      if (mod->attr_mask & IBV_QP_SQ_PSN) {
        attr.sq_psn = internal_qp->current_id.psn;
      }
      if (mod->attr_mask & IBV_QP_RQ_PSN) {
        attr.rq_psn = internal_qp->current_remote.psn;
      }
      if (mod->attr_mask & IBV_QP_AV) {
        attr.ah_attr.dlid = internal_qp->current_remote.lid;
      }

      if (NEXT_IBV_FNC(ibv_modify_qp)
          (internal_qp->real_qp, &attr, mod->attr_mask))
      {
	fprintf(stderr, "%d %d %d %d\n",
                attr.qp_state, attr.qp_access_flags,
                attr.pkey_index, attr.port_num);
	if (attr.qp_access_flags & IBV_ACCESS_REMOTE_WRITE)
	  fprintf(stderr, "IBV_ACCESS_REMOTE_WRITE\n");
	if (attr.qp_access_flags & IBV_ACCESS_REMOTE_READ)
	  fprintf(stderr, "IBV_ACCESS_REMOTE_READ\n");
	if (attr.qp_access_flags & IBV_ACCESS_REMOTE_ATOMIC)
	  fprintf(stderr, "IBV_ACCESS_REMOTE_ATOMIC\n");
	fprintf(stderr, "errno is %d\n", errno);
        fprintf(stderr, "Error: Could not modify qp properly.\n");
        exit(1);
      }
    }
  }

  for (e = list_begin(&qp_list); e != list_end(&qp_list); e = list_next(e))
  {
    struct internal_ibv_qp * internal_qp;
    struct list_elem * w;

    internal_qp = list_entry(e, struct internal_ibv_qp, elem);
    for (w = list_begin(&internal_qp->post_recv_log);
         w != list_end(&internal_qp->post_recv_log);
         w = list_next(w)) {
      struct ibv_post_recv_log * log;
      struct ibv_recv_wr * copy_wr;
      struct ibv_recv_wr * bad_wr;

      log = list_entry(w, struct ibv_post_recv_log, elem);
      copy_wr = copy_recv_wr(&log->wr);
      update_lkey_recv(copy_wr);
      assert(copy_wr->next == NULL);

      PDEBUG("About to repost recv.\n");
      int rslt = _real_ibv_post_recv(internal_qp->real_qp, copy_wr, &bad_wr);
      if (rslt) {
        fprintf(stderr, "Repost recv failed.\n");
	exit(1);
      }
      delete_recv_wr(copy_wr);
    }
  }
}

void refill(void)
{
  struct list_elem * e;
  for (e = list_begin(&qp_list); e != list_end(&qp_list); e = list_next(e)) {
    struct internal_ibv_qp * internal_qp;
    struct list_elem * w;

    internal_qp = list_entry(e, struct internal_ibv_qp, elem);
    for (w = list_begin(&internal_qp->post_send_log);
         w != list_end(&internal_qp->post_send_log);
         w = list_next(w)) {
      struct ibv_post_send_log * log;
      struct ibv_send_wr * copy_wr;
      struct ibv_send_wr * bad_wr;

      log = list_entry(w, struct ibv_post_send_log, elem);
      PDEBUG("log->magic : %x\n", log->magic);
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
          fprintf(stderr, "Warning: unsupported qp type: %d\n",
                  internal_qp->user_qp.qp_type);
          exit(1);
      }

      PDEBUG("About to repost send.\n");
      int rslt = _real_ibv_post_send(internal_qp->real_qp, copy_wr, &bad_wr);
      if (rslt) {
        fprintf(stderr, "Repost recv failed.\n");
	exit(1);
      }
      delete_send_wr(copy_wr);
    }
  }
}

int _fork_init() {
  is_fork = true;
  return NEXT_IBV_FNC(ibv_fork_init)();
}

//! This performs the work of the _get_device_list_wrapper
/*!
  This function will open the real device list, store into real_dev_list
  and then copy the list, returning an image of the copy to the user
  */
struct ibv_device ** _get_device_list(int * num_devices) {
  struct ibv_device ** real_dev_list = NULL;
  int real_num_devices;
  struct dev_list_info * list_info;
  int i;
  struct ibv_device ** user_list =  NULL;

  real_dev_list = NEXT_IBV_FNC(ibv_get_device_list)(&real_num_devices);

  if (num_devices) {
    *num_devices = real_num_devices;
  }

  if (!real_dev_list) {
    return NULL;
  }

  user_list = calloc(real_num_devices + 1, sizeof(struct ibv_device *));
  list_info = (struct dev_list_info *)malloc(sizeof(struct dev_list_info));

  if (!user_list || !list_info) {
    fprintf(stderr, "Error: Could not allocate memory for _get_device_list.\n");
    exit(1);
  }

  memset(user_list, 0, (real_num_devices + 1) * sizeof(struct ibv_device *));
  memset(list_info, 0, sizeof(struct dev_list_info));

  list_info->num_devices = real_num_devices;
  list_info->user_dev_list = user_list;
  list_info->real_dev_list = real_dev_list;

  for (i = 0; i < real_num_devices; i++) {
    struct internal_ibv_dev * dev;

    dev = (struct internal_ibv_dev *) malloc(sizeof(struct internal_ibv_dev));

    if (!dev) {
      fprintf(stderr,
              "Error: Could not allocate memory for _get_device_list.\n");
      exit(1);
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

const char * _get_device_name(struct ibv_device * device)
{
  if (!IS_INTERNAL_IBV_STRUCT(ibv_device_to_internal(device))) {
    return NEXT_IBV_FNC(ibv_get_device_name)(device);
  }
  return NEXT_IBV_FNC(ibv_get_device_name)
                     (ibv_device_to_internal(device)->real_dev);
}

//TODO: I think the GUID could change and need to be translated
uint64_t _get_device_guid(struct ibv_device * dev)
{
  struct internal_ibv_dev * internal_dev = ibv_device_to_internal(dev);

  if (!IS_INTERNAL_IBV_STRUCT(internal_dev)) {
    return NEXT_IBV_FNC(ibv_get_device_guid)(dev);
  }

  return NEXT_IBV_FNC(ibv_get_device_guid)(internal_dev->real_dev);
}

struct ibv_comp_channel * _create_comp_channel(struct ibv_context * ctx) {
  struct internal_ibv_ctx * internal_ctx;
  struct internal_ibv_comp_channel * internal_comp;

  internal_ctx = ibv_ctx_to_internal(ctx);
  assert(IS_INTERNAL_IBV_STRUCT(internal_ctx));

  internal_comp = malloc(sizeof(struct internal_ibv_comp_channel));

  if (!internal_comp) {
    fprintf(stderr, "Error: Could not alloc memory for comp channel\n");
    exit(1);
  }

  internal_comp->real_channel =
  NEXT_IBV_COMP_CHANNEL(ibv_create_comp_channel)(internal_ctx->real_ctx);

  if (!internal_comp->real_channel) {
    fprintf(stderr, "Channel could not be created.");
    exit(1);
  }

  INIT_INTERNAL_IBV_TYPE(internal_comp);
  memcpy(&internal_comp->user_channel,
         internal_comp->real_channel,
         sizeof(struct ibv_comp_channel));
  internal_comp->user_channel.context = ctx;
  list_push_back(&comp_list, &internal_comp->elem);

  return &internal_comp->user_channel;
}

int _destroy_comp_channel(struct ibv_comp_channel * channel)
{
  struct internal_ibv_comp_channel * internal_comp;
  int rslt;

  internal_comp = ibv_comp_to_internal(channel);
  assert(IS_INTERNAL_IBV_STRUCT(internal_comp));

  rslt = NEXT_IBV_COMP_CHANNEL(ibv_destroy_comp_channel)
                                (internal_comp->real_channel);
  list_remove(&internal_comp->elem);
  free(internal_comp);

  return rslt;
}

int _get_cq_event(struct ibv_comp_channel * channel,
                  struct ibv_cq ** cq, void ** cq_context)
{
  struct internal_ibv_comp_channel * internal_channel;
  struct internal_ibv_cq * internal_cq;
  int rslt;

  internal_channel = ibv_comp_to_internal(channel);
  if (!IS_INTERNAL_IBV_STRUCT(internal_channel)) {
    rslt = NEXT_IBV_FNC(ibv_get_cq_event)(channel, cq, cq_context);
  }
  else {
    rslt = NEXT_IBV_FNC(ibv_get_cq_event)
                       (internal_channel->real_channel,
                        cq, cq_context);
  }

  internal_cq = get_cq_from_pointer(*cq);
  assert(internal_cq != NULL);
  *cq = &internal_cq->user_cq;
  *cq_context = internal_cq->user_cq.context;

  return rslt;
}

int _get_async_event(struct ibv_context * ctx, struct ibv_async_event * event)
{
  struct internal_ibv_ctx * internal_ctx = ibv_ctx_to_internal(ctx);
  struct internal_ibv_qp * internal_qp;
  struct internal_ibv_cq * internal_cq;
  struct internal_ibv_srq * internal_srq;
  int rslt;

  if (!IS_INTERNAL_IBV_STRUCT(internal_ctx)) {
    rslt = NEXT_IBV_FNC(ibv_get_async_event)(ctx, event);
  }
  else {
    rslt = NEXT_IBV_FNC(ibv_get_async_event)(internal_ctx->real_ctx, event);
  }

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
      fprintf(stderr, "Warning: Could not identify the ibv_event_type\n");
      break;
    }

  return rslt;
}

void _ack_async_event(struct ibv_async_event * event)
{
  struct internal_ibv_qp * internal_qp;
  struct internal_ibv_cq * internal_cq;
  struct internal_ibv_srq * internal_srq;

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
      fprintf(stderr, "Warning: Could not identify the ibv_event_type\n");
      break;
  }

  NEXT_IBV_FNC(ibv_ack_async_event)(event);
}

//! This performs the work of freeing the device list
/*! This function will free the real device list and then
 * delete the members of the device list copy
 */
void _free_device_list(struct ibv_device ** dev_list)
{
  struct ibv_device ** real_dev_list;
  int i;
  bool all_device_free = true;
  struct dev_list_info * list_info;

  if (!dev_list) return;

  list_info = ibv_device_to_internal(dev_list[0])->list_info;
  real_dev_list = list_info->real_dev_list;

  NEXT_IBV_FNC(ibv_free_device_list)(real_dev_list);

  for (i = 0; i < list_info->num_devices; i++) {
    struct internal_ibv_dev * dev = ibv_device_to_internal(dev_list[i]);
    if (dev->in_use) {
      all_device_free = false;
      break;
    }
  }

  if (all_device_free) {
    for (i = 0; i < list_info->num_devices; i++) {
      struct internal_ibv_dev * dev = ibv_device_to_internal(dev_list[i]);
      free(dev);
    }
    free(dev_list);
    free(list_info);
  }
  else {
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
struct ibv_context * _open_device(struct ibv_device * device) {
  struct internal_ibv_dev * dev = ibv_device_to_internal(device);
  struct internal_ibv_ctx * ctx;

  assert(IS_INTERNAL_IBV_STRUCT(dev));

  ctx = malloc(sizeof(struct internal_ibv_ctx));
  if (!ctx) {
    fprintf(stderr, "Couldn't allocate memory for _open_device!\n");
    exit(1);
  }

  ctx->real_ctx = NEXT_IBV_FNC(ibv_open_device)(dev->real_dev);

  if (ctx->real_ctx == NULL) {
    fprintf(stderr, "Could not allocate the real ctx.\n");
    exit(1);
  }

  INIT_INTERNAL_IBV_TYPE(ctx);
  dev->in_use = true;

  /* setup the trampolines */
  UPDATE_FUNC_ADDR(post_recv, ctx->real_ctx->ops.post_recv);
  UPDATE_FUNC_ADDR(post_srq_recv, ctx->real_ctx->ops.post_srq_recv);
  UPDATE_FUNC_ADDR(post_send, ctx->real_ctx->ops.post_send);
  UPDATE_FUNC_ADDR(poll_cq, ctx->real_ctx->ops.poll_cq);
  UPDATE_FUNC_ADDR(req_notify_cq, ctx->real_ctx->ops.req_notify_cq);

  memcpy(&ctx->user_ctx, ctx->real_ctx, sizeof(struct ibv_context));

  ctx->user_ctx.device = device;

  list_push_back(&ctx_list, &ctx->elem);

  return &ctx->user_ctx;
}

int _query_device(struct ibv_context *context,
                  struct ibv_device_attr *device_attr)
{
  struct internal_ibv_ctx *internal_ctx = ibv_ctx_to_internal(context);

  if (!IS_INTERNAL_IBV_STRUCT(internal_ctx)) {
    return NEXT_IBV_FNC(ibv_query_device)(context, device_attr);
  }

  return NEXT_IBV_FNC(ibv_query_device)(internal_ctx->real_ctx,device_attr);
}


int _query_port(struct ibv_context *context, uint8_t port_num,
                struct ibv_port_attr *port_attr)
{
  struct internal_ibv_ctx *internal_ctx = ibv_ctx_to_internal(context);

  if (!IS_INTERNAL_IBV_STRUCT(internal_ctx)) {
    return NEXT_IBV_FNC(ibv_query_port)(context, port_num, port_attr);
  }

  return NEXT_IBV_FNC(ibv_query_port)(internal_ctx->real_ctx,
                                      port_num, port_attr);
}

int _query_pkey(struct ibv_context *context, uint8_t port_num,
                int index, uint16_t *pkey)
{
  struct internal_ibv_ctx *internal_ctx = ibv_ctx_to_internal(context);

  if (!IS_INTERNAL_IBV_STRUCT(internal_ctx)) {
    return NEXT_IBV_FNC(ibv_query_pkey)(context, port_num, index, pkey);
  }

  return NEXT_IBV_FNC(ibv_query_pkey)(internal_ctx->real_ctx,
                                      port_num, index, pkey);
}

int _query_gid(struct ibv_context *context, uint8_t port_num,
               int index, union ibv_gid *gid)
{
  struct internal_ibv_ctx *internal_ctx = ibv_ctx_to_internal(context);

  if (!IS_INTERNAL_IBV_STRUCT(internal_ctx)) {
    return NEXT_IBV_FNC(ibv_query_gid)(context, port_num, index, gid);
  }

  return NEXT_IBV_FNC(ibv_query_gid)(internal_ctx->real_ctx,
                                     port_num, index, gid);
}

int _close_device(struct ibv_context * ctx)
{
  struct internal_ibv_ctx * internal_ctx = ibv_ctx_to_internal(ctx);
  struct internal_ibv_dev * dev;
  struct dev_list_info * list_info;

  dev = ibv_device_to_internal(internal_ctx->user_ctx.device);
  assert(IS_INTERNAL_IBV_STRUCT(dev));
  dev->in_use = false;
  list_info = dev->list_info;

  int rslt = NEXT_IBV_FNC(ibv_close_device)(internal_ctx->real_ctx);

  list_remove(&internal_ctx->elem);
  free(internal_ctx);

  if (list_info->in_free) {
    int i;
    struct ibv_device ** user_list;
    bool all_device_free = true;
    struct internal_ibv_dev * dev_elem;

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

struct ibv_pd * _alloc_pd(struct ibv_context * context) {
  struct internal_ibv_ctx * internal_ctx = ibv_ctx_to_internal(context);
  struct internal_ibv_pd * pd;

  assert(IS_INTERNAL_IBV_STRUCT(internal_ctx));
  pd = malloc(sizeof(struct internal_ibv_pd));

  if (!pd) {
    fprintf(stderr, "Error: I cannot allocate memory for _alloc_pd\n");
    exit(1);
  }

  pd->real_pd = NEXT_IBV_FNC(ibv_alloc_pd)(internal_ctx->real_ctx);

  if (!pd->real_pd) {
    return pd->real_pd;
  }

  INIT_INTERNAL_IBV_TYPE(pd);
  memcpy(&pd->user_pd, pd->real_pd, sizeof(struct ibv_pd));
  pd->user_pd.context = context;
  pd->pd_id = (int)((dmtcp_get_uniquepid())._pid) + pd_id_count;
  pd_id_count++;

  list_push_back(&pd_list, &pd->elem);

  return &pd->user_pd;
}

int _dealloc_pd(struct ibv_pd * pd)
{
  struct internal_ibv_pd * internal_pd = ibv_pd_to_internal(pd);
  int rslt;

  assert(IS_INTERNAL_IBV_STRUCT(internal_pd));
  rslt = NEXT_IBV_FNC(ibv_dealloc_pd)(internal_pd->real_pd);

  list_remove(&internal_pd->elem);
  free(internal_pd);

  return rslt;
}

struct ibv_mr * _reg_mr(struct ibv_pd * pd, void * addr,
                        size_t length, int flag) {
  struct internal_ibv_pd * internal_pd = ibv_pd_to_internal(pd);
  struct internal_ibv_mr * internal_mr;

  assert(IS_INTERNAL_IBV_STRUCT(internal_pd));
  internal_mr = malloc(sizeof(struct internal_ibv_mr));
  if (!internal_mr) {
    fprintf(stderr, "Error: Could not allocate memory for _reg_mr\n");
    exit(1);
  }

  internal_mr->real_mr = NEXT_IBV_FNC(ibv_reg_mr)(internal_pd->real_pd,
                                                  addr, length, flag);
  if (!internal_mr->real_mr) {
    fprintf(stderr, "Error: Could not register mr.\n");
    free(internal_mr);
    return NULL;
  }

  internal_mr->flags = flag;

  INIT_INTERNAL_IBV_TYPE(internal_mr);
  memcpy(&internal_mr->user_mr, internal_mr->real_mr, sizeof(struct ibv_mr));
  internal_mr->user_mr.context = internal_pd->user_pd.context;
  internal_mr->user_mr.pd = &internal_pd->user_pd;

  /*
  *  We need to check that memery regions created
  *  before checkpointing and after restarting will not
  *  have the same lkey.
  */
  if (is_restart) {
    struct list_elem * e;
    for (e = list_begin(&mr_list); e != list_end(&mr_list); e = list_next(e))
    {
      struct internal_ibv_mr * mr = list_entry(e, struct internal_ibv_mr, elem);
      if (mr->user_mr.lkey == internal_mr->user_mr.lkey) {
        PDEBUG("Error: duplicate lkey/rkey is genereated, will exit.\n");
        exit(1);
      }
    }
  }

  list_push_back(&mr_list, &internal_mr->elem);

  return &internal_mr->user_mr;
}

int _dereg_mr(struct ibv_mr * mr)
{
  struct internal_ibv_mr * internal_mr = ibv_mr_to_internal(mr);
  struct list_elem *e;

  assert(IS_INTERNAL_IBV_STRUCT(internal_mr));
  if (is_restart) {
    for (e = list_begin(&rkey_list);
         e != list_end(&rkey_list);
         e = list_next(e)) {
      struct ibv_rkey_pair * pair = list_entry(e, struct ibv_rkey_pair, elem);
      if (pair->orig_rkey.rkey == internal_mr->user_mr.rkey) {
        list_remove(&pair->elem);
        free(pair);
        break;
      }
    }
  }
  int rslt = NEXT_IBV_FNC(ibv_dereg_mr)(internal_mr->real_mr);

  list_remove(&internal_mr->elem);
  free(internal_mr);

  return rslt;
}

int _ibv_req_notify_cq(struct ibv_cq * cq, int solicited_only)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  struct internal_ibv_cq * internal_cq = ibv_cq_to_internal(cq);
  int rslt;

  assert(IS_INTERNAL_IBV_STRUCT(internal_cq));
  rslt = _real_ibv_req_notify_cq(internal_cq->real_cq, solicited_only);

  if (rslt == 0) {
    struct ibv_req_notify_cq_log * log;

    log = malloc(sizeof(struct ibv_req_notify_cq_log));
    if (!log) {
      fprintf(stderr,
              "Error: Could not allocate memory for _req_notify_cq.\n");
      exit(1);
    }

    log->solicited_only = solicited_only;
    list_push_back(&internal_cq->req_notify_log, &log->elem);
  }

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

struct ibv_cq * _create_cq(struct ibv_context * context,
                           int cqe, void * cq_context,
                           struct ibv_comp_channel * channel,
                           int comp_vector) {
  struct internal_ibv_ctx * internal_ctx = ibv_ctx_to_internal(context);
  struct internal_ibv_cq * internal_cq;
  struct ibv_comp_channel * real_channel;

  assert(IS_INTERNAL_IBV_STRUCT(internal_ctx));
  if (channel) {
    assert(IS_INTERNAL_IBV_STRUCT(ibv_comp_to_internal(channel)));
    real_channel = ibv_comp_to_internal(channel)->real_channel;
  } else {
    real_channel = NULL;
  }

  internal_cq = malloc(sizeof(struct internal_ibv_cq));
  if (!internal_cq) {
    fprintf(stderr,"Error: could not allocate memory for _create_cq\n");
    exit(1);
  }

  /* set up the lists */
  list_init(&internal_cq->wc_queue);
  list_init(&internal_cq->req_notify_log);

  internal_cq->real_cq = NEXT_IBV_FNC(ibv_create_cq)(internal_ctx->real_ctx,
                                                     cqe, cq_context,
                                                     real_channel, comp_vector);

  INIT_INTERNAL_IBV_TYPE(internal_cq);
  internal_cq->comp_vector = comp_vector;
  internal_cq->channel = ibv_comp_to_internal(channel);

  memcpy(&internal_cq->user_cq, internal_cq->real_cq, sizeof(struct ibv_cq));

  internal_cq->user_cq.context = &internal_ctx->user_ctx;
  internal_cq->user_cq.channel = channel;

  list_push_back(&cq_list, &internal_cq->elem);
  return &internal_cq->user_cq;
}

int _resize_cq(struct ibv_cq * cq, int cqe)
{
  struct internal_ibv_cq * internal_cq = ibv_cq_to_internal(cq);
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

int _destroy_cq(struct ibv_cq * cq)
{
  struct internal_ibv_cq * internal_cq = ibv_cq_to_internal(cq);
  int rslt;

  assert(IS_INTERNAL_IBV_STRUCT(internal_cq));
  rslt = NEXT_IBV_FNC(ibv_destroy_cq)(internal_cq->real_cq);

  /* destroy the four lists */
  struct list_elem * e;

  e = list_begin(&internal_cq->wc_queue);
  while(e != list_end(&internal_cq->wc_queue)) {
    struct list_elem * w = e;
    struct ibv_wc_wrapper *wc = list_entry (e, struct ibv_wc_wrapper, elem);
    e = list_next(e);
    list_remove(w);
    free(wc);
  }

  e = list_begin(&internal_cq->req_notify_log);
  while (e != list_end(&internal_cq->req_notify_log)){
    struct list_elem * w = e;
    struct ibv_req_notify_cq_log * log;
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

struct ibv_qp * _create_qp(struct ibv_pd * pd,
                           struct ibv_qp_init_attr * qp_init_attr) {
  struct internal_ibv_pd * internal_pd = ibv_pd_to_internal(pd);
  struct ibv_qp_init_attr attr = *qp_init_attr;
  struct internal_ibv_qp * internal_qp = malloc(sizeof(struct internal_ibv_qp));
  struct list_elem *e;
  struct internal_ibv_ctx * rslt = NULL;
  struct ibv_port_attr attr2;
  int rslt2;

  assert(IS_INTERNAL_IBV_STRUCT(internal_pd));
  internal_qp = malloc(sizeof(struct internal_ibv_qp));
  if (internal_qp == NULL) {
    fprintf(stderr, "Error: I cannot allocate memory for _create_qp\n");
    exit(1);
  }
  memset(internal_qp, 0, sizeof(struct internal_ibv_qp));

  /* fix up the qp_init_attr here */
  assert(IS_INTERNAL_IBV_STRUCT(ibv_cq_to_internal(qp_init_attr->recv_cq)));
  assert(IS_INTERNAL_IBV_STRUCT(ibv_cq_to_internal(qp_init_attr->send_cq)));
  attr.recv_cq = ibv_cq_to_internal(qp_init_attr->recv_cq)->real_cq;
  attr.send_cq = ibv_cq_to_internal(qp_init_attr->send_cq)->real_cq;
  if (attr.srq) {
    assert(IS_INTERNAL_IBV_STRUCT(ibv_srq_to_internal(qp_init_attr->srq)));
    attr.srq = ibv_srq_to_internal(qp_init_attr->srq)->real_srq;
  }

  internal_qp->real_qp = NEXT_IBV_FNC(ibv_create_qp)(internal_pd->real_pd,
                                                     &attr);
  if (internal_qp->real_qp == NULL) {
    PDEBUG("Error: _real_ibv_create_qp fails\n");
    free(internal_qp);
    return NULL;
  }

  INIT_INTERNAL_IBV_TYPE(internal_qp);
  memcpy(&internal_qp->user_qp,
         internal_qp->real_qp,
         sizeof(struct ibv_qp));

  /*
   * After restart, if a new qp is created and has the same qp_num as
   * that of a qp created before checkpoint, there will be a conflict.
   */
  if (is_restart) {
    struct list_elem *w;
    for (w = list_begin(&qp_list);
         w != list_end(&qp_list);
         w = list_next(w)) {
      struct internal_ibv_qp *qp1 = list_entry(w, struct internal_ibv_qp, elem);
      if (qp1->user_qp.qp_num == internal_qp->user_qp.qp_num) {
        fprintf(stderr,
                "Error: duplicate qp_num is genereated after restart\n");
	exit(1);
      }
    }
  }

  /*
   * fix up the user_qp
   * loop to find the right context the "dumb way"
   * This should probably just take the context from pd
   */
  for (e = list_begin(&ctx_list);
       e != list_end(&ctx_list);
       e = list_next(e)) {
    struct internal_ibv_ctx * ctx;

    ctx = list_entry (e, struct internal_ibv_ctx, elem);
    if (ctx->real_ctx == internal_qp->real_qp->context) {
      rslt = ctx;
      break;
    }
  }

  if (!rslt) {
    fprintf(stderr, "Error: Could not find context in _create_qp\n");
    exit(1);
  }

  list_init(&internal_qp->modify_qp_log);
  list_init(&internal_qp->post_send_log);
  list_init(&internal_qp->post_recv_log);
  internal_qp->user_qp.context = &rslt->user_ctx;
  internal_qp->user_qp.pd = &internal_pd->user_pd;
  internal_qp->user_qp.recv_cq = qp_init_attr->recv_cq;
  internal_qp->user_qp.send_cq = qp_init_attr->send_cq;
  internal_qp->user_qp.srq = qp_init_attr->srq;
  internal_qp->init_attr = *qp_init_attr;

  /* get the LID */
  rslt2 = NEXT_IBV_FNC(ibv_query_port)(internal_qp->real_qp->context, 1, &attr2);
  if (rslt2 != 0) {
    fprintf(stderr, "Call to ibv_query_port failed %d.\n", rslt2);
    exit(1);
  }
  internal_qp->original_id.lid = attr2.lid;
  internal_qp->local_qp_pd_id.lid = attr2.lid;
  internal_qp->original_id.qpn = internal_qp->real_qp->qp_num;
  internal_qp->local_qp_pd_id.qpn = internal_qp->real_qp->qp_num;
  internal_qp->port_num = 1;

  list_push_back(&qp_list, &internal_qp->elem);
  return &internal_qp->user_qp;
}

int _destroy_qp(struct ibv_qp * qp)
{
  struct internal_ibv_qp * internal_qp = ibv_qp_to_internal(qp);
  int rslt;

  assert(IS_INTERNAL_IBV_STRUCT(internal_qp));
  rslt = NEXT_IBV_FNC(ibv_destroy_qp)(internal_qp->real_qp);
  struct list_elem * e = list_begin(&internal_qp->modify_qp_log);

  while (e != list_end(&internal_qp->modify_qp_log)) {
    struct list_elem * w = e;
    struct ibv_modify_qp_log * log;

    log = list_entry (e, struct ibv_modify_qp_log, elem);
    e = list_next(e);
    list_remove(w);
    free(log);
  }

  e = list_begin(&internal_qp->post_recv_log);
  while (e != list_end(&internal_qp->post_recv_log)) {
    struct list_elem * w = e;
    struct ibv_post_recv_log * log;

    log = list_entry (e, struct ibv_post_recv_log, elem);
    e = list_next(e);
    list_remove(w);
    free(log);
  }

  e = list_begin(&internal_qp->post_send_log);
  while (e != list_end(&internal_qp->post_send_log)) {
    struct list_elem * w = e;
    struct ibv_post_send_log *log;

    log = list_entry (e, struct ibv_post_send_log, elem);
    assert(log->magic == SEND_MAGIC);
    e = list_next(e);
    list_remove(w);
    free(log);
  }

  list_remove(&internal_qp->elem);
  free(internal_qp);

  return rslt;
}

int _modify_qp(struct ibv_qp * qp, struct ibv_qp_attr * attr, int attr_mask)
{
  struct internal_ibv_qp * internal_qp = ibv_qp_to_internal(qp);
  struct ibv_modify_qp_log * log;
  int rslt;

  assert(IS_INTERNAL_IBV_STRUCT(internal_qp));
  log = malloc(sizeof(struct ibv_modify_qp_log));
  if (!log) {
    fprintf(stderr, "Error: Couldn't allocate memory for log.\n");
    exit(1);
  }

  log->attr = *attr;
  log->attr_mask = attr_mask;
  list_push_back(&internal_qp->modify_qp_log, &log->elem);

  rslt = NEXT_IBV_FNC(ibv_modify_qp)(internal_qp->real_qp, attr, attr_mask);
  internal_qp->user_qp.state = internal_qp->real_qp->state;

  if (attr_mask & IBV_QP_DEST_QPN) {
    internal_qp->remote_id.qpn = attr->dest_qp_num;
    internal_qp->remote_qp_pd_id.qpn = attr->dest_qp_num;
    if (is_restart) {
      ibv_qp_pd_id_t id = {
        .qpn = attr->dest_qp_num,
	.lid = attr->ah_attr.dlid
      };
      uint32_t size = sizeof(internal_qp->remote_pd_id);
      int ret = dmtcp_send_query_to_coordinator("pd_info",
                                                &id, sizeof(id),
                                                &internal_qp->remote_pd_id,
                                                &size);
      assert(size == sizeof(int));
      assert(ret != 0);
    }
  }

  if (attr_mask & IBV_QP_SQ_PSN) {
    internal_qp->original_id.psn = attr->sq_psn & 0xffffff;
  }

  if (attr_mask & IBV_QP_RQ_PSN) {
    internal_qp->remote_id.psn = attr->rq_psn & 0xffffff;
  }

  if (attr_mask & IBV_QP_AV) {
    internal_qp->remote_id.lid = attr->ah_attr.dlid;
    internal_qp->remote_qp_pd_id.lid = attr->ah_attr.dlid;
    // must OR src_path_bits to support LMC
    internal_qp->original_id.lid |= attr->ah_attr.src_path_bits;
    internal_qp->local_qp_pd_id.lid |= attr->ah_attr.src_path_bits;
  }

  if (attr_mask & IBV_QP_PORT) {
    struct ibv_port_attr attr2;
    if (NEXT_IBV_FNC(ibv_query_port)(internal_qp->real_qp->context,
                                     attr->port_num, &attr2) != 0) {
      fprintf(stderr, "Call to ibv_query_port failed\n");
      exit(1);
    }
    internal_qp->original_id.lid = attr2.lid;
    internal_qp->local_qp_pd_id.lid = attr2.lid;
    internal_qp->port_num = attr->port_num;
  }

  return rslt;
}

int _query_qp(struct ibv_qp * qp, struct ibv_qp_attr * attr, int attr_mask,
              struct ibv_qp_init_attr * init_attr)
{
  struct internal_ibv_qp * internal_qp = ibv_qp_to_internal(qp);
  int rslt;

  if (!IS_INTERNAL_IBV_STRUCT(internal_qp)) {
    return NEXT_IBV_FNC(ibv_query_qp)(qp, attr, attr_mask, init_attr);
  }
  else {
    rslt = NEXT_IBV_FNC(ibv_query_qp)(internal_qp->real_qp,
                                      attr, attr_mask, init_attr);
  }

  assert(get_cq_from_pointer(init_attr->recv_cq) != NULL);
  assert(get_cq_from_pointer(init_attr->send_cq) != NULL);
  init_attr->recv_cq = &get_cq_from_pointer(init_attr->recv_cq)->user_cq;
  init_attr->send_cq = &get_cq_from_pointer(init_attr->send_cq)->user_cq;
  if (init_attr->srq) {
    assert(get_srq_from_pointer(init_attr->srq) != NULL);
    init_attr->srq = &get_srq_from_pointer(init_attr->srq)->user_srq;
  }

  return rslt;
}

struct ibv_srq * _create_srq(struct ibv_pd * pd,
                             struct ibv_srq_init_attr * srq_init_attr) {
  struct internal_ibv_pd * internal_pd;
  struct internal_ibv_srq * internal_srq;

  internal_pd = ibv_pd_to_internal(pd);

  assert(IS_INTERNAL_IBV_STRUCT(internal_pd));
  internal_srq = malloc(sizeof(struct internal_ibv_srq));

  if ( internal_srq == NULL ) {
    fprintf(stderr, "Error: I cannot allocate memory for _create_srq\n");
    exit(1);
  }

  internal_srq->real_srq = NEXT_IBV_FNC(ibv_create_srq)(internal_pd->real_pd,
                                                        srq_init_attr);

  if (internal_srq->real_srq == NULL) {
    fprintf(stderr, "Error: _real_ibv_create_srq fail\n");
    free(internal_srq);
    return NULL;
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
  struct internal_ibv_ctx * rslt = NULL;
  for (e = list_begin (&ctx_list);
       e != list_end (&ctx_list);
       e = list_next (e)) {
    struct internal_ibv_ctx * ctx;

    ctx = list_entry (e, struct internal_ibv_ctx, elem);
    if (ctx->real_ctx == internal_srq->real_srq->context) {
      rslt = ctx;
      break;
    }
  }

  if (!rslt) {
    fprintf(stderr, "Error: Could not find context in _create_srq\n");
    exit(1);
  }

  list_init(&internal_srq->modify_srq_log);
  list_init(&internal_srq->post_srq_recv_log);
  internal_srq->user_srq.context = &rslt->user_ctx;
  internal_srq->user_srq.pd = &internal_pd->user_pd;
  internal_srq->init_attr = *srq_init_attr;

  list_push_back(&srq_list, &internal_srq->elem);
  return &internal_srq->user_srq;
}

int _destroy_srq(struct ibv_srq * srq)
{
  struct internal_ibv_srq * internal_srq = ibv_srq_to_internal(srq);
  int rslt;
  struct list_elem * e;

  assert(IS_INTERNAL_IBV_STRUCT(internal_srq));
  rslt = NEXT_IBV_FNC(ibv_destroy_srq)(internal_srq->real_srq);
  e = list_begin(&internal_srq->modify_srq_log);
  while(e != list_end(&internal_srq->modify_srq_log)) {
    struct list_elem * w = e;
    struct ibv_modify_srq_log * log;

    log = list_entry (e, struct ibv_modify_srq_log, elem);
    e = list_next(e);
    list_remove(w);
    free(log);
  }

  e = list_begin(&internal_srq->post_srq_recv_log);
  while(e != list_end(&internal_srq->post_srq_recv_log)) {
    struct list_elem * w = e;
    struct ibv_post_srq_recv_log * log;

    log = list_entry(e, struct ibv_post_srq_recv_log, elem);
    e = list_next(e);
    list_remove(w);
    free(log);
  }

  list_remove(&internal_srq->elem);
  free(internal_srq);

  return rslt;
}

int _modify_srq(struct ibv_srq *srq, struct ibv_srq_attr *attr, int attr_mask)
{
  struct internal_ibv_srq * internal_srq = ibv_srq_to_internal(srq);
  struct ibv_modify_srq_log * log;

  assert(IS_INTERNAL_IBV_STRUCT(internal_srq));
  log = malloc(sizeof(struct ibv_modify_srq_log));
  if (!log) {
    fprintf(stderr, "Error: Couldn't allocate memory for log.\n");
    exit(1);
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

int _query_srq(struct ibv_srq * srq, struct ibv_srq_attr * srq_attr)
{
  if (!IS_INTERNAL_IBV_STRUCT(ibv_srq_to_internal(srq))) {
    return NEXT_IBV_FNC(ibv_query_srq)(srq, srq_attr);
  }

  return NEXT_IBV_FNC(ibv_query_srq)(ibv_srq_to_internal(srq)->real_srq,
                                     srq_attr);
}

int _ibv_post_recv(struct ibv_qp * qp, struct ibv_recv_wr * wr,
                   struct ibv_recv_wr ** bad_wr) {
  struct internal_ibv_qp * internal_qp = ibv_qp_to_internal(qp);
  struct ibv_recv_wr * copy_wr;
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
    struct ibv_post_recv_log * log = malloc(sizeof(struct ibv_post_recv_log));
    struct ibv_recv_wr *tmp;

    if (!log) {
      fprintf(stderr, "Error: could not allocate memory for log.\n");
      exit(1);
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

int _ibv_post_srq_recv(struct ibv_srq * srq, struct ibv_recv_wr * wr,
                       struct ibv_recv_wr ** bad_wr) {
  struct internal_ibv_srq * internal_srq = ibv_srq_to_internal(srq);
  struct ibv_recv_wr * copy_wr;
  int rslt;
  struct ibv_recv_wr *copy_wr1;

  DMTCP_PLUGIN_DISABLE_CKPT();

  assert(IS_INTERNAL_IBV_STRUCT(internal_srq));
  copy_wr = copy_recv_wr(wr);
  update_lkey_recv(copy_wr);

  rslt = _real_ibv_post_srq_recv(internal_srq->real_srq, copy_wr, bad_wr);
  if (rslt) {
    fprintf(stderr, "Error: srq_post_recv failed!\n");
    exit(1);
  }

  delete_recv_wr(copy_wr);

  copy_wr = copy_recv_wr(wr);
  copy_wr1 = copy_wr;
  while (copy_wr1) {
    struct ibv_post_srq_recv_log * log;
    struct ibv_recv_wr *tmp;

    log = malloc(sizeof(struct ibv_post_srq_recv_log));

    if (!log) {
      fprintf(stderr, "Error: could not allocate memory for log.\n");
      exit(1);
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

int _ibv_post_send(struct ibv_qp * qp, struct ibv_send_wr * wr, struct
                   ibv_send_wr ** bad_wr) {
  struct internal_ibv_qp * internal_qp = ibv_qp_to_internal(qp);
  struct ibv_send_wr * copy_wr;
  int rslt;
  struct ibv_send_wr *copy_wr1;

  DMTCP_PLUGIN_DISABLE_CKPT();

  assert(IS_INTERNAL_IBV_STRUCT(internal_qp));
  copy_wr = copy_send_wr(wr);
  update_lkey_send(copy_wr);

  switch (internal_qp->user_qp.qp_type) {
    case IBV_QPT_RC:
      if (is_restart) {
        update_rkey_send(copy_wr, internal_qp->remote_pd_id);
      }
      break;
    case IBV_QPT_UD:
      assert(copy_wr->opcode == IBV_WR_SEND);
      if (is_restart) {
        update_ud_send_restart(copy_wr);
      }
      else {
        update_ud_send(copy_wr);
      }
      break;
    default:
      fprintf(stderr, "Warning: unsupported qp type: %d\n",
              internal_qp->user_qp.qp_type);
      exit(1);
  }

  rslt = _real_ibv_post_send(internal_qp->real_qp, copy_wr, bad_wr);

  delete_send_wr(copy_wr);

  copy_wr = copy_send_wr(wr);
  copy_wr1 = copy_wr;
  while (copy_wr1) {
    struct ibv_post_send_log * log = malloc(sizeof(struct ibv_post_send_log));
    struct ibv_send_wr *tmp;

    if (!log) {
      fprintf(stderr, "Error: could not allocate memory for log.\n");
      exit(1);
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

int _ibv_poll_cq(struct ibv_cq * cq, int num_entries, struct ibv_wc * wc)
{
  int rslt = 0;
  struct internal_ibv_cq * internal_cq = ibv_cq_to_internal(cq);
  int size, i;

  DMTCP_PLUGIN_DISABLE_CKPT();

  assert(IS_INTERNAL_IBV_STRUCT(internal_cq));
  size = list_size(&internal_cq->wc_queue);
  if (size > 0) {
    struct list_elem * e = list_front(&internal_cq->wc_queue);
    for (i = 0; (i < size) && (i < num_entries); i++) {
      struct list_elem * w = e;
      PDEBUG("Polling completion from internal buffer\n");
      wc[i] = list_entry(e, struct ibv_wc_wrapper, elem)->wc;
      e = list_next(e);
      list_remove(w);
      rslt++;
    }

    if (size < num_entries) {
      int ne = _real_ibv_poll_cq(internal_cq->real_cq,
                                 num_entries - size, wc + size);
      if (ne < 0) {
	fprintf(stderr, "poll_cq() error!\n");
	exit(1);
      }
      rslt += ne;
    }
  }
  else {
    rslt = _real_ibv_poll_cq(internal_cq->real_cq, num_entries, wc);
      if (rslt < 0) {
	fprintf(stderr, "poll_cq() error!\n");
	exit(1);
      }
  }

  for (i = 0; i < rslt; i++) {
    struct internal_ibv_qp * internal_qp = qp_num_to_qp(&qp_list, wc[i].qp_num);
    if (i >= size) {
      enum ibv_wc_opcode opcode = wc[i].opcode;
      wc[i].qp_num = internal_qp->user_qp.qp_num;
      if (opcode & IBV_WC_RECV ||
          opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
	if (internal_qp->user_qp.srq) {
	  struct internal_ibv_srq * internal_srq;
	  internal_srq = ibv_srq_to_internal(internal_qp->user_qp.srq);
	  internal_srq->recv_count++;
	}
	else {
          struct list_elem * e;
          struct ibv_post_recv_log * log;

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
          struct list_elem * e;
          struct ibv_post_send_log * log;

          e = list_pop_front(&internal_qp->post_send_log);
          log = list_entry(e, struct ibv_post_send_log, elem);

          assert(log->magic == SEND_MAGIC);
          free(log);
	}
	else {
	  while(1) {
            struct list_elem * e;
            struct ibv_post_send_log * log;

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
        fprintf(stderr,
                "Error: opcode %d specifies unsupported operation.\n", opcode);
        exit(1);
      } else {
        fprintf(stderr, "Unknown or invalid opcode.\n");
        exit(1);
      }
    }
  }

  DMTCP_PLUGIN_ENABLE_CKPT();
  return rslt;
}

void _ack_cq_events(struct ibv_cq * cq, unsigned int nevents)
{
  struct internal_ibv_cq * internal_cq = ibv_cq_to_internal(cq);

  if (!IS_INTERNAL_IBV_STRUCT(internal_cq)) {
    return NEXT_IBV_FNC(ibv_ack_cq_events)(cq, nevents);
  }

  return NEXT_IBV_FNC(ibv_ack_cq_events)(internal_cq->real_cq, nevents);
}

struct ibv_ah * _create_ah(struct ibv_pd *pd, struct ibv_ah_attr *attr) {
  struct internal_ibv_pd * internal_pd = ibv_pd_to_internal(pd);
  struct internal_ibv_ah * internal_ah;
  struct ibv_ah_attr real_attr = *attr;

  assert(IS_INTERNAL_IBV_STRUCT(internal_pd));
  internal_ah = malloc(sizeof(struct internal_ibv_ah));
  if (internal_ah == NULL) {
    fprintf(stderr, "Error: Unable to allocate memory for _create_ah\n");
    exit(1);
  }
  memset(internal_ah, 0, sizeof(struct internal_ibv_ah));
  internal_ah->attr = *attr;
  internal_ah->is_restart = false;

  // On restart, we need to fix the lid
  if (is_restart) {
    uint32_t size;
    dmtcp_send_query_to_coordinator("lidInfo",
                                    &attr->dlid,
                                    sizeof(attr->dlid),
                                    &real_attr.dlid,
                                    &size);
    assert(size == sizeof(attr->dlid));

    internal_ah->is_restart = true;
  }

  internal_ah->real_ah = NEXT_IBV_FNC(ibv_create_ah)(internal_pd->real_pd,
                                                     &real_attr);

  if (internal_ah->real_ah == NULL) {
    fprintf(stderr, "Error: _real_ibv_create_ah fail\n");
    free(internal_ah);
    return NULL;
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

int _destroy_ah(struct ibv_ah *ah) {
  struct internal_ibv_ah *internal_ah;
  int rslt;

  internal_ah = ibv_ah_to_internal(ah);
  assert(IS_INTERNAL_IBV_STRUCT(internal_ah));
  rslt = NEXT_IBV_FNC(ibv_destroy_ah)(internal_ah->real_ah);

  list_remove(&internal_ah->elem);
  free(internal_ah);

  return rslt;
}
