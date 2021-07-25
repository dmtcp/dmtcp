#include <mpi.h>

#include <functional>
#include <numeric>
#include <algorithm>
#include <map>
#include <sstream>

#include "dmtcp_coordinator.h"
#include "lookup_service.h"
#include "mana_coordinator.h"
#include "mana_coord_proto.h"
#include "jtimer.h"

using namespace dmtcp;

// FIXME: For debugging, remove it after debugging
extern DmtcpCoordinator prog;

typedef dmtcp::map<dmtcp::CoordClient*, rank_state_t> ClientToStateMap;
typedef dmtcp::map<dmtcp::CoordClient*, phase_t> ClientToPhaseMap;
typedef ClientToStateMap::value_type RankKVPair;
typedef ClientToPhaseMap::value_type PhaseKVPair;
typedef ClientToStateMap::const_iterator RankMapConstIterator;
typedef ClientToPhaseMap::const_iterator PhaseMapConstIterator;

// FIXME: Why can't we use client states
typedef dmtcp::map<dmtcp::CoordClient*, int> ClientToRankMap;
typedef dmtcp::map<dmtcp::CoordClient*, unsigned int> ClientToGidMap;
static ClientToRankMap clientRanks;
static ClientToGidMap clientGids;

static ClientToStateMap clientStates;
static ClientToPhaseMap clientPhases;

static std::ostream& operator<<(std::ostream &os, const phase_t &st);
static bool allRanksReady(const ClientToStateMap& clientStates, long int size);
static void unblockRanks(const ClientToStateMap& clientStates, long int size);

JTIMER(twoPc);

void
printNonReadyRanks()
{
  ostringstream o;
  o << "Non-ready Rank States:" << std::endl;
  for (PhaseKVPair c : clientPhases) {
    phase_t st = c.second;
    CoordClient *client = c.first;
    if (st != IS_READY) {
      o << client->identity() << ": " << st << std::endl;
    }
  }
  printf("%s\n", o.str().c_str());
  fflush(stdout);
}

void
printMpiDrainStatus(const LookupService& lookupService)
{
  const KeyValueMap* map = lookupService.getMap(MPI_SEND_RECV_DB);
  if (!map) {
    JTRACE("No send recv database");
    return;
  }

  typedef std::pair<KeyValue, KeyValue *> KVPair;
  std::function<uint64_t(uint64_t, KVPair)> sendSum =
                 [](uint64_t sum, KVPair el)
                 { send_recv_totals_t *obj =
                           (send_recv_totals_t*)el.second->data();
                    return sum + obj->sends; };
  std::function<uint64_t(uint64_t, KVPair)> recvSum =
                 [](uint64_t sum, KVPair el)
                 { send_recv_totals_t *obj =
                           (send_recv_totals_t*)el.second->data();
                    return sum + obj->recvs; };
  std::function<uint64_t(uint64_t, KVPair)> sendCountSum =
                 [](uint64_t sum, KVPair el)
                 { send_recv_totals_t *obj =
                           (send_recv_totals_t*)el.second->data();
                    return sum + obj->sendCounts; };
  std::function<uint64_t(uint64_t, KVPair)> recvCountSum =
                 [](uint64_t sum, KVPair el)
                 { send_recv_totals_t *obj =
                           (send_recv_totals_t*)el.second->data();
                    return sum + obj->recvCounts; };
  std::function<string(string, KVPair)> indivStats =
                    [](string str, KVPair el)
                    { send_recv_totals_t *obj =
                              (send_recv_totals_t*)el.second->data();
                      ostringstream o;
                      o << str
                        <<  "Rank-" << std::to_string(obj->rank) << ": "
                        << std::to_string(obj->sends) << ", "
                        << std::to_string(obj->recvs) << ", "
                        << std::to_string(obj->sendCounts) << ", "
                        << std::to_string(obj->recvCounts) << ";\n";
                      return o.str(); };
  uint64_t totalSends = std::accumulate(map->begin(), map->end(),
                                        (uint64_t)0, sendSum);
  uint64_t totalRecvs = std::accumulate(map->begin(), map->end(),
                                        (uint64_t)0, recvSum);
  uint64_t totalSendCounts = std::accumulate(map->begin(), map->end(),
                                        (uint64_t)0, sendCountSum);
  uint64_t totalRecvCounts = std::accumulate(map->begin(), map->end(),
                                        (uint64_t)0, recvCountSum);
  string individuals = std::accumulate(map->begin(), map->end(),
                                       string(""), indivStats);
  ostringstream o;
  o << MPI_SEND_RECV_DB << ": Total Sends: " << totalSends << "; ";
  o << "Total Recvs: " << totalRecvs << ";";
  o << "Total Send counts: " << totalSendCounts << ";";
  o << "Total Recv counts: " << totalRecvCounts << std::endl;
  o << "  Individual Stats: " << individuals;
  printf("%s\n", o.str().c_str());
  fflush(stdout);

  const KeyValueMap* map2 = lookupService.getMap(MPI_WRAPPER_DB);
  if (!map2) {
    JTRACE("No wrapper database");
    return;
  }

  std::function<string(string, KVPair)> rankStats =
                    [](string str, KVPair el)
                    { wr_counts_t *obj = (wr_counts_t*)el.second->data();
                      int rank = *(int*)el.first.data();
                      ostringstream o;
                      o << str
                        <<  "Rank-" << std::to_string(rank) << ": "
                        << std::to_string(obj->sendCount) << ", "
                        << std::to_string(obj->isendCount) << ", "
                        << std::to_string(obj->recvCount) << ", "
                        << std::to_string(obj->irecvCount) << ", "
                        << std::to_string(obj->sendrecvCount) << ";\n";
                      return o.str(); };

  individuals = std::accumulate(map->begin(), map->end(),
                                string(""), rankStats);
  ostringstream o2;
  o2 << MPI_WRAPPER_DB << std::endl;
  o2 << "  Individual Stats: " << individuals;
  printf("%s\n", o2.str().c_str());
  fflush(stdout);
}

#if 1
string
getClientState(CoordClient *client)
{
  ostringstream o;
# if 0
  o << ", " << clientStates[client].rank
    << "/" << clientStates[client].st
    << "/" << (void *)clientStates[client].comm;
# else
  o << ", " << clientRanks[client]
    << "/" << clientPhases[client]
    << "/" << (void *)(unsigned long)clientGids[client];
# endif
  return o.str();
}
#else
string
getClientState(CoordClient *client)
{
  ostringstream o;
  o << ", " << clientPhases[client];
  return o.str();
}
#endif

void
processPreSuspendClientMsgHelper(DmtcpCoordinator *coord,
                                 CoordClient *client,
                                 int &workersAtCurrentBarrier,
                                 const DmtcpMessage& msg,
                                 const void *extraData)
{
  static bool firstTime = true;
  if (firstTime) {
    JTIMER_START(twoPc);
    firstTime = false;
  }
  ComputationStatus status = coord->getStatus();

  // Verify correctness of the response
  if (msg.extraBytes != sizeof(rank_state_t) || !extraData) {
    JWARNING(false)(msg.from)(msg.state)(msg.extraBytes)
            .Text("Received msg with no (or invalid) state information!");
    return;
  }

  // First, insert client's response in our map
  rank_state_t state = *(rank_state_t*)extraData;
  clientStates[client] = state;
  clientPhases[client] = state.st;
  // FIXME: Why can't we use `state'
  clientRanks[client] = state.rank;
  clientGids[client] = state.comm;

  JTRACE("Received pre-suspend response from client")(msg.from)(state.st)
        (state.comm)(state.rank);

  // Next, return early, if we haven't received acks from all clients
  if (!status.minimumStateUnanimous ||
      workersAtCurrentBarrier < status.numPeers) {
    return;
  }
  JTRACE("Received pre-suspend response from all ranks");
  printf("%s\n", prog.printList().c_str());

  JTRACE("Checking if we can send the checkpoint message");
  // We are ready! If all clients responded with IN_READY, inform the
  // ranks of the imminent checkpoint msg and wait for their final approvals...
  if (allRanksReady(clientStates, status.numPeers)) {
    // JTRACE("All ranks ready for checkpoint; broadcasting ckpt msg...");
    // query_t q(CKPT);
    // coord->broadcastMessage(DMT_DO_PRE_SUSPEND, sizeof q, &q);
    JTRACE("All ranks ready for checkpoint; broadcasting suspend msg...");
    JTIMER_STOP(twoPc);
    coord->startCheckpoint();
    goto done;
  }
  JTRACE("Checking if some ranks are in critical section");

  JTRACE("Trying to unblocks ranks in PHASE_1 and PHASE_2");

  // Finally, if there are ranks stuck in PHASE_1 or in PHASE_2 (is PHASE_2
  // required??), unblock them. This can happen only if there are ranks
  // executing in critical section.
  unblockRanks(clientStates, status.numPeers);
done:
  workersAtCurrentBarrier = 0;
  clientStates.clear();
}

void
sendCkptIntentMsg(dmtcp::DmtcpCoordinator *coord)
{
  query_t q(INTENT);
  coord->broadcastMessage(DMT_DO_PRE_SUSPEND, sizeof q, &q);
}

static std::ostream&
operator<<(std::ostream &os, const phase_t &st)
{
  switch (st) {
    case ST_ERROR           : os << "ST_ERROR"; break;
    case ST_UNKNOWN         : os << "ST_UNKNOWN"; break;
    case IS_READY           : os << "IS_READY"; break;
    case PHASE_1            : os << "PHASE_1"; break;
    case IN_CS              : os << "IN_CS"; break;
    case IN_TRIVIAL_BARRIER : os << "IN_TRIVIAL_BARRIER"; break;
    default                 : os << "Unknown state"; break;
  }
  return os;
}

static bool
allRanksReady(const ClientToStateMap& clientStates, long int size)
{
  for (RankKVPair c : clientStates) {
    if (c.second.st == IN_CS) {
      return false;
    }
  }
  return true;
}

static void
unblockRanks(const ClientToStateMap& clientStates, long int size)
{
  query_t *queries = (query_t*) malloc(size * sizeof(query_t));
  memset(queries, NONE, size * sizeof(query_t));

  // if some member of a communicator is in the critical section,
  // give free passes to members in phase 1 or the trivial barrier.
  for (RankKVPair c : clientStates) {
    if (queries[c.second.rank] != NONE) {
      continue; // the message is already decided
    }
    if (c.second.st == IN_CS) {
      queries[c.second.rank] = WAIT_STRAGGLER;
      for (RankKVPair other : clientStates) {
        if (other.second.comm == c.second.comm &&
            other.second.st == PHASE_1) {
          queries[other.second.rank] = FREE_PASS;
        }
      }
    }
  }

  // other ranks just wait for a iteration.
  for (RankKVPair c : clientStates) {
    if (queries[c.second.rank] == NONE) {
      queries[c.second.rank] = WAIT_STRAGGLER;
    }
  }

  // send all queires
  for (RankKVPair c : clientStates) {
    DmtcpMessage msg(DMT_DO_PRE_SUSPEND);
    query_t q = queries[c.second.rank];
    printf("Sending %d to rank %d\n", q, c.second.rank);
    fflush(stdout);
    JTRACE("Sending query to client")(q)
      (c.second.rank)(c.second.comm)(c.second.st);
    msg.extraBytes = sizeof q;
    c.first->sock() << msg;
    c.first->sock().writeAll((const char*)&q, msg.extraBytes);
  }
  free(queries);
}
