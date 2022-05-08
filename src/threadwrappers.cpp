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

#include <sys/syscall.h>
#include "../jalib/jalloc.h"
#include "../jalib/jassert.h"
#include "constants.h"
#include "dmtcp.h"
#include "pluginmanager.h"
#include "processinfo.h"
#include "siginfo.h"
#include "syscallwrappers.h"
#include "threadlist.h"
#include "threadsync.h"
#include "uniquepid.h"
#include "util.h"

using namespace dmtcp;

extern __thread Thread *curThread;

static bool
prepareThreadCreate()
{
  /* pthread_create() should acquire the thread-creation lock. Not doing so can
   * result in a deadlock in the following scenario:
   * 1. user thread: pthread_create() - acquire wrapper-execution lock
   * 2. ckpt-thread: SUSPEND msg received, wait on wrlock for wrapper-exec lock
   * 3. user thread: __clone() - try to acquire wrapper-execution lock
   *
   * We also need to increment the uninitialized-thread-count so that it is
   * safe to checkpoint the newly created thread.
   *
   * There is another possible deadlock situation if we do not grab the
   * thread-creation lock:
   * 1. user thread: pthread_create(): waiting on tbl_lock inside libpthread
   * 2. ckpt-thread: SUSPEND msg received, wait on wrlock for wrapper-exec lock
   * 3. uset thread: a. exiting after returning from user fn.
   *                 b. grabs tbl_lock()
   *                 c. tries to call free() to deallocate previously allocated
   *                 space (stack etc.). The free() wrapper requires
   *                 wrapper-exec lock, which is not available.
   */
  bool threadCreationLockAcquired = ThreadSync::threadCreationLockLock();
  ThreadSync::incrementUninitializedThreadCount();

  JASSERT(Thread_UpdateState(curThread, ST_THREAD_CREATE, ST_RUNNING));

  return threadCreationLockAcquired;
}

static void
postThreadCreate(bool threadCreationLockAcquired)
{
  JASSERT(Thread_UpdateState(curThread, ST_RUNNING, ST_THREAD_CREATE));

  if (threadCreationLockAcquired) {
    ThreadSync::threadCreationLockUnlock();
  }
}

static void
processChildThread(Thread *thread)
{
  dmtcp_init_virtual_tid();

  ThreadList::initThread(thread);
  // Unblock ckpt signal (unblocking a non-blocked signal has no effect).
  // Normally, DMTCP wouldn't allow the ckpt signal to be blocked. However, in
  // some situations (e.g., timer_create), libc would internally block all
  // signals before calling pthread_create to create a helper thread.  Since,
  // the child threads inherit parent signal mask, the helper thread has all
  // signals blocked.
  sigset_t set;
  sigaddset(&set, SigInfo::ckptSignal());
  JASSERT(_real_pthread_sigmask(SIG_UNBLOCK, &set, NULL) == 0) (JASSERT_ERRNO);

  ThreadSync::threadFinishedInitialization();
}

// Invoked via pthread_create as start_routine
// On return, it calls mtcp_threadiszombie()
static void *
thread_start(void *arg)
{
  Thread *thread = (Thread *) arg;

  processChildThread(thread);

  void *result = thread->fn(thread->arg);

  JTRACE("Thread returned") (thread->virtual_tid);
  WRAPPER_EXECUTION_DISABLE_CKPT();
  ThreadList::threadExit();

  /*
   * This thread has finished its execution, do some cleanup on our part.
   *  erasing the virtualTid entry from virtualpidtable
   *  FIXME: What if the process gets checkpointed after erase() but before the
   *  thread actually exits?
   */
  PluginManager::eventHook(DMTCP_EVENT_PTHREAD_RETURN, NULL);
  WRAPPER_EXECUTION_ENABLE_CKPT();
  ThreadSync::unsetOkToGrabLock();
  return result;
}

extern "C" int
pthread_create(pthread_t *pth,
               const pthread_attr_t *attr,
               void *(*start_routine)(void *),
               void *arg)
{
  int retval;

  bool threadCreationLockAcquired = prepareThreadCreate();

  Thread *thread = ThreadList::getNewThread(start_routine, arg);
  retval = _real_pthread_create(pth, attr, thread_start, thread);

  postThreadCreate(threadCreationLockAcquired);

  if (retval == 0) {
    ProcessInfo::instance().clearPthreadJoinState(*pth);
  } else { // if we failed to create new pthread
    ThreadSync::decrementUninitializedThreadCount();
  }
  return retval;
}

// Make sure __clone is not called without pthread_create.
extern "C" int
__clone(int (*fn)(void *arg),
        void *child_stack,
        int flags,
        void *arg,
        int *parent_tidptr,
        struct user_desc *newtls,
        int *child_tidptr)
{
  if (curThread->state == ST_THREAD_CREATE) {
    return _real_clone(
      fn, child_stack, flags, arg, parent_tidptr, newtls, child_tidptr);
  }

  JASSERT(false)
    .Text("Thread-creation with clone syscall isn't supported.");

  return 0;
}

extern "C" long
clone3(struct clone_args *cl_args, size_t size)
{
  if (curThread->state == ST_THREAD_CREATE) {
    return NEXT_FNC(clone3)(cl_args, size);
  }

  JASSERT(false)
    .Text("Thread-creation with clone3 syscall isn't supported.");

  return 0;
}

extern "C" void
pthread_exit(void *retval)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  ThreadList::threadExit();
  PluginManager::eventHook(DMTCP_EVENT_PTHREAD_EXIT, NULL);
  WRAPPER_EXECUTION_ENABLE_CKPT();
  ThreadSync::unsetOkToGrabLock();
  _real_pthread_exit(retval);
  for (;;) { // To hide compiler warning about "noreturn" function
  }
}

/*
 * pthread_join() is a blocking call that waits for the given thread to exit.
 * It examines the value of 'tid' field in 'struct pthread' of the given
 * thread. The kernel will write '0' to this field when the thread exits.
 *
 * In pthread_join(), the thread makes a futex call in the following fashion:
 *   _tid = pd->tid;
 *   while !succeeded
 *     futex(&pd->tid, FUTEX_WAIT, 0, _tid, ...)
 * As we can see, if the checkpoint is issued during pthread_join(), on
 * restart, the tid would have changed, but the call to futex would still use
 * the previously cached tid. This causes the caller to spin with 100% cpu
 * usage.
 *
 * The fix is to use the non blocking pthread_tryjoin_np function. To maintain
 * the semantics of pthread_join(), we need to ensure that only one thread is
 * allowed to wait on the given thread. This is done by keeping track of
 * threads that are being waited on by some other thread.
 *
 * Similar measures are taken for pthread_timedjoin_np().
 */
static struct timespec ts_100ms = { 0, 100 * 1000 * 1000 };
extern "C" int
pthread_join(pthread_t thread, void **retval)
{
  int ret;
  struct timespec ts;

  if (!ProcessInfo::instance().beginPthreadJoin(thread)) {
    return EINVAL;
  }

  while (1) {
    WRAPPER_EXECUTION_DISABLE_CKPT();
    ThreadSync::unsetOkToGrabLock();
    JASSERT(clock_gettime(CLOCK_REALTIME, &ts) != -1);
    TIMESPEC_ADD(&ts, &ts_100ms, &ts);
    ret = _real_pthread_timedjoin_np(thread, retval, &ts);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    ThreadSync::setOkToGrabLock();
    if (ret != ETIMEDOUT) {
      break;
    }
  }

  ProcessInfo::instance().endPthreadJoin(thread);
  return ret;
}

extern "C" int
pthread_tryjoin_np(pthread_t thread, void **retval)
{
  int ret;

  if (!ProcessInfo::instance().beginPthreadJoin(thread)) {
    return EINVAL;
  }

  WRAPPER_EXECUTION_DISABLE_CKPT();
  ret = _real_pthread_tryjoin_np(thread, retval);
  WRAPPER_EXECUTION_ENABLE_CKPT();

  ProcessInfo::instance().endPthreadJoin(thread);
  return ret;
}

extern "C" int
pthread_timedjoin_np(pthread_t thread,
                     void **retval,
                     const struct timespec *abstime)
{
  int ret;
  struct timespec ts;

  if (!ProcessInfo::instance().beginPthreadJoin(thread)) {
    return EINVAL;
  }

  /*
   * We continue to call pthread_tryjoin_np (and sleep) until we have gone past
   * the abstime provided by the caller
   */
  while (1) {
    WRAPPER_EXECUTION_DISABLE_CKPT();
    JASSERT(clock_gettime(CLOCK_REALTIME, &ts) != -1);
    if (TIMESPEC_CMP(&ts, abstime, <)) {
      TIMESPEC_ADD(&ts, &ts_100ms, &ts);
      ret = _real_pthread_timedjoin_np(thread, retval, &ts);
    } else {
      ret = ETIMEDOUT;
    }
    WRAPPER_EXECUTION_ENABLE_CKPT();

    if (ret == EBUSY || ret == 0) {
      break;
    }
    if (TIMESPEC_CMP(&ts, abstime, >=)) {
      ret = ETIMEDOUT;
      break;
    }
  }

  ProcessInfo::instance().endPthreadJoin(thread);
  return ret;
}
