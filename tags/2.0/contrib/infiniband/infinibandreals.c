#include "infinibandreals.h"

#define _GNU_SOURCE
#define _XOPEN_SOURCE 500

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>

typedef int ( *funcptr ) ();
typedef pid_t ( *funcptr_pid_t ) ();
typedef funcptr ( *signal_funcptr ) ();
//
// ==========================================================

// Declare _real_FNC in syscallwrappers.h

static funcptr get_libibverbs_symbol( const char* name )
{
  static void* handle = NULL;
  if ( handle==NULL && ( handle=dlopen ( "/usr/lib64/libibverbs.so.1", RTLD_NOW ) ) == NULL )
  {
    fprintf ( stderr, "dmtcp: get_libibverbs: ERROR in dlopen: %s \n",
              dlerror() );
    abort();
  }

  void* tmp = dlsym ( handle, name );
  if ( tmp == NULL )
  {
    fprintf ( stderr, "dmtcp: get_libibverbs: ERROR in dlsym: %s \n",
              dlerror() );
    abort();
  }
  return ( funcptr ) tmp;
}

#define LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED(type,name) \
    static type (*fn) () = NULL; \
    if (fn==NULL) fn = (void *)get_libibverbs_symbol(#name); \
    return (*fn)


struct ibv_device **_real_ibv_get_device_list(int *num_devices)
{
  LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (struct ibv_device **, ibv_get_device_list)
	(num_devices);
}

const char * _real_ibv_get_device_name(struct ibv_device * dev)
{
  LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED(const char *, ibv_get_device_name)(dev);
}

struct ibv_context *_real_ibv_open_device(struct ibv_device *dev)
{
  LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (struct ibv_context *, ibv_open_device)
	(dev);
}

int _real_ibv_query_device(struct ibv_context *context,struct ibv_device_attr *device_attr)
{
  LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (int, ibv_query_device)
	(context, device_attr);
}

int _real_ibv_query_port(struct ibv_context *context, uint8_t port_num, struct ibv_port_attr *port_attr)
{
  LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (int, ibv_query_port)
	(context, port_num, port_attr);
}

int _real_ibv_query_pkey(struct ibv_context *context, uint8_t port_num,  int index, uint16_t *pkey)
{
  LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (int, ibv_query_pkey)
	(context, index, pkey);
}

int _real_ibv_query_gid(struct ibv_context *context, uint8_t port_num, int index, union ibv_gid *gid)
{
  LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (int, ibv_query_gid)
	(context, port_num, index, gid);
}

uint64_t _real_ibv_get_device_guid(struct ibv_device *device)
{
  LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED(uint64_t, ibv_get_device_guid)(device);
}

struct ibv_comp_channel *_real_ibv_create_comp_channel(struct ibv_context
                                                                *context)
{
  LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED(struct ibv_comp_channel *, ibv_create_comp_channel)(context);
}

int _real_ibv_destroy_comp_channel(struct ibv_comp_channel * channel)
{
  LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED(int, ibv_destroy_comp_channel)(channel);
}

struct ibv_pd *_real_ibv_alloc_pd(struct ibv_context *context)
{
    LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (struct ibv_pd *, ibv_alloc_pd)
        (context);
}

struct ibv_mr *_real_ibv_reg_mr(struct ibv_pd *pd, void *addr,
                                             size_t length,
                                             int access)
{
    LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (struct ibv_mr *, ibv_reg_mr)
        (pd, addr, length, access);
}

struct ibv_cq *_real_ibv_create_cq(struct ibv_context *context, int cqe,
                                                 void *cq_context,
                                                 struct ibv_comp_channel *channel,
                                                 int comp_vector)
{
    LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (struct ibv_cq *, ibv_create_cq)
        (context, cqe, cq_context, channel, comp_vector);
}

struct ibv_srq *_real_ibv_create_srq(struct ibv_pd * pd, struct ibv_srq_init_attr * srq_init_attr)
{

    LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (struct ibv_srq *, ibv_create_srq)
        (pd, srq_init_attr);
}

int _real_ibv_modify_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr, int srq_attr_mask)
{
    LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (int, ibv_modify_srq)
	(srq, srq_attr, srq_attr_mask);
}

int _real_ibv_query_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr)
{
    LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (int, ibv_query_srq) (srq, srq_attr);
}

int _real_ibv_resize_cq(struct ibv_cq * cq, int cqe)
{
  LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (int, ibv_resize_cq) (cq, cqe);
}

struct ibv_qp *_real_ibv_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr)
{
    LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (struct ibv_qp *, ibv_create_qp)
        (pd, qp_init_attr);
}

int _real_ibv_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
                                     int attr_mask)
{
    LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (int, ibv_modify_qp)
        (qp, attr, attr_mask);
}

int _real_ibv_query_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
                            int attr_mask, struct ibv_qp_init_attr *init_attr)
{
  LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (int, ibv_query_qp) (qp, attr, attr_mask, init_attr);
}

int _real_ibv_post_recv(struct ibv_qp *qp, struct ibv_recv_wr *wr,
                        struct ibv_recv_wr **bad_wr)
{
    LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (int, ibv_post_recv)
        (qp, wr, bad_wr);
}

void _real_ibv_ack_cq_events(struct ibv_cq *cq, unsigned int nevents)
{
    LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (void, ibv_ack_cq_events)
        (cq, nevents);
}

int _real_ibv_get_cq_event(struct ibv_comp_channel *channel, struct ibv_cq **cq,
                            void **cq_context)
{
  LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (int, ibv_get_cq_event)
        (channel, cq, cq_context);
}

int _real_ibv_get_async_event(struct ibv_context *context, struct ibv_async_event *event)
{
  LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (int, ibv_get_async_event)
        (context, event);
}

void _real_ibv_ack_async_event(struct ibv_async_event *event)
{
  LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (void, ibv_ack_async_event)(event);
}

int _real_ibv_destroy_cq(struct ibv_cq *cq)
{
    LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (int, ibv_destroy_cq)
        (cq);

}

int _real_ibv_destroy_qp(struct ibv_qp *qp)
{
    LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (int, ibv_destroy_qp)
        (qp);
}

int _real_ibv_destroy_srq(struct ibv_srq *srq)
{
    LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (int, ibv_destroy_srq) (srq);
}

int _real_ibv_dereg_mr(struct ibv_mr *mr)
{
    LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (int, ibv_dereg_mr)
        (mr);

}

int _real_ibv_dealloc_pd(struct ibv_pd *pd)
{
    LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (int, ibv_dealloc_pd)
        (pd);

}

int _real_ibv_close_device(struct ibv_context *context)
{
    LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (int, ibv_close_device)
        (context);

}

void _real_ibv_free_device_list(struct ibv_device **list)
{
    LIBIBVERBS_REAL_FUNC_PASSTHROUGH_TYPED (void, ibv_free_device_list)
        (list);

}
