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
#include "threadsync.h"
#include "uniquepid.h"
#include "util.h"

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
volatile bool restoreInProgress = false;
Thread *motherofall = NULL;
void **motherofall_saved_sp = NULL;
ThreadTLSInfo *motherofall_tlsInfo = NULL;
pid_t motherpid = 0;
sigset_t sigpending_global;
Thread *activeThreads = NULL;
void *saved_sysinfo;
MYINFO_GS_T myinfo_gs __attribute__((visibility("hidden")));

static const char *DMTCP_PRGNAME_PREFIX = "DMTCP:";

static Thread *threads_freelist = NULL;
static DmtcpMutex threadlistLock = DMTCP_MUTEX_INITIALIZER;
static DmtcpMutex threadStateLock = DMTCP_MUTEX_INITIALIZER;

static DmtcpRWLock threadResumeLock;

static __thread Thread *curThread = NULL;
static Thread *ckptThread = NULL;
static int numUserThreads = 0;
static bool originalstartup;

extern bool sem_launch_first_time;
extern sem_t sem_launch; // allocated in coordinatorapi.cpp
static sem_t semNotifyCkptThread;
static sem_t semWaitForCkptThreadSignal;

static void *checkpointhread(void *dummy);
static void stopthisthread(int sig);
static int restarthread(void *threadv);
static int Thread_UpdateState(Thread *th, ThreadState newval,
                              ThreadState oldval);
static void Thread_SaveSigState(Thread *th);
static void Thread_RestoreSigState(Thread *th);

// Copied from src/plugin/pid/pid_syscallsreal.c
// Without this, libdmtcp.so will depend on libdmtcp_plugin.so being loaded
static pid_t _real_getpid(void)
{
  JWARNING("_real_getpid")
          .Text("FIXME: _real_getpid returning virtual pid, not real pid.");
  // libc caches pid of the process and hence after restart, libc:getpid()
  // returns the pre-ckpt value.
  return (pid_t)_real_syscall(SYS_getpid);
}
// Copied from src/plugin/pid/pid.c and .../pid/pid_syscallsreal.c
// Without this, libdmtcp.so will depend on libdmtcp_plugin.so being loaded
LIB_PRIVATE
pid_t dmtcp_get_real_pid()
{
  return _real_getpid();
}

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
  lock_threads();
  while (activeThreads != NULL) {
    ThreadList::threadIsDead(activeThreads); // takes care of updating
                                             // "activeThreads" ptr.
  }
  unlk_threads();
  init();
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
  motherpid = THREAD_REAL_TID();
  TLSInfo_VerifyPidTid(motherpid, motherpid);

  SigInfo::setupCkptSigHandler(&stopthisthread);

  // CONTEXT:  updateTid() resets curThread only if it's non-NULL.
  // ... -> initializeMtcpEngine() -> ThreadList::init() -> updateTid()
  // See addToActiveList() for more information.
  curThread = NULL;

  /* Set up caller as one of our threads so we can work on it */
  motherofall = ThreadList::getNewThread();
  motherofall_saved_sp = &motherofall->saved_sp;
  motherofall_tlsInfo = &motherofall->tlsInfo;
  updateTid(motherofall);

  sem_init(&sem_launch, 0, 0);
  sem_init(&semNotifyCkptThread, 0, 0);
  sem_init(&semWaitForCkptThreadSignal, 0, 0);

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

// Called from:  threadwrappers.cpp:__clone()
void
ThreadList::initThread(Thread *th, int (*fn)(
                         void *), void *arg, int flags, int *ptid, int *ctid)
{
  /* Save exactly what the caller is supplying */
  th->fn = fn;
  th->arg = arg;
  th->flags = flags;
  th->ptid = ptid;
  th->ctid = ctid;
  th->next = NULL;
  th->state = ST_RUNNING;
  th->procname[0] = '\0';
}

/*****************************************************************************
 *
 * Thread exited/exiting.
 *
 *****************************************************************************/
void
ThreadList::threadExit()
{
  curThread->state = ST_ZOMBIE;
}

/*****************************************************************************
 *
 *****************************************************************************/
void
ThreadList::updateTid(Thread *th)
{
  if (curThread == NULL) {
    curThread = th;
  }
  th->tid = THREAD_REAL_TID();
  th->virtual_tid = dmtcp_gettid();

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

  JTRACE("starting thread") (th->tid) (th->virtual_tid);

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
  mtcpHdr->post_restart_debug = &ThreadList::postRestartDebug;
  memcpy(&mtcpHdr->motherofall_tls_info,
         &motherofall->tlsInfo,
         sizeof(motherofall->tlsInfo));
  mtcpHdr->tls_pid_offset = TLSInfo_GetPidOffset();
  mtcpHdr->tls_tid_offset = TLSInfo_GetTidOffset();
  mtcpHdr->myinfo_gs = myinfo_gs;
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
  emptyFreeList();
  SigInfo::saveSigHandlers();

  /* Do this once, same for all threads.  But restore for each thread. */
  if (TLSInfo_HaveThreadSysinfoOffset()) {
    saved_sysinfo = TLSInfo_GetThreadSysinfo();
  }

  MtcpHeader mtcpHdr;
  prepareMtcpHeader(&mtcpHdr);
  CkptSerializer::writeCkptImage(&mtcpHdr, sizeof(mtcpHdr));
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

  Thread_SaveSigState(ckptThread);
  TLSInfo_SaveTLSState(&ckptThread->tlsInfo);

  /* Set up our restart point.  I.e., we get jumped to here after a restore. */
#ifdef SETJMP
  JASSERT(sigsetjmp(ckptThread->jmpbuf, 1) >= 0) (JASSERT_ERRNO);
#else // ifdef SETJMP
  JASSERT(getcontext(&ckptThread->savctx) == 0) (JASSERT_ERRNO);
#endif // ifdef SETJMP
  save_sp(&ckptThread->saved_sp);
  JTRACE("after sigsetjmp/getcontext")
    (curThread->tid) (curThread->virtual_tid) (curThread->saved_sp);

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
      int ret;

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
    TLSInfo_SaveTLSState(&curThread->tlsInfo); // save thread local storage
                                               // state

    /* Set up our restart point, ie, we get jumped to here after a restore */
#ifdef SETJMP
    JASSERT(sigsetjmp(curThread->jmpbuf, 1) >= 0);
#else // ifdef SETJMP
    JASSERT(getcontext(&curThread->savctx) == 0);
#endif // ifdef SETJMP
    save_sp(&curThread->saved_sp);

    JTRACE("Thread after sigsetjmp/getcontext")
      (curThread->tid) (curThread->virtual_tid)
      (curThread->saved_sp) (__builtin_return_address(0));

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
  Thread_RestoreSigState(thread);

  if (thread == motherofall) {
    /* If DMTCP_RESTART_PAUSE==4, wait for gdb attach.*/
    char * pause_param = getenv("DMTCP_RESTART_PAUSE");
    if (pause_param == NULL) {
      pause_param = getenv("MTCP_RESTART_PAUSE");
    }
    if (pause_param != NULL && pause_param[0] == '4' &&
        pause_param[1] == '\0') {
#ifdef HAS_PR_SET_PTRACER
      prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0); // For: gdb attach
#endif // ifdef HAS_PR_SET_PTRACER
      volatile int dummy = 1;
      while (dummy);
#ifdef HAS_PR_SET_PTRACER
      prctl(PR_SET_PTRACER, 0, 0, 0, 0); // Revert permission to default.
#endif // ifdef HAS_PR_SET_PTRACER
    }
  }
}

/*****************************************************************************
 *
 *****************************************************************************/
void
ThreadList::postRestartDebug(double readTime, int restartPause)
{ // Don't try to print before debugging.  Who knows what is working yet?
#ifndef DEBUG
  // printf may fail, but we'll risk it to let user know this:
  printf("\n** DMTCP: It appears DMTCP not configured with '--enable-debug'\n");
  printf("**        If GDB doesn't show source, re-configure and re-compile\n");
#endif
  if (restartPause == 1) {
    // If we're here, user set env. to DMTCP_RESTART_PAUSE==0; is expecting this
    volatile int dummy = 1;
    while (dummy);
    // User should have done GDB attach if we're here.
#ifdef HAS_PR_SET_PTRACER
    prctl(PR_SET_PTRACER, 0, 0, 0, 0); // Revert to default: no ptracer
#endif
  }
  static char restartPauseStr[2];
  restartPauseStr[0] = '0' + restartPause;
  restartPauseStr[1] = '\0';
  setenv("DMTCP_RESTART_PAUSE", restartPauseStr, 1);
  postRestart(readTime);
}

// FIXME:  Is this comment still true?
//   threadlist.h sets these as default arguments: readTime=0.0, restartPause=0
void
ThreadList::postRestart(double readTime)
{
  Thread *thread;
  sigset_t tmp;

  /* If DMTCP_RESTART_PAUSE==2, wait for gdb attach. */
  char * pause_param = getenv("DMTCP_RESTART_PAUSE");
  if (pause_param == NULL) {
    pause_param = getenv("MTCP_RESTART_PAUSE");
  }
  if (pause_param != NULL && pause_param[0] == '2' && pause_param[1] == '\0') {
#ifdef HAS_PR_SET_PTRACER
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0); // Allow 'gdb attach'
#endif // ifdef HAS_PR_SET_PTRACER
    // In src/mtcp_restart.c, we printed to user:
    // "Stopping due to env. var DMTCP_RESTART_PAUSE or MTCP_RESTART_PAUSE ..."
    volatile int dummy = 1;
    while (dummy);
#ifdef HAS_PR_SET_PTRACER
    prctl(PR_SET_PTRACER, 0, 0, 0, 0);   // Revert permission to default.
#endif // ifdef HAS_PR_SET_PTRACER
  }

  SharedData::postRestart();

  /* Fill in the new mother process id */
  motherpid = THREAD_REAL_TID();
  motherofall->tid = motherpid;

  restoreInProgress = true;

  Util::allowGdbDebug(DEBUG_POST_RESTART);

  sigfillset(&tmp);
  for (thread = activeThreads; thread != NULL; thread = thread->next) {
    struct MtcpRestartThreadArg mtcpRestartThreadArg;
    sigandset(&sigpending_global, &tmp, &(thread->sigpending));
    tmp = sigpending_global;

    if (thread == motherofall) {
      continue;
    }

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
    thread->ckptReadTime = readTime;

    /* Create the thread so it can finish restoring itself. */
    pid_t tid = _real_clone(restarthread,

                            // -128 for red zone
                            (void *)((char *)thread->saved_sp - 128),

                            /* Don't do CLONE_SETTLS (it'll puke).  We do it
                             * later via restoreTLSState. */
                            thread->flags & ~CLONE_SETTLS,
                            clonearg, thread->ptid, NULL, thread->ctid);

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

  thread->tid = THREAD_REAL_TID();

  // This function and related ones are defined in src/mtcp/restore_libc.c
  TLSInfo_RestoreTLSState(&thread->tlsInfo);

  if (TLSInfo_HaveThreadSysinfoOffset()) {
    TLSInfo_SetThreadSysinfo(saved_sysinfo);
  }

  if (thread == motherofall) { // if this is a user thread
    /* If DMTCP_RESTART_PAUSE==3, wait for gdb attach.*/
    char * pause_param = getenv("DMTCP_RESTART_PAUSE");
    if (pause_param == NULL) {
      pause_param = getenv("MTCP_RESTART_PAUSE");
    }
    if (pause_param != NULL && pause_param[0] == '3' &&
        pause_param[1] == '\0') {
#ifdef HAS_PR_SET_PTRACER
      prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0); // For: gdb attach
#endif // ifdef HAS_PR_SET_PTRACER
      // In src/mtcp_restart.c, we printed to user:
      // "Stopping due to env. var DMTCP_RESTART_PAUSE or MTCP_RESTART_PAUSE .."
      volatile int dummy = 1;
      while (dummy);
#ifdef HAS_PR_SET_PTRACER
      prctl(PR_SET_PTRACER, 0, 0, 0, 0); // Revert permission to default.
#endif // ifdef HAS_PR_SET_PTRACER
    }
  }

  /* Jump to the stopthisthread routine just after sigsetjmp/getcontext call.
   * Note that if this is the restored checkpointhread, it jumps to the
   * checkpointhread routine
   */
  JTRACE("calling siglongjmp/setcontext") (thread->tid) (thread->virtual_tid);
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

  JTRACE("restoring signal mask for thread") (th->virtual_tid);
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
  // ... -> initializeMtcpEngine() -> ThreadList::init() -> updateTid()
  // -> addToActiveList()
  // NOTE:  After a call to fork(), only the calling thread continues to live.
  // Before initializeMtcpEngine() called init(), it called:
  // ... -> initializeMtcpEngine() -> ThreadSync::initMotherOfAll() ->
  // -> ThreadSync::initThread()
  // Logically, we would have set 'curThread = NULL;; inside
  // ThreadSync::initThread(), but it's inconvenient since curThread
  // is static (file-private).
  // So, updateTid() created the new thread descriptor.  We make sure
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
      JTRACE("Removing duplicate thread descriptor")
        (thread->tid) (thread->virtual_tid);

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

  thread->next = threads_freelist;
  threads_freelist = thread;
}

/*****************************************************************************
 *
 * Return thread from freelist.
 *
 *****************************************************************************/
Thread *
ThreadList::getNewThread()
{
  Thread *thread;

  lock_threads();
  if (threads_freelist == NULL) {
    thread = (Thread *)JALLOC_HELPER_MALLOC(sizeof(Thread));
    JASSERT(thread != NULL);
  } else {
    thread = threads_freelist;
    threads_freelist = threads_freelist->next;
  }
  unlk_threads();
  memset(thread, 0, sizeof(*thread));
  return thread;
}

/*****************************************************************************
 *
 * Call free() on all threads_freelist items
 *
 *****************************************************************************/
void
ThreadList::emptyFreeList()
{
  lock_threads();

  while (threads_freelist != NULL) {
    Thread *thread = threads_freelist;
    threads_freelist = threads_freelist->next;
    JALLOC_HELPER_FREE(thread);
  }

  unlk_threads();
}
