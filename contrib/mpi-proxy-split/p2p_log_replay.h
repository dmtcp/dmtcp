#ifndef _P2P_COMM_H
#define _P2P_COMM_H

#include <mpi.h>
#include "dmtcp.h"
#include "dmtcpalloc.h"

#define REAL_REQUEST_LOG_LEVEL 7
#define STACK_TRACK_LEVEL 7

#ifdef DEBUG
#define USE_REQUEST_LOG
#endif

typedef enum __mpi_req
{
  UNKNOW_REQUEST,
  ISEND_REQUEST,
  IRECV_REQUEST,
  IBCAST_REQUEST,
  IREDUCE_REQUSET,
  IBARRIER_REQUEST,
} mpi_req_t;

// Struct to store the metadata of an async MPI send/recv call
typedef struct __mpi_async_call
{
  // control data
  mpi_req_t type;  // See enum __mpi_req
  // request parameters
  const void *sendbuf;
  void *recvbuf;
  int count;        // Count of data items
  MPI_Datatype datatype; // Data type
  MPI_Comm comm;    // MPI communicator
  int remote_node;  // Can be dest or source depending on the call type
  int tag;          // MPI message tag
} mpi_async_call_t;

// Struct to store and return the MPI message (data) during draining and
// resuming, also used by p2p_drain_send_recv.h
typedef struct __mpi_message
{
  void *buf;
  int count;
  MPI_Datatype datatype;
  int size;
  MPI_Comm comm;
  MPI_Status status;
} mpi_message_t;

// Struct to store request type and backtrace information for debugging
typedef struct __request_info
{
  mpi_req_t type;
  MPI_Request real_request[REAL_REQUEST_LOG_LEVEL];
  int update_counter;
  void *backtrace[STACK_TRACK_LEVEL];
} request_info_t;


extern int g_world_rank; // Global rank of the current process
extern int g_world_size; // Total number of ranks in the current computation

// Fetches the MPI rank and world size; also, verifies that MPI rank and
// world size match the globally stored values in the plugin
extern void getLocalRankInfo();

// Sets the name of the checkpoint directory of the current process to
// "ckpt_rank_<RANK>", where RANK is the MPI rank of the process.
extern void updateCkptDirByRank();

// Restores the state of MPI P2P communication by replaying any pending
// MPI_Isend and MPI_Irecv requests post restart
extern void replayMpiP2pOnRestart();

// Saves the async send/recv call of the given type and params to a global map
// indexed by the MPI_Request 'req'
extern void addPendingRequestToLog(mpi_req_t , const void* , void* , int ,
                                   MPI_Datatype , int , int ,
                                   MPI_Comm, MPI_Request);

// remove finished send/recv call from the global map
extern void clearPendingRequestFromLog(MPI_Request req);

// Log the creation or update of a virtual request
extern void logRequestInfo(MPI_Request request, mpi_req_t req_type);

// Lookup a request's info in the request_log
extern request_info_t* lookupRequestInfo(MPI_Request request);

extern dmtcp::map<MPI_Request, mpi_async_call_t*> g_async_calls;
#endif // ifndef _P2P_LOG_REPLAY_H
