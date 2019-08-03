#include "workerstate.h"
#include "dmtcpalloc.h"

#include "jassert.h"

namespace dmtcp
{
namespace WorkerState
{
static eWorkerState workerState = WorkerState::UNKNOWN;

void
setCurrentState(const eWorkerState &value)
{
  workerState = value;
}

eWorkerState
currentState()
{
  return workerState;
}

ostream&
operator<<(ostream &o, const eWorkerState &s)
{
  o << "WorkerState::";
  switch (s) {
  case WorkerState::UNKNOWN:       o << "UNKNOWN"; break;
  case WorkerState::RUNNING:       o << "RUNNING"; break;
  case WorkerState::PRESUSPEND:    o << "PRESUSPEND"; break;
  case WorkerState::SUSPENDED:     o << "SUSPENDED"; break;
  case WorkerState::CHECKPOINTING: o << "CHECKPOINTING"; break;
  case WorkerState::CHECKPOINTED:  o << "CHECKPOINTED"; break;
  case WorkerState::RESTARTING:    o << "RESTARTING"; break;
  default:
    JASSERT(false) (workerState).Text("Invalid WorkerState");
    break;
  }
  return o;
}
} // namespace WorkerState
} // namespace dmtcp
