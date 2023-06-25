#include <linux/version.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include "config.h"
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11) || \
    defined(HAS_PR_SET_PTRACER)
#include <sys/prctl.h>
#endif  // if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11) ||
// defined(HAS_PR_SET_PTRACER)
#include "jalloc.h"
#include "jassert.h"
#include "ckptserializer.h"
#include "dmtcpalloc.h"
#include "dmtcpworker.h"
#include "mtcp/mtcp_header.h"
#include "pluginmanager.h"
#include "shareddata.h"
#include "siginfo.h"
#include "syscallwrappers.h"
#include "threadlist.h"
#include "tls.h"
#include "threadsync.h"
#include "uniquepid.h"
#include "util.h"
#include "shareddata.h"

// For i386 and x86_64, SETJMP currently has bugs.  Don't turn this
// on for them until they are debugged.
// Default is to use  setcontext/getcontext.
#if defined(__arm__) || defined(__aarch64__)
#define SETJMP /* setcontext/getcontext not defined for ARM glibc */
#endif         // if defined(__arm__) || defined(__aarch64__)

#ifdef SETJMP
#include <setjmp.h>
#else  // ifdef SETJMP
#include <ucontext.h>
#endif  // ifdef SETJMP

using namespace dmtcp;

// Globals
ATOMIC_SHARED_GLOBAL bool restoreInProgress = false;
Thread *motherofall = NULL;
pid_t motherpid = 0;
sigset_t sigpending_global;
Thread *activeThreads = NULL;
void *saved_sysinfo;

static const char *DMTCP_PRGNAME_PREFIX = "DMTCP:";

static DmtcpMutex threadlistLock = DMTCP_MUTEX_INITIALIZER;
static DmtcpMutex threadStateLock = DMTCP_MUTEX_INITIALIZER;

static DmtcpRWLock threadResumeLock;

__thread Thread *curThread ATTR_TLS_INITIAL_EXEC = NULL;
Thread *ckptThread = NULL;

static int numUserThreads = 0;
static bool originalstartup;
// Let dmtcp.h:DMTCP_RESTART_PAUSE_WHILE(cond) use (dmtcp::restartPauseLevel
volatile int dmtcp::restartPauseLevel = 0;

extern bool sem_launch_first_time;
extern sem_t sem_launch; // allocated in coordinatorapi.cpp
static sem_t semNotifyCkptThread;
static sem_t semWaitForCkptThreadSignal;

static void *checkpointhread(void *dummy);
static void stopthisthread(int sig);
static int restarthread(void *threadv);
static void Thread_SaveSigState(Thread *th);
static void Thread_RestoreSigState(Thread *th);

/*****************************************************************************
 *
 * Lock and unlock the 'activeThreads' list
 *
 *****************************************************************************/
static void
lock_threads(void)
{
  JASSERT(DmtcpMutexLock(&threadlistLock) == 0) (JASSERT_ERRNO);
}

static void
unlk_threads(void)
{
  JASSERT(DmtcpMutexUnlock(&threadlistLock) == 0) (JASSERT_ERRNO);
}

bool dmtcp_is_ckpt_thread()
{
  return ckptThread == curThread;
}

Thread *
dmtcp_get_motherofall()
{
  if (motherofall != nullptr) {
    return motherofall;
  }

  ThreadList::init();

  ASSERT_NOT_NULL(motherofall);

  return motherofall;
}

Thread *
dmtcp_get_current_thread()
{
  if (curThread != nullptr) {
    return curThread;
  }

  ASSERT_NULL(motherofall);
  ThreadList::init();

  ASSERT_NOT_NULL(curThread);
  return curThread;
}

/*****************************************************************************
 *
 * We will use the region beyond the end of stack for our temporary stack.
 * glibc sigsetjmp will mangle pointers;  We need the unmangled pointer.
 * So, we can't rely on parsing the jmpbuf for the saved sp.
 *
 *****************************************************************************/
static void
save_sp(void **sp)
{
#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(mov %%esp, %0)
                  : "=g" (*sp)
                    : : "memory");
#elif defined(__arm__) || defined(__aarch64__)
  asm volatile ("mov %0,sp"
                : "=r" (*sp)
                : : "memory");
#else // if defined(__i386__) || defined(__x86_64__)
# error "assembly instruction not translated"
#endif // if defined(__i386__) || defined(__x86_64__)
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
void
ThreadList::resetOnFork()
{
  while (activeThreads != NULL) {
    ThreadList::threadIsDead(activeThreads); // takes care of updating
                                             // "activeThreads" ptr.
  }

  // CONTEXT:  initThread() resets curThread only if it's non-NULL.
  // ... -> initializeMtcpEngine() -> ThreadList::init() -> initThread()
  // See addToActiveList() for more information.
  curThread = motherofall = nullptr;

  init();
  createCkptThread();
}

/*****************************************************************************
 *
 *  This routine must be called at startup time to initiate checkpointing
 *
 *****************************************************************************/
void
ThreadList::init()
{
  /* Save this process's pid.  Then verify that the TLS has it where it should
   * be. When we do a restore, we will have to modify each thread's TLS with the
   * new motherpid. We also assume that GS uses the first GDT entry for its
   * descriptor.
   */

  /* libc/getpid can lie if we had used kernel fork() instead of libc fork(). */
  motherpid = getpid();

  if (motherofall == nullptr) {
    /* Set up caller as one of our threads so we can work on it */
    motherofall = ThreadList::getNewThread(NULL, NULL);
    initThread(motherofall);
  }
}

/*****************************************************************************
 *
 *****************************************************************************/
void
ThreadList::createCkptThread()
{
  sem_init(&sem_launch, 0, 0);
  sem_init(&semNotifyCkptThread, 0, 0);
  sem_init(&semWaitForCkptThreadSignal, 0, 0);

  SigInfo::setupCkptSigHandler(&stopthisthread);

  originalstartup = true;
  pthread_t checkpointhreadid;

  /* Spawn off a thread that will perform the checkpoints from time to time */
  JASSERT(pthread_create(&checkpointhreadid, NULL, checkpointhread, NULL) == 0);

  /* Stop until checkpoint thread has finished initializing.
   * Some programs (like gcl) implement their own glibc functions in
   * a non-thread-safe manner.  In case we're using non-thread-safe glibc,
   * don't run the checkpoint thread and user thread at the same time.
   */
  errno = 0;
  while (-1 == sem_wait(&sem_launch) && errno == EINTR) {
    errno = 0;
  }
  sem_destroy(&sem_launch);
}

/*****************************************************************************
 *
 *****************************************************************************/
Thread *
ThreadList::getNewThread(void *(*fn)(void *), void *arg)
{
  Thread *th = (Thread*) JALLOC_MALLOC(sizeof(Thread));
  /* Save exactly what the caller is supplying */
  th->fn = fn;
  th->arg = arg;
  th->flags = 0;
  th->ptid = NULL;
  th->ctid = NULL;
  th->next = NULL;
  th->state = ST_RUNNING;
  th->exiting = 0;
  th->wrapperLockCount = 0;
  th->procname[0] = '\0';
  return th;
}

/*****************************************************************************
 *
 * Thread exited/exiting.
 *
 *****************************************************************************/
void
ThreadList::threadExit()
{
  curThread->exiting = 1;
}

/*****************************************************************************
 *
 *****************************************************************************/
void
ThreadList::initThread(Thread *th)
{
  if (curThread == NULL) {
    curThread = th;
  }
  th->tid = _real_syscall(SYS_gettid);

  th->flags = (CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SYSVSEM
              | CLONE_SIGHAND | CLONE_THREAD
              | CLONE_SETTLS | CLONE_PARENT_SETTID
              | CLONE_CHILD_CLEARTID
              | 0);

  th->ptid = (pid_t*)((char*) pthread_self() + TLSInfo_GetTidOffset());
  th->ctid = th->ptid;

  JTRACE("starting thread") (th->tid);

  // Check and remove any thread descriptor which has the same tid as ours.
  // Also, remove any dead threads from the list.
  ThreadList::addToActiveList(th);
}

/*************************************************************************
 *
 *  Prepare MTCP Header
 *
 *************************************************************************/
static void
prepareMtcpHeader(MtcpHeader *mtcpHdr)
{
  memset(mtcpHdr, 0, sizeof(*mtcpHdr));
  strncpy(mtcpHdr->signature, MTCP_SIGNATURE, strlen(MTCP_SIGNATURE) + 1);
  mtcpHdr->saved_brk = sbrk(0);
  mtcpHdr->end_of_stack = (void *)ProcessInfo::instance().endOfStack();
  // TODO: Now that we have a separate mtcp dir, the code dealing with
  // restoreBuf should go in there.
  mtcpHdr->restore_addr = (void *)ProcessInfo::instance().restoreBufAddr();
  mtcpHdr->restore_size = ProcessInfo::instance().restoreBufLen();

  mtcpHdr->vdsoStart = (void *)ProcessInfo::instance().vdsoStart();
  mtcpHdr->vdsoEnd = (void *)ProcessInfo::instance().vdsoEnd();
  mtcpHdr->vvarStart = (void *)ProcessInfo::instance().vvarStart();
  mtcpHdr->vvarEnd = (void *)ProcessInfo::instance().vvarEnd();

  mtcpHdr->post_restart = &ThreadList::postRestart;
}

/*************************************************************************
 *
 *  Write checkpoint image
 *
 *************************************************************************/
void
ThreadList::writeCkpt()
{
  // Remove stale threads from activeThreads list.
  SigInfo::saveSigHandlers();

  /* Do this once, same for all threads.  But restore for each thread. */
  if (TLSInfo_HaveThreadSysinfoOffset()) {
    saved_sysinfo = TLSInfo_GetThreadSysinfo();
  }

  MtcpHeader mtcpHdr;
  prepareMtcpHeader(&mtcpHdr);

  string ckptFilename = ProcessInfo::instance().getTempCkptFilename();

  CkptSerializer::writeCkptImage(&mtcpHdr, sizeof(mtcpHdr), ckptFilename);
}

/*************************************************************************
 *
 *  This executes as a thread.  It sleeps for the checkpoint interval
 *    seconds, then wakes to write the checkpoint file.
 *
 *************************************************************************/
static void *
checkpointhread(void *dummy)
{
  /* This is the start function of the checkpoint thread.
   * We also call sigsetjmp/getcontext to get a snapshot of this call frame,
   * since we will never exit this call frame.  We always return
   * to this call frame at time of startup, on restart.  Hence, restart
   * will forget any modifications to our local variables since restart.
   */

  ckptThread = curThread;
  ckptThread->state = ST_CKPNTHREAD;

  // Important:  we set this in the ckpt thread to avoid a race,
  // since: (i) the ckpt thread must read this; and (ii) if we had
  // set it earlier, it could be invoked and modified earlier
  // inside a generic command like CoordinatorAPI::recvMsgFromCoordi).
  sem_launch_first_time = true;

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

    JASSERT(pthread_sigmask(SIG_SETMASK, &set, NULL) == 0);
  }

  /* Set up our restart point.  I.e., we get jumped to here after a restore. */
#ifdef SETJMP
  JASSERT(sigsetjmp(ckptThread->jmpbuf, 1) >= 0) (JASSERT_ERRNO);
#else // ifdef SETJMP
  JASSERT(getcontext(&ckptThread->savctx) == 0) (JASSERT_ERRNO);
#endif // ifdef SETJMP
  save_sp(&ckptThread->saved_sp);
  JTRACE("after sigsetjmp/getcontext") (curThread->tid) (curThread->saved_sp);

  if (originalstartup) {
    originalstartup = false;
  } else {
    /* We are being restored.  Wait for all other threads to finish being
     * restored before resuming checkpointing.
     */
    JTRACE("waiting for other threads after restore");
    ThreadList::waitForAllRestored(ckptThread);
    JTRACE("resuming after restore");
  }

  /* This is a sleep-checkpoint-resume loop by the checkpoint thread.
   * On restart, we arrive back at getcontext, above, and then re-enter the
   * loop.
   */
  while (1) {
    /* Wait a while between writing checkpoint files */
    JTRACE("before DmtcpWorker::waitForCheckpointRequest()");
    DmtcpWorker::waitForCheckpointRequest();

    restoreInProgress = false;

    ThreadList::suspendThreads();

    JTRACE("Prepare plugin, etc. for checkpoint");
    DmtcpWorker::preCheckpoint();

    // Gather ckpt-thread's TLS state as it could have changed as a result of
    // dlopening libraries with TLS objects.
    TLSInfo_SaveTLSState(ckptThread);

    // Save signal mask and capture any pending signals.
    Thread_SaveSigState(ckptThread);

    /* All other threads halted in 'stopthisthread' routine (they are all
     * in state ST_SUSPENDED).  It's safe to write checkpoint file now.
     */
    ThreadList::writeCkpt();

    DmtcpWorker::postCheckpoint();

    ThreadList::resumeThreads();
  }

  return NULL;
}

void
ThreadList::suspendThreads()
{
  int needrescan;
  Thread *thread;
  Thread *next;

  DmtcpRWLockInit(&threadResumeLock);
  JASSERT(DmtcpRWLockWrLock(&threadResumeLock) == 0);

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

      if (thread == curThread) {
        continue;
      }

      /* Do various things based on thread's state */
      switch (thread->state) {
      case ST_RUNNING:

        /* Thread is running. Send it a signal so it will call stopthisthread.
         * We will need to rescan (hopefully it will be suspended by then)
         */
        if (Thread_UpdateState(thread, ST_SIGNALED, ST_RUNNING)) {
          if (THREAD_TGKILL(motherpid, thread->tid,
                            SigInfo::ckptSignal()) < 0) {
            JASSERT(errno == ESRCH) (JASSERT_ERRNO) (thread->tid)
            .Text("error signalling thread");
            ThreadList::threadIsDead(thread);
          } else {
            needrescan = 1;
          }
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
    if (needrescan) {
      usleep(10);
    }
  } while (needrescan);
  unlk_threads();

  for (int i = 0; i < numUserThreads; i++) {
    sem_wait(&semNotifyCkptThread);
  }

  JASSERT(activeThreads != NULL);
  JTRACE("everything suspended") (numUserThreads);
}

void ThreadList::vforkSuspendThreads()
{
  ThreadList::suspendThreads();
}

void ThreadList::vforkResumeThreads()
{
  ThreadList::resumeThreads();
}

/* Resume all threads. */
void
ThreadList::resumeThreads()
{
  JTRACE("resuming user threads");
  JASSERT(DmtcpRWLockUnlock(&threadResumeLock) == 0) (JASSERT_ERRNO);
}

/*************************************************************************
 *
 *  Signal handler for user threads.
 *
 *************************************************************************/
void
stopthisthread(int signum)
{
  /* Possible state change scenarios:
   * 1. STOPSIGNAL received from ckpt-thread. In this case, the ckpt-thread
   * already changed the state to ST_SIGNALED. No need to check for locks.
   * Proceed normally.
   *
   * 2. STOPSIGNAL received from Superior thread. In this case we change the
   * state to ST_SIGNALED, if currently in ST_RUNNING.
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

  // make sure we don't get called twice for same thread
  if (Thread_UpdateState(curThread, ST_SUSPINPROG, ST_SIGNALED)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)
    JWARNING(prctl(PR_GET_NAME, curThread->procname) != -1) (JASSERT_ERRNO)
    .Text("prctl(PR_GET_NAME, ...) failed");
#endif // if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)

    Thread_SaveSigState(curThread); // save sig state (and block sig delivery)
    TLSInfo_SaveTLSState(curThread); // save thread local storage state

    /* Set up our restart point, ie, we get jumped to here after a restore */
#ifdef SETJMP
    JASSERT(sigsetjmp(curThread->jmpbuf, 1) >= 0);
#else // ifdef SETJMP
    JASSERT(getcontext(&curThread->savctx) == 0);
#endif // ifdef SETJMP
    save_sp(&curThread->saved_sp);

    JTRACE("Thread after sigsetjmp/getcontext")
      (curThread->tid) (curThread->saved_sp) (__builtin_return_address(0));

    if (!restoreInProgress) {
      /* We are a user thread and all context is saved.
       * Wait for ckpt thread to write ckpt, and resume.
       */

      /* Tell the checkpoint thread that we're all saved away */
      JASSERT(Thread_UpdateState(curThread, ST_SUSPENDED, ST_SUSPINPROG));
      sem_post(&semNotifyCkptThread);

      /* Then wait for the ckpt thread to write the ckpt file then wake us up */
      JTRACE("User thread suspended") (curThread->tid);

      // We can't use sem_wait here because sem_wait registers a cleanup
      // handler before going into blocking wait. The handler is popped before
      // returning from it. However, on restart, the thread will do a longjump
      // and thus will never come out of the sem_wait, thus the handler is
      // never popped. This causes a problem later on during pthread_exit. The
      // pthread_exit routine executes all registered cleanup handlers.
      // However, the sem_wait cleanup handler is now invalid and thus we get a
      // segfault.
      // The change in sem_wait behavior was first introduce in glibc 2.21.
      JASSERT(DmtcpRWLockRdLock(&threadResumeLock) == 0);

      JASSERT(Thread_UpdateState(curThread, ST_RUNNING, ST_SUSPENDED));

      JASSERT(DmtcpRWLockUnlock(&threadResumeLock) == 0);
    } else {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)
      if (!Util::strStartsWith(curThread->procname, DMTCP_PRGNAME_PREFIX)) {
        // Add the "DMTCP:" prefix.
        string newName = string(DMTCP_PRGNAME_PREFIX) + curThread->procname;
        strncpy(curThread->procname,
                newName.c_str(),
                sizeof(curThread->procname));

        // Add a NULL at the end to make sure the string terminates in all cases
        curThread->procname[sizeof(curThread->procname) - 1] = '\0';
      }
      JASSERT(prctl(PR_SET_NAME, curThread->procname) != -1 || errno == EINVAL)
        (curThread->procname) (JASSERT_ERRNO)
      .Text("prctl(PR_SET_NAME, ...) failed");
#endif // if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)

      JASSERT(Thread_UpdateState(curThread, ST_RUNNING, ST_SUSPENDED));

      /* Else restoreinprog >= 1;  This stuff executes to do a restart */
      ThreadList::waitForAllRestored(curThread);
    }

    JTRACE("User thread returning to user code")
      (curThread->tid) (__builtin_return_address(0));
  }
}

/*****************************************************************************
 *
 *  Wait for all threads to finish restoring their context, then release them
 *  all to continue on their way.
 *
 *****************************************************************************/
void
ThreadList::waitForAllRestored(Thread *thread)
{
  if (thread == ckptThread) {
    int i;
    for (i = 0; i < numUserThreads; i++) {
      sem_wait(&semNotifyCkptThread);
    }

    // Now that all threads have been created, restore the signal handler. We
    // need to do it before calling DmtcpWorker::postRestart() because that
    // routine will invoke restart hooks for all plugins. Some of the plugins
    // might perform tasks that could potentially generate a signal. For
    // example, the timer plugin may restore a timer which will fire right away,
    // and not having an appropriate signal handler could kill the process.
    SigInfo::restoreSigHandlers();

    JTRACE("before DmtcpWorker::postRestart()");

    DmtcpWorker::postRestart(thread->ckptReadTime);

    JTRACE("after DmtcpWorker::postRestart()");

    /* raise the signals which were pending for the entire process at the time
     * of checkpoint. It is assumed that if a signal is pending for all threads
     * including the ckpt-thread, then it was sent to the process as opposed to
     * sent to individual threads.
     */
    for (i = SIGRTMAX; i > 0; --i) {
      if (sigismember(&sigpending_global, i) == 1) {
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
  }

  PluginManager::eventHook(DMTCP_EVENT_THREAD_RESUME);

  Thread_RestoreSigState(thread);

  if (thread == motherofall) {
    DMTCP_RESTART_PAUSE_WHILE(restartPauseLevel == 7);
  }
}

/*****************************************************************************
 *
 *****************************************************************************/
void
ThreadList::postRestart(double readTime, int restartPause)
{
  // This function and related ones are defined in src/mtcp/restore_libc.c
  TLSInfo_RestoreTLSState(motherofall);
  TLSInfo_RestoreTLSTidPid(motherofall);

  restartPauseLevel = restartPause;
  DMTCP_RESTART_PAUSE_WHILE(restartPauseLevel == 2);

  postRestartWork(readTime);
}

/*****************************************************************************
 *
 *****************************************************************************/
void
ThreadList::postRestartWork(double readTime)
{
  Thread *thread;
  sigset_t tmp;

  if (TLSInfo_HaveThreadSysinfoOffset()) {
    TLSInfo_SetThreadSysinfo(saved_sysinfo);
  }

  dmtcp_update_virtual_to_real_tid(motherofall->tid);

  DMTCP_RESTART_PAUSE_WHILE(restartPauseLevel == 3);

  SharedData::postRestart();

  restoreInProgress = true;

  Util::allowGdbDebug(DEBUG_POST_RESTART);

  sigfillset(&tmp);
  for (thread = activeThreads; thread != NULL; thread = thread->next) {
    sigandset(&sigpending_global, &tmp, &(thread->sigpending));
    tmp = sigpending_global;

    if (thread == motherofall) {
      continue;
    }

    thread->ckptReadTime = readTime;

    /* Create the thread so it can finish restoring itself. */
    pid_t tid = _real_clone(restarthread,

                            // -128 for red zone
                            (void *)((char *)thread->saved_sp - 128),

                            /* Don't do CLONE_SETTLS (it'll puke).  We do it
                             * later via restoreTLSState. */
                            thread->flags & ~CLONE_SETTLS,
                            thread, thread->ptid, NULL, thread->ctid);

    JASSERT(tid > 0);  // (JASSERT_ERRNO) .Text("Error recreating thread");
    JTRACE("Thread recreated") (thread->tid) (tid);
  }

  restarthread(motherofall);
}

/*****************************************************************************
 *
 *****************************************************************************/
static int
restarthread(void *threadv)
{
  Thread *thread = (Thread *)threadv;

  TLSInfo_RestoreTLSState(thread);
  TLSInfo_RestoreTLSTidPid(thread);

  if (TLSInfo_HaveThreadSysinfoOffset()) {
    TLSInfo_SetThreadSysinfo(saved_sysinfo);
  }

  dmtcp_update_virtual_to_real_tid(thread->tid);

  if (thread == motherofall) { // if this is a user thread
    DMTCP_RESTART_PAUSE_WHILE(restartPauseLevel == 4);
  }

  /* Jump to the stopthisthread routine just after sigsetjmp/getcontext call.
   * Note that if this is the restored checkpointhread, it jumps to the
   * checkpointhread routine
   */
  JTRACE("calling siglongjmp/setcontext") (thread->tid);
#ifdef SETJMP
  siglongjmp(thread->jmpbuf, 1); /* Shouldn't return */
#else // ifdef SETJMP
  setcontext(&thread->savctx); /* Shouldn't return */
#endif // ifdef SETJMP
  JASSERT(false);
  return 0;   /* NOTREACHED : stop compiler warning */
}

/*****************************************************************************
 *
 *****************************************************************************/
int
Thread_UpdateState(Thread *th, ThreadState newval, ThreadState oldval)
{
  int res = 0;

  JASSERT(DmtcpMutexLock(&threadStateLock) == 0);
  if (oldval == th->state) {
    th->state = newval;
    res = 1;
  }
  JASSERT(DmtcpMutexUnlock(&threadStateLock) == 0);
  return res;
}

/*****************************************************************************
 *
 *  Save signal mask and list of pending signals delivery
 *
 *****************************************************************************/
void
Thread_SaveSigState(Thread *th)
{
  // Save signal block mask
  JASSERT(pthread_sigmask(SIG_SETMASK, NULL, &th->sigblockmask) == 0);

  // Save pending signals
  sigpending(&th->sigpending);
}

/*****************************************************************************
 *
 *  Restore signal mask and all pending signals
 *
 *****************************************************************************/
void
Thread_RestoreSigState(Thread *th)
{
  int i;

  JTRACE("restoring signal mask for thread") (th->tid);
  JASSERT(pthread_sigmask(SIG_SETMASK, &th->sigblockmask, NULL) == 0);

  // Raise the signals which were pending for only this thread at the time of
  // checkpoint.
  for (i = SIGRTMAX; i > 0; --i) {
    if (sigismember(&th->sigpending, i) == 1 &&
        sigismember(&th->sigblockmask, i) == 1 &&
        sigismember(&sigpending_global, i) == 0 &&
        i != dmtcp_get_ckpt_signal()) {
      if (i != SIGCHLD) {
        JNOTE("\n*** WARNING:  SIGCHLD was delivered prior to ckpt.\n"
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
void
ThreadList::addToActiveList(Thread *th)
{
  int tid;
  Thread *thread;
  Thread *next_thread;

  lock_threads();

  // CONTEXT:  After fork(), we called:
  // ... -> initializeMtcpEngine() -> ThreadList::init() -> initThread()
  // -> addToActiveList()
  // NOTE:  After a call to fork(), only the calling thread continues to live.
  // Before initializeMtcpEngine() called init(), it called:
  // ... -> initializeMtcpEngine() -> ThreadSync::initMotherOfAll() ->
  // -> ThreadSync::initThread()
  // Logically, we would have set 'curThread = NULL;; inside
  // ThreadSync::initThread(), but it's inconvenient since curThread
  // is static (file-private).
  // So, initThread() created the new thread descriptor.  We make sure
  // to set curThread to th, the new descriptor, now, in case it wasn't
  // done yet.
  // We had also set curThread to NULL in ThreadList::init().  This also
  // makes logical sense, but only because a call to fork() allows
  // only the calling thread (caller of ThreadList::init()) to live on.
  // So, that solution seems less general.  So, we'll handle it here, too:
  curThread = th;

  tid = curThread->tid;
  JASSERT(tid != 0);

  // First remove duplicate descriptors.
  for (thread = activeThreads; thread != NULL; thread = next_thread) {
    next_thread = thread->next;
    if (thread != curThread && thread->tid == tid) {
      JTRACE("Removing duplicate thread descriptor") (thread->tid);

      // There will be at most one duplicate descriptor.
      threadIsDead(thread);
      continue;
    }

    // FIXME:  This causes segfault on second restart.  Why?
    // JASSERT(thread != curThread)(thread)
    // .Text("adding curThread, but it's already on activeThreads");

    /* NOTE:  ST_ZOMBIE is used only for the sake of efficiency.  We
     *   test threads in state ST_ZOMBIE using tgkill to remove them
     *   early (before reaching a checkpoint) so that the
     *   threadrdescriptor list does not grow too long.
     */
    if (thread->exiting) {
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
}

/*****************************************************************************
 *
 *  Thread has exited - remove it from activeThreads list and release memory.
 *
 *****************************************************************************/
void
ThreadList::threadIsDead(Thread *thread)
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

  JALLOC_FREE(thread);
}
