/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <pthread.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "jassert.h"
#include "syscallwrappers.h"
#include "threadinfo.h"
#include "threadsync.h"
#include "workerstate.h"

using namespace dmtcp;

/*
 * _wrapperExecutionLock is used to make the checkpoint safe by making sure
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
 *   A counter (_uninitializedThreadCount) for the number of newly
 *     created uninitialized threads is kept.  The counter is made
 *     thread-safe by using a mutex.
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
static DmtcpRWLock _wrapperExecutionLock;

static DmtcpMutex libdlLock = DMTCP_MUTEX_INITIALIZER;
static pid_t libdlLockOwner = 0;

static DmtcpMutex presuspendEventHookLock = DMTCP_MUTEX_INITIALIZER;


/* The following two functions dmtcp_libdlLock{Lock,Unlock} are used by dlopen
 * plugin.
 */
extern "C" int
dmtcp_libdlLockLock()
{
  return ThreadSync::libdlLockLock();
}

extern "C" void
dmtcp_libdlLockUnlock()
{
  ThreadSync::libdlLockUnlock();
}

void
ThreadSync::initMotherOfAll()
{
  DmtcpRWLockInit(&_wrapperExecutionLock);
}

void
ThreadSync::acquireLocks()
{
  // JASSERT(WorkerState::currentState() == WorkerState::PRESUSPEND);

  /* TODO: We should introduce the notion of lock ranks/priorities for all
   * these locks to prevent future deadlocks due to rank violation.
   */

  JTRACE("Waiting for libdlLock");
  JASSERT(DmtcpMutexLock(&libdlLock) == 0);

  JTRACE("Waiting for other threads to exit DMTCP-Wrappers");
  JASSERT(DmtcpRWLockWrLock(&_wrapperExecutionLock) == 0);

  JTRACE("Done acquiring all locks");
}

void
ThreadSync::releaseLocks()
{
  JASSERT(WorkerState::currentState() == WorkerState::SUSPENDED);

  JTRACE("Releasing ThreadSync locks");
  JASSERT(DmtcpRWLockUnlock(&_wrapperExecutionLock) == 0);
  JASSERT(DmtcpMutexUnlock(&libdlLock) == 0);
}

void
ThreadSync::resetLocks(bool resetPresuspendEventHookLock)
{
  DmtcpRWLockInit(&_wrapperExecutionLock);
  curThread->wrapperLockCount = 0;

  DmtcpMutexInit(&libdlLock, DMTCP_MUTEX_NORMAL);

  libdlLockOwner = 0;

  if (resetPresuspendEventHookLock) {
    DmtcpMutexInit(&presuspendEventHookLock, DMTCP_MUTEX_NORMAL);
  }
}

bool
ThreadSync::libdlLockLock()
{
  int saved_errno = errno;
  bool lockAcquired = false;

  if ((WorkerState::currentState() == WorkerState::RUNNING ||
       WorkerState::currentState() == WorkerState::PRESUSPEND) &&
      libdlLockOwner != dmtcp_gettid()) {
    JASSERT(DmtcpMutexLock(&libdlLock) == 0);
    libdlLockOwner = dmtcp_gettid();
    lockAcquired = true;
  }
  errno = saved_errno;
  return lockAcquired;
}

void
ThreadSync::libdlLockUnlock()
{
  int saved_errno = errno;

  JASSERT(libdlLockOwner == 0 || libdlLockOwner == dmtcp_gettid())
    (libdlLockOwner) (dmtcp_gettid());
  JASSERT(WorkerState::currentState() == WorkerState::RUNNING ||
          WorkerState::currentState() == WorkerState::PRESUSPEND);
  libdlLockOwner = 0;
  JASSERT(DmtcpMutexUnlock(&libdlLock) == 0);
  errno = saved_errno;
}

bool
ThreadSync::wrapperExecutionLockLock()
{
  int saved_errno = errno;
  bool lockAcquired = false;

  if (curThread == nullptr) {
    return lockAcquired;
  }

  if ((WorkerState::currentState() == WorkerState::RUNNING ||
       WorkerState::currentState() == WorkerState::PRESUSPEND)) {
    if (curThread->wrapperLockCount == 0) {
      // If we don't have a lock, acquire it now.
      if (DmtcpRWLockRdLock(&_wrapperExecutionLock) != 0) {
        fprintf(stderr, "ERROR %d at %s:%d %s: Failed to acquire lock\n",
                errno, __FILE__, __LINE__, __PRETTY_FUNCTION__);
        _exit(DMTCP_FAIL_RC);
      }
    }
    curThread->wrapperLockCount++;
    lockAcquired = true;
  }

  errno = saved_errno;
  return lockAcquired;
}

void
ThreadSync::wrapperExecutionLockLockForNewThread(Thread *thread)
{
  JASSERT(thread != nullptr);
  JASSERT(thread->wrapperLockCount == 0);

  if (DmtcpRWLockRdLockIgnoreQueuedWriter(&_wrapperExecutionLock) != 0) {
    fprintf(stderr, "ERROR %d at %s:%d %s: Failed to acquire lock\n",
            errno, __FILE__, __LINE__, __PRETTY_FUNCTION__);
    _exit(DMTCP_FAIL_RC);
  }

  thread->wrapperLockCount++;
}

void
ThreadSync::wrapperExecutionLockUnlockForNewThread(Thread *thread)
{
  JASSERT(thread != nullptr);
  JASSERT(thread->wrapperLockCount == 1);

  if (DmtcpRWLockUnlock(&_wrapperExecutionLock) != 0) {
    fprintf(stderr, "ERROR %d at %s:%d %s: Failed to release lock\n",
            errno, __FILE__, __LINE__, __PRETTY_FUNCTION__);
    _exit(DMTCP_FAIL_RC);
  }

  thread->wrapperLockCount = 0;
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
 *    family of wrapper. That would be fixed in a later commit.
 * 2. We need to come up with a strategy for certain blocking system calls
 *    that can change the state of the process (e.g. accept).
 * 3. Using trywrlock() can result in starvation if multiple other threads are
 *    rapidly acquiring releasing the lock. For example thread A acquires the
 *    rdlock for 100 ms. Thread B executes and trywrlock and fails. Thread B
 *    sleeps goes to sleep for some time. While thread B is sleeping, thread A
 *    releases the rdlock and reacquires it or some other thread acquires the
 *    rdlock. This would cause the thread B to starve. This scenario can be
 *    easily observed if thread A calls
 *      epoll_wait(fd, events, max_events, -1).
 *    It is wrapped by the epoll_wait wrapper in IPC plugin, which then makes
 *    repeated calls to _real_epoll_wait with smaller timeout.
 */
void
ThreadSync::wrapperExecutionLockLockExcl()
{
  int saved_errno = errno;

  JASSERT(curThread != nullptr);

  if (WorkerState::currentState() == WorkerState::RUNNING ||
      WorkerState::currentState() == WorkerState::PRESUSPEND) {
    if (DmtcpRWLockWrLock(&_wrapperExecutionLock) != 0) {
      fprintf(stderr, "ERROR %s:%d %s: Failed to acquire lock\n",
              __FILE__, __LINE__, __PRETTY_FUNCTION__);
      _exit(DMTCP_FAIL_RC);
    }
    curThread->wrapperLockCount++;
  }
  errno = saved_errno;
  return;
}

// NOTE: Don't do any fancy stuff in this wrapper which can cause the process
// to go into DEADLOCK
void
ThreadSync::wrapperExecutionLockUnlock()
{
  int saved_errno = errno;

  if (curThread == nullptr) {
    return;
  }

  JASSERT(curThread->wrapperLockCount != 0);
  curThread->wrapperLockCount -= 1;

  if (curThread->wrapperLockCount == 0 &&
      DmtcpRWLockUnlock(&_wrapperExecutionLock) != 0) {
    fprintf(stderr, "ERROR %s:%d %s: Failed to release lock.\n",
            __FILE__, __LINE__, __PRETTY_FUNCTION__);
    _exit(DMTCP_FAIL_RC);
  }

  errno = saved_errno;
}

// GNU g++ uses __thread.  But the C++0x standard says to use thread_local.
// If your compiler fails here, you can: change "__thread" to "thread_local";
// or delete "__thread" (but if user code calls these routines from multiple
// threads, it will not be thread-safe).
// In GCC 4.3 and later, g++ supports -std=c++0x and -std=g++0x.
void
ThreadSync::presuspendEventHookLockLock()
{
  JTRACE("Acquiring event-hook lock");
  JASSERT(DmtcpMutexLock(&presuspendEventHookLock) == 0);
}

void
ThreadSync::presuspendEventHookLockUnlock()
{
  JTRACE("Releasing event-hook lock");
  JASSERT(DmtcpMutexUnlock(&presuspendEventHookLock) == 0);
}
