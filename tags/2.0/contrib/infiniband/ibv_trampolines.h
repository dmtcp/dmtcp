void _uninstall_poll_cq_trampoline(void);
void _install_poll_cq_trampoline(void);
void _uninstall_post_recv_trampoline(void);
void _install_post_recv_trampoline(void);
void _uninstall_post_srq_recv_trampoline(void);
void _install_post_srq_recv_trampoline(void);
void _uninstall_post_send_trampoline(void);
void _install_post_send_trampoline(void);
void _uninstall_req_notify_cq_trampoline(void);
void _install_req_notify_cq_trampoline(void);

void _dmtcp_setup_ibv_trampolines(int (*post_recv_ptr)(struct ibv_qp *,
                                  struct ibv_recv_wr *, struct ibv_recv_wr **),
				  int (*post_srq_recv_ptr)(struct ibv_srq *,
                                  struct ibv_recv_wr *, struct ibv_recv_wr **),
                                  int (*post_send_ptr)(struct ibv_qp *,
                                      struct ibv_send_wr *, struct ibv_send_wr **),
                                  int (*poll_cq_ptr)(struct ibv_cq *, int, struct ibv_wc *),
                                  int (*req_notify_ptr)(struct ibv_cq *, int));
