#include "dmtcpalloc.h"
#include "workerstate.h"

#include "jassert.h"

namespace dmtcp
{
namespace WorkerState
{

static eWorkerState workerState = WorkerState::UNKNOWN;

void setCurrentState(const eWorkerState& value)
{
  workerState = value;
}


eWorkerState currentState()
{
  return workerState;
}


ostream& operator << (ostream& o, const eWorkerState& s)
{
  o << "WorkerState::";
  switch (s) {
    case WorkerState::UNKNOWN:      o << "UNKNOWN"; break;
    case WorkerState::RUNNING:      o << "RUNNING"; break;
    case WorkerState::SUSPENDED:    o << "SUSPENDED"; break;
    case WorkerState::FD_LEADER_ELECTION:  o << "FD_LEADER_ELECTION"; break;
    case WorkerState::NAME_SERVICE_DATA_REGISTERED: o << "NAME_SERVICE_DATA_REGISTERED"; break;
    case WorkerState::DONE_QUERYING: o << "DONE_QUERYING"; break;
    case WorkerState::DRAINED:      o << "DRAINED"; break;
    case WorkerState::RESTARTING:   o << "RESTARTING"; break;
    case WorkerState::CHECKPOINTED: o << "CHECKPOINTED"; break;
    case WorkerState::REFILLED:     o << "REFILLED"; break;
    default:
      JASSERT(false) (workerState) .Text("Invalid WorkerState");
      break;
  }
  return o;
}

} // namespace WorkerState
} // namespace dmtcp
