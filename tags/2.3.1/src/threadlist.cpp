#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <semaphore.h>
#include <sys/resource.h>
#include <linux/version.h>
#include "threadlist.h"
#include "siginfo.h"
#include "dmtcpalloc.h"
#include "syscallwrappers.h"
#include "mtcpinterface.h"
#include "ckptserializer.h"
#include "uniquepid.h"
#include "jalloc.h"
#include "jassert.h"
#include "util.h"
#include "mtcp/mtcp_header.h"

// FIXME: Replace DPRINTF in the code below with JTRACE/JNOTE after the
//        locking mechanism has been fixed.
#undef DPRINTF
#define DPRINTF(...) do{}while(0)

// For i386 and x86_64, SETJMP currently has bugs.  Don't turn this
//   on for them until they are debugged.
// Default is to use  setcontext/getcontext.
#if defined(__arm__)
# define SETJMP /* setcontext/getcontext not defined for ARM glibc */
#endif

#ifdef SETJMP
# include <setjmp.h>
#else
# include <ucontext.h>
#endif


using namespace dmtcp;

//Globals
volatile int restoreInProgress = 0;
Thread *motherofall = NULL;
void **motherofall_saved_sp = NULL;
ThreadTLSInfo *motherofall_tlsInfo = NULL;
pid_t motherpid = 0;
sigset_t sigpending_global;
Thread *activeThreads = NULL;
void *saved_sysinfo;
MYINFO_GS_T myinfo_gs __attribute__ ((visibility ("hidden")));


static Thread *threads_freelist = NULL;
static pthread_mutex_t threadlistLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t threadStateLock = PTHREAD_MUTEX_INITIALIZER;

static __thread Thread *curThread = NULL;
static Thread *ckptThread = NULL;
static int numUserThreads = 0;
static int originalstartup;

static sem_t sem_start;
static sem_t semNotifyCkptThread;
static sem_t semWaitForCkptThreadSignal;

static void *checkpointhread (void *dummy);
static void suspendThreads();
static void resumeThreads();
static void stopthisthread(int sig);
static int restarthread(void *threadv);
static int Thread_UpdateState(Thread *th,
                              ThreadState newval,
                              ThreadState oldval);
static void Thread_SaveSigState(Thread *th);
static void Thread_RestoreSigState(Thread *th);

/*****************************************************************************
 *
 * Lock and unlock the 'activeThreads' list
 *
 *****************************************************************************/
static void lock_threads (void) {
  JASSERT(_real_pthread_mutex_lock(&threadlistLock) == 0) (JASSERT_ERRNO);
}
static void unlk_threads (void) {
  JASSERT(_real_pthread_mutex_unlock(&threadlistLock) == 0) (JASSERT_ERRNO);
}

/*****************************************************************************
 *
 * We will use the region beyond the end of stack for our temporary stack.
 * glibc sigsetjmp will mangle pointers;  We need the unmangled pointer.
 * So, we can't rely on parsing the jmpbuf for the saved sp.
 *
 *****************************************************************************/
static void save_sp(void **sp)
{
#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(mov %%esp,%0)
		: "=g" (*sp)
                : : "memory");
#elif defined(__arm__)
  asm volatile ("mov %0,sp"
		: "=r" (*sp)
                : : "memory");
#else
# error "assembly instruction not translated"
#endif
}

/*****************************************************************************
 *
 * Get _real_ tid/pid
 *
 *****************************************************************************/

/*****************************************************************************
 *
 * New process. Empty the activeThreads list
 *
 *****************************************************************************/
void ThreadList::resetOnFork()
{
  lock_threads();
  while (activeThreads != NULL) {
    ThreadList::threadIsDead(activeThreads); // takes care of updating "activeThreads" ptr.
  }
  unlk_threads();
}

/*****************************************************************************
 *
 *  This routine must be called at startup time to initiate checkpointing
 *
 *****************************************************************************/
void ThreadList::init()
{
  /* Save this process's pid.  Then verify that the TLS has it where it should
   * be. When we do a restore, we will have to modify each thread's TLS with the
   * new motherpid. We also assume that GS uses the first GDT entry for its
   * descriptor.
   */

  /* libc/getpid can lie if we had used kernel fork() instead of libc fork(). */
  motherpid = THREAD_REAL_TID();
  TLSInfo_VerifyPidTid(motherpid, motherpid);

  SigInfo::setupCkptSigHandler(&stopthisthread);

  /* Set up caller as one of our threads so we can work on it */
  motherofall = ThreadList::getNewThread();
  motherofall_saved_sp = &motherofall->saved_sp;
  motherofall_tlsInfo = &motherofall->tlsInfo;
  updateTid(motherofall);

  sem_init(&sem_start, 0, 0);
  sem_init(&semNotifyCkptThread, 0, 0);
  sem_init(&semWaitForCkptThreadSignal, 0, 0);

  originalstartup = 1;
  pthread_t checkpointhreadid;
  /* Spawn off a thread that will perform the checkpoints from time to time */
  JASSERT(pthread_create(&checkpointhreadid, NULL, checkpointhread, NULL) == 0);

  /* Stop until checkpoint thread has finished initializing.
   * Some programs (like gcl) implement their own glibc functions in
   * a non-thread-safe manner.  In case we're using non-thread-safe glibc,
   * don't run the checkpoint thread and user thread at the same time.
   */
  errno = 0;
  while (-1 == sem_wait(&sem_start) && errno == EINTR)
    errno = 0;
  sem_destroy(&sem_start);
}

/*****************************************************************************
 *
 *****************************************************************************/
void ThreadList::initThread(Thread* th, int (*fn)(void*), void *arg, int flags,
                            int *ptid, int *ctid)
{
  /* Save exactly what the caller is supplying */
  th->fn    = fn;
  th->arg   = arg;
  th->flags = flags;
  th->ptid  = ptid;
  th->ctid  = ctid;
  th->next  = NULL;
  th->state = ST_RUNNING;

  /* libpthread may recycle the thread stacks after the thread exits (due to
   * return, pthread_exit, or pthread_cancel) by reusing them for a different
   * thread created by a subsequent call to pthread_create().
   *
   * Part of thread-stack also contains the "struct pthread" with pid and tid
   * as member fields. While reusing the stack for the new thread, the tid
   * field is reset but the pid field is left unchanged (under the assumption
   * that pid never changes). This causes a problem if the thread exited before
   * checkpoint and the new thread is created after restart and hence the pid
   * field contains the wrong value (pre-ckpt pid as opposed to current-pid).
   *
   * The solution is to put the motherpid in the pid slot every time a new
   * thread is created to make sure that struct pthread has the correct value.
   */
  TLSInfo_UpdatePid();
}

/*****************************************************************************
 *
 * Thread exited/exiting.
 *
 *****************************************************************************/
void ThreadList::threadExit()
{
  curThread->state = ST_ZOMBIE;
}

/*****************************************************************************
 *
 *****************************************************************************/
void ThreadList::updateTid(Thread *th)
{
  if (curThread == NULL)
    curThread = th;
  th->tid = THREAD_REAL_TID();
  th->virtual_tid = _real_gettid();
  JTRACE("starting thread") (th->tid) (th->virtual_tid);
  // Check and remove any thread descriptor which has the same tid as ours.
  // Also, remove any dead threads from the list.
  ThreadList::addToActiveList();
}

/*************************************************************************
 *
 *  Send a signal to ckpt-thread to wake it up from select call and exit.
 *
 *************************************************************************/
void ThreadList::killCkpthread()
{
  JTRACE("Kill checkpinthread") (ckptThread->tid);
  THREAD_TGKILL(motherpid, ckptThread->tid, SigInfo::ckptSignal());
}

/*************************************************************************
 *
 *  Prepare MTCP Header
 *
 *************************************************************************/
static void prepareMtcpHeader(MtcpHeader *mtcpHdr)
{
  memset(mtcpHdr, 0, sizeof(*mtcpHdr));
  strncpy(mtcpHdr->signature, MTCP_SIGNATURE, strlen(MTCP_SIGNATURE) + 1);
  mtcpHdr->saved_brk = sbrk(0);
  // TODO: Now that we have a separate mtcp dir, the code dealing with
  // restoreBuf should go in there.
  mtcpHdr->restore_addr = (void*) ProcessInfo::instance().restoreBufAddr();
  mtcpHdr->restore_size = ProcessInfo::instance().restoreBufLen();
  mtcpHdr->post_restart = &ThreadList::postRestart;
  memcpy(&mtcpHdr->motherofall_tls_info,
         &motherofall->tlsInfo,
         sizeof(motherofall->tlsInfo));
  mtcpHdr->tls_pid_offset = TLSInfo_GetPidOffset();
  mtcpHdr->tls_tid_offset = TLSInfo_GetTidOffset();
  mtcpHdr->myinfo_gs = myinfo_gs;
}

/*************************************************************************
 *
 *  This executes as a thread.  It sleeps for the checkpoint interval
 *    seconds, then wakes to write the checkpoint file.
 *
 *************************************************************************/
static void *checkpointhread (void *dummy)
{
  /* This is the start function of the checkpoint thread.
   * We also call sigsetjmp/getcontext to get a snapshot of this call frame,
   * since we will never exit this call frame.  We always return
   * to this call frame at time of startup, on restart.  Hence, restart
   * will forget any modifications to our local variables since restart.
   */

  ckptThread = curThread;
  ckptThread->state = ST_CKPNTHREAD;

  /* For checkpoint thread, we want to block delivery of all but some special
   * signals
   */
  {
    /*
     * For the checkpoint thread, we should not block SIGSETXID which is used
     * by the setsid family of system calls to change the session leader. Glibc
     * uses this signal to notify the process threads of the change in session
     * leader information. This signal is not documented and is used internally
     * by glibc. It is defined in <glibc-src-root>/nptl/pthreadP.h
     * screen was getting affected by this since it used setsid to change the
     * session leaders.
     * Similarly, SIGCANCEL/SIGTIMER is undocumented, but used by glibc.
     */
#define SIGSETXID (__SIGRTMIN + 1)
#define SIGCANCEL (__SIGRTMIN) /* aka SIGTIMER */
    sigset_t set;

    sigfillset(&set);
    sigdelset(&set, SIGSETXID);
    sigdelset(&set, SIGCANCEL);

    // FIXME: Compiler issuing warning here; Why do we mix ASSERT and JASSERT?
    ASSERT(pthread_sigmask(SIG_SETMASK, &set, NULL) == 0);
  }

  Thread_SaveSigState(ckptThread);
  TLSInfo_SaveTLSState(&ckptThread->tlsInfo);
  /* Release user thread after we've initialized. */
  sem_post(&sem_start);

  /* Set up our restart point, ie, we get jumped to here after a restore */
#ifdef SETJMP
  JASSERT(sigsetjmp(ckptThread->jmpbuf, 1) >= 0) (JASSERT_ERRNO);
#else
  JASSERT(getcontext(&ckptThread->savctx) == 0) (JASSERT_ERRNO);
#endif
  save_sp(&ckptThread->saved_sp);
  JTRACE("after sigsetjmp/getcontext")
    (curThread->tid) (curThread->virtual_tid) (curThread->saved_sp);

  if (originalstartup) {
    originalstartup = 0;
  } else {
    /* We are being restored.  Wait for all other threads to finish being
     * restored before resuming checkpointing.
     */
    JTRACE("waiting for other threads after restore");
    ThreadList::waitForAllRestored(ckptThread);
    JTRACE("resuming after restore");
  }

  while (1) {
    /* Wait a while between writing checkpoint files */
    JTRACE("before callbackSleepBetweenCheckpoint(0)");
    callbackSleepBetweenCheckpoint(0);

    restoreInProgress = 0;
    suspendThreads();
    SigInfo::saveSigHandlers();
    /* Do this once, same for all threads.  But restore for each thread. */
    if (TLSInfo_HaveThreadSysinfoOffset())
      saved_sysinfo = TLSInfo_GetThreadSysinfo();

    /* All other threads halted in 'stopthisthread' routine (they are all
     * in state ST_SUSPENDED).  It's safe to write checkpoint file now.
     */
    JTRACE("before callbackSleepBetweenCheckpoint(0)");
    callbackPreCheckpoint();

    // Remove stale threads from activeThreads list.
    ThreadList::emptyFreeList();

    MtcpHeader mtcpHdr;
    prepareMtcpHeader(&mtcpHdr);
    CkptSerializer::writeCkptImage(&mtcpHdr, sizeof(mtcpHdr));

    JTRACE("before callbackPostCheckpoint(0, NULL)");
    callbackPostCheckpoint(0, NULL);

    /* Resume all threads. */
    JTRACE("resuming everything");
    resumeThreads();
    JTRACE("everything resumed");
  }
  return NULL;
}

static void suspendThreads()
{
  int needrescan;
  Thread *thread;
  Thread *next;

  /* Halt all other threads - force them to call stopthisthread
   * If any have blocked checkpointing, wait for them to unblock before
   * signalling
   */
  lock_threads();
  do {
    needrescan = 0;
    numUserThreads = 0;
    for (thread = activeThreads; thread != NULL; thread = next) {
      next = thread->next;
      int ret;
      /* Do various things based on thread's state */
      switch (thread->state) {

        case ST_RUNNING:
          /* Thread is running. Send it a signal so it will call stopthisthread.
           * We will need to rescan (hopefully it will be suspended by then)
           */
          if (Thread_UpdateState(thread, ST_SIGNALED, ST_RUNNING)) {
            if (THREAD_TGKILL(motherpid, thread->tid, SigInfo::ckptSignal()) < 0) {
              JASSERT(errno == ESRCH) (JASSERT_ERRNO) (thread->tid)
                .Text("error signalling thread");
              ThreadList::threadIsDead(thread);
            } else {
              needrescan = 1;
            }
          }
          break;

        case ST_ZOMBIE:
          ret = THREAD_TGKILL(motherpid, thread->tid, 0);
          JASSERT(ret == 0 || errno == ESRCH);
          if (ret == -1 && errno == ESRCH) {
            ThreadList::threadIsDead(thread);
          }
          break;

        case ST_SIGNALED:
          if (THREAD_TGKILL(motherpid, thread->tid, 0) == -1 && errno == ESRCH) {
            ThreadList::threadIsDead(thread);
          } else {
            needrescan = 1;
          }
          break;

        case ST_SUSPINPROG:
          numUserThreads++;
          break;

        case ST_SUSPENDED:
          numUserThreads++;
          break;

        case ST_CKPNTHREAD:
          break;

        default:
          JASSERT(false);
      }
    }
    if (needrescan) usleep(10);
  } while (needrescan);
  unlk_threads();

  // All we want to do is unlock the jassert/jalloc locks, if we reset them, it
  // serves the purpose without having a callback.
  // TODO: Check for correctness.
  JALIB_CKPT_UNLOCK();

  for (int i = 0; i < numUserThreads; i++) {
    sem_wait(&semNotifyCkptThread);
  }

  JASSERT(activeThreads != NULL);
  JTRACE("everything suspended") (numUserThreads);
}

static void resumeThreads()
{
  int i;
  for (i = 0; i < numUserThreads; i++) {
    sem_post(&semWaitForCkptThreadSignal);
  }
}

/*************************************************************************
 *
 *  Signal handler for user threads.
 *
 *************************************************************************/
void stopthisthread (int signum)
{
  // If this is checkpoint thread - exit immidiately
  if (curThread == ckptThread) return;

  /* Possible state change scenarios:
   * 1. STOPSIGNAL received from ckpt-thread. In this case, the ckpt-thread
   * already changed the state to ST_SIGNALED. No need to check for locks.
   * Proceed normally.
   *
   * 2. STOPSIGNAL received from Superior thread. In this case we change the
   * state to ST_SIGNALED, if currently in ST_RUNNING. If we are holding
   * any locks (callback_holds_any_locks), we return from the signal handler.
   *
   * 3. STOPSIGNAL raised by this thread itself, after releasing all the locks.
   * In this case, we had already changed the state to ST_SIGNALED as a
   * result of step (2), so the ckpt-thread will never send us a signal.
   *
   * 4. STOPSIGNAL received from Superior thread. Ckpt-threads sends a signal
   * before we had a chance to change state from ST_RUNNING ->
   * ST_SIGNALED. This puts the STOPSIGNAL in the queue. The ckpt-thread will
   * later call sigaction(STOPSIGNAL, SIG_IGN) followed by
   * sigaction(STOPSIGNAL, stopthisthread) to discard all pending signals.
   */
  if (Thread_UpdateState(curThread, ST_SIGNALED, ST_RUNNING)) {
    int retval;
    callbackHoldsAnyLocks(&retval);
    if (retval) return;
  }

//  DPRINTF("Thread (%d) return address: %p\n",
//          curThread->tid, __builtin_return_address(0));

  // make sure we don't get called twice for same thread
  if (Thread_UpdateState(curThread, ST_SUSPINPROG, ST_SIGNALED)) {

    Thread_SaveSigState(curThread); // save sig state (and block sig delivery)
    TLSInfo_SaveTLSState(&curThread->tlsInfo); // save thread local storage state

    /* Set up our restart point, ie, we get jumped to here after a restore */
#ifdef SETJMP
  ASSERT(sigsetjmp(curThread->jmpbuf, 1) >= 0);
#else
  ASSERT(getcontext(&curThread->savctx) == 0);
#endif
  save_sp(&curThread->saved_sp);
//  DPRINTF("after sigsetjmp/getcontext. tid: %d, vtid: %d, saved_sp: %p\n",
//          curThread->tid, curThread->virtual_tid, curThread->saved_sp);

    if (!restoreInProgress) {
      /* We are a user thread and all context is saved.
       * Wait for ckpt thread to write ckpt, and resume.
       */

      /* This sets a static variable in dmtcp.  It must be passed
       * from this user thread to ckpt thread before writing ckpt image
       */
      if (dmtcp_ptrace_enabled == NULL) {
        callbackPreSuspendUserThread();
      }

      /* Tell the checkpoint thread that we're all saved away */
      ASSERT(Thread_UpdateState(curThread, ST_SUSPENDED, ST_SUSPINPROG));
      sem_post(&semNotifyCkptThread);

      /* This sets a static variable in dmtcp.  It must be passed
       * from this user thread to ckpt thread before writing ckpt image
       */
      if (dmtcp_ptrace_enabled != NULL && dmtcp_ptrace_enabled()) {
        callbackPreSuspendUserThread();
      }

      /* Then wait for the ckpt thread to write the ckpt file then wake us up */
//      DPRINTF("User thread (%d) suspended\n", curThread->tid);
      sem_wait(&semWaitForCkptThreadSignal);
//      DPRINTF("User thread (%d) resuming\n", curThread->tid);
    } else {
      /* Else restoreinprog >= 1;  This stuff executes to do a restart */
      ThreadList::waitForAllRestored(curThread);
//      DPRINTF("thread (%d) restored\n", curThread->tid);
    }

    ASSERT(Thread_UpdateState(curThread, ST_RUNNING, ST_SUSPENDED));

//    DPRINTF("Thread (%d) returning to user code: %p\n",
//            curThread->tid, __builtin_return_address(0));

    callbackPreResumeUserThread(restoreInProgress);
  }
}

/*****************************************************************************
 *
 *  Wait for all threads to finish restoring their context, then release them
 *  all to continue on their way.
 *
 *****************************************************************************/
void ThreadList::waitForAllRestored(Thread *thread)
{
  if (thread == ckptThread) {
    int i;
    for (i = 0; i < numUserThreads; i++) {
      sem_wait(&semNotifyCkptThread);
    }

    JTRACE("before callback_post_ckpt(1=restarting)");
    callbackPostCheckpoint(1, NULL); //mtcp_restoreargv_start_addr);
    JTRACE("after callback_post_ckpt(1=restarting)");

    SigInfo::restoreSigHandlers();

    /* raise the signals which were pending for the entire process at the time
     * of checkpoint. It is assumed that if a signal is pending for all threads
     * including the ckpt-thread, then it was sent to the process as opposed to
     * sent to individual threads.
     */
    for (i = SIGRTMAX; i > 0; --i) {
      if (sigismember(&sigpending_global, i)) {
        kill(getpid(), i);
      }
    }

    // if this was last of all, wake everyone up
    for (i = 0; i < numUserThreads; i++) {
      sem_post(&semWaitForCkptThreadSignal);
    }
  } else {
    sem_post(&semNotifyCkptThread);
    sem_wait(&semWaitForCkptThreadSignal);
    Thread_RestoreSigState(thread);
  }
}

/*****************************************************************************
 *
 *****************************************************************************/
void ThreadList::postRestart(void)
{
  Thread *thread;
  sigset_t tmp;

  /* Fill in the new mother process id */
  motherpid = THREAD_REAL_TID();
  motherofall->tid = motherpid;

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
      mtcpRestartThreadArg.virtualTid = thread->virtual_tid;
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

/*****************************************************************************
 *
 *****************************************************************************/
static int restarthread (void *threadv)
{
  Thread *thread = (Thread*) threadv;
  thread->tid = THREAD_REAL_TID();
  TLSInfo_RestoreTLSState(&thread->tlsInfo);

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
 * If there is a thread descriptor with the same tid, it must be from a dead
 * thread. Remove it now.
 *
 *****************************************************************************/
void ThreadList::addToActiveList()
{
  int tid;
  Thread *thread;
  Thread *next_thread;

  lock_threads();

  tid = curThread->tid;
  JASSERT (tid != 0);

  // First remove duplicate descriptors.
  for (thread = activeThreads; thread != NULL; thread = next_thread) {
    next_thread = thread->next;
    if (thread != curThread && thread->tid == tid) {
      JTRACE("Removing duplicate thread descriptor")
        (thread->tid) (thread->virtual_tid);
      // There will be at most one duplicate descriptor.
      threadIsDead(thread);
      continue;
    }
    /* NOTE:  ST_ZOMBIE is used only for the sake of efficiency.  We
     *   test threads in state ST_ZOMBIE using tgkill to remove them
     *   early (before reaching a checkpoint) so that the
     *   threadrdescriptor list does not grow too long.
     */
    if (thread->state == ST_ZOMBIE) {
      /* if no thread with this tid, then we can remove zombie descriptor */
      if (-1 == THREAD_TGKILL(motherpid, thread->tid, 0)) {
        JTRACE("Killing zombie thread") (thread->tid);
        threadIsDead(thread);
      }
    }
  }

  curThread->next = activeThreads;
  curThread->prev = NULL;
  if (activeThreads != NULL) {
    activeThreads->prev = curThread;
  }
  activeThreads = curThread;

  unlk_threads();
  return;
}

/*****************************************************************************
 *
 *  Thread has exited - move it from activeThreads list to freelist.
 *
 *  threadisdead() used to free() the Thread struct before returning. However,
 *  if we do that while in the middle of a checkpoint, the call to free() might
 *  deadlock in JAllocator. For this reason, we put the to-be-removed threads
 *  on this threads_freelist and call free() only when it is safe to do so.
 *
 *  This has an added benefit of reduced number of calls to malloc() as the
 *  Thread structs in the freelist can be recycled.
 *
 *****************************************************************************/
void ThreadList::threadIsDead (Thread *thread)
{
  JASSERT(thread != NULL);
  DPRINTF("Putting thread (%d) on freelist\n", thread->tid);

  /* Remove thread block from 'threads' list */
  if (thread->prev != NULL) {
    thread->prev->next = thread->next;
  }
  if (thread->next != NULL) {
    thread->next->prev = thread->prev;
  }
  if (thread == activeThreads) {
    activeThreads = activeThreads->next;
  }

  thread->next = threads_freelist;
  threads_freelist = thread;
}

/*****************************************************************************
 *
 * Return thread from freelist.
 *
 *****************************************************************************/
Thread *ThreadList::getNewThread()
{
  Thread *thread;

  lock_threads();
  if (threads_freelist == NULL) {
    thread = (Thread*) JALLOC_HELPER_MALLOC(sizeof(Thread));
    ASSERT(thread != NULL);
  } else {
    thread = threads_freelist;
    threads_freelist = threads_freelist->next;
  }
  unlk_threads();
  memset(thread, 0, sizeof (*thread));
  return thread;
}

/*****************************************************************************
 *
 * Call free() on all threads_freelist items
 *
 *****************************************************************************/
void ThreadList::emptyFreeList()
{
  lock_threads();

  while (threads_freelist != NULL) {
    Thread *thread = threads_freelist;
    threads_freelist = threads_freelist->next;
    JALLOC_HELPER_FREE(thread);
  }

  unlk_threads();
}

