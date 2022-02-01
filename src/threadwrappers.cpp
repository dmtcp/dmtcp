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
#include <semaphore.h> // Added only for 'sem_t' in definition struct ThreadArg
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

// pthread_create() in this file used to call __clone() in
//   src/plugin/pid/pid_miscwrappers.cpp.  At that time, we had two
//   independent definition of 'struct ThreadArg' (which were unfortunately
//   called by the same name).  Now pthread_create() does not call
//   pid_miscwrappers.cpp:__clone(), but the dmtcp_clone_XXX() functions
//   are shared between the two files.  This is forced upon us, because
//   dmtcp_clone_XXX() is needed here for pthread_create(), and it needs
//   to use the virtualPidTable of the pid plugin.
// FIXME:  Surely, there is a better architecture between this libdmtcp.so
//   plugin and the libdmtcp_pid.so plugin.
// These declarations could be added to src/plugin/pid/virtualpidtable.h,
//   but it seems bad style to mix the two plugins.  Which is better.
// This struct must be kept in sync with src/plugin/pid/pid_miscwrappers.cpp
struct ThreadArg {
  union {
    int (*fn) (void *arg);  // clone() calls fn returning int
    void *(*pthread_fn) (void *arg);  // pthread_create calls fn returning void*
  };
  union {
    void *arg;              // used for clone()
    void *pthread_arg;      // used for pthread_create()
  };
  void *pthread_fn_retval;  // used for pthread_create()
  pid_t virtualTid;         // used by dmtcp_clone_XXX()
  sem_t sem;
};
extern "C" pid_t dmtcp_clone_pre(int (*fn)(void *arg), void *arg,
                                  struct ThreadArg *threadArg);
extern "C" int dmtcp_clone_post_parent(int tid, int virtualTid,
                                         struct ThreadArg *threadArg);
extern "C" int dmtcp_clone_post_child(int (*fn)(void *), void *arg);
extern "C" int
dmtcp_libc_clone(int (*fn)(void *arg), void *child_stack, int flags, void *arg,
               int *parent_tidptr, struct user_desc *newtls, int *child_tidptr);

static __thread int inside_pthread_create = 0;

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
  if (inside_pthread_create) {
    return dmtcp_libc_clone(clone_start, child_stack, flags, arg,
                            ptid, tls, ctid);
  }

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

// We need to split pthread_start into pthread_start/pthread_start2
//   because dmtcp_clone_post_child() expects a function returning
//   an int, as required by clone().  This allows us to reuse
//   dmtcp_clone_post_child() both for clone() and for pthread_create().
static int pthread_start2(void *arg);
static void *
pthread_start(void *arg) {
  // arg is same as the threadArg that was passed into _real_pthread_create()
  dmtcp_clone_post_child(&pthread_start2, arg);
  void *retval = ((struct ThreadArg *)arg)->pthread_fn_retval;
  JALLOC_HELPER_FREE(arg); // Was allocated in caller thread in pthread_create()
  inside_pthread_create = 0;
  return retval;
}

// Invoked via pthread_start as start_routine
// On return, it calls mtcp_threadiszombie()
static int
pthread_start2(void *arg)
{
  struct ThreadArg *threadArg = (struct ThreadArg *)arg;
  void *thread_arg = threadArg->pthread_arg;
  void *(*pthread_fn) (void *) = threadArg->pthread_fn;
  pid_t virtualTid = threadArg->virtualTid;

  JASSERT(pthread_fn != 0x0);

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
  threadArg->pthread_fn_retval = result;
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
  threadArg->pthread_fn_retval = result;
  return 0; // success
}

extern "C" int
pthread_create(pthread_t *thread,
               const pthread_attr_t *attr,
               void *(*start_routine)(void *),
               void *arg)
{
  int retval;
  inside_pthread_create = 1;

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
  threadArg->pthread_arg = arg;

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
#define REFACTOR_CLONE
#ifdef REFACTOR_CLONE
  // Copy of __clone in this file:
  WRAPPER_EXECUTION_DISABLE_CKPT();
  ThreadSync::incrementUninitializedThreadCount();

  Thread *threadEntry = ThreadList::getNewThread();
  // We pass NULL for fn.  DMTCP saves fn, but never uses it.
  int flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD |
    CLONE_SYSVSEM | CLONE_SETTLS | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID;
  // NOTE: initThread saves ptid and ctid as 'int *' in Thread data structure,
  //       but we never actually use them.  This is useful when implementing
  //       clone(), which makes ptdi/ctid into 'out' parameters.  But for
  //       pthread_create, we can store a NULL pointer in threadEntry instead.
  ThreadList::initThread(threadEntry, NULL, arg, flags,
                         NULL /*ptid*/, NULL /*ctid*/);

  // if (ckpthread == NULL) {
  // ckptthread = threadEntry;
  // threadEntry->stateInit(ST_CKPNTHREAD);
  // }

#if 0
  // from __clone of this file:
  //   pthread_create calls libc:pthread_create, which calls this __clone,
  //   The argument thread here is used both on input and output.
  pid_t tid = _real_clone(pthread_start, child_stack, flags, thread,
                          ptid, tls, ctid);
#else
  // This simulates the call to clone() without actually using clone().
  // This is important, because glibc-2.34 defines pthread_create() to
  //   call clone3(), and clone3() is a library-private symbol of glibc.
  //   So, we can't longer interpose on clone3.
  // Similarly, musl libc defines pthread_create() directly, and does
  //   not factor it through any variant of clone().

  // The function pointerr for pthread_start2
  pid_t virtualTid = dmtcp_clone_pre(pthread_start2, arg, threadArg);

  retval = _real_pthread_create(thread, attr, pthread_start, threadArg);

  // Since we did not go through a wrapper around clone(), we must
  //   set ptid and ctid here, directly.
  // FXIME:  Set ptid and ctid.  (We could set ptid in advance.)
  int tid = dmtcp_gettid();
  dmtcp_clone_post_parent(tid, virtualTid, threadArg);
#endif

  // FIXME:  set ptid, ctid, check for failure, and remove threadEntry from list
  if (tid == -1) {
    JTRACE("Clone call failed")(JASSERT_ERRNO);
    ThreadSync::decrementUninitializedThreadCount();
    ThreadList::threadIsDead(threadEntry);
  }

  WRAPPER_EXECUTION_ENABLE_CKPT();
#else
  retval = _real_pthread_create(thread, attr, pthread_start, threadArg);
#endif
  if (threadCreationLockAcquired) {
    ThreadSync::threadCreationLockUnlock();
  }
  if (retval == 0) {
    // new pthread created; we will 'JALLOC_HELPER_FREE(threadArg)'
    //   and do 'inside_pthread_create = 0' at end of pthread_start().
    ProcessInfo::instance().clearPthreadJoinState(*thread);
  } else { // if we failed to create new pthread
    JALLOC_HELPER_FREE(threadArg);
    inside_pthread_create = 0;
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
