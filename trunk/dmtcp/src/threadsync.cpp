/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <stdlib.h>
#include <unistd.h>
#include <map>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/limits.h>

#include "constants.h"
#include "threadsync.h"
#include "util.h"
#include "dmtcpworker.h"
#include "dmtcpmessagetypes.h"
#include "../jalib/jconvert.h"
#include "../jalib/jalloc.h"
#include "../jalib/jfilesystem.h"

/*
 * WrapperProtectionLock is used to make the checkpoint safe by making sure
 *   that no user-thread is executing any DMTCP wrapper code when it receives
 *   the checkpoint signal.
 * Working:
 *   On entering the wrapper in DMTCP, the user-thread acquires the read lock,
 *     and releases it before leaving the wrapper.
 *   When the Checkpoint-thread wants to send the SUSPEND signal to user
 *     threads, it must acquire the write lock. It is blocked until all the
 *     existing read-locks by user threads have been released. NOTE that this
 *     is a WRITER-PREFERRED lock.
 *
 * There is a corner case too -- the newly created thread that has not been
 *   initialized yet; we need to take some extra efforts for that.
 * Here are the steps to handle the newly created uninitialized thread:
 *   A counter for the number of newly created uninitialized threads is kept.
 *     The counter is made thread safe by using a mutex.
 *   The calling thread (parent) increments the counter before calling clone.
 *   The newly created child thread decrements the counter at the end of
 *     initialization in MTCP/DMTCP.
 *   After acquiring the Write lock, the checkpoint thread waits until the
 *     number of uninitialized threads is zero. At that point, no thread is
 *     executing in the clone wrapper and it is safe to do a checkpoint.
 *
 * XXX: Currently this security is provided only for the clone wrapper; this
 * should be extended to other calls as well.           -- KAPIL
 */
// NOTE: PTHREAD_RWLOCK_WRITER_NONRECURSIVE_INITIALIZER_NP is not POSIX.
static pthread_rwlock_t
  _wrapperExecutionLock = PTHREAD_RWLOCK_WRITER_NONRECURSIVE_INITIALIZER_NP;
static pthread_rwlock_t
  _threadCreationLock = PTHREAD_RWLOCK_WRITER_NONRECURSIVE_INITIALIZER_NP;
static bool _wrapperExecutionLockAcquiredByCkptThread = false;
static bool _threadCreationLockAcquiredByCkptThread = false;

static pthread_mutex_t destroyDmtcpWorkerLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t theCkptCanStart = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

static pthread_mutex_t uninitializedThreadCountLock = PTHREAD_MUTEX_INITIALIZER;
static int _uninitializedThreadCount = 0;
static bool _checkpointThreadInitialized = false;

#define INVALID_USER_THREAD_COUNT 0
static int preResumeThreadCount = INVALID_USER_THREAD_COUNT;
static pthread_mutex_t preResumeThreadCountLock = PTHREAD_MUTEX_INITIALIZER;

static __thread int _wrapperExecutionLockLockCount = 0;
static __thread int _threadCreationLockLockCount = 0;
static __thread bool _threadPerformingDlopenDlsym = false;
static __thread bool _sendCkptSignalOnFinalUnlock = false;
static __thread bool _isOkToGrabWrapperExecutionLock = true;
static __thread bool _hasThreadFinishedInitialization = false;


void dmtcp::ThreadSync::acquireLocks()
{
  JASSERT(WorkerState::currentState() == WorkerState::RUNNING);

  JTRACE("waiting for dmtcp_lock():"
         " to get synchronized with _runCoordinatorCmd if we use DMTCP API");
  _dmtcp_lock();

  JTRACE("Waiting for lock(&theCkptCanStart)");
  JASSERT(_real_pthread_mutex_lock(&theCkptCanStart) == 0)(JASSERT_ERRNO);

  JTRACE("Waiting for threads creation lock");
  JASSERT(_real_pthread_rwlock_wrlock(&_threadCreationLock) == 0)
    (JASSERT_ERRNO);
  _threadCreationLockAcquiredByCkptThread = true;

  JTRACE("Waiting for other threads to exit DMTCP-Wrappers");
  JASSERT(_real_pthread_rwlock_wrlock(&_wrapperExecutionLock) == 0)
    (JASSERT_ERRNO);
  _wrapperExecutionLockAcquiredByCkptThread = true;

  JTRACE("Waiting for newly created threads to finish initialization")
    (_uninitializedThreadCount);
  waitForThreadsToFinishInitialization();

  unsetOkToGrabLock();
  JTRACE("Done acquiring all locks");
}

void dmtcp::ThreadSync::releaseLocks()
{
  JASSERT(WorkerState::currentState() == WorkerState::SUSPENDED);

  JTRACE("Releasing ThreadSync locks");
  JASSERT(_real_pthread_rwlock_unlock(&_wrapperExecutionLock) == 0)
    (JASSERT_ERRNO);
  _wrapperExecutionLockAcquiredByCkptThread = false;
  JASSERT(_real_pthread_rwlock_unlock(&_threadCreationLock) == 0)
    (JASSERT_ERRNO);
  _threadCreationLockAcquiredByCkptThread = false;
  JASSERT(_real_pthread_mutex_unlock(&theCkptCanStart) == 0)
    (JASSERT_ERRNO);

  _dmtcp_unlock();
  setOkToGrabLock();
}

void dmtcp::ThreadSync::resetLocks()
{
  pthread_rwlock_t newLock = PTHREAD_RWLOCK_WRITER_NONRECURSIVE_INITIALIZER_NP;
  _wrapperExecutionLock = newLock;
  _threadCreationLock = newLock;

  _wrapperExecutionLockLockCount = 0;
  _threadCreationLockLockCount = 0;

  pthread_mutex_t newCountLock = PTHREAD_MUTEX_INITIALIZER;
  uninitializedThreadCountLock = newCountLock;
  pthread_mutex_t newPreResumeThreadCountLock = PTHREAD_MUTEX_INITIALIZER;
  preResumeThreadCountLock = newPreResumeThreadCountLock;

  pthread_mutex_t newDestroyDmtcpWorker = PTHREAD_MUTEX_INITIALIZER;
  destroyDmtcpWorkerLock = newDestroyDmtcpWorker;

  _checkpointThreadInitialized = false;
  _wrapperExecutionLockAcquiredByCkptThread = false;
  _threadCreationLockAcquiredByCkptThread = false;
}

bool dmtcp::ThreadSync::isThisThreadHoldingAnyLocks()
{
  // If the wrapperExec lock has been acquired by the ckpt thread, then we are
  // certainly not holding it :). It's possible for the count to be still '1',
  // as it may happen that the thread got suspended after releasing the lock
  // and before decrementing the lock-count.
  return (_wrapperExecutionLockAcquiredByCkptThread == false ||
          _threadCreationLockAcquiredByCkptThread == false) &&
         (_threadCreationLockLockCount > 0 ||
          _wrapperExecutionLockLockCount > 0 ||
          _hasThreadFinishedInitialization == false);
}

bool dmtcp::ThreadSync::isOkToGrabLock()
{
  return _isOkToGrabWrapperExecutionLock;
}

void dmtcp::ThreadSync::setOkToGrabLock()
{
  _isOkToGrabWrapperExecutionLock = true;
}

void dmtcp::ThreadSync::unsetOkToGrabLock()
{
  _isOkToGrabWrapperExecutionLock = false;
}

void dmtcp::ThreadSync::setSendCkptSignalOnFinalUnlock()
{
  JASSERT(_sendCkptSignalOnFinalUnlock == false);
  _sendCkptSignalOnFinalUnlock = true;
}

void dmtcp::ThreadSync::sendCkptSignalOnFinalUnlock()
{
  if (_sendCkptSignalOnFinalUnlock && isThisThreadHoldingAnyLocks() == false) {
    _sendCkptSignalOnFinalUnlock = false;
    JASSERT(raise(DmtcpWorker::determineMtcpSignal()) == 0)
      (getpid()) (gettid()) (JASSERT_ERRNO);
  }
}

extern "C" LIB_PRIVATE
void dmtcp_setThreadPerformingDlopenDlsym()
{
  dmtcp::ThreadSync::setThreadPerformingDlopenDlsym();
}

extern "C" LIB_PRIVATE
void dmtcp_unsetThreadPerformingDlopenDlsym()
{
  dmtcp::ThreadSync::unsetThreadPerformingDlopenDlsym();
}

bool dmtcp::ThreadSync::isThreadPerformingDlopenDlsym()
{
  return _threadPerformingDlopenDlsym;
}

void dmtcp::ThreadSync::setThreadPerformingDlopenDlsym()
{
  _threadPerformingDlopenDlsym = true;
}

void dmtcp::ThreadSync::unsetThreadPerformingDlopenDlsym()
{
  _threadPerformingDlopenDlsym = false;
}

bool dmtcp::ThreadSync::isCheckpointThreadInitialized()
{
  return _checkpointThreadInitialized;
}

void dmtcp::ThreadSync::setCheckpointThreadInitialized()
{
  JASSERT(_checkpointThreadInitialized == false);
  _checkpointThreadInitialized = true;
}

void dmtcp::ThreadSync::destroyDmtcpWorkerLockLock()
{
  JASSERT(_real_pthread_mutex_lock(&destroyDmtcpWorkerLock) == 0)
    (JASSERT_ERRNO);
}

int dmtcp::ThreadSync::destroyDmtcpWorkerLockTryLock()
{
  return _real_pthread_mutex_trylock(&destroyDmtcpWorkerLock);
}

void dmtcp::ThreadSync::destroyDmtcpWorkerLockUnlock()
{
  JASSERT(_real_pthread_mutex_unlock(&destroyDmtcpWorkerLock) == 0)
    (JASSERT_ERRNO);
}

void dmtcp::ThreadSync::delayCheckpointsLock()
{
  JASSERT(_real_pthread_mutex_lock(&theCkptCanStart)==0)(JASSERT_ERRNO);
}

void dmtcp::ThreadSync::delayCheckpointsUnlock(){
  JASSERT(_real_pthread_mutex_unlock(&theCkptCanStart)==0)(JASSERT_ERRNO);
}

static void incrementWrapperExecutionLockLockCount()
{
  _wrapperExecutionLockLockCount++;
}

static void decrementWrapperExecutionLockLockCount()
{
  if (_wrapperExecutionLockLockCount <= 0) {
    JASSERT(false) (_wrapperExecutionLockLockCount)
      .Text("wrapper-execution lock count can't be negative");
  }
  _wrapperExecutionLockLockCount--;
  dmtcp::ThreadSync::sendCkptSignalOnFinalUnlock();
}

static void incrementThreadCreationLockLockCount()
{
  _threadCreationLockLockCount++;
}

static void decrementThreadCreationLockLockCount()
{
  _threadCreationLockLockCount--;
  dmtcp::ThreadSync::sendCkptSignalOnFinalUnlock();
}

// XXX: Handle deadlock error code
// NOTE: Don't do any fancy stuff in this wrapper which can cause the process
//       to go into DEADLOCK
bool dmtcp::ThreadSync::wrapperExecutionLockLock()
{
  int saved_errno = errno;
  bool lockAcquired = false;
  while (1) {
    if (WorkerState::currentState() == WorkerState::RUNNING &&
        isThreadPerformingDlopenDlsym() == false &&
        isCheckpointThreadInitialized() == true  &&
        isOkToGrabLock() == true) {
      incrementWrapperExecutionLockLockCount();
      int retVal = _real_pthread_rwlock_tryrdlock(&_wrapperExecutionLock);
      if (retVal != 0 && retVal == EBUSY) {
        decrementWrapperExecutionLockLockCount();
        struct timespec sleepTime = {0, 100*1000*1000};
        nanosleep(&sleepTime, NULL);
        continue;
      }
      if (retVal != 0 && retVal != EDEADLK) {
        fprintf(stderr, "ERROR %d at %s:%d %s: Failed to acquire lock\n",
                errno, __FILE__, __LINE__, __PRETTY_FUNCTION__);
        _exit(1);
      }
      // retVal should always be 0 (success) here.
      lockAcquired = retVal == 0 ? true : false;
      if (!lockAcquired) {
        decrementWrapperExecutionLockLockCount();
      }
    }
    break;
  }
  errno = saved_errno;
  return lockAcquired;
}

/*
 * Execute fork() and exec() wrappers in exclusive mode
 *
 * fork() and exec() wrappers pass on the state/information about the current
 * process/program to the to-be-created process/program.
 *
 * There can be a potential race in the wrappers if this information gets
 * changed between the point where it was acquired and the point where the
 * process/program is created. An example of this situation would be a
 * different thread executing an open() call in parallel creating a
 * file-descriptor, which is not a part of the information/state gathered
 * earlier. This can result in unexpected behavior and can cause the
 * program/process to fail.
 *
 * This patch fixes this by acquiring the Wrapper-protection-lock in exclusive
 * mode (write-lock) when executing these wrappers. This guarantees that no
 * other thread would be executing inside a wrapper that can change the process
 * state/information.
 *
 * NOTE:
 * 1. Currently, we do not have WRAPPER_EXECUTION_LOCK/UNLOCK for socket()
 * family of wrapper. That would be fixed in a later commit.
 * 2. We need to comeup with a strategy for certain blocking system calls that
 * can change the state of the process (e.g. accept).
 */
bool dmtcp::ThreadSync::wrapperExecutionLockLockExcl()
{
  int saved_errno = errno;
  bool lockAcquired = false;
  while (1) {
    if (WorkerState::currentState() == WorkerState::RUNNING &&
        isCheckpointThreadInitialized() == true) {
      incrementWrapperExecutionLockLockCount();
      int retVal = _real_pthread_rwlock_trywrlock(&_wrapperExecutionLock);
      if (retVal != 0 && retVal == EBUSY) {
        decrementWrapperExecutionLockLockCount();
        struct timespec sleepTime = {0, 100*1000*1000};
        nanosleep(&sleepTime, NULL);
        continue;
      }
      if (retVal != 0 && retVal != EDEADLK) {
        fprintf(stderr, "ERROR %s:%d %s: Failed to acquire lock\n",
                __FILE__, __LINE__, __PRETTY_FUNCTION__);
        _exit(1);
      }
      // retVal should always be 0 (success) here.
      lockAcquired = retVal == 0 ? true : false;
    }
    break;
  }
  if (!lockAcquired) {
    decrementWrapperExecutionLockLockCount();
  }
  errno = saved_errno;
  return lockAcquired;
}

// NOTE: Don't do any fancy stuff in this wrapper which can cause the process
// to go into DEADLOCK
void dmtcp::ThreadSync::wrapperExecutionLockUnlock()
{
  int saved_errno = errno;
  if (WorkerState::currentState() != WorkerState::RUNNING &&
      !DmtcpWorker::exitInProgress()) {
    fprintf(stderr, "DMTCP INTERNAL ERROR: %s:%d: %s\n"
            "       This process is not in RUNNING state and yet this thread\n"
            "       managed to acquire the wrapperExecutionLock.\n"
            "       This should not be happening, something is wrong.\n",
            __FILE__, __LINE__, __PRETTY_FUNCTION__);
    _exit(1);
  }
  if (_real_pthread_rwlock_unlock(&_wrapperExecutionLock) != 0) {
    fprintf(stderr, "ERROR %s:%d %s: Failed to release lock\n",
            __FILE__, __LINE__, __PRETTY_FUNCTION__);
    _exit(1);
  } else {
    decrementWrapperExecutionLockLockCount();
  }
  errno = saved_errno;
}

bool dmtcp::ThreadSync::threadCreationLockLock()
{
  int saved_errno = errno;
  bool lockAcquired = false;
  while (1) {
    if (WorkerState::currentState() == WorkerState::RUNNING) {
      incrementThreadCreationLockLockCount();
      int retVal = _real_pthread_rwlock_tryrdlock(&_threadCreationLock);
      if (retVal != 1 && retVal == EBUSY) {
        decrementThreadCreationLockLockCount();
        struct timespec sleepTime = {0, 100*1000*1000};
        nanosleep(&sleepTime, NULL);
        continue;
      }
      if (retVal != 0 && retVal != EDEADLK) {
        fprintf(stderr, "ERROR %s:%d %s: Failed to acquire lock\n",
                __FILE__, __LINE__, __PRETTY_FUNCTION__);
        _exit(1);
      }
      // retVal should always be 0 (success) here.
      lockAcquired = retVal == 0 ? true : false;
    }
    break;
  }
  if (!lockAcquired) {
    decrementThreadCreationLockLockCount();
  }
  errno = saved_errno;
  return lockAcquired;
}

void dmtcp::ThreadSync::threadCreationLockUnlock()
{
  int saved_errno = errno;
  if (WorkerState::currentState() != WorkerState::RUNNING) {
    fprintf(stderr, "DMTCP INTERNAL ERROR: %s:%d %s:\n"
            "       This process is not in RUNNING state and yet this thread\n"
            "       managed to acquire the threadCreationLock.\n"
            "       This should not be happening, something is wrong.",
            __FILE__, __LINE__, __PRETTY_FUNCTION__);
    _exit(1);
  }
  if (_real_pthread_rwlock_unlock(&_threadCreationLock) != 0) {
    fprintf(stderr, "ERROR %s:%d %s: Failed to release lock\n",
            __FILE__, __LINE__, __PRETTY_FUNCTION__);
    _exit(1);
  } else {
    decrementThreadCreationLockLockCount();
  }
  errno = saved_errno;
}

// GNU g++ uses __thread.  But the C++0x standard says to use thread_local.
//   If your compiler fails here, you can: change "__thread" to "thread_local";
//   or delete "__thread" (but if user code calls these routines from multiple
//   threads, it will not be thread-safe).
//   In GCC 4.3 and later, g++ supports -std=c++0x and -std=g++0x.
extern "C"
int dmtcp_plugin_disable_ckpt()
{
  return dmtcp::ThreadSync::wrapperExecutionLockLock();
}

extern "C"
void dmtcp_plugin_enable_ckpt()
{
  dmtcp::ThreadSync::wrapperExecutionLockUnlock();
}


void dmtcp::ThreadSync::waitForThreadsToFinishInitialization()
{
  while (_uninitializedThreadCount != 0) {
    struct timespec sleepTime = {0, 10*1000*1000};
    JTRACE("sleeping")(sleepTime.tv_nsec);
    nanosleep(&sleepTime, NULL);
  }
}

void dmtcp::ThreadSync::incrementUninitializedThreadCount()
{
  int saved_errno = errno;
  if (WorkerState::currentState() == WorkerState::RUNNING) {
    JASSERT(_real_pthread_mutex_lock(&uninitializedThreadCountLock) == 0)
      (JASSERT_ERRNO);
    _uninitializedThreadCount++;
    //JTRACE(":") (_uninitializedThreadCount);
    JASSERT(_real_pthread_mutex_unlock(&uninitializedThreadCountLock) == 0)
      (JASSERT_ERRNO);
  }
  errno = saved_errno;
}

void dmtcp::ThreadSync::decrementUninitializedThreadCount()
{
  int saved_errno = errno;
  if (WorkerState::currentState() == WorkerState::RUNNING) {
    JASSERT(_real_pthread_mutex_lock(&uninitializedThreadCountLock) == 0)
      (JASSERT_ERRNO);
    JASSERT(_uninitializedThreadCount > 0) (_uninitializedThreadCount);
    _uninitializedThreadCount--;
    //JTRACE(":") (_uninitializedThreadCount);
    JASSERT(_real_pthread_mutex_unlock(&uninitializedThreadCountLock) == 0)
      (JASSERT_ERRNO);
  }
  errno = saved_errno;
}

void dmtcp::ThreadSync::threadFinishedInitialization()
{
  decrementUninitializedThreadCount();
  _hasThreadFinishedInitialization = true;
}

void dmtcp::ThreadSync::incrNumUserThreads()
{
  // This routine is called from within stopthisthread so it is not safe to
  // call JNOTE/JTRACE etc.
  if (_real_pthread_mutex_lock(&preResumeThreadCountLock) != 0) {
    JASSERT(false) .Text("Failed to acquire preResumeThreadCountLock");
  }
  preResumeThreadCount++;
  if (_real_pthread_mutex_unlock(&preResumeThreadCountLock) != 0) {
    JASSERT(false) .Text("Failed to release preResumeThreadCountLock");
  }
}

void dmtcp::ThreadSync::processPreResumeCB()
{
  if (_real_pthread_mutex_lock(&preResumeThreadCountLock) != 0) {
    JASSERT(false) .Text("Failed to acquire preResumeThreadCountLock");
  }
  JASSERT(preResumeThreadCount > 0) (gettid()) (preResumeThreadCount);
  preResumeThreadCount--;
  if (_real_pthread_mutex_unlock(&preResumeThreadCountLock) != 0) {
    JASSERT(false) .Text("Failed to release preResumeThreadCountLock");
  }
}

void dmtcp::ThreadSync::waitForUserThreadsToFinishPreResumeCB()
{
  if (preResumeThreadCount != INVALID_USER_THREAD_COUNT) {
    while (preResumeThreadCount != 0) {
      struct timespec sleepTime = {0, 10*1000*1000};
      nanosleep(&sleepTime, NULL);
    }
  }
  // Now we wait for the lock to make sure that the user threads have released
  // it.
  if (_real_pthread_mutex_lock(&preResumeThreadCountLock) != 0) {
    JASSERT(false) .Text("Failed to acquire preResumeThreadCountLock");
  }
  if (_real_pthread_mutex_unlock(&preResumeThreadCountLock) != 0) {
    JASSERT(false) .Text("Failed to release preResumeThreadCountLock");
  }
}
