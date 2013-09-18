/*! \file ibvctx.c */

/* FILE: ibvctx.c
 * AUTHOR: GREG KERR
 * EMAIL: kerr.g@neu.edu
 * Copyright (C) 2011 Greg Kerr, Gene Cooperman, Kapil Arya
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * */

#include "ibvctx.h"
#include <infiniband/verbs.h>
#include <linux/types.h>
#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>
#include "infinibandreals.h"
#include "ibv_internal.h"
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "lib/list.h"
#include "ibv_trampolines.h"
#include "dmtcpplugin.h"
#include <pthread.h>
#include <errno.h>

//! This flag can be used for debugging
static bool is_restart = false;

//! This is a list of devices open with ibv_open_device
static struct ibv_device ** _dev_list;
//! This is the int of how many devices were in _dev_list
static int _dmtcp_num_devices;

/* these lists will track the resources so they can be recreated
 * at restart time */
//! This is a list of devices
static struct list dev_list = LIST_INITIALIZER(dev_list);
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
//! This is the list of rkey pairs
static struct list rkey_list = LIST_INITIALIZER(rkey_list);

static void send_qp_info(void);
static void query_qp_info(void);
static void send_rkey_info(void);
static void query_rkey_info(void);
static void post_restart(void);
static void post_restart2(void);

/* These files are processed by sed at compile time */
#include "keys.ic"
#include "ibv_wr_ops_send.ic"
#include "ibv_wr_ops_recv.ic"
#include "get_qp_from_pointer.ic"
#include "get_cq_from_pointer.ic"
#include "get_srq_from_pointer.ic"

void dmtcp_process_event(DmtcpEvent_t event, DmtcpEventData_t* data)
{
  bool isRestart = (bool) data;
  switch (event) {
  case DMTCP_EVENT_WRITE_CKPT:
    sleep(1);
    pre_checkpoint();
    break;
  case DMTCP_EVENT_RESTART:
    post_restart();
    break;
//  case DMTCP_EVENT_RESUME:
//    sleep(1);
//    pre_checkpoint();
//    break;
  case DMTCP_EVENT_REGISTER_NAME_SERVICE_DATA:
    if (data->nameserviceInfo.isRestart) {
      send_qp_info();
      send_rkey_info();
    }
    break;
  case DMTCP_EVENT_SEND_QUERIES:
    if (data->nameserviceInfo.isRestart) {
      query_qp_info();
      query_rkey_info();
      post_restart2();
    }
    break;
  default:
    break;
  }
  
  NEXT_DMTCP_PROCESS_EVENT(event, data);
  //return;
}


/*! This will populate the coordinator with information about the new QPs */
static void send_qp_info(void)
{
  //TODO: Check all possible DMTCP states before sending/querying
  //TODO: BREAK REPLAYING OF MODIFY_QP LOG INTO TWO STAGES.
  //GO AHEAD AND MOVE QP INTO INIT STATE BEFORE DOING THIS.
  // IF A QP WAS NEVER MOVED INTO RTR THEN IT WON'T HAVE A CORRESPONDING QP
  struct list_elem *e;
  char hostname[128];
  gethostname(hostname,128);
  for (e = list_begin(&qp_list); e != list_end(&qp_list); e = list_next(e)) {
    struct internal_ibv_qp * internal_qp = list_entry(e, struct internal_ibv_qp, elem);
    if (internal_qp->user_qp.state != IBV_QPS_INIT) {
      PDEBUG("Sending over original_id: %d  %d %d and current_id: %d %d %d from %s\n", internal_qp->original_id.qpn, internal_qp->original_id.lid, internal_qp->original_id.psn, internal_qp->current_id.qpn, internal_qp->current_id.lid, internal_qp->current_id.psn, hostname);
      dmtcp_send_key_val_pair_to_coordinator(&internal_qp->original_id, sizeof(internal_qp->original_id),
                                             &internal_qp->current_id, sizeof(internal_qp->current_id));
    }
  }
}

/*! This will query the coordinator for information about the new QPs */
static void query_qp_info(void)
{
  char hostname[128];
  gethostname(hostname, 128);
  struct list_elem *e;
  for (e = list_begin(&qp_list); e != list_end(&qp_list); e = list_next(e)) {
    struct internal_ibv_qp * internal_qp = list_entry(e, struct internal_ibv_qp, elem);
    size_t size = sizeof(internal_qp->current_remote);
    PDEBUG("Querying for remote_id: %d %d %d from %s\n", internal_qp->remote_id.qpn, internal_qp->remote_id.lid, internal_qp->remote_id.psn, hostname);
    dmtcp_send_query_to_coordinator(&internal_qp->remote_id, sizeof(internal_qp->remote_id),
                                    &internal_qp->current_remote, &size);
    assert(size == sizeof(struct ibv_qp_id));
  }
}

/*! This will populate the coordinator with information about the new rkeys */
static void send_rkey_info(void)
{
  struct list_elem *e;
  char hostname[128];
  gethostname(hostname,128);
  for (e = list_begin(&rkey_list); e != list_end(&qp_list); e = list_next(e)) {
    struct ibv_rkey_pair * pair = list_entry(e, struct ibv_rkey_pair, elem);
    if (pair->new_rkey != 0) {
      PDEBUG("Sending over original_rkey: %d and new_rkey: %d from %s\n", pair->orig_rkey, pair->new_rkey, hostname);
      dmtcp_send_key_val_pair_to_coordinator(&pair->orig_rkey, sizeof(pair->orig_rkey),
                                             &pair->new_rkey, sizeof(pair->new_rkey));
    }
  }
}

/*! This will query the coordinator for information about the new rkeys */
static void query_rkey_info(void)
{
  char hostname[128];
  gethostname(hostname, 128);
  struct list_elem *e;
  for (e = list_begin(&rkey_list); e != list_end(&rkey_list); e = list_next(e)) {
    struct ibv_rkey_pair * pair = list_entry(e, struct ibv_rkey_pair, elem);
    if (pair->new_rkey == 0) {
      size_t size = sizeof(pair->new_rkey);
      PDEBUG("Querying for new_rkey: %d from %s\n", pair->orig_rkey, hostname);
      dmtcp_send_query_to_coordinator(&pair->orig_rkey, sizeof(pair->orig_rkey),
                                      &pair->new_rkey, &size);
      assert(size == sizeof(uint32_t));
    }
  }
}
/*! This will drain the given completion queue
 */
static void drain_completion_queue(struct internal_ibv_cq * internal_cq)
{
  _uninstall_poll_cq_trampoline();
  int ne;
  /* This loop will drain the completion queue and buffer the entries */
  do {
    struct ibv_wc_wrapper * wc = malloc(sizeof(struct ibv_wc_wrapper));
    ne = ibv_poll_cq(internal_cq->real_cq, 1, &wc->wc);

    if (ne > 0) {
      PDEBUG("Found a completion on the queue.\n");
      list_push_back(&internal_cq->wc_queue, &wc->elem);
      struct internal_ibv_qp * internal_qp = qp_num_to_qp(&qp_list, wc->wc.qp_num);
      enum ibv_wc_opcode opcode = wc->wc.opcode;
      if (opcode & IBV_WC_RECV ||
          opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
	if (internal_qp->user_qp.srq) {
	  struct internal_ibv_srq * internal_srq = ibv_srq_to_internal(internal_qp->user_qp.srq);
	  internal_srq->recv_count++;
	}
	else {
          struct list_elem * e = list_pop_front(&internal_qp->post_recv_log);
          struct ibv_post_recv_log * log = list_entry(e, struct ibv_post_recv_log, elem);
          free(log);
	}
      } else if (opcode == IBV_WC_SEND ||
                 opcode == IBV_WC_RDMA_WRITE ||
                 opcode == IBV_WC_RDMA_READ ||
                 opcode == IBV_WC_COMP_SWAP ||
                 opcode == IBV_WC_FETCH_ADD) {
        struct list_elem * e = list_pop_front(&internal_qp->post_send_log);
        struct ibv_post_send_log * log = list_entry(e, struct ibv_post_send_log, elem);
        assert(log->magic == SEND_MAGIC);
        free(log);
      } else if (opcode == IBV_WC_BIND_MW) {
        fprintf(stderr, "Error: opcode %d specifies unsupported operation.\n", opcode);
        exit(1);
      } else {
        fprintf(stderr, "Unknown or invalid opcode.\n");
        exit(1);
      }
    } else {
      free(wc);
    }
  } while (ne > 0);
  _install_poll_cq_trampoline();
}

/*! This will first check the completion queue for any
 * left completions and remove them, saving them into a buffer
 * for later user. It will then close all the resources
 */
void pre_checkpoint(void)
{
  char host[128];
  gethostname(host, 128);
  PDEBUG("I made it into pre_checkpoint.\n");

  struct list_elem * e;
  for (e = list_begin(&cq_list); e != list_end(&cq_list); e = list_next(e)) {
    struct internal_ibv_cq * internal_cq = list_entry(e, struct internal_ibv_cq, elem);
    drain_completion_queue(internal_cq);
  }  

  PDEBUG("Made it out of pre_checkpoint\n");
}

// TODO: Must handle case of modifying after checkpoint
void post_restart(void)
{
  PDEBUG("in post_restart\n");
  is_restart = true; 
  /* code to re-open device */
  int num;
  _dev_list = _real_ibv_get_device_list(&num);

  if (!num)
  {
    fprintf(stderr, "Error: ibv_get_device_list returned 0 devices.\n");
    exit(1);
  }

  struct list_elem *e;
  for (e = list_begin(&ctx_list); e != list_end(&ctx_list); e = list_next(e))
  {
    struct internal_ibv_ctx * internal_ctx = list_entry(e, struct internal_ibv_ctx, elem);

    for (int i = 0; i < num; i++)
    {
      if (!strncmp(internal_ctx->user_ctx.device->dev_name, _dev_list[i]->dev_name, strlen(_dev_list[i]->dev_name)))
      {
          internal_ctx->real_ctx = _real_ibv_open_device(_dev_list[i]);

          if (!internal_ctx->real_ctx)
          {
            fprintf(stderr, "Error: Could not re-open the context.\n");
            exit(1);
          }
          // here we need to make sure that recreated 
          // contexts and old ones have
          // the same cmd_fd and async_fd
	  if (internal_ctx->real_ctx->cmd_fd != internal_ctx->user_ctx.cmd_fd) {
            if (dup2(internal_ctx->real_ctx->cmd_fd, internal_ctx->user_ctx.cmd_fd) != internal_ctx->user_ctx.cmd_fd) {
              fprintf(stderr, "Error: Could not duplicate cmd_fd.\n");
	      exit(1);
            } 
	    close(internal_ctx->real_ctx->cmd_fd);
	    internal_ctx->real_ctx->cmd_fd = internal_ctx->user_ctx.cmd_fd;
	  }
	  if (internal_ctx->real_ctx->async_fd != internal_ctx->user_ctx.async_fd) {
            if (dup2(internal_ctx->real_ctx->async_fd, internal_ctx->user_ctx.async_fd) != internal_ctx->user_ctx.async_fd) {
              fprintf(stderr, "Error: Could not duplicate async_fd.\n");
	      exit(1);
            } 
	    close(internal_ctx->real_ctx->async_fd);
	    internal_ctx->real_ctx->async_fd = internal_ctx->user_ctx.async_fd;
	  }

          break;
      }
    }
  }
  /* end code to re-open device */

  /* code to alloc the protection domain */
//  sleep(60);
  for (e = list_begin(&pd_list); e != list_end(&pd_list); e = list_next(e))
  {
    struct internal_ibv_pd * internal_pd = list_entry(e, struct internal_ibv_pd, elem);

    struct internal_ibv_ctx * internal_ctx = ibv_ctx_to_internal(internal_pd->user_pd.context);

    internal_pd->real_pd = _real_ibv_alloc_pd(internal_ctx->real_ctx);

    if (!internal_pd->real_pd)
    {
      fprintf(stderr, "Error: Could not re-alloc the pd.\n");
      exit(1);
    }
  }
  /*end code to alloc the protection domain */

  /* code to register the memory region */
  for (e = list_begin(&mr_list); e != list_end(&mr_list); e = list_next(e))
  {
    struct internal_ibv_mr * internal_mr = list_entry(e, struct internal_ibv_mr, elem);

    struct internal_ibv_pd * internal_pd = ibv_pd_to_internal(internal_mr->user_mr.pd);

    internal_mr->real_mr = _real_ibv_reg_mr(internal_pd->real_pd, internal_mr->user_mr.addr,
                                            internal_mr->user_mr.length, internal_mr->flags);

    if (!internal_mr->real_mr)
    {
      fprintf(stderr, "Error: Could not re-reg the mr.\n");
      exit(1);
    }
    struct list_elem * w;
    for (w = list_begin(&rkey_list); w != list_end(&rkey_list); w = list_next(w)) {
      struct ibv_rkey_pair * pair = list_entry(w, struct ibv_rkey_pair, elem);
      if (pair->orig_rkey == internal_mr->user_mr.rkey) {
        assert(pair->new_rkey == 0);
        pair->new_rkey = internal_mr->real_mr->rkey;
      }
    }
  }
  /* end code to register the memory region */

  /* code to register the completion channel */
  for (e = list_begin(&comp_list); e != list_end(&comp_list); e = list_next(e))
  {
    PDEBUG("About to deal with completion channels\n");
    struct internal_ibv_comp_channel * internal_comp = list_entry(e, struct internal_ibv_comp_channel, elem);

    struct internal_ibv_ctx * internal_ctx = ibv_ctx_to_internal(internal_comp->user_channel.context);

    internal_comp->real_channel = _real_ibv_create_comp_channel(internal_ctx->real_ctx);
    if (!internal_comp->real_channel)
    {
      fprintf(stderr, "Error: Could not re-create the comp channel.\n");
      exit(1);
    }

    if (internal_comp->real_channel->fd != internal_comp->user_channel.fd)
    {
      if(dup2(internal_comp->real_channel->fd, internal_comp->user_channel.fd) == -1)
      {
        fprintf(stderr, "Error: could not duplicate the file descriptor.\n");
        exit(1);
      }
      close(internal_comp->real_channel->fd);
      internal_comp->real_channel->fd = internal_comp->user_channel.fd;
    }

    internal_comp->recreate_channel = false;
  }
  /* end code to register the completion channel */

  /* code to register the completion queue */
  for (e = list_begin(&cq_list); e != list_end(&cq_list); e = list_next(e))
  {
    struct internal_ibv_cq * internal_cq = list_entry(e, struct internal_ibv_cq, elem);

    struct internal_ibv_ctx * internal_ctx = ibv_ctx_to_internal(internal_cq->user_cq.context);
    internal_cq->real_cq = _real_ibv_create_cq(internal_ctx->real_ctx, internal_cq->user_cq.cqe,
                           internal_cq->user_cq.cq_context, NULL, internal_cq->comp_vector);

    if (!internal_cq->real_cq)
    {
      fprintf(stderr, "Error: could not recreate the cq.\n");
      exit(1);
    }

    struct list_elem *w;
    for (w = list_begin(&internal_cq->req_notify_log); w != list_end(&internal_cq->req_notify_log); w = list_next(w))
    {
      struct ibv_req_notify_cq_log * log = list_entry(w, struct ibv_req_notify_cq_log, elem);
      _uninstall_req_notify_cq_trampoline();
      if (ibv_req_notify_cq(internal_cq->real_cq, log->solicited_only))
      {
        fprintf(stderr, "Error: Could not repost req_notify_cq\n");
        exit(1);
      }
      _install_req_notify_cq_trampoline();
    }
//    PDEBUG("Size of post_recv_log is %d\n", list_size(&internal_cq->post_recv_log));
//    PDEBUG("Size of post_send_log is %d\n", list_size(&internal_cq->post_send_log));
  }

  /* end code to register completion queue */


  /* code to re-create the shared receive queue */
  for (e = list_begin(&srq_list); e != list_end(&srq_list); e = list_next(e))
  {
    struct internal_ibv_srq * internal_srq = list_entry(e, struct internal_ibv_srq, elem);
    struct ibv_srq_init_attr new_attr = internal_srq->init_attr;
    internal_srq->real_srq = _real_ibv_create_srq(ibv_pd_to_internal(internal_srq->user_srq.pd)->real_pd, &new_attr);
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
    struct internal_ibv_qp * internal_qp = list_entry(e, struct internal_ibv_qp, elem);

    struct ibv_qp_init_attr new_attr = internal_qp->init_attr;
    new_attr.recv_cq = ibv_cq_to_internal(internal_qp->user_qp.recv_cq)->real_cq;
    new_attr.send_cq = ibv_cq_to_internal(internal_qp->user_qp.send_cq)->real_cq;
    if(new_attr.srq)
      new_attr.srq = ibv_srq_to_internal(internal_qp->user_qp.srq)->real_srq;

    internal_qp->real_qp = _real_ibv_create_qp(ibv_pd_to_internal(internal_qp->user_qp.pd)->real_pd,
                           &new_attr);

    if (!internal_qp->real_qp)
    {
      fprintf(stderr, "Error: Could not recreate the qp.\n");
      exit(1);
    }

    /* code to populate the ID */
    /* get the port num */
    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    if (_real_ibv_query_qp(internal_qp->real_qp, &attr, IBV_QP_PORT, &init_attr))
    {
      fprintf(stderr, "Call to ibv_query_qp failed.\n");
      exit(1);
    }
    
    // TODO: SETTING PORT NUM TO 1 IN THIS CALL IS POTENTIALLY UNSAFE
    // THIS CAN BE FIXED IN THE CALL TO IBV_MODIFY_QP
    /* get the LID */
    struct ibv_port_attr attr2;
    if (_real_ibv_query_port(internal_qp->real_qp->context, internal_qp->port_num, &attr2))
    {
      fprintf(stderr, "Call to ibv_query_port failed.\n");
      exit(1);
    }

    /* generate the new PSN */
    srand48(getpid() * time(NULL));
    uint32_t psn = lrand48() & 0xffffff;

    internal_qp->current_id.qpn = internal_qp->real_qp->qp_num;
    internal_qp->current_id.lid = attr2.lid;
    internal_qp->current_id.psn = psn;
    /* end code to populate the ID */
  }
  /* end code to re-create the queue pairs */
//  sleep(60);

}

//TODO: This is just debug code
void print_attr_mask(int attr_mask)
{
  if (attr_mask & IBV_QP_STATE)
    PDEBUG("IBV_QP_STATE\n");
  if (attr_mask & IBV_QP_CUR_STATE)
    PDEBUG("IBV_QP_CUR_STATE\n");
  if (attr_mask & IBV_QP_EN_SQD_ASYNC_NOTIFY)
    PDEBUG("IBV_QP_EN_SQD_ASYNC_NOTIFY\n");
  if (attr_mask & IBV_QP_ACCESS_FLAGS)
    PDEBUG("IBV_QP_ACCESS_FLAGS\n");
  if (attr_mask & IBV_QP_PKEY_INDEX)
    PDEBUG("IBV_QP_PKEY_INDEX\n");
  if (attr_mask & IBV_QP_PORT)
    PDEBUG("IBV_QP_PORT\n");
  if (attr_mask & IBV_QP_QKEY)
    PDEBUG("IBV_QP_QKEY\n");
  if (attr_mask & IBV_QP_AV)
    PDEBUG("IBV_QP_AV\n");
  if (attr_mask & IBV_QP_PATH_MTU)
    PDEBUG("IBV_QP_PATH_MTU\n");
  if (attr_mask & IBV_QP_TIMEOUT)
    PDEBUG("IBV_QP_TIMEOUT\n");
  if (attr_mask & IBV_QP_RETRY_CNT)
    PDEBUG("IBV_QP_RETRY_CNT\n");
  if (attr_mask & IBV_QP_RNR_RETRY)
    PDEBUG("IBV_QP_RNR_RETRY\n");
  if (attr_mask & IBV_QP_RQ_PSN)
    printf("IBV_QP_RQ_PSN\n");
  if (attr_mask & IBV_QP_SQ_PSN)
    printf("IBV_QP_SQ_PSN\n");
  if (attr_mask & IBV_QP_DEST_QPN)
    printf("IBV_DEST_QPN\n");
  if (attr_mask & IBV_QP_MAX_QP_RD_ATOMIC)
    printf("IBV_QP_MAX_QP_RD_ATOMIC\n");
  if (attr_mask & IBV_QP_ALT_PATH)
    printf("IBV_QP_ALT_PATH\n");
  if (attr_mask & IBV_QP_MIN_RNR_TIMER)
    printf("IBV_QP_MIN_RNR_TIMER\n");
  if (attr_mask & IBV_QP_MAX_DEST_RD_ATOMIC)
    printf("IBV_QP_MAX_DEST_RD_ATOMIC\n");
  if (attr_mask & IBV_QP_PATH_MIG_STATE)
    printf("IBV_QP_PATH_MIG_STATE\n");
  if (attr_mask & IBV_QP_CAP)
    printf("IBV_QP_CAP\n");
}
void post_restart2(void)
{
  PDEBUG("in post_restart2\n");
  struct list_elem * e;
  for (e = list_begin(&srq_list); e != list_end(&srq_list); e = list_next(e))
  {
    _uninstall_post_srq_recv_trampoline();
    struct internal_ibv_srq * internal_srq = list_entry(e, struct internal_ibv_srq, elem);
    struct list_elem * w;
    uint32_t i;
    for (i = 0; i < internal_srq->recv_count; i++) {
      w = list_pop_front(&internal_srq->post_srq_recv_log);
      struct ibv_post_srq_recv_log * log = list_entry(w, struct ibv_post_srq_recv_log, elem);
      free(log);
    }
   
    PDEBUG("About to repost recv for srq.\n");
    for (w = list_begin(&internal_srq->post_srq_recv_log);
         w != list_end(&internal_srq->post_srq_recv_log);
         w = list_next(w))
    {
      struct ibv_post_srq_recv_log * log = list_entry(w, struct ibv_post_srq_recv_log, elem);
      struct ibv_recv_wr * copy_wr = copy_recv_wr(&log->wr);
      update_lkey_recv(copy_wr);

      struct ibv_recv_wr * bad_wr;
      int rslt = ibv_post_srq_recv(internal_srq->real_srq, copy_wr, &bad_wr);
      if (rslt) {
        fprintf(stderr, "Call to ibv_post_srq_recv failed.\n");
        fprintf(stderr, "error number is %d\n", errno);
	exit(1);
      }
      delete_recv_wr(copy_wr);
    }
    _install_post_srq_recv_trampoline();
    for (w = list_rbegin(&internal_srq->modify_srq_log);
	 w != list_rend(&internal_srq->modify_srq_log);
	 w = list_prev(w))
    {
      struct ibv_modify_srq_log * mod = list_entry(w, struct ibv_modify_srq_log, elem);
      struct ibv_srq_attr attr = mod->attr;
      
      if (mod->attr_mask & IBV_SRQ_MAX_WR){
	if(_real_ibv_modify_srq(internal_srq->real_srq, &attr, IBV_SRQ_MAX_WR)){
	  fprintf(stderr, "Error: Cound not modify srq properly.\n");
//	  exit(1);
	}
	break;
      }
    }
  }
  for (e = list_begin(&qp_list); e != list_end(&qp_list); e = list_next(e))
  {
    struct internal_ibv_qp * internal_qp = list_entry(e, struct internal_ibv_qp, elem);

    struct list_elem * w;
    for (w = list_begin(&internal_qp->modify_qp_log);
         w != list_end(&internal_qp->modify_qp_log);
         w = list_next(w))
    {
      struct ibv_modify_qp_log * mod = list_entry(w, struct ibv_modify_qp_log, elem);

      struct ibv_qp_attr attr = mod->attr;

      if (mod->attr_mask & IBV_QP_DEST_QPN)
      {
        attr.dest_qp_num = internal_qp->current_remote.qpn;
      }

      if (mod->attr_mask & IBV_QP_SQ_PSN)
      {
        attr.sq_psn = internal_qp->current_id.psn;
      }

      if (mod->attr_mask & IBV_QP_RQ_PSN)
      {
        attr.rq_psn = internal_qp->current_remote.psn;
      }

      if (mod->attr_mask & IBV_QP_AV)
      {
        attr.ah_attr.dlid = internal_qp->current_remote.lid;
      }

//      print_attr_mask(mod->attr_mask);

      PDEBUG("About to set X to true.\n");
      if (_real_ibv_modify_qp(internal_qp->real_qp, &attr, mod->attr_mask))
      {
	fprintf(stderr, "%d %d %d %d\n", attr.qp_state, attr.qp_access_flags, attr.pkey_index, attr.port_num);
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

  PDEBUG("before repost\n");
  for (e = list_begin(&qp_list); e != list_end(&qp_list); e = list_next(e))
  {
    _uninstall_post_recv_trampoline();
    struct internal_ibv_qp * internal_qp = list_entry(e, struct internal_ibv_qp, elem);
    struct list_elem * w;
    for (w = list_begin(&internal_qp->post_recv_log);
         w != list_end(&internal_qp->post_recv_log);
         w = list_next(w))
    {
      struct ibv_post_recv_log * log = list_entry(w, struct ibv_post_recv_log, elem);
      struct ibv_recv_wr * copy_wr = copy_recv_wr(&log->wr);
      update_lkey_recv(copy_wr);
      assert(copy_wr->next == NULL);

      struct ibv_recv_wr * bad_wr;
    //  PDEBUG("About to repost recv.\n");
      int rslt = ibv_post_recv(internal_qp->real_qp, copy_wr, &bad_wr);
      delete_recv_wr(copy_wr);
    }
    _install_post_recv_trampoline();
  }
  for (e = list_begin(&qp_list); e != list_end(&qp_list); e = list_next(e))
  {
    _uninstall_post_send_trampoline();
    struct internal_ibv_qp * internal_qp = list_entry(e, struct internal_ibv_qp, elem);
    struct list_elem * w;
    for (w = list_begin(&internal_qp->post_send_log);
         w != list_end(&internal_qp->post_send_log);
         w = list_next(w))
    {
      struct ibv_post_send_log * log = list_entry(w, struct ibv_post_send_log, elem);
      PDEBUG("log->magic : %x\n", log->magic);
      assert(log->magic == SEND_MAGIC);
      assert(&log->wr != NULL);
      struct ibv_send_wr * copy_wr = copy_send_wr(&log->wr);
      update_lkey_send(copy_wr);
      update_rkey_send(copy_wr);

      struct ibv_send_wr * bad_wr;
      PDEBUG("About to repost send.\n");
      int rslt = ibv_post_send(internal_qp->real_qp, copy_wr, &bad_wr);

      delete_send_wr(copy_wr);
    }
    _install_post_send_trampoline();
  }
  PDEBUG("after repost\n");
}

//! This performs the work of the _get_device_list_wrapper
/*!
  This function will open the real device list, store into _dev_list
  and then copy the list, returning an image of the copy to the user
  */
struct ibv_device ** _get_device_list(int * num_devices) {
  _dev_list = _real_ibv_get_device_list(&_dmtcp_num_devices);
  struct ibv_device ** user_list =  0;

  if (num_devices) {
    *num_devices = _dmtcp_num_devices;
  }

  user_list = calloc(_dmtcp_num_devices + 1, sizeof(struct ibv_device *));

  if (!user_list) {
    fprintf(stderr, "Error: Could not allocate memory for _get_device_list.\n");
    exit(1);
  }

  for (int i = 0; i < _dmtcp_num_devices; i++) {
    struct internal_ibv_dev * dev = (struct internal_ibv_dev *) malloc(sizeof(struct internal_ibv_dev));

    if (!dev) {
      fprintf(stderr, "Error: Could not allocate memory for _get_device_list.\n");
      exit(1);
    }

    memcpy(&dev->user_dev, _dev_list[i], sizeof(struct ibv_device));
    dev->real_dev = _dev_list[i];
    user_list[i] = &dev->user_dev;
  }

  return user_list;
}

const char * _get_device_name(struct ibv_device * device)
{
  return _real_ibv_get_device_name(ibv_device_to_internal(device)->real_dev);
}

//TODO: I think the GUID could change and need to be translated
uint64_t _get_device_guid(struct ibv_device * dev)
{
  struct internal_ibv_dev * internal_dev = ibv_device_to_internal(dev);

  return _real_ibv_get_device_guid(internal_dev->real_dev);
}

struct ibv_comp_channel * _create_comp_channel(struct ibv_context * ctx) {
  struct internal_ibv_ctx * internal_ctx = ibv_ctx_to_internal(ctx);
  struct internal_ibv_comp_channel * internal_comp = malloc(sizeof(struct internal_ibv_comp_channel));

  if (!internal_comp) {
    fprintf(stderr, "Error: Could not alloc memory for comp channel\n");
    exit(1);
  }

  internal_comp->real_channel = _real_ibv_create_comp_channel(internal_ctx->real_ctx);
  if (!internal_comp->real_channel) {
    fprintf(stderr, "Channel could not be created.");
    exit(1);
  }
  memcpy(&internal_comp->user_channel, internal_comp->real_channel, sizeof(struct ibv_comp_channel));

  internal_comp->user_channel.context = ctx;

  list_push_front(&comp_list, &internal_comp->elem);

  internal_comp->recreate_channel = false;
  return &internal_comp->user_channel;
}

int _destroy_comp_channel(struct ibv_comp_channel * channel)
{
  struct internal_ibv_comp_channel * internal_comp = ibv_comp_to_internal(channel);

  int rslt = _real_ibv_destroy_comp_channel(internal_comp->real_channel);

  if (rslt) {
    list_remove(&internal_comp->elem);
    free(channel);
  }

  return rslt;
}

int _get_cq_event(struct ibv_comp_channel * channel, struct ibv_cq ** cq, void ** cq_context)
{
  struct internal_ibv_comp_channel * internal_channel = ibv_comp_to_internal(channel);

  int rslt;
  if (internal_channel->recreate_channel == true) {
    struct internal_ibv_ctx * internal_ctx = ibv_ctx_to_internal(internal_channel->user_channel.context);
    internal_channel->real_channel = _real_ibv_create_comp_channel(internal_ctx->real_ctx);
    rslt = _real_ibv_get_cq_event(internal_channel->real_channel, cq, cq_context);
    internal_channel->recreate_channel = false;
  } else {
    rslt = _real_ibv_get_cq_event(internal_channel->real_channel, cq, cq_context);
  }

  struct internal_ibv_cq * internal_cq = get_cq_from_pointer(*cq);

  *cq = &internal_cq->user_cq;
  *cq_context = internal_cq->user_cq.context;

  return rslt;
}

int _get_async_event(struct ibv_context * ctx, struct ibv_async_event * event)
{
  struct internal_ibv_ctx * internal_ctx = ibv_ctx_to_internal(ctx);

  int rslt = _real_ibv_get_async_event(internal_ctx->real_ctx, event);

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
    internal_qp = get_qp_from_pointer(event->element.qp);
    event->element.qp = &internal_qp->user_qp;
    break;
    /* CQ events */
  case IBV_EVENT_CQ_ERR:
    internal_cq = get_cq_from_pointer(event->element.cq);
    event->element.cq = &internal_cq->user_cq;
    break;
    /*SRQ events */
  case IBV_EVENT_SRQ_ERR:
  case IBV_EVENT_SRQ_LIMIT_REACHED:
    internal_srq = get_srq_from_pointer(event->element.srq);
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
    event->element.qp = internal_qp->real_qp;
    break;
    /* CQ events */
  case IBV_EVENT_CQ_ERR:
    internal_cq = ibv_cq_to_internal(event->element.cq);
    event->element.cq = internal_cq->real_cq;
    break;
    /*SRQ events */
  case IBV_EVENT_SRQ_ERR:
  case IBV_EVENT_SRQ_LIMIT_REACHED:
    internal_srq = ibv_srq_to_internal(event->element.srq);
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

  _real_ibv_ack_async_event(event);
}

//! This performs the work of freeing the device list
/*! This function will free the real device list and then
 * delete the members of the device list copy
 */
void _free_device_list(struct ibv_device ** list)
{
  _real_ibv_free_device_list(_dev_list);
  int i;

/*  for (i = 0; i < _dmtcp_num_devices; i++) {
    free(list[i]);
  }
*/
  free(list);
}

struct ibv_context * _open_device(struct ibv_device * device) {
  struct internal_ibv_ctx * ctx = malloc(sizeof(struct internal_ibv_ctx));

  if (!ctx) {
    fprintf(stderr, "Couldn't allocate memory for _open_device!\n");
    exit(1);
  }

  ctx->real_ctx = _real_ibv_open_device(ibv_device_to_internal(device)->real_dev);

  if (ctx->real_ctx == NULL) {
    fprintf(stderr, "Could not allocate the real ctx.\n");
    exit(1);
  }

  memcpy(&ctx->user_ctx, ctx->real_ctx, sizeof(struct ibv_context));

  ctx->user_ctx.device = device;

  list_push_back(&ctx_list, &ctx->elem);

  /* setup the trampolines */
  _dmtcp_setup_ibv_trampolines(ctx->real_ctx->ops.post_recv,
			       ctx->real_ctx->ops.post_srq_recv,
                               ctx->real_ctx->ops.post_send,
                               ctx->real_ctx->ops.poll_cq,
                               ctx->real_ctx->ops.req_notify_cq);
  return &ctx->user_ctx;
}

int _query_device(struct ibv_context *context, struct ibv_device_attr *device_attr)
{
  struct internal_ibv_ctx *internal_ctx = ibv_ctx_to_internal(context);

  return _real_ibv_query_device(internal_ctx->real_ctx,device_attr);
}


int _query_port(struct ibv_context *context, uint8_t port_num, struct ibv_port_attr *port_attr)
{
  struct internal_ibv_ctx *internal_ctx = ibv_ctx_to_internal(context);

  return _real_ibv_query_port(internal_ctx->real_ctx,port_num, port_attr);
}

int _query_pkey(struct ibv_context *context, uint8_t port_num,  int index, uint16_t *pkey)
{
  struct internal_ibv_ctx *internal_ctx = ibv_ctx_to_internal(context);

  return _real_ibv_query_pkey(internal_ctx->real_ctx,port_num, index, pkey);
}

int _query_gid(struct ibv_context *context, uint8_t port_num, int index, union ibv_gid *gid)
{
  struct internal_ibv_ctx *internal_ctx = ibv_ctx_to_internal(context);

  return _real_ibv_query_gid(internal_ctx->real_ctx,port_num, index, gid);
}

int _close_device(struct ibv_context * ctx)
{
  struct internal_ibv_ctx * internal_ctx = ibv_ctx_to_internal(ctx);
  int rslt = _real_ibv_close_device(internal_ctx->real_ctx);
  list_remove(&internal_ctx->elem);
  free(internal_ctx);

  return rslt;
}

struct ibv_pd * _alloc_pd(struct ibv_context * context) {
  struct internal_ibv_ctx * internal_ctx = ibv_ctx_to_internal(context);

  struct internal_ibv_pd * pd = malloc(sizeof(struct internal_ibv_pd));

  if (!pd) {
    PDEBUG("Error: I could not create a new PD.\n");
    fprintf(stderr, "Error: I cannot allocate memory for _alloc_pd\n");
    exit(1);
  }

  pd->real_pd = _real_ibv_alloc_pd(internal_ctx->real_ctx);

  if (!pd->real_pd) {
    return pd->real_pd;
  }

  memcpy(&pd->user_pd, pd->real_pd, sizeof(struct ibv_pd));
  pd->user_pd.context = context;

  list_push_back(&pd_list, &pd->elem);

  return &pd->user_pd;
}

int _dealloc_pd(struct ibv_pd * pd)
{
  struct internal_ibv_pd * internal_pd = ibv_pd_to_internal(pd);
  int rslt = _real_ibv_dealloc_pd(internal_pd->real_pd);
  list_remove(&internal_pd->elem);
  free(internal_pd);

  return rslt;
}

struct ibv_mr * _reg_mr(struct ibv_pd * pd, void * addr, size_t length, int flag) {
  struct internal_ibv_pd * internal_pd = ibv_pd_to_internal(pd);
  struct internal_ibv_mr * internal_mr = malloc(sizeof(struct internal_ibv_mr));

  if (!internal_mr) {
    fprintf(stderr, "Error: Could not allocate memory for _reg_mr\n");
    exit(1);
  }

  /*  
  *  We need to make sure that memery regions created
  *  before checkpointing and after restarting will not
  *  have the same lkey or rkey. Same trick for qps. 
  */
  bool dup_flag = false;
  while(1){
    internal_mr->real_mr = _real_ibv_reg_mr(internal_pd->real_pd, addr, length, flag);
    if (!internal_mr->real_mr) {
      fprintf(stderr, "Error: Could not register mr.\n");
      fprintf(stderr, "errno is %d.\n", errno);
      exit(1);
    }
    
    struct list_elem * e;
    for (e = list_begin(&mr_list); e != list_end(&mr_list); e = list_next(e))
    {
      struct internal_ibv_mr * mr = list_entry(e, struct internal_ibv_mr, elem);
      if ((mr->user_mr.lkey == internal_mr->real_mr->lkey) || (mr->user_mr.rkey == internal_mr->real_mr->rkey)) {
	dup_flag = true;
        fprintf(stderr, "Warning: duplicate lkey/rkey is genereated, will create a new mr.\n");
        _real_ibv_dereg_mr(internal_mr->real_mr);
      }
    }
    if (!dup_flag){
      break;
    }
  }
  
  internal_mr->flags = flag;

  memcpy(&internal_mr->user_mr, internal_mr->real_mr, sizeof(struct ibv_mr));
//  internal_mr->user_mr.context = ibv_ctx_to_internal(internal_mr->user_mr.context)->real_ctx;
  internal_mr->user_mr.context = internal_pd->user_pd.context;
  internal_mr->user_mr.pd = &internal_pd->user_pd;

  struct ibv_rkey_pair * pair = malloc(sizeof(struct ibv_rkey_pair));
  if (!pair){
    fprintf(stderr, "Error: Could not allocate memory for ibv_rkey_pair.\n");
    exit(1);
  }
  pair->orig_rkey = internal_mr->real_mr->rkey;
  pair->new_rkey = 0;
  list_push_back(&rkey_list, &pair->elem);
  list_push_back(&mr_list, &internal_mr->elem);

  return &internal_mr->user_mr;
}

int _dereg_mr(struct ibv_mr * mr)
{
  struct internal_ibv_mr * internal_mr = ibv_mr_to_internal(mr);
  struct list_elem *e;
  for (e = list_begin(&rkey_list); e != list_end(&rkey_list); e = list_next(e)) {
    struct ibv_rkey_pair * pair = list_entry(e, struct ibv_rkey_pair, elem);
    if (pair->orig_rkey == internal_mr->user_mr.rkey) {
      list_remove(&pair->elem);
      free(pair);
      break;
    }
  }
  int rslt = _real_ibv_dereg_mr(internal_mr->real_mr);

  list_remove(&internal_mr->elem);
  free(internal_mr);

  return rslt;
}

int _req_notify_cq(struct ibv_cq * cq, int solicited_only)
{
  struct internal_ibv_cq * internal_cq = ibv_cq_to_internal(cq);

  int rslt = ibv_req_notify_cq(internal_cq->real_cq, solicited_only);

  if (rslt == 0) {
    struct ibv_req_notify_cq_log * log = malloc(sizeof(struct ibv_req_notify_cq_log));

    if (!log) {
      fprintf(stderr, "Error: Could not allocate memory for _req_notify_cq.\n");
      exit(1);
    }

    log->solicited_only = solicited_only;
    list_push_back(&internal_cq->req_notify_log, &log->elem);
  }

  return rslt;
}

struct ibv_cq * _create_cq(struct ibv_context * context, int cqe, void * cq_context,
                           struct ibv_comp_channel * channel, int comp_vector) {
  struct internal_ibv_ctx * internal_ctx = ibv_ctx_to_internal(context);
  struct internal_ibv_cq * internal_cq = malloc(sizeof(struct internal_ibv_cq));
  struct ibv_comp_channel * real_channel;
  if (channel) {
    real_channel = ibv_comp_to_internal(channel)->real_channel;
  } else {
    real_channel = NULL;
  }

  if (!internal_cq) {
    fprintf(stderr,"Error: could not allocate memory for _create_cq\n");
    exit(1);
  }

  /* set up the lists */
  list_init(&internal_cq->wc_queue);
  list_init(&internal_cq->req_notify_log);

  internal_cq->real_cq = _real_ibv_create_cq(internal_ctx->real_ctx, cqe, cq_context,
                         real_channel, comp_vector);

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

  int rslt = _real_ibv_resize_cq(cq, cqe);

  if (!rslt) {
    cq->cqe = cqe;
  }

  return rslt;
}

int _destroy_cq(struct ibv_cq * cq)
{
  struct internal_ibv_cq * internal_cq = ibv_cq_to_internal(cq);

  int rslt = _real_ibv_destroy_cq(internal_cq->real_cq);

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
    struct ibv_req_notify_cq_log *log = list_entry(e, struct ibv_req_notify_cq_log, elem);
    e = list_next(e);
    list_remove(w);
    free(log);
  }
  /* end destroying the lists */

  list_remove(&internal_cq->elem);
  free(internal_cq);

  return rslt;
}

struct ibv_qp * _create_qp(struct ibv_pd * pd, struct ibv_qp_init_attr * qp_init_attr) {
  struct internal_ibv_pd * internal_pd = ibv_pd_to_internal(pd);
  struct internal_ibv_qp * internal_qp = malloc(sizeof(struct internal_ibv_qp));

  if ( internal_qp == NULL ) {
    fprintf(stderr, "Error: I cannot allocate memory for _create_qp\n");
    exit(1);
  }

  /* fix up the qp_init_attr here */
  struct ibv_qp_init_attr attr = *qp_init_attr;
  attr.recv_cq = ibv_cq_to_internal(qp_init_attr->recv_cq)->real_cq;
  attr.send_cq = ibv_cq_to_internal(qp_init_attr->send_cq)->real_cq;
  if ( attr.srq )
    attr.srq = ibv_srq_to_internal(qp_init_attr->srq)->real_srq;

  bool dup_flag = false;
  while(1){
    internal_qp->real_qp = _real_ibv_create_qp(internal_pd->real_pd, &attr);
    if( internal_qp->real_qp == NULL ) {
      PDEBUG("Error: _real_ibv_create_qp fail\n");
      free( internal_qp);
      return NULL;
    }
    struct list_elem * e;
    for (e = list_begin(&qp_list); e != list_end(&qp_list); e = list_next(e))
    {
      struct internal_ibv_qp * qp = list_entry(e, struct internal_ibv_qp, elem);
      if ((qp->user_qp.qp_num == internal_qp->real_qp->qp_num)) {
	dup_flag = true;
        fprintf(stderr, "Warning: duplicate qp_num 0x%06x is genereated, will create a new qp.\n", internal_qp->real_qp->qp_num);
	_real_ibv_destroy_qp(internal_qp->real_qp);
      }
    }
    if (!dup_flag){
      break;
    }
  }

  memcpy(&internal_qp->user_qp, internal_qp->real_qp, sizeof(struct ibv_qp));
  /* fix up the user_qp*/
  /* loop to find the right context the "dumb way" */
  /* This should probably just take the contxt from pd */
  struct list_elem *e;
  struct internal_ibv_ctx * rslt = NULL;
  for (e = list_begin (&ctx_list); e != list_end (&ctx_list); e = list_next (e)) {
    struct internal_ibv_ctx * ctx = list_entry (e, struct internal_ibv_ctx, elem);

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
  /* code to populate the ID */
  /* get the port num */
  struct ibv_qp_attr attr3;
  struct ibv_qp_init_attr init_atty;
  if (_real_ibv_query_qp(internal_qp->real_qp, &attr3, IBV_QP_PORT, &init_atty)) {
    fprintf(stderr, "Call to ibv_query_qp failed.\n");
    exit(1);
  }
  /* get the LID */
  struct ibv_port_attr attr2;
  int rslt2;
  if ((rslt2 = _real_ibv_query_port(internal_qp->real_qp->context, 1, &attr2))) {
    fprintf(stderr, "Call to ibv_query_port failed %d.\n", rslt2);
    exit(1);
  }
  internal_qp->original_id.lid = attr2.lid;
  internal_qp->original_id.qpn = internal_qp->real_qp->qp_num;
  internal_qp->port_num = 1;

  list_push_back(&qp_list, &internal_qp->elem);
  return &internal_qp->user_qp;
}

int _destroy_qp(struct ibv_qp * qp)
{
  struct internal_ibv_qp * internal_qp = ibv_qp_to_internal(qp);

  int rslt = _real_ibv_destroy_qp(internal_qp->real_qp);

  struct list_elem * e = list_begin(&internal_qp->modify_qp_log);
  while(e != list_end(&internal_qp->modify_qp_log)) {
    struct list_elem * w = e;
    struct ibv_modify_qp_log * log = list_entry (e, struct ibv_modify_qp_log, elem);
    e = list_next(e);
    list_remove(w);
    free(log);
  }

  e = list_begin(&internal_qp->post_recv_log);
  while (e != list_end(&internal_qp->post_recv_log)) {
    struct list_elem * w = e;
    struct ibv_post_recv_log *log = list_entry (e, struct ibv_post_recv_log, elem);
    e = list_next(e);
    list_remove(w);
    free(log);
  }

  e = list_begin(&internal_qp->post_send_log);
  while (e != list_end(&internal_qp->post_send_log)) {
    struct list_elem * w = e;
    struct ibv_post_send_log *log = list_entry (e, struct ibv_post_send_log, elem);
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
  struct ibv_modify_qp_log * log = malloc(sizeof(struct ibv_modify_qp_log));

  if (!log) {
    fprintf(stderr, "Error: Couldn't allocate memory for log.\n");
    exit(1);
  }

  log->attr = *attr;
  log->attr_mask = attr_mask;
  list_push_back(&internal_qp->modify_qp_log, &log->elem);

  int rslt = _real_ibv_modify_qp(internal_qp->real_qp, attr, attr_mask);

  internal_qp->user_qp.state = internal_qp->real_qp->state;

  if (attr_mask & IBV_QP_DEST_QPN) {
    internal_qp->remote_id.qpn = attr->dest_qp_num;
  }

  if (attr_mask & IBV_QP_SQ_PSN) {
    internal_qp->original_id.psn = attr->sq_psn & 0xffffff;
  }

  if (attr_mask & IBV_QP_RQ_PSN) {
    internal_qp->remote_id.psn = attr->rq_psn & 0xffffff;
  }

  if (attr_mask & IBV_QP_AV) {
    internal_qp->remote_id.lid = attr->ah_attr.dlid;
  }
  
  if (attr_mask & IBV_QP_PORT) {
    struct ibv_port_attr attr2;
    int rslt2;
    if ((rslt2 = _real_ibv_query_port(internal_qp->real_qp->context, attr->port_num, &attr2))) {
      fprintf(stderr, "Call to ibv_query_port failed %d\n", rslt2);
      exit(1);
    }
    internal_qp->original_id.lid = attr2.lid;
    internal_qp->port_num = attr->port_num;
  }

  return rslt;
}

int _query_qp(struct ibv_qp * qp, struct ibv_qp_attr * attr, int attr_mask,
              struct ibv_qp_init_attr * init_attr)
{
  struct internal_ibv_qp * internal_qp = ibv_qp_to_internal(qp);

  int rslt = _real_ibv_query_qp(internal_qp->real_qp, attr, attr_mask, init_attr);

  return rslt;
}

struct ibv_srq * _create_srq(struct ibv_pd * pd, struct ibv_srq_init_attr * srq_init_attr) {
  struct internal_ibv_pd * internal_pd = ibv_pd_to_internal(pd);
  struct internal_ibv_srq * internal_srq = malloc(sizeof(struct internal_ibv_srq));

  if ( internal_srq == NULL ) {
    fprintf(stderr, "Error: I cannot allocate memory for _create_srq\n");
    exit(1);
  }

  internal_srq->real_srq = _real_ibv_create_srq(internal_pd->real_pd, srq_init_attr);

  if( internal_srq->real_srq == NULL ) {
    PDEBUG("Error: _real_ibv_create_srq fail\n");
    free(internal_srq);
    return NULL;
  }

  internal_srq->recv_count = 0;
  memcpy(&internal_srq->user_srq, internal_srq->real_srq, sizeof(struct ibv_srq));
  /* fix up the user_srq*/
  /* loop to find the right context the "dumb way" */
  /* This should probably just take the contxt from pd */
  struct list_elem *e;
  struct internal_ibv_ctx * rslt = NULL;
  for (e = list_begin (&ctx_list); e != list_end (&ctx_list); e = list_next (e)) {
    struct internal_ibv_ctx * ctx = list_entry (e, struct internal_ibv_ctx, elem);

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

  int rslt = _real_ibv_destroy_srq(internal_srq->real_srq);

  struct list_elem * e;
  e = list_begin(&internal_srq->modify_srq_log);
  while(e != list_end(&internal_srq->modify_srq_log)) {
    struct list_elem * w = e;
    struct ibv_modify_srq_log * log = list_entry (e, struct ibv_modify_srq_log, elem);
    e = list_next(e);
    list_remove(w);
    free(log);
  }

  e = list_begin(&internal_srq->post_srq_recv_log);
  while(e != list_end(&internal_srq->post_srq_recv_log)) {
    struct list_elem * w = e;
    struct ibv_post_srq_recv_log * log = list_entry(e, struct ibv_post_srq_recv_log, elem);
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
  struct ibv_modify_srq_log * log = malloc(sizeof(struct ibv_modify_srq_log));

  if (!log) {
    fprintf(stderr, "Error: Couldn't allocate memory for log.\n");
    exit(1);
  }

  log->attr = *attr;
  log->attr_mask = attr_mask;

  int rslt = _real_ibv_modify_srq(internal_srq->real_srq, attr, attr_mask);
  if (!rslt) {
    list_push_back(&internal_srq->modify_srq_log, &log->elem);
  }
  return rslt;
}

int _query_srq(struct ibv_srq * srq, struct ibv_srq_attr * srq_attr)
{
  return _real_ibv_query_srq(ibv_srq_to_internal(srq)->real_srq, srq_attr);
}

int _post_recv(struct ibv_qp * qp, struct ibv_recv_wr * wr, struct ibv_recv_wr ** bad_wr)
{
  /* this makes sure the code didn't "double trampoline" */
  static volatile int z = 0;
  assert(z == 0);

  struct internal_ibv_qp * internal_qp = ibv_qp_to_internal(qp);

  //TODO: Technically this does multiple loops, but, the code is cleaner
  struct ibv_recv_wr * copy_wr = copy_recv_wr(wr);
  update_lkey_recv(copy_wr);

  _uninstall_post_recv_trampoline();
  z = 1;
  int rslt = ibv_post_recv(internal_qp->real_qp, copy_wr, bad_wr);
  z = 0;
  _install_post_recv_trampoline();


  delete_recv_wr(copy_wr);

  struct ibv_recv_wr * copy_wr1 = copy_recv_wr(wr);
  while (copy_wr1) {
    struct ibv_post_recv_log * log = malloc(sizeof(struct ibv_post_recv_log));

    if (!log) {
      fprintf(stderr, "Error: could not allocate memory for log.\n");
      exit(1);
    }    
    log->wr = *copy_wr1;
    log->wr.next = NULL;

    list_push_back(&internal_qp->post_recv_log, &log->elem);
    copy_wr1 = copy_wr1->next;
  }

  return rslt;
}

int _post_srq_recv(struct ibv_srq * srq, struct ibv_recv_wr * wr, struct ibv_recv_wr ** bad_wr)
{
  /* this makes sure the code didn't "double trampoline" */
  static volatile int z = 0;
  assert(z == 0);

  struct internal_ibv_srq * internal_srq = ibv_srq_to_internal(srq);

  //TODO: Technically this does multiple loops, but, the code is cleaner
  struct ibv_recv_wr * copy_wr = copy_recv_wr(wr);
  
  update_lkey_recv(copy_wr);

  _uninstall_post_srq_recv_trampoline();
  z = 1;
  int rslt = ibv_post_srq_recv(internal_srq->real_srq, copy_wr, bad_wr);
  if (rslt) {
    fprintf(stderr, "Error: srq_post_recv failed!\n");
    exit(1);
  }
  z = 0;
  _install_post_srq_recv_trampoline();

  delete_recv_wr(copy_wr);

  struct ibv_recv_wr * copy_wr1 = copy_recv_wr(wr);
  while (copy_wr1) {
    struct ibv_post_srq_recv_log * log = malloc(sizeof(struct ibv_post_srq_recv_log));

    if (!log) {
      fprintf(stderr, "Error: could not allocate memory for log.\n");
      exit(1);
    }    
    log->wr = *copy_wr1;
    log->wr.next = NULL;

    list_push_back(&internal_srq->post_srq_recv_log, &log->elem);
    copy_wr1 = copy_wr1->next;
  }

  return rslt;
}

int _post_send(struct ibv_qp * qp, struct ibv_send_wr * wr, struct ibv_send_wr ** bad_wr)
{
  /* this makes sure the code didn't "double trampoline" */
  static volatile int z = 0;
  assert(z == 0);
  struct internal_ibv_qp * internal_qp = ibv_qp_to_internal(qp);
  struct ibv_send_wr * copy_wr = copy_send_wr(wr);
  update_lkey_send(copy_wr);
  update_rkey_send(copy_wr);

  _uninstall_post_send_trampoline();
  z = 1;
  int rslt = ibv_post_send(internal_qp->real_qp, copy_wr, bad_wr);
  z = 0;
  _install_post_send_trampoline();

  delete_send_wr(copy_wr);

  struct ibv_send_wr *copy_wr1 = copy_send_wr(wr);
  while (copy_wr1) {
    struct ibv_post_send_log * log = malloc(sizeof(struct ibv_post_send_log));

    if (!log) {
      fprintf(stderr, "Error: could not allocate memory for log.\n");
      exit(1);
    }
    log->magic = SEND_MAGIC;
    log->wr = *copy_wr1;
    log->wr.next = NULL;

    list_push_back(&internal_qp->post_send_log, &log->elem);
    copy_wr1 = copy_wr1->next;
  }

  return rslt;
}

int _poll_cq(struct ibv_cq * cq, int num_entries, struct ibv_wc * wc)
{
  static volatile int z = 0;
  assert(z == 0);
  int rslt = 0;

  struct internal_ibv_cq * internal_cq = ibv_cq_to_internal(cq);

  int size = list_size(&internal_cq->wc_queue);

  if (size > 0) {
    struct list_elem * e = list_front(&internal_cq->wc_queue);
    for (int i = 0; (i < size) && (i < num_entries); i++) {
      PDEBUG("Polling completion from internal buffer\n");
      struct list_elem * w = e;
      wc[i] = list_entry(e, struct ibv_wc_wrapper, elem)->wc;
      e = list_next(e);
      list_remove(w);
      rslt++;
    }

    if (size < num_entries) {
      z = 1;
      _uninstall_poll_cq_trampoline();
      int ne = ibv_poll_cq(internal_cq->real_cq, num_entries - size, wc + size);
      if (ne < 0) {
	fprintf(stderr, "poll_cq() error!\n");
	exit(1);
      }
      z = 0;
      _install_poll_cq_trampoline();
      rslt += ne;
    }
  }
  else {
    z = 1;
    _uninstall_poll_cq_trampoline();
    rslt = ibv_poll_cq(internal_cq->real_cq, num_entries, wc);
      if (rslt < 0) {
	fprintf(stderr, "poll_cq() error!\n");
	exit(1);
      }
    _install_poll_cq_trampoline();
    z = 0;
  }

  for (int i = 0; i < rslt; i++) {
    struct internal_ibv_qp * internal_qp = qp_num_to_qp(&qp_list, wc[i].qp_num);
    if (i >= size) {
      enum ibv_wc_opcode opcode = wc[i].opcode;
      if (opcode & IBV_WC_RECV ||
          opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
	if (internal_qp->user_qp.srq) {
	  struct internal_ibv_srq * internal_srq = ibv_srq_to_internal(internal_qp->user_qp.srq);
	  internal_srq->recv_count++;
	}
	else {
          struct list_elem * e = list_pop_front(&internal_qp->post_recv_log);
          struct ibv_post_recv_log * log = list_entry(e, struct ibv_post_recv_log, elem);
          free(log);
	}
      } else if (opcode == IBV_WC_SEND ||
                 opcode == IBV_WC_RDMA_WRITE ||
                 opcode == IBV_WC_RDMA_READ ||
                 opcode == IBV_WC_COMP_SWAP ||
                 opcode == IBV_WC_FETCH_ADD) {
        struct list_elem * e = list_pop_front(&internal_qp->post_send_log);
        struct ibv_post_send_log * log = list_entry(e, struct ibv_post_send_log, elem);
        assert(log->magic == SEND_MAGIC);
        free(log);
      } else if (opcode == IBV_WC_BIND_MW) {
        fprintf(stderr, "Error: opcode %d specifies unsupported operation.\n", opcode);
        exit(1);
      } else {
        fprintf(stderr, "Unknown or invalid opcode.\n");
        exit(1);
      }
    }
  }

  return rslt;
}

void _ack_cq_events(struct ibv_cq * cq, unsigned int nevents)
{
  struct internal_ibv_cq * internal_cq = ibv_cq_to_internal(cq);

  return _real_ibv_ack_cq_events(internal_cq->real_cq, nevents);
}
