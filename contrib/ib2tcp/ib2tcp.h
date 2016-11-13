#ifndef IB2TCP_H
#define IB2TCP_H

#include <infiniband/verbs.h>
#include <linux/types.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "jassert.h"
#include "dmtcpalloc.h"

namespace dmtcp
{
typedef struct IB_QId {
  uint32_t lid;
  uint32_t qp_num;
} IB_QId;

class IB_QP
{
  public:
    IB_QP() {}

    IB_QP(struct ibv_qp *q, int lid, struct ibv_qp_init_attr *qp_init_attr)
    {
      localId.lid = 1;
      localId.qp_num = q->qp_num;
      remoteId.lid = 2;
      remoteId.qp_num = 2;
      recv_cq = q->recv_cq;
      send_cq = q->send_cq;
      srq = qp_init_attr->srq;
    }

    ~IB_QP() {}

    IB_QId getLocalId() { return localId; }

    IB_QId getRemoteId() { return remoteId; }

    struct ibv_cq *recv_cq;
    struct ibv_cq *send_cq;
    struct ibv_srq *srq;
    IB_QId localId;
    IB_QId remoteId;
    struct sockaddr_storage remoteAddr;
    size_t remoteAddrLen;
};

template<typename WRType>
class IB_WR
{
  public:
    IB_WR(IB_QP *q, WRType *w)
    {
      ibqp = q;
      init(w);
    }

    IB_WR(WRType *w)
    {
      ibqp = NULL;
      init(w);
    }

    void init(WRType *w)
    {
      struct ibv_sge *sg_list;

      memcpy(&wr, w, sizeof wr);
      if (w->num_sge <= 1) {
        sg_list = static_sg_list;
      } else {
        sg_list = (struct ibv_sge *)malloc(w->num_sge * sizeof(struct ibv_sge));
        JNOTE("Allocating SG List********") (sg_list) (w->num_sge);
      }
      for (int i = 0; i < w->num_sge; i++) {
        memcpy(&sg_list[i], &w->sg_list[i], sizeof(struct ibv_sge));
      }
      wr.sg_list = sg_list;
    }

    ~IB_WR()
    {
      if (wr.num_sge > 1) {
        JNOTE("Freeing SG List********") (wr.sg_list) (wr.num_sge);
        free(wr.sg_list);
      }
    }

    IB_QP *ibqp;
    WRType wr;
    struct ibv_sge static_sg_list[1];
};

namespace IB2TCP
{
void openListenSocket();
void postRestart();
void init();
void registerNSData();
void sendQueries();
void createTCPConnections();
void doRecvMsg(int fd);
void doSendMsg();
void regQIdToListenAddrWithCoord(IB_QId qid);
void regQIdToListenAddrWithCoord(IB_QId qid);
void createQP(struct ibv_qp *qp, struct ibv_qp_init_attr *qp_init_attr);
void modifyQP(struct ibv_qp *qp, struct ibv_qp_attr *attr, int mask);
int postSend(struct ibv_qp *qp,
             struct ibv_send_wr *wr,
             struct ibv_send_wr **bad_wr);
int postRecv(struct ibv_qp *qp,
             struct ibv_recv_wr *wr,
             struct ibv_recv_wr **bad_wr);
int postSrqRecv(struct ibv_srq *srq,
                struct ibv_recv_wr *wr,
                struct ibv_recv_wr **bad_wr);
int pollCq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc);
int postPollCq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc);
int req_notify_cq(struct ibv_cq *cq, int solicited_only);
}

bool operator<(IB_QId &a, IB_QId &b);
bool operator>(IB_QId &a, IB_QId &b);
bool operator==(IB_QId &a, IB_QId &b);
bool operator!=(IB_QId &a, IB_QId &b);
}
#endif // ifndef IB2TCP_H
