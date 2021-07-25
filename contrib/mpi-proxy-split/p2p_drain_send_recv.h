#ifndef _P2P_SEND_RECV_H
#define _P2P_SEND_RECV_H

#include <unordered_set>
#include "dmtcp.h"
#include "dmtcpalloc.h"
#include "p2p_log_replay.h"

extern int *g_sendBytesByRank; // Number of bytes sent to other ranks
extern int *g_rsendBytesByRank; // Number of bytes sent to other ranks by MPI_Rsend
extern int *g_bytesSentToUsByRank; // Number of bytes other ranks sent to us
extern int *g_recvBytesByRank; // Number of bytes received from other ranks
extern std::unordered_set<MPI_Comm> active_comms;
extern dmtcp::vector<mpi_message_t*> g_message_queue;

void initialize_drain_send_recv();
void registerLocalSendsAndRecvs();
void drainSendRecv();
int recvFromAllComms(int source);
int recvMsgIntoInternalBuffer(MPI_Status status);
bool isBufferedPacket(int source, int tag, MPI_Comm comm, int *flag,
                      MPI_Status *status);
int consumeBufferedPacket(void *buf, int count, MPI_Datatype datatype,
                          int source, int tag, MPI_Comm comm,
                          MPI_Status *mpi_status, int size);
void removePendingSendRequests();
void resetDrainCounters();
int localRankToGlobalRank(int localRank, MPI_Comm localComm);
#endif
