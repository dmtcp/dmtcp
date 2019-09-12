#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <infiniband/verbs.h>
#include <linux/types.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <queue>

#include "jassert.h"
#include "jsocket.h"
#include "config.h"
#include "dmtcp.h"
#include "dmtcpalloc.h"
#include "util.h"

#include "ib2tcp.h"
#include "ibwrappers.h"

using namespace dmtcp;

vector<IB_WR<struct ibv_send_wr> *>sendQueue;

map<IB_QP *, vector<IB_WR<struct ibv_recv_wr> *> >recvQueue;
map<struct ibv_srq *, vector<IB_WR<struct ibv_recv_wr> *> >srecvQueue;

map<IB_QP *, int>qpToFd;
map<int, IB_QP *>fdToQP;

vector<int>socks;

map<uint32_t, IB_QP *>queuePairs;

map<struct ibv_cq *, vector<struct ibv_wc> >compQueue;
map<struct ibv_cq *, sem_t *>compQueueSema;

sem_t sem_queue;

jalib::JServerSocket listenSock(-1);
static struct sockaddr_storage listenAddr;
static size_t listenAddrLen;
static int fdCounter = 500;
pthread_t sendTh = -1;
pthread_t recvTh = -1;

static void *sendThread(void *arg);
static void *recvThread(void *arg);
int isVirtIB = 0;

static pthread_mutex_t _lock;
static void
do_lock()
{
  JASSERT(pthread_mutex_lock(&_lock) == 0);
}

static void
do_unlock()
{
  JASSERT(pthread_mutex_unlock(&_lock) == 0);
}
static void
ib2tcp_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_PRECHECKPOINT:
    save_term_settings();
    break;

  case DMTCP_EVENT_RESTART:
    IB2TCP::postRestart();
    dmtcp_global_barrier("IB2TCP::restart");
    IB2TCP::registerNSData();
    dmtcp_global_barrier("IB2TCP::register_ns_data");
    IB2TCP::sendQueries();
    dmtcp_global_barrier("IB2TCP::send_queries");
    IB2TCP::createTCPConnections();
    break;
  }
}

DmtcpPluginDescriptor_t ib2tcp_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "ib2tcp",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "IB2TCP plugin",
  ib2tcp_EventHook
};

DMTCP_DECL_PLUGIN(ib2tcp_plugin);

/**************************************************************/
/**************************************************************/
static void *
sendThread(void *arg)
{
  while (isVirtIB == 0) {
    sleep(1);
  }

  while (1) {
    sem_wait(&sem_queue);
    IB2TCP::doSendMsg();
  }
}

static void *
recvThread(void *arg)
{
  fd_set fds;
  int maxFd;
  size_t i;

  while (isVirtIB == 0) {
    sleep(1);
  }

  while (1) {
    struct timeval timeout = { 1, 0 };
    maxFd = -1;
    FD_ZERO(&fds);

    for (i = 0; i < socks.size(); ++i) {
      FD_SET(socks[i], &fds);
      maxFd = std::max(maxFd, socks[i]);
    }

    if (maxFd < 0) {
      sleep(1);
      continue;
    }

    int retval = select(maxFd + 1, &fds, NULL, NULL, &timeout);
    JWARNING(retval != -1) (JASSERT_ERRNO) (maxFd).Text("select failed");

    if (retval > 0) {
      for (i = 0; i < socks.size(); ++i) {
        int fd = socks[i];
        if (fd >= 0 && FD_ISSET(fd, &fds)) {
          IB2TCP::doRecvMsg(fd);
        }
      }
    }
  }
}

/**************************************************************/
/**************************************************************/
void
IB2TCP::openListenSocket()
{
  if (Util::isValidFd(listenSock.sockfd())) {
    return;
  }
  listenSock = jalib::JServerSocket(jalib::JSockAddr::ANY, 0);
  JASSERT(listenSock.isValid());
  listenSock.changeFd(fdCounter++);
  JASSERT(dmtcp_is_protected_fd(fdCounter) == 0);

  // Setup restore socket for name service
  struct sockaddr_in addr_in;
  addr_in.sin_family = AF_INET;
  dmtcp_get_local_ip_addr(&addr_in.sin_addr);
  addr_in.sin_port = htons(listenSock.port());
  memcpy(&listenAddr, &addr_in, sizeof(addr_in));
  listenAddrLen = sizeof(addr_in);

  JNOTE("opened listen socket") (listenSock.sockfd())
    (inet_ntoa(addr_in.sin_addr)) (ntohs(addr_in.sin_port));
}

void
IB2TCP::init()
{
  if (sendTh == -1) {
    JASSERT(pthread_create(&sendTh, NULL, sendThread, NULL) == 0);
    JASSERT(pthread_create(&recvTh, NULL, recvThread, NULL) == 0);
  }
  sem_init(&sem_queue, 0, 0);
}

void
IB2TCP::postRestart()
{
  isVirtIB = 1;
  pthread_mutex_init(&_lock, NULL);
  openListenSocket();
}

void
IB2TCP::registerNSData()
{
  if (!isVirtIB) {
    return;
  }

  map<uint32_t, IB_QP *>::iterator it;
  for (it = queuePairs.begin(); it != queuePairs.end(); it++) {
    IB_QP *ibqp = it->second;
    if (ibqp->localId < ibqp->remoteId) {
      dmtcp_send_key_val_pair_to_coordinator("IB2TCP",
                                             &ibqp->localId.lid,
                                             sizeof(ibqp->localId.lid),
                                             &listenAddr,
                                             listenAddrLen);
    }
  }
}

void
IB2TCP::sendQueries()
{
  if (!isVirtIB) {
    return;
  }

  map<uint32_t, IB_QP *>::iterator it;
  for (it = queuePairs.begin(); it != queuePairs.end(); it++) {
    IB_QP *ibqp = it->second;
    if (ibqp->localId > ibqp->remoteId) {
      uint32_t size = sizeof(ibqp->remoteAddr);
      dmtcp_send_query_to_coordinator("IB2TCP",
                                      &ibqp->remoteId.lid,
                                      sizeof(ibqp->remoteId.lid),
                                      &ibqp->remoteAddr,
                                      &size);
      ibqp->remoteAddrLen = size;
    } else {
      ibqp->remoteAddrLen = -1;
    }
  }
}

void
IB2TCP::createTCPConnections()
{
  if (!isVirtIB) {
    return;
  }

  size_t numRemaining = 0;
  map<uint32_t, IB_QP *>::iterator it;

  // First do a connect
  for (it = queuePairs.begin(); it != queuePairs.end(); it++) {
    IB_QP *ibqp = it->second;
    IB_QId remoteId = ibqp->getRemoteId();
    if (ibqp->localId > ibqp->remoteId) {
      struct sockaddr_in *addr_in = (struct sockaddr_in *)&ibqp->remoteAddr;
      JNOTE("connecting to remote node")
        (inet_ntoa(addr_in->sin_addr)) (ntohs(addr_in->sin_port));
      jalib::JSocket sock = jalib::JClientSocket((sockaddr *)&ibqp->remoteAddr,
                                                 ibqp->remoteAddrLen);
      sock.changeFd(fdCounter++);
      int fd = sock.sockfd();
      JASSERT(fd != -1);
      JASSERT(Util::writeAll(fd,
                             &remoteId,
                             sizeof(remoteId)) == sizeof(remoteId))
        (JASSERT_ERRNO);
      fdToQP[fd] = ibqp;
      qpToFd[ibqp] = fd;
      socks.push_back(fd);
    } else {
      numRemaining++;
    }
  }

  // Now accept all connections.
  for (size_t i = 0; i < numRemaining; i++) {
    IB_QId localId;
    jalib::JSocket sock = listenSock.accept();
    JASSERT(sock.isValid());
    sock.changeFd(fdCounter++);
    JASSERT(Util::readAll(sock.sockfd(), &localId,
                          sizeof(localId)) == sizeof(localId))
      (JASSERT_ERRNO);

    JASSERT(queuePairs.find(localId.qp_num) != queuePairs.end())
      (localId.qp_num) (localId.lid);

    IB_QP *ibqp = queuePairs[localId.qp_num];
    JASSERT(ibqp->localId < ibqp->remoteId)
      (ibqp->localId.qp_num) (ibqp->remoteId.qp_num);

    fdToQP[sock.sockfd()] = ibqp;
    qpToFd[ibqp] = sock.sockfd();
    socks.push_back(sock.sockfd());
  }
}

/**************************************************************/
/**************************************************************/
void
IB2TCP::doRecvMsg(int fd)
{
  struct msghdr msg;
  size_t nbytes = 0;

  JASSERT(Util::readAll(fd, &nbytes, sizeof nbytes) == sizeof nbytes);

  // struct ibv_qp* ibqp = fdToQP[fd];
  IB_QP *ibqp = fdToQP[fd];
  struct ibv_recv_wr *wr;
  IB_WR<struct ibv_recv_wr> *ibwr;

  do_lock();
  if (ibqp->srq != NULL) {
    ibwr = srecvQueue[ibqp->srq].front();
    wr = &ibwr->wr;
    srecvQueue[ibqp->srq].erase(srecvQueue[ibqp->srq].begin());

    /* Special handling for srq
     * If a WR is being posted to a UD QP, the Global Routing Header (GRH) of
     * the incoming message will be placed in the first  40 bytes  of  the
     * buffer(s) in the scatter list.  If no GRH is present in the incoming
     * message, then the first bytes will be undefined.  This means that in all
     * cases, the actual data of the incoming message will start at an offset
     * of 40 bytes into the buffer(s) in the scatter list.
     */

    // JASSERT(wr->sg_list[0].length > 40);
    // wr->sg_list[0].addr += 40;
    // wr->sg_list[0].length -= 40;
  } else {
    ibwr = recvQueue[ibqp].front();
    wr = &ibwr->wr;
    recvQueue[ibqp].erase(recvQueue[ibqp].begin());
  }

  do_unlock();

  size_t numleft = nbytes;
  for (size_t i = 0; i < wr->num_sge && numleft > 0; i++) {
    struct ibv_sge *sge = &wr->sg_list[i];
    if (numleft < sge->length) {
      JASSERT(Util::readAll(fd, (void *)sge->addr, numleft) == numleft)
        (JASSERT_ERRNO);
      numleft = 0;
      break;
    }
    JASSERT(Util::readAll(fd, (void *)sge->addr, sge->length) == sge->length)
      (JASSERT_ERRNO);
    numleft -= sge->length;
  }

  struct ibv_wc wc;
  wc.wr_id = wr->wr_id;
  wc.status = IBV_WC_SUCCESS;
  wc.opcode = IBV_WC_RECV;
  wc.vendor_err = 0;
  wc.byte_len = nbytes;
  wc.imm_data = 0;
  wc.qp_num = ibqp->localId.qp_num;
  wc.src_qp = ibqp->remoteId.qp_num;
  wc.wc_flags = 0;
  wc.pkey_index = 0;
  wc.slid = 0;
  wc.sl = 0;
  wc.dlid_path_bits = 0;

  delete ibwr;
  struct ibv_cq *cq = ibqp->recv_cq;
  do_lock();
  compQueue[cq].push_back(wc);
  if (compQueueSema.find(cq) == compQueueSema.end()) {
    sem_t *sem = (sem_t *)malloc(sizeof(sem_t));
    JASSERT(sem != NULL);
    sem_init(sem, 0, 0);
    compQueueSema[cq] = sem;
  }
  sem_post(compQueueSema[cq]);
  do_unlock();
}

void
IB2TCP::doSendMsg()
{
  do_lock();
  IB_WR<struct ibv_send_wr> *ibwr = sendQueue.front();
  struct ibv_send_wr *wr = &ibwr->wr;
  sendQueue.erase(sendQueue.begin());
  do_unlock();
  IB_QP *ibqp = ibwr->ibqp;
  int fd = qpToFd[ibqp];
  size_t i;
  size_t nbytes = 0;

  for (size_t i = 0; i < wr->num_sge; i++) {
    nbytes += wr->sg_list[i].length;
  }

  JASSERT(Util::writeAll(fd, &nbytes, sizeof nbytes) == sizeof nbytes)
    (JASSERT_ERRNO);

  for (size_t i = 0; i < wr->num_sge; i++) {
    struct ibv_sge *sge = &wr->sg_list[i];
    JASSERT(Util::writeAll(fd, (void *)sge->addr, sge->length) == sge->length)
      (JASSERT_ERRNO);
  }

  struct ibv_wc wc;
  wc.wr_id = wr->wr_id;
  wc.status = IBV_WC_SUCCESS;
  wc.opcode = IBV_WC_SEND;
  wc.vendor_err = 0;
  wc.byte_len = nbytes;
  wc.imm_data = 0;
  wc.qp_num = ibqp->localId.qp_num;
  wc.src_qp = ibqp->remoteId.qp_num;
  wc.wc_flags = 0;
  wc.pkey_index = 0;
  wc.slid = 0;
  wc.sl = 0;
  wc.dlid_path_bits = 0;

  delete ibwr;
  struct ibv_cq *cq = ibqp->recv_cq;
  do_lock();
  compQueue[cq].push_back(wc);
  if (compQueueSema.find(cq) == compQueueSema.end()) {
    sem_t *sem = (sem_t *)malloc(sizeof(sem_t));
    JASSERT(sem != NULL);
    sem_init(sem, 0, 0);
    compQueueSema[cq] = sem;
  }
  sem_post(compQueueSema[cq]);
  do_unlock();
}

/**************************************************************/
/**************************************************************/

void
IB2TCP::createQP(struct ibv_qp *qp, struct ibv_qp_init_attr *qp_init_attr)
{
  IB2TCP::init();

  /* code to populate the ID */
  /* get the port num */
  struct ibv_qp_attr qattr;
  struct ibv_port_attr pattr;
  struct ibv_qp_init_attr init_atty;

  JASSERT(_real_ibv_query_qp(qp, &qattr, IBV_QP_PORT, &init_atty) == 0);

  /* get the LID */
  JASSERT(qp->context->ops.query_port(qp->context, 1, &pattr) == 0);

  // IB_QP ibqp(qp, pattr.lid);
  IB_QP *ibqp = new IB_QP(qp, pattr.lid, qp_init_attr);
  do_lock();
  queuePairs[qp->qp_num] = ibqp;
  do_unlock();
}

void
IB2TCP::modifyQP(struct ibv_qp *qp, struct ibv_qp_attr *attr, int mask)
{
  JASSERT(queuePairs.find(qp->qp_num) != queuePairs.end());

  do_lock();
  IB_QP *ibqp = queuePairs[qp->qp_num];
  do_unlock();

  if (mask & IBV_QP_DEST_QPN) {
    ibqp->remoteId.qp_num = attr->dest_qp_num;
  }

  if (mask & IBV_QP_AV) {
    ibqp->remoteId.lid = attr->ah_attr.dlid - attr->ah_attr.src_path_bits;
  }

  if (mask & IBV_QP_PORT) {
    struct ibv_port_attr qattr;
    JASSERT(qp->context->ops.query_port(qp->context,
                                        attr->port_num,
                                        &qattr) == 0);
    ibqp->localId.lid = qattr.lid;
  }
}

int
IB2TCP::postSend(struct ibv_qp *qp,
                 struct ibv_send_wr *wr,
                 struct ibv_send_wr **bad_wr)
{
  do_lock();
  for (struct ibv_send_wr *w = wr; w != NULL; w = w->next) {
    IB_QP *ibqp = queuePairs[qp->qp_num];
    IB_WR<struct ibv_send_wr> *ibwr = new IB_WR<struct ibv_send_wr>(ibqp, w);
    sendQueue.push_back(ibwr);
    sem_post(&sem_queue);
  }
  do_unlock();
}

int
IB2TCP::postRecv(struct ibv_qp *qp,
                 struct ibv_recv_wr *wr,
                 struct ibv_recv_wr **bad_wr)
{
  do_lock();
  for (struct ibv_recv_wr *w = wr; w != NULL; w = w->next) {
    IB_QP *ibqp = queuePairs[qp->qp_num];
    IB_WR<struct ibv_recv_wr> *ibwr = new IB_WR<struct ibv_recv_wr>(ibqp, w);
    recvQueue[ibqp].push_back(ibwr);
  }
  do_unlock();
}

int
IB2TCP::postSrqRecv(struct ibv_srq *srq,
                    struct ibv_recv_wr *wr,
                    struct ibv_recv_wr **bad_wr)
{
  do_lock();
  for (struct ibv_recv_wr *w = wr; w != NULL; w = w->next) {
    IB_WR<struct ibv_recv_wr> *ibwr = new IB_WR<struct ibv_recv_wr>(w);
    srecvQueue[srq].push_back(ibwr);
  }
  do_unlock();
}

int
IB2TCP::pollCq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc)
{
  int i;

  if (compQueueSema.find(cq) == compQueueSema.end()) {
    sem_t *sem = (sem_t *)malloc(sizeof(sem_t));
    JASSERT(sem != NULL);
    sem_init(sem, 0, 0);
    compQueueSema[cq] = sem;
  }
  sem_wait(compQueueSema[cq]);
  do_lock();
  for (i = 0; i < num_entries && compQueue[cq].size() > 0; i++) {
    struct ibv_wc w = compQueue[cq].front();
    compQueue[cq].erase(compQueue[cq].begin());
    wc[i] = w;
  }
  do_unlock();
  return i;
}

int
IB2TCP::postPollCq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc)
{
  do_lock();
  for (int i = 0; i < num_entries; i++) {
    IB_QP *ibqp = queuePairs[wc[i].qp_num];
    enum ibv_wc_opcode opcode = wc[i].opcode;
    if (opcode & IBV_WC_RECV ||
        opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
      IB_WR<struct ibv_recv_wr> *ibwr;
      if (ibqp->srq != NULL) {
        ibwr = srecvQueue[ibqp->srq].front();
        srecvQueue[ibqp->srq].erase(srecvQueue[ibqp->srq].begin());
      } else {
        ibwr = recvQueue[ibqp].front();
        recvQueue[ibqp].erase(recvQueue[ibqp].begin());
      }
      delete ibwr;
#if 0
      if (internal_qp->user_qp.srq) {
        struct internal_ibv_srq *internal_srq = ibv_srq_to_internal(
            internal_qp->user_qp.srq);
        internal_srq->recv_count++;
      } else {
        struct list_elem *e = list_pop_front(&internal_qp->post_recv_log);
        struct ibv_post_recv_log *log = list_entry(e,
                                                   struct ibv_post_recv_log,
                                                   elem);
        free(log);
      }
#endif // if 0
    } else if (opcode == IBV_WC_SEND ||
               opcode == IBV_WC_RDMA_WRITE ||
               opcode == IBV_WC_RDMA_READ ||
               opcode == IBV_WC_COMP_SWAP ||
               opcode == IBV_WC_FETCH_ADD) {
      JASSERT(opcode == IBV_WC_SEND) (opcode);
#if 0
      struct list_elem *e = list_pop_front(&internal_qp->post_send_log);
      struct ibv_post_send_log *log = list_entry(e,
                                                 struct ibv_post_send_log,
                                                 elem);
      assert(log->magic == SEND_MAGIC);
      free(log);
#endif // if 0

      for (size_t k = 0; k < sendQueue.size(); k++) {
        IB_WR<struct ibv_send_wr> *ibwr = sendQueue[k];
        if (ibwr->ibqp == ibqp) {
          sendQueue.erase(sendQueue.begin() + k);
          delete ibwr;
          break;
        }
      }
    } else if (opcode == IBV_WC_BIND_MW) {
      JASSERT(false) (opcode).Text("Opcode not supported");
    } else {
      JASSERT(false) (opcode).Text("Unknown opcode");
    }
  }
  do_unlock();
}

int
IB2TCP::req_notify_cq(struct ibv_cq *cq, int solicited_only)
{
  JASSERT(false);
}

bool
dmtcp::operator<(IB_QId &a, IB_QId &b)
{
  return (a.lid < b.lid) || (a.lid == b.lid && a.qp_num < b.qp_num);
}

bool
dmtcp::operator>(IB_QId &a, IB_QId &b)
{
  return (a.lid > b.lid) || (a.lid == b.lid && a.qp_num > b.qp_num);
}

bool
dmtcp::operator==(IB_QId &a, IB_QId &b)
{
  return a.lid == b.lid && a.qp_num == b.qp_num;
}

bool
dmtcp::operator!=(IB_QId &a, IB_QId &b)
{
  return !(a == b);
}
