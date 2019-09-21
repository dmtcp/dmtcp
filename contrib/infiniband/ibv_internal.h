/*! \file ibv_internal.h */
#include <infiniband/verbs.h>
#include <inttypes.h>
#include <stdbool.h>
#include "debug.h"
#include "ibvidentifier.h"
#include "lib/list.h"

/* Two 64-bit fixed, random numbers to verify whether a struct is shadowed
 * (struct internal_ibv_XXX) or real (struct ibv_XXX).  This signature
 * allows us to detect recursive cases:  A target application may call our
 * wrapper using the shadowed struct.  We then pass on the real struct to the
 * real function in the IB library.  But if the real function calls a second
 * real IB function, then it will pass the real struct to our wrapper
 * for the second IB function.
 */

#define STRUCT_MAGIC1 15377651903861113382ULL
#define STRUCT_MAGIC2 10393203704971477992ULL

#define INIT_INTERNAL_IBV_TYPE(stru) \
  do {                               \
    stru->magic1 = STRUCT_MAGIC1;    \
    stru->magic2 = STRUCT_MAGIC2;    \
  } while (0)

#define IS_INTERNAL_IBV_STRUCT(stru)  \
  ((stru->magic1 == STRUCT_MAGIC1) && (stru->magic2 == STRUCT_MAGIC2))

struct dev_list_info {
  int num_devices;
  bool in_free;
  struct ibv_device **user_dev_list;
  struct ibv_device **real_dev_list;
};

/*
 * On restart, the real resources inside the internal structures below
 * are recreated. However, the old real resources are not released,
 * thus leading to a memory leak. Currenly we do not handle this memory
 * leak, because the memory leak is tiny, and destroying those resources
 * at checkpoint time will affect the performance.
 */

// ! A wrapper around a device
struct internal_ibv_dev {
  struct ibv_device user_dev;
  uint64_t magic1;
  uint64_t magic2;
  struct ibv_device *real_dev;
  bool in_use;
  struct dev_list_info *list_info;
};

// ! A wrapper around a context
struct internal_ibv_ctx {
  struct ibv_context user_ctx;
  uint64_t magic1;
  uint64_t magic2;
  struct ibv_context *real_ctx;
  struct list_elem elem;
};

// ! A wrapper around a comp channel
struct internal_ibv_comp_channel {
  struct ibv_comp_channel user_channel;
  uint64_t magic1;
  uint64_t magic2;
  struct ibv_comp_channel *real_channel;
  struct list_elem elem;
};

// ! A wrapper around a protection domain
struct internal_ibv_pd {
  struct ibv_pd user_pd;
  uint64_t magic1;
  uint64_t magic2;
  struct ibv_pd *real_pd;
  struct list_elem elem;

  // global unique id defined in the plugin, for use of rdma identification
  uint32_t pd_id;
};

// ! A wrapper around a memory region
struct internal_ibv_mr {
  struct ibv_mr user_mr;
  uint64_t magic1;
  uint64_t magic2;
  struct ibv_mr *real_mr;
  int flags;             /*!< The flags used to create the memory region */
  struct list_elem elem;
};

// ! A wrapper around a work completion so it can be put into a list
struct ibv_wc_wrapper {
  struct ibv_wc wc;
  struct list_elem elem;
};

// ! A wrapper around a completion queue
struct internal_ibv_cq {
  struct ibv_cq user_cq;
  uint64_t magic1;
  uint64_t magic2;
  struct ibv_cq *real_cq;
  int comp_vector;
  struct list wc_queue; /*!< This queue buffers remaining completion events at
                           checkpoint time */
  struct list req_notify_log; /*!< This list contains log entries of calls to
                                 ibv_req_notify_cq */
  struct list_elem elem;
};

// ! A wrapper around a queue pair
struct internal_ibv_qp {
  struct ibv_qp user_qp;
  uint64_t magic1;
  uint64_t magic2;
  struct ibv_qp *real_qp;
  struct ibv_qp_init_attr init_attr; /*!< The attributes used to construct the queue */
  uint32_t remote_pd_id;
  struct list modify_qp_log;

  /* This list contains log entries that track what recv work requests were
   * posted. As recv work requests are polled from the CQ, entries in this list
   * are deleted.
   */
  struct list post_recv_log;

  /* This list contains log entries that track what send work requests were
   * posted. As send work requests are polled from the CQ, entries in this list
   * are deleted.
   */
  struct list post_send_log;
  struct list_elem elem;
};

// ! A wrapper around a shared receive queue
struct internal_ibv_srq {
  struct ibv_srq user_srq;
  struct ibv_srq *real_srq;
  uint64_t magic1;
  uint64_t magic2;
  struct ibv_srq_init_attr init_attr;
  struct list modify_srq_log;
  struct list post_srq_recv_log;
  uint32_t recv_count;
  struct list_elem elem;
};

struct internal_ibv_ah {
  struct ibv_ah user_ah;
  uint64_t magic1;
  uint64_t magic2;
  struct ibv_ah *real_ah;
  struct ibv_ah_attr attr;

  struct list_elem elem;
};

// ! A log entry of a call made to ibv_modify_qp
struct ibv_modify_qp_log {
  /* The attr used in the original call to ibv_modify_qp */
  struct ibv_qp_attr attr;
  /* The attr_mask used in the original call to ibv_modify_qp */
  int attr_mask;
  struct list_elem elem;
};

// ! A log entry of a call made to ibv_modify_srq
struct ibv_modify_srq_log {
  struct ibv_srq_attr attr;
  int attr_mask;
  struct list_elem elem;
};

// ! A log entry of a recv work request
struct ibv_post_recv_log {
  struct ibv_recv_wr wr;
  struct list_elem elem;
};

struct ibv_post_srq_recv_log {
  struct ibv_recv_wr wr;
  struct list_elem elem;
};

#define SEND_MAGIC 0xdeadbabe

// ! A log entry of a send work request
struct ibv_post_send_log {
  uint32_t magic;
  struct ibv_send_wr wr;
  struct list_elem elem;
};

// ! A log entry of a call made to ibv_req_notify_cq
struct ibv_req_notify_cq_log {
  int solicited_only;
  struct list_elem elem;
};

/* These are the functions to cast types */

// ! This function locates an ibv_qp based on qp_num */

/*!
 \param qp_num The id number of the qp being located
 \return A pointer to the internal_ibv_qp
 */
static inline struct internal_ibv_qp *
qp_num_to_qp(struct list *l, uint32_t qp_num) {
  struct list_elem *e;

  for (e = list_begin(l); e != list_end(l); e = list_next(e)) {
    struct internal_ibv_qp *internal_qp;

    internal_qp = list_entry(e, struct internal_ibv_qp, elem);
    if (internal_qp->real_qp->qp_num == qp_num) {
      return internal_qp;
    }
  }
  return NULL;
}

// ! This function converts an ibv_device to an internal_ibv_dev

/*!
 \param dev a pointer to an ibv_device which is embedded in an internal_ibv_dev
 \return A pointer to the internal_ibv_dev struct which dev is embedded in
 */
static inline struct internal_ibv_dev *
ibv_device_to_internal(struct ibv_device *dev) {
  return (struct internal_ibv_dev *) dev;
}

// ! Function that converts an ibv_context to internal_ibv_ctx

/*!
 * \param ctx a pointer to an ibv_context which is embedded in an
 * internal_ibv_ctx
 * \return A pointer to the internal_ibv_ctx struct which ctx is embedded in
 * */
static inline struct internal_ibv_ctx *
ibv_ctx_to_internal(struct ibv_context *ctx) {
  return (struct internal_ibv_ctx *) ctx;
}

// ! Function that converts an ibv_pd to an internal_ibv_pd

/*!
 * \param pd a pointer to an ibv_pd which is embedded in an internal_ibv_pd
 * \return A pointer to the internal_ibv_pd struct which pd is embedded in
 * */
static inline struct internal_ibv_pd *
ibv_pd_to_internal(struct ibv_pd *pd) {
  return (struct internal_ibv_pd *) pd;
}

// ! Function that converts an ibv_mr to an internal_ibv_mr

/*!
 * \param mr a pointer to an ibv_mr which is embedded in an internal_ibv_mr
 * \return A pointer to the internal_ibv_mr which mr is embedded in
 */
static inline struct internal_ibv_mr *
ibv_mr_to_internal(struct ibv_mr *mr) {
  return (struct internal_ibv_mr *) mr;
}

// ! Function that converts an ibv_comp_channel to an internal_ibv_comp_channel

/*!
 * \param comp a pointer to an ibv_comp_channel which is embedded in an
 * internal_ibv_comp_channel
 * \return A pointer to internal_ibv_comp_channel which comp is embedded in
 */
static inline struct internal_ibv_comp_channel *
ibv_comp_to_internal(struct ibv_comp_channel *comp) {
  return (struct internal_ibv_comp_channel *) comp;
}

// ! Function that converts an ibv_cq to an internal_ibv_cq

/*!
 * \param cq a pointer to an ibv_cq which is embedded in an internal_ibv_cq
 * \return A pointer to the internal_ibv_cq which cq is embedded in
 */
static inline struct internal_ibv_cq *
ibv_cq_to_internal(struct ibv_cq *cq) {
  return (struct internal_ibv_cq *) cq;
}

// ! Function that converts an ibv_qp to an internal_ibv_qp

/*!
 * \param qp a pointer to an ibv_qp whcih is embedded in an internal_ibv_qp
 * \return A pointer to the internal_ibv_qp which qp is embedded in
 */
static inline struct internal_ibv_qp *
ibv_qp_to_internal(struct ibv_qp *qp) {
  return (struct internal_ibv_qp *) qp;
}

// ! Function that converts an ibv_srq to an internal_ibv_srq

/*!
 * \param srq a pointer to an ibv_srq whcih is embedded in an internal_ibv_srq
 * \return A pointer to the internal_ibv_srq which srq is embedded in
 */
static inline struct internal_ibv_srq *
ibv_srq_to_internal(struct ibv_srq *srq) {
  return (struct internal_ibv_srq *) srq;
}

static inline struct internal_ibv_ah *
ibv_ah_to_internal(struct ibv_ah *ah) {
  return (struct internal_ibv_ah *) ah;
}
