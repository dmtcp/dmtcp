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

struct ThreadArg {
  union {
    int (*fn) (void *arg);
    void *(*pthread_fn) (void *arg);  // pthread_create calls fn -> void *
  };
  void *arg;
  void *mtcpArg;
  pid_t virtualTid;
};

// Invoked via __clone
LIB_PRIVATE
int
clone_start(void *arg)
{
  Thread *thread = (Thread *)arg;

  ThreadSync::initThread();

  ThreadList::updateTid(thread);

  /* Thread finished initialization.  It's now safe for this thread to
   * participate in checkpoint.  Decrement the uninitializedThreadCount in
   * DmtcpWorker.
   */
  ThreadSync::decrementUninitializedThreadCount();

  JTRACE("Calling user function") (dmtcp_gettid());
  int ret = thread->fn(thread->arg);

  ThreadList::threadExit();
  return ret;
}

/*****************************************************************************
 *
 *  This is our clone system call wrapper
 *
 *    Note:
 *
 *      pthread_create eventually calls __clone to create threads
 *      It uses flags = 0x3D0F00:
 *	      CLONE_VM = VM shared between processes
 *	      CLONE_FS = fs info shared between processes (root, cwd, umask)
 *	   CLONE_FILES = open files shared between processes (fd table)
 *	 CLONE_SIGHAND = signal handlers and blocked signals shared
 *	                         (sigaction common to parent and child)
 *	  CLONE_THREAD = add to same thread group
 *	 CLONE_SYSVSEM = share system V SEM_UNDO semantics
 *	  CLONE_SETTLS = create a new TLS for the child from newtls parameter
 *	 CLONE_PARENT_SETTID = set the TID in the parent (before MM copy)
 *	CLONE_CHILD_CLEARTID = clear the TID in the child and do
 *				 futex wake at that address
 *	      CLONE_DETACHED = create clone detached
 *
 *****************************************************************************/

// need to forward user clone
extern "C" int
__clone(int (*fn)(void *arg),
        void *child_stack,
        int flags,
        void *arg,
        int *ptid,
        struct user_desc *tls,
        int *ctid)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  ThreadSync::incrementUninitializedThreadCount();

  Thread *thread = ThreadList::getNewThread();
  ThreadList::initThread(thread, fn, arg, flags, ptid, ctid);

  // if (ckpthread == NULL) {
  // ckptthread = thread;
  // thread->stateInit(ST_CKPNTHREAD);
  // }

  pid_t tid = _real_clone(clone_start, child_stack, flags, thread,
                          ptid, tls, ctid);

  if (tid == -1) {
    JTRACE("Clone call failed")(JASSERT_ERRNO);
    ThreadSync::decrementUninitializedThreadCount();
    ThreadList::threadIsDead(thread);
  }

  WRAPPER_EXECUTION_ENABLE_CKPT();
  return tid;
}

#if 0
# if defined(__i386__) || defined(__x86_64__)
asm (".global clone ; .type clone,@function ; clone = __clone");
# elif defined(__arm__)

// In arm, '@' is a comment character;  Arm uses '%' in type directive
asm (".global clone ; .type clone,%function ; clone = __clone");
# else // if defined(__i386__) || defined(__x86_64__)
#  error Not implemented on this architecture
# endif // if defined(__i386__) || defined(__x86_64__)
#endif // if 0

// Invoked via pthread_create as start_routine
// On return, it calls mtcp_threadiszombie()
static void *
pthread_start(void *arg)
{
  struct ThreadArg *threadArg = (struct ThreadArg *)arg;
  void *thread_arg = threadArg->arg;
  void *(*pthread_fn) (void *) = threadArg->pthread_fn;
  pid_t virtualTid = threadArg->virtualTid;

  JASSERT(pthread_fn != 0x0);
  JALLOC_HELPER_FREE(arg); // Was allocated in calling thread in pthread_create

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
  void *result = (*pthread_fn)(thread_arg);
  JTRACE("Thread returned") (virtualTid);
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
pthread_create(pthread_t *thread,
               const pthread_attr_t *attr,
               void *(*start_routine)(void *),
               void *arg)
{
  int retval;

  // We have to use DMTCP-specific memory allocator because using glibc:malloc
  // can interfere with user threads.
  // We use JALLOC_HELPER_FREE to free this memory in two places:
  // 1. near the beginning of pthread_start (wrapper for start_routine),
  // providing that the __clone call succeeds with no tid conflict.
  // 2. if the call to _real_pthread_create fails, then free memory
  // near the end of this function.
  // We use MALLOC/FREE so that pthread_create() can be called again, without
  // waiting for the new thread to give up the buffer in pthread_start().
  struct ThreadArg *threadArg =
    (struct ThreadArg *)JALLOC_HELPER_MALLOC(sizeof(struct ThreadArg));

  threadArg->pthread_fn = start_routine;
  threadArg->arg = arg;

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
  retval = _real_pthread_create(thread, attr, pthread_start, threadArg);
  if (threadCreationLockAcquired) {
    ThreadSync::threadCreationLockUnlock();
  }
  if (retval == 0) {
    ProcessInfo::instance().clearPthreadJoinState(*thread);
  } else { // if we failed to create new pthread
    JALLOC_HELPER_FREE(threadArg);
    ThreadSync::decrementUninitializedThreadCount();
  }
  return retval;
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
