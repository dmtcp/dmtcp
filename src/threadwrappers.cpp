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

#include <sys/param.h> // for EXEC_PAGESIZE
#include <sys/syscall.h>
#include "../jalib/jalloc.h"
#include "constants.h"
#include "dmtcp.h"
#include "dmtcpworker.h"
#include "plugin/pid/pidhelpers.h"
#include "pluginmanager.h"
#include "processinfo.h"
#include "siginfo.h"
#include "syscallwrappers.h"
#include "threadlist.h"
#include "threadsync.h"
#include "uniquepid.h"
#include "util.h"
#include "dmtcp_assert.h"

using namespace dmtcp;

// gettid / tkill / tgkill are not defined in libc.
extern "C" pid_t
gettid(void) __THROW
{
  return dmtcp_gettid();
}

LIB_PRIVATE pid_t
dmtcp_gettid()
{
  return dmtcp_get_current_thread()->tid;
}

static void
processChildThread(Thread *thread)
{
  dmtcp_init_virtual_tid();
  ASSERT_NE(0,
            thread->wrapperLockCount,
            "child thread entered without inherited wrapper lock: tid={}",
            thread->tid);

  ThreadList::initThread(thread);
  // Unblock ckpt signal (unblocking a non-blocked signal has no effect).
  // Normally, DMTCP wouldn't allow the ckpt signal to be blocked. However, in
  // some situations (e.g., timer_create), libc would internally block all
  // signals before calling pthread_create to create a helper thread.  Since,
  // the child threads inherit parent signal mask, the helper thread has all
  // signals blocked.
  sigset_t set;
  sigaddset(&set, SigInfo::ckptSignal());
  ASSERT_PTHREAD_SUCCESS(
    _real_pthread_sigmask(SIG_UNBLOCK, &set, NULL),
    "unblocking checkpoint signal in child thread: signal={}",
    SigInfo::ckptSignal());

  // Lock was acquired by the parent thread on our behalf.
  ThreadSync::wrapperExecutionLockUnlock();
}

// Invoked via pthread_create as start_routine
// On return, it calls mtcp_threadiszombie()
static void *
thread_start(void *arg)
{
  Thread *thread = (Thread *) arg;

  processChildThread(thread);

  void *result = thread->fn(thread->arg);

  TRACE("Thread returned: tid={}", thread->tid);
  WrapperLock wrapperLock;
  ThreadList::threadExit();

  /*
   * This thread has finished its execution, do some cleanup on our part.
   *  erasing the virtualTid entry from virtualpidtable
   *  FIXME: What if the process gets checkpointed after erase() but before the
   *  thread actually exits?
   */
  PluginManager::eventHook(DMTCP_EVENT_PTHREAD_RETURN, NULL);
  return result;
}

extern "C" int
pthread_create(pthread_t *pth,
               const pthread_attr_t *attr,
               void *(*start_routine)(void *),
               void *arg)
{
  WrapperLock wrapperLock;
  Thread *thread = dmtcp_get_current_thread();

  Thread *newThread = ThreadList::getNewThread(start_routine, arg);
  ThreadSync::wrapperExecutionLockLockForNewThread(newThread);
  ASSERT_NE(0,
            newThread->wrapperLockCount,
            "new thread wrapper lock was not pre-acquired: tid={}",
            newThread->tid);

  ASSERT(Thread_UpdateState(thread, ST_THREAD_CREATE, ST_RUNNING),
         "Failed to mark thread (tid:{}) from RUNNING to THREAD_CREATE",
         thread->tid);

  int retval = _real_pthread_create(pth, attr, thread_start, newThread);

  ASSERT(Thread_UpdateState(thread, ST_RUNNING, ST_THREAD_CREATE),
         "Failed to mark thread (tid:{}) from THREAD_CREATE to RUNNING",
         thread->tid);

  if (retval == 0) {
    ProcessInfo::instance().clearPthreadJoinState(*pth);
    // Since glibc 2.42, pthread_create installs a lightweight guard region at
    // the low end of the new thread's stack via madvise(MADV_GUARD_INSTALL).
    // The checkpoint memory scan would classify these guard pages as occupied
    // and fault while reading them. Record the guard region here so the
    // checkpoint thread can temporarily remove it while writing the checkpoint
    // image and reinstall it afterward (see ThreadList::writeCkpt() and
    // ThreadList::postRestartWork()).
    //
    // glibc places the guard immediately below the lowest usable stack byte, so
    // the region is [stack_addr - guardSize, stack_addr). guardSize is the
    // configured guard (pthread_attr_getguardsize(), rounded up to the page
    // size), floored at EXEC_PAGESIZE: glibc enforces that floor so the guard is
    // at least one page under any supported kernel page size. On aarch64
    // EXEC_PAGESIZE is 64 KB (vs. a 4 KB runtime page), so the guard is 64 KB
    // there even though getguardsize() reports 4 KB. This reproduces glibc's own
    // sizing exactly, giving the precise per-thread region independent of how
    // the kernel lays out or merges stack VMAs.
    //
    // The MADV_GUARD_* macros (and the lightweight guard they compensate for)
    // both appeared in glibc 2.42. Keep this condition in sync with the one in
    // ThreadList::updateStackGuards(), which removes/reinstalls the region.
#if defined(MADV_GUARD_INSTALL) && defined(MADV_GUARD_REMOVE)
    pthread_attr_t new_attr;
    void *stack_addr;
    size_t stack_size, guard_size;
    pthread_getattr_np(*pth, &new_attr);
    pthread_attr_getstack(&new_attr, &stack_addr, &stack_size);
    pthread_attr_getguardsize(&new_attr, &guard_size);
    pthread_attr_destroy(&new_attr);

    size_t pageSize = sysconf(_SC_PAGESIZE);
    guard_size = (guard_size + pageSize - 1) & ~(pageSize - 1);
    if (guard_size < (size_t) EXEC_PAGESIZE) {
      guard_size = EXEC_PAGESIZE;
    }
    newThread->guardAddr = (char *)stack_addr - guard_size;
    newThread->guardSize = guard_size;
#endif
  } else { // if we failed to create new pthread
    ThreadSync::wrapperExecutionLockUnlockForNewThread(newThread);
    ThreadList::threadIsDead(newThread);
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
  Thread *thread = dmtcp_get_current_thread();
  if (thread->state == ST_THREAD_CREATE) {
    return _real_clone(
      fn, child_stack, flags, arg, parent_tidptr, newtls, child_tidptr);
  }

  ASSERT(false,
         "Thread creation with clone syscall is not supported: flags={}",
         flags);

  return 0;
}

extern "C" long
clone3(struct clone_args *cl_args, size_t size)
{
  Thread *thread = dmtcp_get_current_thread();
  if (thread->state == ST_THREAD_CREATE) {
    return NEXT_FNC(clone3)(cl_args, size);
  }

  ASSERT(false,
         "Thread creation with clone3 syscall is not supported: size={}",
         size);

  return 0;
}

extern "C" void
pthread_exit(void *retval)
{
  {
    WrapperLock wrapperLock;

    ThreadList::threadExit();
    PluginManager::eventHook(DMTCP_EVENT_PTHREAD_EXIT, NULL);
  }

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
    WrapperLock wrapperLock;
    ASSERT_NE(-1, clock_gettime(CLOCK_REALTIME, &ts),
              "reading CLOCK_REALTIME before pthread_join");
    TIMESPEC_ADD(&ts, &ts_100ms, &ts);
    ret = _real_pthread_timedjoin_np(thread, retval, &ts);
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

  {
    WrapperLock wrapperLock;
    ret = _real_pthread_tryjoin_np(thread, retval);
  }

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
    WrapperLock wrapperLock;
    ASSERT_NE(-1,
              clock_gettime(CLOCK_REALTIME, &ts),
              "reading CLOCK_REALTIME before pthread_timedjoin_np");
    if (TIMESPEC_CMP(&ts, abstime, <)) {
      TIMESPEC_ADD(&ts, &ts_100ms, &ts);
      ret = _real_pthread_timedjoin_np(thread, retval, &ts);
    } else {
      ret = ETIMEDOUT;
    }

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
