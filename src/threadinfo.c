#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>  /* for CLONE_SETTLS, needs _GNU_SOURCE */
#include "threadinfo.h"
#include "dmtcp.h"
#include "tlsinfo.h"

static void restoreAllThreads(void);
static int restarthread (void *threadv);

static pthread_mutex_t threadStateLock = PTHREAD_MUTEX_INITIALIZER;
static int ckptSignal = -1;

extern volatile int restoreInProgress;
extern Thread *motherofall;
extern pid_t motherpid;
extern sigset_t sigpending_global;
extern Thread *activeThreads;
extern void *saved_sysinfo;

static int restarthread(void *threadv);
static void restoreAllThreads(void);

/*
 * struct MtcpRestartThreadArg
 *
 * DMTCP requires the virtual_tid of the threads being created during
 *  the RESTARTING phase.  We use a MtcpRestartThreadArg struct to pass
 *  the virtual_tid of the thread being created from MTCP to DMTCP.
 *
 * actual clone call: clone (fn, child_stack, flags, void *, ... )
 * new clone call   : clone (fn, child_stack, flags,
 *                           (struct MtcpRestartThreadArg *), ...)
 *
 * DMTCP automatically extracts arg from this structure and passes that
 * to the _real_clone call.
 *
 * IMPORTANT NOTE: While updating, this struct must be kept in sync
 * with the struct of the same name in mtcpinterface.cpp
 */
struct MtcpRestartThreadArg {
  void *arg;
  pid_t virtual_tid;
};


/*****************************************************************************
 *
 *  This routine must be called at startup time to initiate checkpointing
 *
 *****************************************************************************/
void Thread_Init()
{
}

/*****************************************************************************
 *
 *****************************************************************************/
int Thread_UpdateState(Thread *th, ThreadState newval, ThreadState oldval)
{
  int res = 0;
  ASSERT(_real_pthread_mutex_lock(&threadStateLock) == 0);
  if (oldval == th->state) {;
    th->state = newval;
    res = 1;
  }
  ASSERT(_real_pthread_mutex_unlock(&threadStateLock) == 0);
  return res;
}

/*****************************************************************************
 *
 *  Save signal mask and list of pending signals delivery
 *
 *****************************************************************************/
void Thread_SaveSigState(Thread *th)
{
  // Save signal block mask
  ASSERT(pthread_sigmask (SIG_SETMASK, NULL, &th->sigblockmask) == 0);

  // Save pending signals
  sigpending(&th->sigpending);
}

/*****************************************************************************
 *
 *  Restore signal mask and all pending signals
 *
 *****************************************************************************/
void Thread_RestoreSigState (Thread *th)
{
  int i;
  DPRINTF("restoring handlers for thread: %d", th->virtual_tid);
  ASSERT(pthread_sigmask (SIG_SETMASK, &th->sigblockmask, NULL) == 0);

  // Raise the signals which were pending for only this thread at the time of
  // checkpoint.
  for (i = SIGRTMAX; i > 0; --i) {
    if (sigismember(&th->sigpending, i)  == 1  &&
        sigismember(&th->sigblockmask, i) == 1 &&
        sigismember(&sigpending_global, i) == 0 &&
        i != dmtcp_get_ckpt_signal()) {
      if (i != SIGCHLD) {
        PRINTF("\n*** WARNING:  SIGCHLD was delivered prior to ckpt.\n"
               "*** Will raise it on restart.  If not desired, change\n"
               "*** this line raising SIGCHLD.");
      }
      raise(i);
    }
  }
}

/*****************************************************************************
 *
 *  The original program's memory and files have been restored
 *
 *****************************************************************************/
void Thread_PostRestart()
{
  /* Now we can access all our files and memory that existed at the time of the
   * checkpoint.
   * We are still on the temporary stack, though.
   */

  /* Call another routine because our internal stack is whacked and we can't
   * have local vars.
   */

  ///JA: v54b port
  // so restarthread will have a big stack
#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(mov %0,%%esp)
		: : "g" ((char*)motherofall->saved_sp - 128) //-128 for red zone
                : "memory");
#elif defined(__arm__)
  asm volatile ("mov sp,%0"
		: : "r" (motherofall->saved_sp - 128) //-128 for red zone
                : "memory");
#else
#  error "assembly instruction not translated"
#endif
  restoreAllThreads();
}

static void restoreAllThreads(void)
{
  Thread *thread;
  sigset_t tmp;

  /* Fill in the new mother process id */
  motherpid = THREAD_REAL_TID();
  motherofall->tid = motherpid;
  TLSInfo_RestoreTLSState(motherofall);

  restoreInProgress = 1;

  sigfillset(&tmp);
  for (thread = activeThreads; thread != NULL; thread = thread->next) {
    struct MtcpRestartThreadArg mtcpRestartThreadArg;
    sigandset(&sigpending_global, &tmp, &(thread->sigpending));
    tmp = sigpending_global;

    if (thread == motherofall) continue;

    /* DMTCP needs to know virtual_tid of the thread being recreated by the
     *  following clone() call.
     *
     * Threads are created by using syscall which is intercepted by DMTCP and
     *  the virtual_tid is sent to DMTCP as a field of MtcpRestartThreadArg
     *  structure. DMTCP will automatically extract the actual argument
     *  (clonearg->arg) from clone_arg and will pass it on to the real
     *  clone call.
     */
    void *clonearg = thread;
    if (dmtcp_real_to_virtual_pid != NULL) {
      mtcpRestartThreadArg.arg = thread;
      mtcpRestartThreadArg.virtual_tid = thread->virtual_tid;
      clonearg = &mtcpRestartThreadArg;
    }

    /* Create the thread so it can finish restoring itself. */
    pid_t tid = _real_clone(restarthread,
                            // -128 for red zone
                            (void*)((char*)thread->saved_sp - 128),
                            /* Don't do CLONE_SETTLS (it'll puke).  We do it
                             * later via restoreTLSState. */
                            thread->flags & ~CLONE_SETTLS,
                            clonearg, thread->ptid, NULL, thread->ctid);

    ASSERT (tid > 0); // (JASSERT_ERRNO) .Text("Error recreating thread");
    DPRINTF("Thread recreated: orig_tid %d, new_tid: %d", thread->tid, tid);
  }
  restarthread (motherofall);
}

static int restarthread (void *threadv)
{
  Thread *thread = (Thread*) threadv;
  thread->tid = THREAD_REAL_TID();
  TLSInfo_RestoreTLSState(thread);

  if (TLSInfo_HaveThreadSysinfoOffset())
    TLSInfo_SetThreadSysinfo(saved_sysinfo);

  /* Jump to the stopthisthread routine just after sigsetjmp/getcontext call.
   * Note that if this is the restored checkpointhread, it jumps to the
   * checkpointhread routine
   */
  DPRINTF("calling siglongjmp/setcontext: tid: %d, vtid: %d",
         thread->tid, thread->virtual_tid);
#ifdef SETJMP
  siglongjmp(thread->jmpbuf, 1); /* Shouldn't return */
#else
  setcontext(&thread->savctx); /* Shouldn't return */
#endif
  ASSERT_NOT_REACHED();
  return (0); /* NOTREACHED : stop compiler warning */
}

