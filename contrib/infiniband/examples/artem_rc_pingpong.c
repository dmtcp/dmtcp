/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFbuf_free(&ctx->out_b)TWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <malloc.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <time.h>

#include "pingpong.h"

enum {
	PINGPONG_RECV_WRID = 1,
	PINGPONG_SEND_WRID = 2,
};

static int page_size;


#define PDEBUG(fmt, args ...) printf(fmt"\n", ##args );
//#define PDEBUG(fmt, args ... )

// -------------------- circle buffer ---------------------//
#define BSIZE 256
typedef unsigned int uint;

struct buffer{
  void *buf[BSIZE];
  uint size, s, e;
};

inline uint buf_inc(uint c, uint modulo){
  return (c + 1)% modulo;
}

inline int buf_add(struct buffer *b)
{
  unsigned int tmp = buf_inc(b->e, b->size);
  if( tmp != b->s ){
    b->e = tmp;
    return 0;
  }
  return -1;
}

inline int buf_del(struct buffer *b)
{
  if( b->e == b->s ){
    return -1;
  }
  b->s = buf_inc(b->s, b->size);
  return 0;
}

inline unsigned int buf_free(struct buffer *b)
{
  if( b->e >= b->s)
    return b->size - (b->e - b->s + 1);
  else
    return b->s - (b->e + 1);
}

inline uint buf_used(struct buffer *b)
{
  return b->size - buf_free(b);
}

// ------------------------ pingpong date -----------------------//

struct pcontext {
  // Infiniband-related data
	struct ibv_context	*context;
	struct ibv_comp_channel *channel;
	struct ibv_pd		*pd;
	struct ibv_mr		*in_mr[BSIZE], *out_mr[BSIZE];
	struct ibv_cq		*cq;
	struct ibv_qp		*qp;
  
  // internal transceiver & flow control data
  struct buffer in_b, out_b;
  int	size;
	uint received, received_ack;
	uint snd_posted, snd_finished,snd_received;
	uint counter, rem_counter;
};

struct pdest {
	int lid;
	int qpn;
	int psn;
};

static struct pcontext *init_ctx(struct ibv_device *ib_dev, int size, int port,
    uint use_events)
{
	struct pcontext *ctx;
	int i;

	ctx = malloc(sizeof *ctx);
	if (!ctx)
		return NULL;

  memset(ctx,0,sizeof *ctx);
	ctx->size     = size;

	ctx->context = ibv_open_device(ib_dev);
	if (!ctx->context) {
		fprintf(stderr, "Couldn't get context for %s\n",
			ibv_get_device_name(ib_dev));
		return NULL;
	}

  if( use_events ){
    ctx->channel = ibv_create_comp_channel(ctx->context);
    if (!ctx->channel) {
  		fprintf(stderr, "Couldn't create completion channel\n");
    	return NULL;
    }
      // Completion queue
    ctx->cq = ibv_create_cq(ctx->context, 2*BSIZE + 1, NULL,
				ctx->channel, 0);
    if (!ctx->cq) {
      fprintf(stderr, "Couldn't create CQ\n");
      return NULL;
    }
  }else{
      // Completion queue
    ctx->cq = ibv_create_cq(ctx->context, 100, NULL,NULL, 0);
    if (!ctx->cq) {
      fprintf(stderr, "Couldn't create CQ\n");
      return NULL;
    }
  }  
  
	ctx->pd = ibv_alloc_pd(ctx->context);
	if (!ctx->pd) {
		fprintf(stderr, "Couldn't allocate PD\n");
		return NULL;
	}

  // Init buffers
  ctx->in_b.size = BSIZE;
  for(i = 0; i < BSIZE ; i++){
    ctx->in_b.buf[i] = memalign(page_size, size);
	  if (!ctx->in_b.buf[i]) {
		  fprintf(stderr, "Couldn't allocate work buf.\n");
  		return NULL;
	  }
  	memset(ctx->in_b.buf[i], 0xff, size);
  	ctx->in_mr[i] = ibv_reg_mr(ctx->pd, ctx->in_b.buf[i], size, 
        IBV_ACCESS_LOCAL_WRITE);
  }

  ctx->out_b.size = BSIZE;
  for(i = 0; i < BSIZE ; i++){
    ctx->out_b.buf[i] = memalign(page_size, size);
	  if (!ctx->out_b.buf[i]) {
		  fprintf(stderr, "Couldn't allocate work buf.\n");
  		return NULL;
	  }
  	memset(ctx->out_b.buf[i], 0xff, size);
  	ctx->out_mr[i] = ibv_reg_mr(ctx->pd, ctx->out_b.buf[i], size, 
        IBV_ACCESS_LOCAL_WRITE);
  }


	{
		struct ibv_qp_init_attr attr = {
			.send_cq = ctx->cq,
			.recv_cq = ctx->cq,
			.cap     = {
				.max_send_wr  = 50, //BSIZE,
				.max_recv_wr  = BSIZE,
				.max_send_sge = 1,
				.max_recv_sge = 1
			},
			.qp_type = IBV_QPT_RC
		};

		ctx->qp = ibv_create_qp(ctx->pd, &attr);
		if (!ctx->qp)  {
			fprintf(stderr, "Couldn't create QP\n");
			return NULL;
		}
	}

	{
		struct ibv_qp_attr attr = {
			.qp_state        = IBV_QPS_INIT,
			.pkey_index      = 0,
			.port_num        = port,
			.qp_access_flags = 0
		};

		if (ibv_modify_qp(ctx->qp, &attr,
				  IBV_QP_STATE              |
				  IBV_QP_PKEY_INDEX         |
				  IBV_QP_PORT               |
				  IBV_QP_ACCESS_FLAGS)) {
			fprintf(stderr, "Failed to modify QP to INIT\n");
			return NULL;
		}
	}

	return ctx;
}

static int connect_ctx(struct pcontext *ctx, int port, int my_psn,
			  enum ibv_mtu mtu, int sl, struct pdest *dest)
{
	struct ibv_qp_attr attr = {
		.qp_state		= IBV_QPS_RTR,
		.path_mtu		= mtu,
		.dest_qp_num		= dest->qpn,
		.rq_psn			= dest->psn,
		.max_dest_rd_atomic	= 1,
		.min_rnr_timer		= 12,
		.ah_attr		= {
			.is_global	= 0,
			.dlid		= dest->lid,
			.sl		= sl,
			.src_path_bits	= 0,
			.port_num	= port
		}
	};
	if (ibv_modify_qp(ctx->qp, &attr,
			  IBV_QP_STATE              |
			  IBV_QP_AV                 |
			  IBV_QP_PATH_MTU           |
			  IBV_QP_DEST_QPN           |
			  IBV_QP_RQ_PSN             |
			  IBV_QP_MAX_DEST_RD_ATOMIC |
			  IBV_QP_MIN_RNR_TIMER)) {
		fprintf(stderr, "Failed to modify QP to RTR\n");
		return 1;
	}

	attr.qp_state	    = IBV_QPS_RTS;
	attr.timeout	    = 14;
	attr.retry_cnt	    = 7;
	attr.rnr_retry	    = 7;
	attr.sq_psn	    = my_psn;
	attr.max_rd_atomic  = 1;
	if (ibv_modify_qp(ctx->qp, &attr,
			  IBV_QP_STATE              |
			  IBV_QP_TIMEOUT            |
			  IBV_QP_RETRY_CNT          |
			  IBV_QP_RNR_RETRY          |
			  IBV_QP_SQ_PSN             |
			  IBV_QP_MAX_QP_RD_ATOMIC)) {
		fprintf(stderr, "Failed to modify QP to RTS\n");
		return 1;
	}

	return 0;
}

static struct pdest *client_exch_dest(const char *servername, int port,
						 const struct pdest *my_dest)
{
	struct addrinfo *res, *t;
	struct addrinfo hints = {
		.ai_family   = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM
	};
	char *service;
	char msg[sizeof "0000:000000:000000"];
	int n;
	int sockfd = -1;
	struct pdest *rem_dest = NULL;

	if (asprintf(&service, "%d", port) < 0)
		return NULL;

	n = getaddrinfo(servername, service, &hints, &res);

	if (n < 0) {
		fprintf(stderr, "%s for %s:%d\n", gai_strerror(n), servername, port);
		free(service);
		return NULL;
	}

	for (t = res; t; t = t->ai_next) {
		sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
		if (sockfd >= 0) {
			if (!connect(sockfd, t->ai_addr, t->ai_addrlen))
				break;
			close(sockfd);
			sockfd = -1;
		}
	}

	freeaddrinfo(res);
	free(service);

	if (sockfd < 0) {
		fprintf(stderr, "Couldn't connect to %s:%d\n", servername, port);
		return NULL;
	}

	sprintf(msg, "%04x:%06x:%06x", my_dest->lid, my_dest->qpn, my_dest->psn);
	if (write(sockfd, msg, sizeof msg) != sizeof msg) {
		fprintf(stderr, "Couldn't send local address\n");
		goto out;
	}

	if (read(sockfd, msg, sizeof msg) != sizeof msg) {
		perror("client read");
		fprintf(stderr, "Couldn't read remote address\n");
		goto out;
	}

	write(sockfd, "done", sizeof "done");

	rem_dest = malloc(sizeof *rem_dest);
	if (!rem_dest)
		goto out;

	sscanf(msg, "%x:%x:%x", &rem_dest->lid, &rem_dest->qpn, &rem_dest->psn);

out:
	close(sockfd);
	return rem_dest;
}

static struct pdest *server_exch_dest(struct pcontext *ctx,
						 int ib_port, enum ibv_mtu mtu,
						 int port, int sl, const struct pdest *my_dest)
{
	struct addrinfo *res, *t;
	struct addrinfo hints = {
		.ai_flags    = AI_PASSIVE,
		.ai_family   = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM
	};
	char *service;
	char msg[sizeof "0000:000000:000000"];
	int n;
	int sockfd = -1, connfd;
	struct pdest *rem_dest = NULL;

	if (asprintf(&service, "%d", port) < 0)
		return NULL;

	n = getaddrinfo(NULL, service, &hints, &res);

	if (n < 0) {
		fprintf(stderr, "%s for port %d\n", gai_strerror(n), port);
		free(service);
		return NULL;
	}

	for (t = res; t; t = t->ai_next) {
		sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
		if (sockfd >= 0) {
			n = 1;

			setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof n);

			if (!bind(sockfd, t->ai_addr, t->ai_addrlen))
				break;
			close(sockfd);
			sockfd = -1;
		}
	}

	freeaddrinfo(res);
	free(service);

	if (sockfd < 0) {
		fprintf(stderr, "Couldn't listen to port %d\n", port);
		return NULL;
	}

	listen(sockfd, 1);
	connfd = accept(sockfd, NULL, 0);
	close(sockfd);
	if (connfd < 0) {
		fprintf(stderr, "accept() failed\n");
		return NULL;
	}

	n = read(connfd, msg, sizeof msg);
	if (n != sizeof msg) {
		perror("server read");
		fprintf(stderr, "%d/%d: Couldn't read remote address\n", n, (int) sizeof msg);
		goto out;
	}

	rem_dest = malloc(sizeof *rem_dest);
	if (!rem_dest)
		goto out;

	sscanf(msg, "%x:%x:%x", &rem_dest->lid, &rem_dest->qpn, &rem_dest->psn);

	if (connect_ctx(ctx, ib_port, my_dest->psn, mtu, sl, rem_dest)) {
		fprintf(stderr, "Couldn't connect to remote QP\n");
		free(rem_dest);
		rem_dest = NULL;
		goto out;
	}

	sprintf(msg, "%04x:%06x:%06x", my_dest->lid, my_dest->qpn, my_dest->psn);
	if (write(connfd, msg, sizeof msg) != sizeof msg) {
		fprintf(stderr, "Couldn't send local address\n");
		free(rem_dest);
		rem_dest = NULL;
		goto out;
	}

	read(connfd, msg, sizeof msg);

out:
	close(connfd);
	return rem_dest;
}


int close_ctx(struct pcontext *ctx)
{
  int i;
	if (ibv_destroy_qp(ctx->qp)) {
		fprintf(stderr, "Couldn't destroy QP\n");
		return 1;
	}

	if (ibv_destroy_cq(ctx->cq)) {
		fprintf(stderr, "Couldn't destroy CQ\n");
		return 1;
	}

  for(i=0;i<BSIZE;i++){
    if (ibv_dereg_mr(ctx->in_mr[i])) {
      fprintf(stderr, "Couldn't deregister MR\n");
      return 1;
    }
    free(ctx->in_b.buf[i]);
    if (ibv_dereg_mr(ctx->out_mr[i])) {
      fprintf(stderr, "Couldn't deregister MR\n");
      return 1;
    }
    free(ctx->out_b.buf[i]);
  }

	if (ibv_dealloc_pd(ctx->pd)) {
		fprintf(stderr, "Couldn't deallocate PD\n");
		return 1;
	}

	if (ctx->channel) {
		if (ibv_destroy_comp_channel(ctx->channel)) {
			fprintf(stderr, "Couldn't destroy completion channel\n");
			return 1;
		}
	}

	if (ibv_close_device(ctx->context)) {
		fprintf(stderr, "Couldn't release context\n");
		return 1;
	}

	free(ctx);

	return 0;
}


uint rem_unack_tot = 0, rem_unack_count = 0;
uint local_unack_tot = 0, local_unack_count = 0;

static int post_send(struct pcontext *ctx, uint quota)
{
  uint *end = &ctx->out_b.e;
  int ret = 0, i = 0;
  uint rem_unack = ctx->received - ctx->received_ack;
  uint to_send = ctx->snd_received + quota - ctx->snd_posted;

  //------ Counters -----
  rem_unack_tot += rem_unack;
  rem_unack_count++;
  local_unack_tot += ctx->snd_posted - ctx->snd_received;
  local_unack_count++;
  //---------------------
  
  
  to_send = to_send > 10 ? 10 : to_send;
  
  to_send = to_send < buf_free(&ctx->out_b) ? to_send : buf_free(&ctx->out_b);

  
  for(i=0;i<to_send;i++){
    void *buf = ctx->out_b.buf[*end];
    struct ibv_sge list = {
      .addr	= (uintptr_t)buf,
      .length = ctx->size,
      .lkey	= ctx->out_mr[*end]->lkey
    };
    struct ibv_send_wr wr = {
  		.wr_id	    = PINGPONG_SEND_WRID,
  		.sg_list    = &list,
  		.num_sge    = 1,
  		.opcode     = IBV_WR_SEND,
  		.send_flags = IBV_SEND_SIGNALED,
  	};
	
    struct ibv_send_wr *bad_wr;
    *(int*)buf = ++ctx->counter;
    *((int*)buf + 1) = ctx->received;
    ctx->received_ack = ctx->received;
    if( ret = ibv_post_send(ctx->qp, &wr, &bad_wr) ){
      printf("Error while ibv_post_send\n");
      abort();
    }
    buf_add(&ctx->out_b);
    ctx->snd_posted++;
  }
  return to_send;
}

static int get_recv(struct pcontext *ctx)
{
  if( ctx->in_b.size - buf_free(&ctx->in_b) > 0 ){
    int *ptr = (int *)ctx->in_b.buf[ctx->in_b.s];
    buf_del(&ctx->in_b);
    ctx->snd_received = ptr[1];
    ctx->rem_counter = ptr[0];
  }
}

static int post_recv(struct pcontext *ctx)
{
  uint *end = &ctx->in_b.e;
  int ret = 0;
  
  while( (buf_free(&ctx->in_b)!=0) && (ret == 0) ){
    void *buf = ctx->in_b.buf[*end];
    struct ibv_sge list = {
      .addr	= (uintptr_t) buf,
      .length = ctx->size,
      .lkey	= ctx->in_mr[*end]->lkey
    };
    struct ibv_recv_wr wr = {
      .wr_id	    = PINGPONG_RECV_WRID,
      .sg_list    = &list,
      .num_sge    = 1,
    };
    struct ibv_recv_wr *bad_wr;
		if( !(ret = ibv_post_recv(ctx->qp, &wr, &bad_wr)) )
      buf_add(&ctx->in_b);
    else{
      printf("Error while ibv_post_recv\n");
      abort();
    }

  }
	return 0;
}


static void usage(const char *argv0)
{
	printf("Usage:\n");
	printf("  %s            start a server and wait for connection\n", argv0);
	printf("  %s <host>     connect to server at <host>\n", argv0);
	printf("\n");
	printf("Options:\n");
  printf("  -s, --size=<size>      size of message to exchange (default 4096)\n");
	printf("  -m, --mtu=<size>       path MTU (default 1024)\n");
	printf("  -n, --iters=<iters>(iters - ctx->snd_posted)    number of exchanges (default 1000)\n");
  printf("  -e, --events           use events\n");
  printf("  -q, --quota=<quota>    use events\n");
}

int main(int argc, char *argv[])
{
	struct ibv_device **dev_list;
	struct ibv_device	*ib_dev;
	struct pcontext *ctx;
	struct pdest my_dest;
	struct pdest *rem_dest;
	struct timeval           start, end;
	char   *servername = NULL;
	int    port = 18515;
	int    ib_port = 1;
	int    size = 4096;
	enum ibv_mtu mtu = IBV_MTU_1024;
	int    iters = 1000;
	int    rcnt, scnt;
	int    num_cq_events = 0;
  uint quota = 50;
  uint can_send = 1;
  int sl = 0;
  uint iter_num = 0;
  uint use_events = 0;

	srand48(getpid() * time(NULL));
	while (1) {
		int c;

		static struct option long_options[] = 
    {
      { .name = "size",     .has_arg = 1, .val = 's' },
			{ .name = "iters",    .has_arg = 1, .val = 'n' },
      { .name = "evbents",    .has_arg = 1, .val = 'e' },
			{ 0 }
		};

		c = getopt_long(argc, argv, "s:n:e", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {

    case 's':
			size = strtol(optarg, NULL, 0);
			break;

		case 'n':
			iters = strtol(optarg, NULL, 0);
			break;
    
    case 'e':
      use_events = 1;
      break;

		default:
			usage(argv[0]);
			return 1;
		}
	}

	if (optind == argc - 1)
		servername = strdup(argv[optind]);
	else if (optind < argc) {
		usage(argv[0]);
		return 1;
	}

	page_size = sysconf(_SC_PAGESIZE);

	dev_list = ibv_get_device_list(NULL);
	if (!dev_list) {
		perror("Failed to get IB devices list");
		return 1;
	}

	ib_dev = *dev_list;
	if (!ib_dev) {
		fprintf(stderr, "No IB devices found\n");
		return 1;
	}

	ctx = init_ctx(ib_dev, size, ib_port, use_events);
	if (!ctx)
		return 1;

	post_recv(ctx);

  if( use_events ){
    if (ibv_req_notify_cq(ctx->cq, 0)) {
      fprintf(stderr, "Couldn't request CQ notification\n");
      return 1;
    }
  }

	my_dest.lid = pp_get_local_lid(ctx->context, ib_port);
	my_dest.qpn = ctx->qp->qp_num;
	my_dest.psn = lrand48() & 0xffffff;
	if (!my_dest.lid) {
		fprintf(stderr, "Couldn't get local LID\n");
		return 1;
	}

	printf("  local address:  LID 0x%04x, QPN 0x%06x, PSN 0x%06x\n",
	       my_dest.lid, my_dest.qpn, my_dest.psn);

	if (servername)
		rem_dest = client_exch_dest(servername, port, &my_dest);
	else
		rem_dest = server_exch_dest(ctx, ib_port, mtu, port, sl, &my_dest);

	if (!rem_dest)
		return 1;

	printf("  remote address: LID 0x%04x, QPN 0x%06x, PSN 0x%06x\n",
	       rem_dest->lid, rem_dest->qpn, rem_dest->psn);

	if (servername)
		if (connect_ctx(ctx, ib_port, my_dest.psn, mtu, sl, rem_dest))
			return 1;



	post_send(ctx,quota);
  can_send = 0;

	if (gettimeofday(&start, NULL)) {
		perror("gettimeofday");
		return 1;
	}

	rcnt = scnt = 0;
	while ( rcnt < iters || scnt < iters ) {
		struct ibv_cq *ev_cq;
		void          *ev_ctx;
		struct ibv_wc wc[100];
		int ne, i;
   
    iter_num++;
    if( use_events ){
      if( rcnt < iters || ctx->snd_posted > ctx->snd_finished ){
        if (ibv_get_cq_event(ctx->channel, &ev_cq, &ev_ctx)) {
          fprintf(stderr, "Failed to get cq_event\n");
          return 1;
        }

        ++num_cq_events;

        if (ev_cq != ctx->cq) {
          fprintf(stderr, "CQ event for unknown CQ %p\n", ev_cq);
          return 1;
        }

        if (ibv_req_notify_cq(ctx->cq, 0)) {
          fprintf(stderr, "Couldn't request CQ notification\n");
          return 1;
        }
      }
    }
    
    if( (ne = ibv_poll_cq(ctx->cq, 10, wc)) < 0) {
			fprintf(stderr, "poll CQ failed %d\n", ne);
			return 1;
  	}
    
    if( !ne )
      continue;

		for (i = 0; i < ne; ++i) {
			if (wc[i].status != IBV_WC_SUCCESS) {
				fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
					ibv_wc_status_str(wc[i].status),
					wc[i].status, (int) wc[i].wr_id);
				return 1;
			}

			switch ((int) wc[i].wr_id) {
			case PINGPONG_SEND_WRID:
				++scnt;
				ctx->snd_finished++;
        buf_del(&ctx->out_b);
				break;

			case PINGPONG_RECV_WRID:{
				ctx->received++;
        get_recv(ctx);
    		++rcnt;
        can_send = 1;
				break;
			}
			default:
				fprintf(stderr, "Completion for unknown wr_id %d\n",
					(int) wc[i].wr_id);
				return 1;
			}
		}
    
    if( buf_used(&ctx->in_b) < 1.5*quota )
      post_recv(ctx);
    
    if( can_send ){
      post_send(ctx,quota);
      can_send = 0;
    }
  }

	if (gettimeofday(&end, NULL)) {
		perror("gettimeofday");
		return 1;
	}

	{
		float usec = (end.tv_sec - start.tv_sec) * 1000000 +
			(end.tv_usec - start.tv_usec);
		long long bytes = (long long) size * iters * 2;

		printf("%lld bytes in %.2f seconds = %.2f Mbit/sec\n",
		       bytes, usec / 1000000., bytes * 8. / usec);
		printf("%d iters in %.2f seconds = %.2f usec/iter\n",
		       iters, usec / 1000000., usec / iters);
	}
  
  printf("Avg remote unacked: %f, Avg. local unacked: %f\n",
    (float)rem_unack_tot/rem_unack_count, 
    (float)local_unack_tot/local_unack_count);
  
  if( num_cq_events )
      ibv_ack_cq_events(ctx->cq, num_cq_events);
      
  //sleep(1);
	if (close_ctx(ctx))
		return 1;

	ibv_free_device_list(dev_list);
	free(rem_dest);

	return 0;
}
