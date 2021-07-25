#ifndef TWO_PHASE_ALGO_H
#define TWO_PHASE_ALGO_H

#include <mpi.h>

#include <condition_variable>
#include <functional>
#include <mutex>

#include "jassert.h"
#include "dmtcpmessagetypes.h"
#include "workerstate.h"
#include "mana_coord_proto.h"
#include "split_process.h"

// Convenience macro
#define twoPhaseCommit(comm, fnc) \
        dmtcp_mpi::TwoPhaseAlgo::instance().commit(comm, __FUNCTION__, fnc)

using namespace dmtcp;

namespace dmtcp_mpi
{
  using mutex_t = std::mutex;
  using cv_t = std::condition_variable;
  using lock_t  = std::unique_lock<mutex_t>;
  using lg_t  = std::lock_guard<mutex_t>;

  // This class encapsulates the two-phase MPI collecitve algorithm and the
  // corresponding checkpointing protocol for MANA
  class TwoPhaseAlgo
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif

      // Returns the singleton instance of the class
      static TwoPhaseAlgo& instance()
      {
        static TwoPhaseAlgo algo;
        return algo;
      }

      // Sets '_ckptPending' to false, indicating that the checkpointing
      // finished successfully
      void clearCkptPending()
      {
        lock_t lock(_ckptPendingMutex);
        _ckptPending = false;
      }

      // Resets the client state after a checkpoint.
      void resetStateAfterCkpt()
      {
        _currState = IS_READY;
      }

      // Return true if _currState == IN_BARRIER
      bool isInBarrier();

      // The main function of the two-phase protocol for MPI collectives
      int commit(MPI_Comm , const char* , std::function<int(void)> );
      void commit_begin(MPI_Comm);
      void commit_finish();

      // Implements the pre-suspend checkpointing protocol for coordination
      // between DMTCP coordinator and peers
      void preSuspendBarrier(const void *);

      // Log the MPI_Ibarrier call if we checkpoint in trivial barrier or phase1
      void logIbarrierIfInTrivBarrier();

      // Replay the trivial barrier if _replayTrivialBarrier true
      void replayTrivialBarrier();

    private:

      // Private constructor
      TwoPhaseAlgo()
        : _currState(IS_READY),
          _ckptPendingMutex(), _phaseMutex(),
          _wrapperMutex(),
          _commAndStateMutex(),
          _phaseCv(),
          _comm(MPI_COMM_NULL),
          _request(MPI_REQUEST_NULL),
          _inWrapper(false),
          _ckptPending(false),
          phase1_freepass(false),
          _replayTrivialBarrier(false)
      {
      }

      // Returns the current checkpointing state: '_currState' of the rank
      phase_t getCurrState()
      {
        lock_t lock(_phaseMutex);
        return _currState;
      }

      // Sets the current checkpointing state: '_currState' of the rank to the
      // given state 'st'
      void setCurrState(phase_t st)
      {
        lock_t lock(_phaseMutex);
        _currState = st;
        _phaseCv.notify_one();
      }

      // Returns true if we are currently executing in an MPI collective wrapper
      // function
      bool isInWrapper()
      {
        return _inWrapper;
      }

      // Returns true if a checkpoint intent message was received from the
      // coordinator and we haven't yet finished checkpointing
      bool isCkptPending()
      {
        lock_t lock(_ckptPendingMutex);
        return _ckptPending;
      }

      // Sets '_ckptPending' to true to indicate that a checkpoint intent
      // message was received from the the coordinator
      void setCkptPending()
      {
        lock_t lock(_ckptPendingMutex);
        _ckptPending = true;
        setCurrentState(WorkerState::PRE_SUSPEND);
      }

      // Stopping point before entering and after exiting the actual MPI
      // collective call to avoid domino effect and provide bounds on
      // checkpointing time. 'comm' indicates the MPI communicator used
      // for the collective call, and 'p' is the current phase.
      void stop(MPI_Comm);

      // Wait until the state is changed to a new state
      phase_t waitForNewStateAfter(phase_t oldState);

      // Sends the given message 'msg' (along with the given 'extraData') to
      // the coordinator
      bool informCoordinatorOfCurrState(const DmtcpMessage& , const void* );

      // Sets '_inWrapper' to true and sets '_comm' to the given 'comm'
      void wrapperEntry(MPI_Comm );

      // Sets '_inWrapper' to false
      void wrapperExit();

      // Returns true if we are executing in an MPI collective wrapper function
      bool inWrapper();

      // Checkpointing state of the current process (MPI rank)
      phase_t _currState;

      // Lock to protect accesses to '_ckptPending'
      mutex_t _ckptPendingMutex;

      // Lock to protect accesses to '_currState'
      mutex_t _phaseMutex;

      // Lock to protect accesses to '_inWrapper'
      mutex_t _wrapperMutex;

      // lock to make atomic read/write for comm and state
      mutex_t _commAndStateMutex;

      // Condition variable to wait-signal based on the state of '_currState'
      cv_t _phaseCv;

      // MPI communicator corresponding to the current MPI collective call
      MPI_Comm _comm;

      // MPI request used in the trivial barrier.
      MPI_Request _request;

      // True if a free-pass message was received from the coordinator
      bool _freePass;

      // True if we have entered an MPI collective wrapper function
      // TODO: Use C++ atomics
      bool _inWrapper;

      // True if a checkpoint intent message was received from the coordinator
      // and we haven't yet finished checkpointing
      // TODO: Use C++ atomics
      bool _ckptPending;

      // True if a freepass is given by the coordinator
      bool phase1_freepass;

      // True if we need to replay trivial barrier on restart
      bool _replayTrivialBarrier;
  };
};

// Forces the current process to synchronize with the coordinator in order to
// get to a globally safe state for checkpointing
extern void drainMpiCollectives(const void* );

// Clears the pending checkpoint state for the two-phase checkpointing algo
extern void clearPendingCkpt();

// Log the MPI_Ibarrier call if we checkpoint in trivial barrier or phase1
extern void logIbarrierIfInTrivBarrier();

// Save and restore global variables that may be changed during
// restart events. These are called from mpi_plugin.cpp using
// DMTCP_PRIVATE_BARRIER_RESTART.
extern void save2pcGlobals();
extern void restore2pcGlobals();
#endif // ifndef TWO_PHASE_ALGO_H
