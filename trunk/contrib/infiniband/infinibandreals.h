#include <infiniband/verbs.h>

// store the resulting ibv_device struct and the value of *num_devices
// Do we even need to checkpoint this
struct ibv_device **_real_ibv_get_device_list(int *num_devices);
// its better to store the whole structure by nature

// save those values for everything
struct ibv_context *_real_ibv_open_device(struct ibv_device *device);
const char *_real_ibv_get_device_name(struct ibv_device *device);
int _real_ibv_query_device(struct ibv_context *context, struct ibv_device_attr *device_attr);
int _real_ibv_query_port(struct ibv_context *context, uint8_t port_num, struct ibv_port_attr *port_attr);
int _real_ibv_query_pkey(struct ibv_context *context, uint8_t port_num,  int index, uint16_t *pkey);
int _real_ibv_query_gid(struct ibv_context *context, uint8_t port_num, int index, union ibv_gid *gid);
int _real_ibv_get_cq_event(struct ibv_comp_channel *channel, struct ibv_cq **cq, void **cq_context);
int _real_ibv_get_async_event(struct ibv_context *context, struct ibv_async_event *event);
void _real_ibv_ack_async_event(struct ibv_async_event *event);
uint64_t _real_ibv_get_device_guid(struct ibv_device *device);
struct ibv_comp_channel *_real_ibv_create_comp_channel(struct ibv_context
                                                            *context);
int _real_ibv_destroy_comp_channel(struct ibv_comp_channel * channel);
int _real_ibv_resize_cq(struct ibv_cq * cq, int cqe);
struct ibv_pd *_real_ibv_alloc_pd(struct ibv_context *context);
struct ibv_mr *_real_ibv_reg_mr(struct ibv_pd *pd, void *addr,
                                            size_t length,
                                            int access);
struct ibv_cq *_real_ibv_create_cq(struct ibv_context *context, int cqe,
                                                 void *cq_context,
                                                 struct ibv_comp_channel *channel,
                                                 int comp_vector);
struct ibv_srq *_real_ibv_create_srq(struct ibv_pd * pd, struct ibv_srq_init_attr * srq_init_attr);
int _real_ibv_modify_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr, int srq_attr_mask);
int _real_ibv_query_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr);

struct ibv_qp *_real_ibv_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr);
int _real_ibv_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
                                     int attr_mask);
int _real_ibv_query_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
                            int attr_mask, struct ibv_qp_init_attr *init_attr);
int _real_ibv_post_recv(struct ibv_qp *qp, struct ibv_recv_wr *wr,
                            struct ibv_recv_wr **bad_wr);
void _real_ibv_ack_cq_events(struct ibv_cq *cq, unsigned int nevents);
int _real_ibv_destroy_cq(struct ibv_cq *cq);
int _real_ibv_destroy_qp(struct ibv_qp *qp);
int _real_ibv_destroy_srq(struct ibv_srq *srq);
int _real_ibv_dereg_mr(struct ibv_mr *mr);
int _real_ibv_dealloc_pd(struct ibv_pd *pd);
int _real_ibv_close_device(struct ibv_context *context);
void _real_ibv_free_device_list(struct ibv_device **list);
