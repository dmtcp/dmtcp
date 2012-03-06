/****************************************************************************
 *   Copyright (C) 2006-2011 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include <sys/syscall.h>
#include "constants.h"
#include "dmtcpworker.h"
#include "mtcpinterface.h"
#include "syscallwrappers.h"
#include "virtualpidtable.h"
#include "dmtcpmodule.h"
#include "uniquepid.h"
#include "../jalib/jassert.h"
#include "../jalib/jalloc.h"

struct ThreadArg {
  int ( *fn ) ( void *arg );  // clone() calls fn that returns int
  void * ( *pthread_fn ) ( void *arg ); // pthread_create calls fn -> void *
  void *arg;
  pid_t virtualTid;
};

// Invoked via pthread_create as start_routine
// On return, it calls mtcp_threadiszombie()
static void *pthread_start(void *arg)
{
  struct ThreadArg *threadArg = (struct ThreadArg*) arg;
  void *thread_arg = threadArg->arg;
  void * (*pthread_fn) (void *) = threadArg->pthread_fn;
  pid_t virtualTid = threadArg->virtualTid;

  JASSERT(pthread_fn != 0x0);
  JALLOC_HELPER_FREE(arg); // Was allocated in calling thread in pthread_create
  mtcpFuncPtrs.fill_in_pthread_id(_real_gettid(), pthread_self());
  dmtcp::ThreadSync::decrementUninitializedThreadCount();
  void *result = (*pthread_fn)(thread_arg);
  mtcpFuncPtrs.threadiszombie();
#ifdef PID_VIRTUALIZATION
  /*
   * This thread has finished its execution, do some cleanup on our part.
   *  erasing the virtualTid entry from virtualpidtable
   *  FIXME: What if the process gets checkpointed after erase() but before the
   *  thread actually exits?
   */
  dmtcp::VirtualPidTable::instance().erase(virtualTid);
#endif
  JTRACE("Thread returned") (virtualTid);
  dmtcp::ProcessInfo::instance().eraseTid(virtualTid);
  return result;
}

#ifdef PID_VIRTUALIZATION
// Invoked via __clone
LIB_PRIVATE
int clone_start(void *arg)
{
  struct ThreadArg *threadArg = (struct ThreadArg*) arg;
  int (*fn) (void *) = threadArg->fn;
  void *thread_arg = threadArg->arg;
  pid_t virtualTid = threadArg -> virtualTid;

  // Free memory previously allocated through JALLOC_HELPER_MALLOC in __clone
  JALLOC_HELPER_FREE(threadArg);

  dmtcp::VirtualPidTable::instance().updateMapping(virtualTid, _real_gettid());

  /* Thread finished initialization.  It's now safe for this thread to
   * participate in checkpoint.  Decrement the uninitializedThreadCount in
   * DmtcpWorker.
   */
  dmtcp::ThreadSync::decrementUninitializedThreadCount();

  JTRACE ( "Calling user function" ) (virtualTid);
  return (*fn) ( thread_arg );
}
#endif

extern "C" int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                              void *(*start_routine)(void*), void *arg)
{
  int retval;
  // We have to use DMTCP-specific memory allocator because using glibc:malloc
  // can interfere with user threads.
  // We use JALLOC_HELPER_FREE to free this memory in two places:
  // 1. near the beginning of pthread_start (wrapper for start_routine),
  //     providing that the __clone call succeeds with no tid conflict.
  // 2. if the call to _real_pthread_create fails, then free memory
  //     near the end of this function.
  // We use MALLOC/FREE so that pthread_create() can be called again, without
  // waiting for the new thread to give up the buffer in pthread_start().
  struct ThreadArg *threadArg =
    (struct ThreadArg *) JALLOC_HELPER_MALLOC (sizeof (struct ThreadArg));
  threadArg->pthread_fn = start_routine;
  threadArg->arg = arg;

  /* pthread_create() should acquire the thread-creation lock. Not doing so can
   * result in a deadlock in the following scenario:
   * 1. user thread: pthread_create() - acquire wrapper-execution lock
   * 2. ckpt-thread: SUSPEND msg received, wait on wrlock for wrapper-exection lock
   * 3. user thread: __clone() - try to acquire wrapper-execution lock
   *
   * We also need to increment the uninitialized-thread-count so that it is
   * safe to checkpoint the newly created thread.
   *
   * There is another possible deadlock situation if we do not grab the thread-creation lock:
   * 1. user thread: pthread_create(): waiting on tbl_lock inside libpthread
   * 2. ckpt-thread: SUSPEND msg received, wait on wrlock for wrapper-exec lock
   * 3. uset thread: a. exiting after returning from user fn.
   *                 b. grabs tbl_lock()
   *                 c. tries to call free() to deallocate previously allocated
   *                 space (stack etc.). The free() wrapper requires
   *                 wrapper-exec lock, which is not available.
   */
  bool threadCreationLockAcquired = dmtcp::ThreadSync::threadCreationLockLock();
  dmtcp::ThreadSync::incrementUninitializedThreadCount();
  retval = _real_pthread_create(thread, attr, pthread_start, threadArg);
  if (threadCreationLockAcquired) {
    dmtcp::ThreadSync::threadCreationLockUnlock();
  }
  if (retval != 0) { // if we failed to create new pthread
    JALLOC_HELPER_FREE(threadArg);
    dmtcp::ThreadSync::decrementUninitializedThreadCount();
  }
  return retval;
}

//need to forward user clone
extern "C" int __clone(int (*fn) (void *arg), void *child_stack, int flags, void *arg,
                       int *parent_tidptr, struct user_desc *newtls, int *child_tidptr)
{
  pid_t tid;
  /*
   * struct MtcpRestartThreadArg
   *
   * DMTCP requires the virtualTid of the threads being created during
   *  the RESTARTING phase.  We use an MtcpRestartThreadArg structure to pass
   *  the virtualTid of the thread being created from MTCP to DMTCP.
   *
   * actual clone call: clone (fn, child_stack, flags, void *, ... )
   * new clone call   : clone (fn, child_stack, flags,
   *                           (struct MtcpRestartThreadArg *), ...)
   *
   * DMTCP automatically extracts arg from this structure and passes that
   * to the _real_clone call.
   *
   * IMPORTANT NOTE: While updating, this struct must be kept in sync
   * with the struct of the same name in mtcp.c
   */
  struct MtcpRestartThreadArg {
    void * arg;
    pid_t virtualTid;
  } *mtcpRestartThreadArg;

  pid_t virtualTid = -1;

  WRAPPER_EXECUTION_DISABLE_CKPT();
  dmtcp::ThreadSync::incrementUninitializedThreadCount();

  if (!dmtcp_is_running_state()) {
    mtcpRestartThreadArg = (struct MtcpRestartThreadArg *) arg;
    arg                  = mtcpRestartThreadArg -> arg;
    virtualTid           = mtcpRestartThreadArg -> virtualTid;
  }

  // We have to use DMTCP-specific memory allocator because using glibc:malloc
  // can interfere with user threads.
  // We use JALLOC_HELPER_FREE to free this memory in two places:
  //   1.  later in this function in case of failure on call to __clone; and
  //   2.  near the beginnging of clone_start (wrapper for start_routine).
  struct ThreadArg *threadArg =
    (struct ThreadArg *) JALLOC_HELPER_MALLOC(sizeof (struct ThreadArg));
  threadArg->fn = fn;
  threadArg->arg = arg;
  threadArg->virtualTid = virtualTid;

  if (!dmtcp_is_running_state()) {
    JTRACE("Calling libc:__clone");
    tid = _real_clone(clone_start, child_stack, flags, threadArg,
                      parent_tidptr, newtls, child_tidptr);
  } else {
    JTRACE("Calling mtcp:__clone");
    tid = mtcpFuncPtrs.clone(clone_start, child_stack, flags, threadArg,
                             parent_tidptr, newtls, child_tidptr);
  }

  if (tid > 0) {
    JTRACE("New thread created") (tid);
    dmtcp::VirtualPidTable::instance().updateMapping(virtualTid, tid);
  } else { // if the call to clone failed
    JTRACE("Clone call failed")(JASSERT_ERRNO);
    // Free memory previously allocated by calling JALLOC_HELPER_MALLOC
    JALLOC_HELPER_FREE(threadArg);
    dmtcp::ThreadSync::decrementUninitializedThreadCount();
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();

  return tid;
}

extern "C" void pthread_exit(void * retval)
{
  mtcpFuncPtrs.threadiszombie();
#ifdef PID_VIRTUALIZATION
  dmtcp::VirtualPidTable::instance().erase(gettid());
#endif
  dmtcp::ProcessInfo::instance().eraseTid(gettid());
  _real_pthread_exit(retval);
  for(;;); // To hide compiler warning about "noreturn" function
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
 * restart, the tid would have changed, but the call to futex would still used
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
extern "C" int pthread_join(pthread_t thread, void **retval)
{
  int ret;
  if (!dmtcp::ProcessInfo::instance().beginPthreadJoin(thread)) {
    return EINVAL;
  }

  while (1) {
    WRAPPER_EXECUTION_DISABLE_CKPT();
    ret = _real_pthread_tryjoin_np(thread, retval);
    WRAPPER_EXECUTION_ENABLE_CKPT();

    if (ret != EBUSY) {
      break;
    }

    const struct timespec timeout = {(time_t) 0, (long)100 * 1000 * 1000};
    nanosleep(&timeout, NULL);
  }

#ifdef PTRACE
  /* Wrap the call to pthread_join() to make sure we call
   * delete_thread_on_pthread_join().
   * FIXME:  MTCP:process_pthread_join(thread) is calling threadisdead() THIS
   *         SHOULDN'T BE NECESSARY.
   */
  if (ret == 0) {
    mtcpFuncPtrs.process_pthread_join(thread);
  }
#endif

  dmtcp::ProcessInfo::instance().endPthreadJoin(thread);
  return ret;
}

extern "C" int pthread_tryjoin_np(pthread_t thread, void **retval)
{
  int ret;
  if (!dmtcp::ProcessInfo::instance().beginPthreadJoin(thread)) {
    return EINVAL;
  }

  WRAPPER_EXECUTION_DISABLE_CKPT();
  ret = _real_pthread_tryjoin_np(thread, retval);
  WRAPPER_EXECUTION_ENABLE_CKPT();

#ifdef PTRACE
  /* Wrap the call to pthread_join() to make sure we call
   * delete_thread_on_pthread_join().
   * FIXME:  MTCP:process_pthread_join(thread) is calling threadisdead() THIS
   *         SHOULDN'T BE NECESSARY.
   */
  if (ret == 0) {
    mtcpFuncPtrs.process_pthread_join(thread);
  }
#endif

  dmtcp::ProcessInfo::instance().endPthreadJoin(thread);
  return ret;
}

extern "C" int pthread_timedjoin_np(pthread_t thread, void **retval,
                                    const struct timespec *abstime)
{
  int ret;
  if (!dmtcp::ProcessInfo::instance().beginPthreadJoin(thread)) {
    return EINVAL;
  }

  /*
   * We continue to call pthread_tryjoin_np (and sleep) until we have gone past
   * the abstime provided by the caller
   */
  while (1) {
    struct timeval tv;
    struct timespec ts;
    JASSERT(gettimeofday(&tv, NULL) == 0);
    TIMEVAL_TO_TIMESPEC(&tv, &ts);

    WRAPPER_EXECUTION_DISABLE_CKPT();
    ret = _real_pthread_tryjoin_np(thread, retval);
    WRAPPER_EXECUTION_ENABLE_CKPT();

    if (ret == 0) {
      break;
    }

    if (ts.tv_sec > abstime->tv_sec || (ts.tv_sec == abstime->tv_sec &&
                                        ts.tv_nsec > abstime->tv_nsec)) {
      ret = ETIMEDOUT;
      break;
    }

    const struct timespec timeout = {(time_t) 0, (long)100 * 1000 * 1000};
    nanosleep(&timeout, NULL);
  }

#ifdef PTRACE
  /* Wrap the call to pthread_join() to make sure we call
   * delete_thread_on_pthread_join().
   * FIXME:  MTCP:process_pthread_join(thread) is calling threadisdead() THIS
   *         SHOULDN'T BE NECESSARY.
   */
  if (ret == 0) {
    mtcpFuncPtrs.process_pthread_join(thread);
  }
#endif

  dmtcp::ProcessInfo::instance().endPthreadJoin(thread);
  return ret;
}
