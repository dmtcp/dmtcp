#include <ucontext.h>
#include "dmtcp.h"
#include "coordinatorapi.h"

#include "record-replay.h"
#include "two-phase-algo.h"
#include "virtual-ids.h"
#include "siginfo.h"

using namespace dmtcp_mpi;

struct twoPhaseHistory {
  int lineNo;
  int _comm;
  int comm;
  phase_t state;
  phase_t currState;
};
#define commStateHistoryLength 100
struct twoPhaseHistory commStateHistory[commStateHistoryLength];
int commStateHistoryLast = -1;
// USAGE:
//  commStateHistoryAdd(
//    {.lineNo = __LINE__, .comm = comm, ._comm = _comm,
//     .state = state, .currState = getCurrState()});
//  Use _commAndStateMutex.lock/unlock if not already present.
void commStateHistoryAdd(struct twoPhaseHistory item) {
  commStateHistory[++commStateHistoryLast % commStateHistoryLength] = item;
}

void
drainMpiCollectives(const void *data)
{
  TwoPhaseAlgo::instance().preSuspendBarrier(data);
}

void
clearPendingCkpt()
{
  TwoPhaseAlgo::instance().clearCkptPending();
}

void
resetTwoPhaseState()
{
  TwoPhaseAlgo::instance().resetStateAfterCkpt();
}

void
logIbarrierIfInTrivBarrier()
{
  TwoPhaseAlgo::instance().logIbarrierIfInTrivBarrier();
}


void
save2pcGlobals()
{
  // FIXME: not used right now, but useful if we want to save and restore some
  // important variables before and after replaying MPI functions
}

void
restore2pcGlobals()
{
  // FIXME: not used right now, but useful if we want to save and restore some
  // important variables before and after replaying MPI functions
  TwoPhaseAlgo::instance().replayTrivialBarrier();
}

using namespace dmtcp_mpi;

int
TwoPhaseAlgo::commit(MPI_Comm comm, const char *collectiveFnc,
                     std::function<int(void)>doRealCollectiveComm)
{
  if (!LOGGING() || comm == MPI_COMM_NULL) {
    return doRealCollectiveComm(); // lambda function: already captured args
  }

  if (!LOGGING()) {
    return doRealCollectiveComm();
  }

  commit_begin(comm);
  int retval = doRealCollectiveComm();
  commit_finish();
  return retval;
}

void
TwoPhaseAlgo::commit_begin(MPI_Comm comm)
{
  _commAndStateMutex.lock();
  // maintain consistent view for DMTCP coordinator
  commStateHistoryAdd(
    {.lineNo = __LINE__, ._comm = _comm, .comm = -1,
     .state = ST_UNKNOWN, .currState = getCurrState()});
  wrapperEntry(comm);
  setCurrState(IN_TRIVIAL_BARRIER);
  commStateHistoryAdd(
    {.lineNo = __LINE__, ._comm = _comm, .comm = -1,
     .state = ST_UNKNOWN, .currState = getCurrState()});
  _commAndStateMutex.unlock();

  // Call the trivial barrier
  DMTCP_PLUGIN_DISABLE_CKPT();
  setCurrState(IN_TRIVIAL_BARRIER);
  // Set state again incase we returned from beforeTrivialBarrier
  MPI_Request request;
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  int flag = 0;
  int tb_rc = -1;
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  tb_rc = NEXT_FUNC(Ibarrier)(realComm, &request);
  RETURN_TO_UPPER_HALF();
  MPI_Request virtRequest = ADD_NEW_REQUEST(request);
  _request = virtRequest;
  JASSERT(tb_rc == MPI_SUCCESS)
    .Text("The trivial barrier in two-phase-commit algorithm failed");
  DMTCP_PLUGIN_ENABLE_CKPT();
#if 0
  while (!flag && request != MPI_REQUEST_NULL) {
    int rc;
    rc = MPI_Test(&request, &flag, MPI_STATUS_IGNORE);
#ifdef DEBUG
    JASSERT(rc == MPI_SUCCESS)(rc)(realComm)(request)(comm);
#endif
    // FIXME: make this smaller
    struct timespec test_interval = {.tv_sec = 0, .tv_nsec = 100000};
    nanosleep(&test_interval, NULL);
  }
#else
  MPI_Wait(&_request, MPI_STATUS_IGNORE);
#endif

  if (isCkptPending()) {
    stop(comm);
  }
  REMOVE_OLD_REQUEST(_request);
  _request = MPI_REQUEST_NULL;
  setCurrState(IN_CS);
}

void
TwoPhaseAlgo::commit_finish()
{
  _commAndStateMutex.lock();
  // maintain consistent view for DMTCP coordinator
  commStateHistoryAdd(
    {.lineNo = __LINE__, ._comm = _comm, .comm = -1,
     .state = ST_UNKNOWN, .currState = getCurrState()});
  setCurrState(IS_READY);
  wrapperExit();
  commStateHistoryAdd(
    {.lineNo = __LINE__, ._comm = _comm, .comm = -1,
     .state = ST_UNKNOWN, .currState = getCurrState()});
  _commAndStateMutex.unlock();
}

void
TwoPhaseAlgo::logIbarrierIfInTrivBarrier()
{
  JASSERT(!phase1_freepass)
    .Text("phase1_freepass should be false when about to checkpoint");
  phase_t st = getCurrState();
  if (st == IN_TRIVIAL_BARRIER || st == PHASE_1) {
    _replayTrivialBarrier = true;
  } else {
    _replayTrivialBarrier = false;
  }
}

void
TwoPhaseAlgo::replayTrivialBarrier()
{
  if (_replayTrivialBarrier) {
    MPI_Request request;
    MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(_comm);
    int flag = 0;
    int tb_rc = -1;
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    tb_rc = NEXT_FUNC(Ibarrier)(realComm, &request);
    RETURN_TO_UPPER_HALF();
    UPDATE_REQUEST_MAP(_request, request);
    _replayTrivialBarrier = false;
  }
}

void
TwoPhaseAlgo::preSuspendBarrier(const void *data)
{
  JASSERT(data).Text("Pre-suspend barrier called with NULL data!");
  DmtcpMessage msg(DMT_PRE_SUSPEND_RESPONSE);

  phase_t st = getCurrState();
  query_t query = *(query_t*)data;
  static int procRank = -1;

  if (procRank == -1) {
    JASSERT(MPI_Comm_rank(MPI_COMM_WORLD, &procRank) == MPI_SUCCESS &&
            procRank != -1);
  }

  switch (query) {
    case INTENT:
      setCkptPending();
      if (getCurrState() == PHASE_1) {
        // If in PHASE_1, wait for us to finish doing IN_CS
        if (waitForNewStateAfter(PHASE_1) != IN_CS) {
          break;
        } else {
          while (waitForNewStateAfter(IN_CS) != IN_CS);
        }
      }
      break;
    case FREE_PASS:
      phase1_freepass = true;
      // If in PHASE_1, wait for us to finish PHASE_1 (to enter IN_CS)
      while (waitForNewStateAfter(ST_UNKNOWN) == PHASE_1);
      break;
    case WAIT_STRAGGLER:
      // Maybe some peers in critical section and some in PHASE_1 or
      // IN_TRIVIAL_BARRIER. This may happen if a checkpoint pending is
      // announced after some member already entered the critical
      // section. Then the coordinator will send
      // WAIT_STRAGGLER to those ranks in the critical section.
      // In this case, we just report back the current state.
      // But the user thread can continue running and enter IS_READY,
      // IN_TRIVIAL_BARRIER for a different communication, etc.
      break;
    default:
      JWARNING(false)(query).Text("Unknown query from coordinator");
      break;
  }
  _commAndStateMutex.lock();
  commStateHistoryAdd(
    {.lineNo = __LINE__, ._comm = _comm, .comm = -1,
     .state = ST_UNKNOWN, .currState = getCurrState()});
  // maintain consistent view for DMTCP coordinator
  st = getCurrState();
  int gid = VirtualGlobalCommId::instance().getGlobalId(_comm);
  commStateHistoryAdd(
    {.lineNo = __LINE__, ._comm = _comm, .comm = gid,
     .state = st, .currState = getCurrState()});
  _commAndStateMutex.unlock();
  rank_state_t state = { .rank = procRank, .comm = gid, .st = st};
  _commAndStateMutex.lock();
  commStateHistoryAdd(
    {.lineNo = __LINE__, ._comm = _comm, .comm = gid,
     .state = st, .currState = getCurrState()});
  JASSERT(state.comm != MPI_COMM_NULL || state.st == IS_READY)
	 (state.comm)(state.st)(gid)(_currState)(query);
  commStateHistoryAdd(
    {.lineNo = __LINE__, ._comm = _comm, .comm = gid,
     .state = st, .currState = getCurrState()});
  _commAndStateMutex.unlock();
  JTRACE("Sending DMT_PRE_SUSPEND_RESPONSE message")(procRank)(gid)(st);
  msg.extraBytes = sizeof state;
  informCoordinatorOfCurrState(msg, &state);
}


// Local functions

bool
TwoPhaseAlgo::isInBarrier() {
  lock_t lock(_phaseMutex);
  return _currState == IN_TRIVIAL_BARRIER;
}

void
TwoPhaseAlgo::stop(MPI_Comm comm)
{
  // We got here because we must have received a ckpt-intent msg from
  // the coordinator
  // INVARIANT: Ckpt should be pending when we get here
  JASSERT(isCkptPending());
  setCurrState(PHASE_1);

  while (isCkptPending() && !phase1_freepass) {
    sleep(1);
  }
  phase1_freepass = false;
}

phase_t
TwoPhaseAlgo::waitForNewStateAfter(phase_t oldState)
{
  lock_t lock(_phaseMutex);
  // The user thread will notify us if transition to any of these states
  _phaseCv.wait(lock, [this, oldState]
                      { return _currState != oldState &&
                        ( _currState == IN_TRIVIAL_BARRIER ||
                          _currState == PHASE_1 ||
                          _currState == IN_CS ||
                          _currState == IS_READY); });
  return _currState;
}

bool
TwoPhaseAlgo::informCoordinatorOfCurrState(const DmtcpMessage& msg,
                                           const void *extraData)
{
  // This is a weird API.. doesn't return success or failure
  CoordinatorAPI::sendMsgToCoordinator(msg, extraData, msg.extraBytes);
  return true;
}

void
TwoPhaseAlgo::wrapperEntry(MPI_Comm comm)
{
  lock_t lock(_wrapperMutex);
  _inWrapper = true;
  _comm = comm;
}

void
TwoPhaseAlgo::wrapperExit()
{
  lock_t lock(_wrapperMutex);
  JASSERT(_currState == IS_READY);
  _inWrapper = false;
  _comm = MPI_COMM_NULL;
}
