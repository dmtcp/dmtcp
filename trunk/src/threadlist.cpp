#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <semaphore.h>
#include <sys/resource.h>
#include <linux/version.h>
#include <linux/limits.h>
#include "threadlist.h"
#include "tlsinfo.h"
#include "siginfo.h"
#include "dmtcpalloc.h"
#include "syscallwrappers.h"
#include "mtcpinterface.h"
#include "ckptserializer.h"
#include "uniquepid.h"
#include "jalloc.h"
#include "jassert.h"


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

static Thread *activeThreads = NULL;
static Thread *threads_freelist = NULL;
static pthread_mutex_t threadlistLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t threadStateLock = PTHREAD_MUTEX_INITIALIZER;

static pid_t motherpid = 0;
static __thread dmtcp::Thread *curThread = NULL;
Thread *motherofall = NULL;
static Thread *ckptThread = NULL;
static int numUserThreads = 0;
static volatile int restoreInProgress = 0;
static int originalstartup;

static sigset_t sigpending_global;
static void *checkpointhread (void *dummy);
static sem_t sem_start;
static sem_t semNotifyCkptThread;
static sem_t semWaitForCkptThreadSignal;
static void *saved_sysinfo;

static int restarthread (void *threadv);
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
 *  This routine must be called at startup time to initiate checkpointing
 *
 *****************************************************************************/
void Thread::init(int (*_fn)(void*), void *_arg, int _flags,
                  pid_t *_ptid, int *_ctid)
{
  /* Save exactly what the caller is supplying */
  fn    = _fn;
  arg   = _arg;
  flags = _flags;
  ptid  = _ptid;
  ctid  = _ctid;
  next  = NULL;
  state = ST_RUNNING;

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
  TLSInfo::updatePid();
}

void Thread::updateTid()
{
  if (curThread == NULL)
    curThread = this;
  tid = ThreadList::_real_tid();
  virtual_tid = _real_gettid();
  JTRACE("starting thread") (tid);
  // Check and remove any thread descriptor which has the same tid as ours.
  // Also, remove any dead threads from the list.
  ThreadList::addToActiveList();
}

bool Thread::updateState(ThreadState newval, ThreadState oldval)
{
  bool res = false;
  JASSERT(_real_pthread_mutex_lock(&threadStateLock) == 0);
  if (oldval == state) {;
    state = newval;
    res = true;
  }
  JASSERT(_real_pthread_mutex_unlock(&threadStateLock) == 0);
  return res;
}

/*****************************************************************************
 *
 *  Save signal mask and list of pending signals delivery
 *
 *****************************************************************************/
void Thread::saveSigState()
{
  /* For checkpoint thread, we want to block delivery of all but some special
   * signals
   */
  if (this == ckptThread) {
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

    JASSERT(pthread_sigmask(SIG_SETMASK, &set, NULL) == 0)
      .Text("error getting signal mask");
  }
  // Save signal block mask
  JASSERT(pthread_sigmask (SIG_SETMASK, NULL, &sigblockmask) == 0)
    .Text("error getting signal mask");

  // Save pending signals
  ::sigpending(&sigpending);
}

/*****************************************************************************
 *
 *  Restore signal mask and all pending signals
 *
 *****************************************************************************/
void Thread::restoreSigState ()
{
  int i;
  JTRACE("restoring handlers for thread") (virtual_tid);
  JASSERT(pthread_sigmask (SIG_SETMASK, &sigblockmask, NULL) == 0)
    .Text("error setting signal mask");

  // Raise the signals which were pending for only this thread at the time of
  // checkpoint.
  for (i = SIGRTMAX; i > 0; --i) {
    if (sigismember(&sigpending, i)  == 1  &&
        sigismember(&sigblockmask, i) == 1 &&
        sigismember(&sigpending_global, i) == 0 &&
        i != SigInfo::ckptSignal()) {
      JWARNING(i != SIGCHLD)
        .Text("\n*** WARNING:  SIGCHLD was delivered prior to ckpt.\n"
              "*** Will raise it on restart.  If not desired, change\n"
              "*** this line raising SIGCHLD.");
      raise(i);
    }
  }
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
extern "C" pid_t dmtcp_get_real_pid() __attribute((weak));
extern "C" pid_t dmtcp_get_real_tid() __attribute((weak));
extern "C" int dmtcp_real_tgkill(pid_t pid, pid_t tid, int sig)
                                                       __attribute((weak));

pid_t ThreadList::_real_pid() {
  pid_t pid = getpid();
  if (dmtcp_get_real_pid != NULL)
    pid = dmtcp_get_real_pid();
  return pid;
}

pid_t ThreadList::_real_tid() {
  pid_t tid = _real_syscall(SYS_gettid);
  if (dmtcp_get_real_tid != NULL)
    tid = dmtcp_get_real_tid();
  return tid;
}

int ThreadList::_real_tgkill(pid_t tgid, pid_t tid, int sig) {
  if (dmtcp_real_tgkill != NULL)
    return dmtcp_real_tgkill(tgid, tid, sig);
  return _real_syscall(SYS_tgkill, tgid, tid, sig);
}

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
  motherpid = _real_pid();
  TLSInfo::verifyPidTid(motherpid, motherpid);

  SigInfo::setupCkptSigHandler(&stopthisthread);

  /* Set up caller as one of our threads so we can work on it */
  motherofall = ThreadList::getNewThread();
  motherofall->init();
  motherofall->updateTid();

  sem_init(&sem_start, 0, 0);

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

/*************************************************************************
 *
 *  Send a signal to ckpt-thread to wake it up from select call and exit.
 *
 *************************************************************************/
void ThreadList::killCkpthread()
{
  JTRACE("Kill checkpinthread") (ckptThread->tid);
  _real_tgkill(motherpid, ckptThread->tid, SigInfo::ckptSignal());
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

  ckptThread->saveSigState();
  TLSInfo::saveTLSState(ckptThread);
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
    restoreInProgress = 0;
    /* Wait a while between writing checkpoint files */
    JTRACE("before callbackSleepBetweenCheckpoint(0)");
    callbackSleepBetweenCheckpoint(0);
    ThreadList::suspendThreads();
    SigInfo::saveSigHandlers();
    /* Do this once, same for all threads.  But restore for each thread. */
    if (TLSInfo::have_thread_sysinfo_offset())
      saved_sysinfo = TLSInfo::get_thread_sysinfo();

    /* All other threads halted in 'stopthisthread' routine (they are all
     * in state ST_SUSPENDED).  It's safe to write checkpoint file now.
     */
    JTRACE("before callbackSleepBetweenCheckpoint(0)");
    callbackPreCheckpoint();

    // Remove stale threads from activeThreads list.
    ThreadList::emptyFreeList();

    ProcessInfo::instance().setRestoreFinishFnPtr(&ThreadList::postRestart);
    CkptSerializer::writeCkptImage();

    JTRACE("before callbackPostCheckpoint(0, NULL)");
    callbackPostCheckpoint(0, NULL);

    /* Resume all threads. */
    JTRACE("resuming everything");
    ThreadList::resumeThreads();
    JTRACE("everything resumed");
  }
  return NULL;
}

void ThreadList::suspendThreads()
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
          if (thread->updateState(ST_SIGNALED, ST_RUNNING)) {
            if (_real_tgkill(motherpid, thread->tid, SigInfo::ckptSignal()) < 0) {
              JASSERT(errno == ESRCH) (JASSERT_ERRNO) (thread->tid)
                .Text("error signalling thread");
              threadIsDead(thread);
            } else {
              needrescan = 1;
            }
          }
          break;

        case ST_ZOMBIE:
          ret = _real_tgkill(motherpid, thread->tid, 0);
          JASSERT(ret == 0 || errno == ESRCH);
          if (ret == -1 && errno == ESRCH) {
            threadIsDead(thread);
          }
          break;

        case ST_SIGNALED:
          if (_real_tgkill(motherpid, thread->tid, 0) == -1 && errno == ESRCH) {
            threadIsDead(thread);
          } else {
            needrescan = 1;
          }
          break;

        case ST_SUSPINPROG:
          needrescan = 1;
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

  JASSERT(activeThreads != NULL);
  JTRACE("everything suspended");
}

void ThreadList::resumeThreads()
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
static int growstackrlimit(size_t kbStack) {
  size_t size = kbStack * 1024;  /* kbStack was in units of kilobytes */
  struct rlimit rlim;
  getrlimit(RLIMIT_STACK, &rlim);
  if (rlim.rlim_cur == RLIM_INFINITY)
    return 1;
  if (rlim.rlim_max == RLIM_INFINITY || rlim.rlim_max - rlim.rlim_cur > size) {
    rlim.rlim_cur += size;
    // Increase it by further 1MB to ensure any other local variables can fit
    // easily.
    rlim.rlim_cur += (1024 * 1024);
    setrlimit(RLIMIT_STACK, &rlim);
    return 1;
  } else {
    JWARNING(false) (rlim.rlim_max) (rlim.rlim_cur)
      .Text("Couldn't extend stack limit for growstack.");
  }
  return 0;
}

static volatile unsigned int growstackValue = 0;
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ > 3)
static void __attribute__ ((optimize(0))) growstack (int kbStack)
#else
static void growstack (int kbStack) /* opimize attribute not implemented */
#endif
{
  /* With the split stack option (-fsplit-stack in gcc-4.6.0 and later),
   * we can only hope for 64 KB of free stack.  (Also, some users will use:
   * ld --stack XXX.)  We'll use half of the 64 KB here.
   */
  const int kbIncrement = 1024; /* half the size of kbStack */
  char array[kbIncrement * 1024] __attribute__ ((unused));
  /* Again, try to prevent compiler optimization */
  volatile int dummy_value __attribute__ ((unused)) = 1;
  if (kbStack > 0)
    growstack(kbStack - kbIncrement);
  else
    growstackValue++;
}

static void grow_stack_for_motherofall()
{
  static int is_first_checkpoint = 1;
  /* Grow stack only on first ckpt.  Kernel agrees this is main stack and
   * will mmap it.  On second ckpt and later, we would segfault if we tried
   * to grow the former stack beyond the portion that is already mmap'ed.
   */
  JASSERT(curThread == motherofall);
  static char *orig_stack_ptr;
  /* FIXME:
   * Some apps will use "ld --stack XXX" with a small stack.  This
   * trend will become more common with the introduction of split stacks.
   * BUT NOTE PROBLEM PREV. COMMENT ON KERNEL NOT GROWING STACK ON RESTART
   * Grow the stack by kbStack*1024 so that large stack is allocated oni
   * restart.  The kernel won't do it automatically for us any more,
   * since it thinks the stack is in a different place after restart.
   */
  int kbStack = 2048; /* double the size of kbIncrement in growstack */
  if (is_first_checkpoint) {
    orig_stack_ptr = (char *)&kbStack;
    is_first_checkpoint = 0;
    JTRACE("temp. grow main stack by %d kilobytes") (kbStack);
    growstackrlimit(kbStack);
    growstack(kbStack);
  } else if (orig_stack_ptr - (char *)&kbStack > 3 * kbStack*1024 / 4) {
    JWARNING(false) (kbStack)
      .Text("Stack within %d bytes of end; Consider increasing 'kbStack'");
  }
}

void ThreadList::stopthisthread (int signum)
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
  if (curThread->updateState(ST_SIGNALED, ST_RUNNING)) {
    int retval;
    callbackHoldsAnyLocks(&retval);
    if (retval) return;
  }

  JTRACE("Thread return address") (curThread->tid) (__builtin_return_address(0));

  // make sure we don't get called twice for same thread
  if (curThread->updateState(ST_SUSPINPROG, ST_SIGNALED)) {

    curThread->saveSigState(); // save sig state (and block sig delivery)
    TLSInfo::saveTLSState(curThread); // save thread local storage state

    if (curThread == motherofall) {
      grow_stack_for_motherofall();
    }

    /* Set up our restart point, ie, we get jumped to here after a restore */
#ifdef SETJMP
  JASSERT(sigsetjmp(curThread->jmpbuf, 1) >= 0) (JASSERT_ERRNO);
#else
  JASSERT(getcontext(&curThread->savctx) == 0) (JASSERT_ERRNO);
#endif
  save_sp(&curThread->saved_sp);
  JTRACE("after sigsetjmp/getcontext")
    (curThread->tid) (curThread->virtual_tid) (curThread->saved_sp);

    if (!restoreInProgress) {
      /* We are a user thread and all context is saved.
       * Wait for ckpt thread to write ckpt, and resume.
       */

      /* This sets a static variable in dmtcp.  It must be passed
       * from this user thread to ckpt thread before writing ckpt image
       */
      callbackPreSuspendUserThread();

      /* Tell the checkpoint thread that we're all saved away */
      JASSERT(curThread->updateState(ST_SUSPENDED, ST_SUSPINPROG));

      /* Then wait for the ckpt thread to write the ckpt file then wake us up */
      JTRACE("User thread suspended") (curThread->tid);
      sem_wait(&semWaitForCkptThreadSignal);
      JTRACE("User thread resuming") (curThread->tid);
    } else {
      /* Else restoreinprog >= 1;  This stuff executes to do a restart */
      waitForAllRestored(curThread);
      JTRACE("thread restored") (curThread->tid);
    }

    JASSERT(curThread->updateState(ST_RUNNING, ST_SUSPENDED))
      .Text("checkpoint was written when thread in SUSPENDED state");

    JTRACE("thread returning to user code.")
      (curThread->tid) (__builtin_return_address (0));

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
    sem_destroy(&semNotifyCkptThread);
    sem_destroy(&semWaitForCkptThreadSignal);
  } else {
    sem_post(&semNotifyCkptThread);
    sem_wait(&semWaitForCkptThreadSignal);
    thread->restoreSigState();
  }
}

/*****************************************************************************
 *
 *  The original program's memory and files have been restored
 *
 *****************************************************************************/
void ThreadList::postRestart()
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
  motherpid = ThreadList::_real_pid();
  motherofall->tid = motherpid;
  TLSInfo::restoreTLSState(motherofall);

  restoreInProgress = 1;
  sem_init(&semNotifyCkptThread, 0, 0);
  sem_init(&semWaitForCkptThreadSignal, 0, 0);

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

    JASSERT (tid > 0) (JASSERT_ERRNO) .Text("Error recreating thread");
    JTRACE("Thread recreated") (thread->tid) (tid);
  }
  restarthread (motherofall);
}

static int restarthread (void *threadv)
{
  Thread *thread = (Thread*) threadv;
  thread->tid = ThreadList::_real_tid();
  TLSInfo::restoreTLSState(thread);

  if (TLSInfo::have_thread_sysinfo_offset())
    TLSInfo::set_thread_sysinfo(saved_sysinfo);

  /* Jump to the stopthisthread routine just after sigsetjmp/getcontext call.
   * Note that if this is the restored checkpointhread, it jumps to the
   * checkpointhread routine
   */
  JTRACE("calling siglongjmp/setcontext") (thread->tid) (thread->virtual_tid);
#ifdef SETJMP
  siglongjmp(thread->jmpbuf, 1); /* Shouldn't return */
#else
  setcontext(&thread->savctx); /* Shouldn't return */
#endif
  JASSERT(false) .Text("NOT REACHED");
  return (0); /* NOTREACHED : stop compiler warning */
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
      if (-1 == _real_tgkill(motherpid, thread->tid, 0)) {
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
  JTRACE("Putting thread on freelist") (thread->tid);

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
    JASSERT(thread != NULL) .Text("Error allocating thread struct");
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

