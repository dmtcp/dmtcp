#include <linux/version.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <string.h>
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
#include "ckptserializer.h"
#include "constants.h"
#include "coordinatorapi.h"
#include "dmtcp.h"
#include "dmtcpalloc.h"
#include "dmtcpworker.h"
#include "jalloc.h"
#include "mtcp/mtcp_header.h"
#include "plugin/pid/pidhelpers.h"
#include "pluginmanager.h"
#include "shareddata.h"
#include "siginfo.h"
#include "syscallwrappers.h"
#include "threadlist.h"
#include "threadsync.h"
#include "tls.h"
#include "uniquepid.h"
#include "util.h"
#include "dmtcp_assert.h"

/****************************
 * Start of TSAN utilities
 ****************************/

// ThreadSanitizer Fiber API (weak: resolves to the TSAN runtime only for TSAN
// targets, NULL no-op otherwise).  A "fiber" is a separate TSAN ThreadState
// (with its own event trace) that can be bound to the current OS thread.
// * The TSAN helper thread will _not_ have a fiber (by design for TSAN).
// * All other threads, including motherofall, have a fiber.
// * All threads go through restarthread(); If needed, each thread will use
//    __tsan_switch_to_fiber() to restore its fiber from 'Thread' in threadlist.
// * The checkpoint thread is a special case: on restart it is given a FRESH
//   fiber (via __tsan_create_fiber()) rather than resuming its old one --
//   see the comment in restarthread() for why.
extern "C" void *__tsan_get_current_fiber() __attribute__((weak));
extern "C" void __tsan_switch_to_fiber(void *fiber, unsigned flags)
  __attribute__((weak));
extern "C" void *__tsan_create_fiber(unsigned flags) __attribute__((weak));
extern "C" void __tsan_destroy_fiber(void *fiber) __attribute__((weak));
extern "C" void __tsan_ignore_thread_begin() __attribute__((weak));
extern "C" void __tsan_ignore_thread_end() __attribute__((weak));
// Below, address is arbitrary, unique memory address for synchronization
extern "C" void __tsan_release(void *address) __attribute__((weak));
extern "C" void __tsan_acquire(void *address) __attribute__((weak));

bool
is_tsan()
{
  return (__tsan_get_current_fiber != NULL);
}

/****************************
 * End of TSAN utilities
 ****************************/

// For i386 and x86_64, SETJMP currently has bugs.  Don't turn this
// on for them until they are debugged.
// Default is to use  setcontext/getcontext.
#if defined(__arm__) || defined(__aarch64__) || defined(__riscv)
#define SETJMP  /* setcontext/getcontext not defined for ARM and RISC-V glibc */
#endif         // if defined(__arm__) || defined(__aarch64__)

#ifdef SETJMP
#include <setjmp.h>
#else  // ifdef SETJMP
#include <ucontext.h>
#endif  // ifdef SETJMP

using namespace dmtcp;

// Globals
ATOMIC_SHARED_GLOBAL bool restoreInProgress = false;
static Thread motherofallStorage;
Thread *motherofall = NULL;
sigset_t sigpending_global;
Thread *activeThreads = NULL;
void *saved_sysinfo;

static const char *DMTCP_PRGNAME_PREFIX = "DMTCP:";

static DmtcpMutex threadlistLock = DMTCP_MUTEX_INITIALIZER;
static DmtcpMutex threadStateLock = DMTCP_MUTEX_INITIALIZER;
// Thread initialization runs before curThread exists; normal locks call gettid().
static DmtcpMutex threadInitLock = DMTCP_MUTEX_INITIALIZER_LLL;

static DmtcpRWLock threadResumeLock;

__thread Thread *curThread ATTR_TLS_INITIAL_EXEC = NULL;
Thread *ckptThread = NULL;

static int numUserThreads = 0;
static bool originalstartup;

// TSAN's pthread_create() interceptor may spawn its own helper thread as a
// nested pthread_create() call during createCkptThread()'s own (first-ever)
// call. So the LAST pthread_t registered in that window (see
// registerCkptThreadWindowCandidate(), below) is the real checkpoint
// thread; any earlier one is the helper.
//
// A candidate's classification isn't known until the window closes. So it
// is resolved lazily in endCkptThreadCreationWindow(), which waits for each
// candidate to show up in activeThreads. That wait must run on the parent,
// not by blocking the child inside thread_start(): TSAN's interceptor does
// not return to the caller until the created thread(s) make progress.
static bool expectCkptThreadNext = false;
static pthread_t ckptWindowCandidatePthreads[4];
static int ckptWindowCandidateCount = 0;

static void lock_threads(void);
static void unlk_threads(void);

void
ThreadList::beginCkptThreadCreationWindow()
{
  expectCkptThreadNext = true;
  ckptWindowCandidateCount = 0;
}

void
ThreadList::registerCkptThreadWindowCandidate(pthread_t pth)
{
  if (!expectCkptThreadNext) {
    return;
  }
  ASSERT(ckptWindowCandidateCount <
           (int) (sizeof(ckptWindowCandidatePthreads) /
                  sizeof(ckptWindowCandidatePthreads[0])),
         "more nested thread creations during ckpt-thread creation than "
         "expected");
  ckptWindowCandidatePthreads[ckptWindowCandidateCount++] = pth;
}

static Thread *
findThreadByPthreadSelf(void *pthreadSelf)
{
  Thread *found = NULL;
  while (found == NULL) {
    lock_threads();
    for (Thread *th = activeThreads; th != NULL; th = th->next) {
      if (th->pthreadSelf == pthreadSelf) {
        found = th;
        break;
      }
    }
    unlk_threads();
    if (found == NULL) {
      sched_yield();
    }
  }
  return found;
}

void
ThreadList::endCkptThreadCreationWindow()
{
  expectCkptThreadNext = false;
  if (!is_tsan()) {
    return;
  }
  ASSERT(ckptWindowCandidateCount >= 1,
         "no thread was registered for the checkpoint thread's own creation");
  // The LAST candidate registered is the real checkpoint thread; any
  // earlier ones are the (at most one, in every run observed so far)
  // TSAN-internal helper-thread spawn.
  for (int i = 0; i < ckptWindowCandidateCount - 1; i++) {
    Thread *th =
      findThreadByPthreadSelf((void *) ckptWindowCandidatePthreads[i]);
    th->is_tsan_helper = true;
    TRACE("TSAN: classified tid={} as the TSAN helper thread", th->tid);
  }
}

// Let dmtcp.h:DMTCP_RESTART_PAUSE_WHILE(cond) use (dmtcp::restartPauseLevel
volatile int dmtcp::restartPauseLevel = 0;

extern bool sem_launch_first_time;
extern sem_t sem_launch;  // allocated in coordinatorapi.cpp
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
  ASSERT_LOCK_SUCCESS(DmtcpMutexLock(&threadlistLock));
}

static void
unlk_threads(void)
{
  ASSERT_LOCK_SUCCESS(DmtcpMutexUnlock(&threadlistLock));
}

static int
signalThread(Thread *thread, int sig)
{
  return dmtcp_tgkill(getpid(), thread->tid, sig);
}

bool dmtcp_is_ckpt_thread()
{
  return ckptThread == curThread;
}

Thread *
dmtcp_get_current_thread()
{
  if (curThread != nullptr) {
    return curThread;
  }

  Thread *th = ThreadList::init();
  // Reaching here (not motherofall) means this thread bypassed DMTCP's own
  // pthread_create() wrapper entirely -- see thread_start() in
  // threadwrappers.cpp for the path it skipped. For a TSAN target, that's
  // TSAN's own helper/background thread, spawned via a raw clone().
  if (is_tsan() && th != motherofall) {
    th->is_tsan_helper = true;
  }
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

#elif defined(__riscv)
  asm volatile ("addi %0, sp, 0"
                  : "=r" (*sp)
                  : : "memory");

#else  // if defined(__i386__) || defined(__x86_64__)
# error "assembly instruction not translated"
#endif  // if defined(__i386__) || defined(__x86_64__)
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
    ThreadList::threadIsDead(activeThreads);  // takes care of updating
                                             // "activeThreads" ptr.
  }

  // After fork, only the calling thread remains.
  curThread = motherofall = NULL;

  init();
  createCkptThread();
}

/*****************************************************************************
 *
 *  This routine must be called at startup time to initiate checkpointing
 *
 *****************************************************************************/
Thread *
ThreadList::init()
{
  if (curThread != nullptr) {
    return curThread;
  }

  Thread *th;

  ASSERT_LOCK_SUCCESS(DmtcpMutexLock(&threadInitLock));
  if (motherofall == nullptr && _real_getpid() == _real_gettid()) {
    // We need to use static storage for motherofall to avoid calling
    // JALLOC_MALLOC during initialization which could lead to infinite recursion.
    motherofall = &motherofallStorage;
    th = motherofall;
  } else {
    th = (Thread *) JALLOC_MALLOC(sizeof(Thread));
  }

  ASSERT_NOT_NULL(th, "thread descriptor is null");
  memset(th, 0, sizeof(*th));

  th->next = NULL;
  th->prev = NULL;
  th->state = ST_RUNNING;
  th->exiting = 0;
  th->wrapperLockCount = 0;
  th->procname[0] = '\0';

  curThread = th;

  th->flags = (CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SYSVSEM
              | CLONE_SIGHAND | CLONE_THREAD
              | CLONE_SETTLS | CLONE_PARENT_SETTID
              | CLONE_CHILD_CLEARTID
              | 0);

  th->ptid = (pid_t*)((char*) pthread_self() + TLSInfo_GetTidOffset());
  th->ctid = th->ptid;
  // Also set eagerly here (not just later, in TLSInfo_SaveTLSState() at
  // checkpoint time) so that a thread becomes findable-by-identity (see
  // findThreadByPthreadSelf(), used for TSAN helper-thread detection) as
  // soon as it registers itself, without waiting for a checkpoint.
  th->pthreadSelf = (void *) pthread_self();

  th->tid = dmtcp_pid_init_thread_tid();
  if (th != motherofall) {
    // Libc helper threads can inherit a mask that blocks the checkpoint signal.
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SigInfo::ckptSignal());
    ASSERT_PTHREAD_SUCCESS(
      _real_pthread_sigmask(SIG_UNBLOCK, &set, NULL),
      "unblocking checkpoint signal in child thread: signal={}",
      SigInfo::ckptSignal());
  }

  TRACE("starting thread: tid={}", th->tid);

  // Check and remove any thread descriptor which has the same tid as ours.
  // Also, remove any dead threads from the list.
  ThreadList::addToActiveList(th);

  ASSERT_LOCK_SUCCESS(DmtcpMutexUnlock(&threadInitLock));
  return th;
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

  // Flag the immediately following pthread_create() call so that its
  // wrapper (in threadwrappers.cpp) can report any thread creation request
  // it sees during this call as belonging to this window -- see the
  // comment above expectCkptThreadNext for why more than one such request
  // can happen here.
  ThreadList::beginCkptThreadCreationWindow();

  /* Spawn off a thread that will perform the checkpoints from time to time */
  ASSERT_PTHREAD_SUCCESS(pthread_create(&checkpointhreadid,
                                        NULL,
                                        checkpointhread,
                                        NULL));

  ThreadList::endCkptThreadCreationWindow();

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
 * Thread exited/exiting.
 *
 *****************************************************************************/
void
ThreadList::threadExit()
{
  curThread->exiting = 1;
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

  string ckptFilename = ProcessInfo::instance().getTempCkptFilename();

  DmtcpCkptHeader header = ProcessInfo::instance();
  header.savedBrk = (uint64_t) sbrk(0);
  header.postRestartAddr = (uint64_t) &ThreadList::postRestart;

  // TODO(kapil): re-add this block after handling non-4096 sizes.
  // const ssize_t pagesize = Util::pageSize();
  // ASSERT_EQ(sizeof(header) % pagesize, 0ul);

  CkptSerializer::writeCkptImage(header, ckptFilename);
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

  // EXPERIMENT (not working): runs only once, on the first launch (this is
  // before the sigsetjmp restart point below) -- see the "EXPERIMENT (not
  // working)" comment in restarthread(), below.
  // if (is_tsan()) {
  //   ckptThread->tsan_fiber_ctx = __tsan_get_current_fiber();
  // }

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

    ASSERT_PTHREAD_SUCCESS(pthread_sigmask(SIG_SETMASK, &set, NULL),
                               "setting checkpoint-thread signal mask");
  }

  /* Set up our restart point.  I.e., we get jumped to here after a restore. */
#ifdef SETJMP
  ASSERT(sigsetjmp(ckptThread->jmpbuf, 1) >= 0,
         "failed to save checkpoint-thread jump context: tid={}",
         ckptThread->tid);
#else  // ifdef SETJMP
  ASSERT_NE(-1, getcontext(&ckptThread->savctx),
               "failed to save checkpoint-thread context: tid={}",
               ckptThread->tid);
#endif  // ifdef SETJMP
  save_sp(&ckptThread->saved_sp);
  TRACE("Saved checkpoint-thread restart context: tid={} saved_sp={}",
        curThread->tid, curThread->saved_sp);

  // On restart, we reach here via siglongjmp()/setcontext() from
  // restarthread(), which already switched this thread to a fresh TSAN
  // fiber before making that jump -- see the comment there for why.

  if (originalstartup) {
    originalstartup = false;
  } else {
    /* We are being restored.  Wait for all other threads to finish being
     * restored before resuming checkpointing.
     */
    TRACE("waiting for other threads after restore");
    ThreadList::waitForAllRestored(ckptThread);
    TRACE("resuming after restore");
  }

  /* This is a sleep-checkpoint-resume loop by the checkpoint thread.
   * On restart, we arrive back at getcontext, above, and then re-enter the
   * loop.
   */
  while (1) {
    /* Wait a while between writing checkpoint files */
    TRACE("before DmtcpWorker::waitForCheckpointRequest()");
    DmtcpWorker::waitForCheckpointRequest();

    restoreInProgress = false;

    ThreadList::suspendThreads();

    TRACE("Release locks and wait for exiting threads to die.");
    DmtcpWorker::releaseLocks();

    ThreadList::waitForExitingThreads();

    TRACE("Prepare plugin, etc. for checkpoint");
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
  ASSERT_LOCK_SUCCESS(DmtcpRWLockWrLock(&threadResumeLock));

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

      if (thread->exiting == 1) {
        continue;
      }

      /* Do various things based on thread's state */
      switch (thread->state) {
      case ST_RUNNING:

        /* Thread is running. Send it a signal so it will call stopthisthread.
         * We will need to rescan (hopefully it will be suspended by then)
         */
        if (Thread_UpdateState(thread, ST_SIGNALED, ST_RUNNING)) {
          if (signalThread(thread, SigInfo::ckptSignal()) < 0) {
            ASSERT_ERRNO(errno == ESRCH,
                         "error signalling thread for checkpoint: tid={} "
                         "signal={}",
                         thread->tid, SigInfo::ckptSignal());
            ThreadList::threadIsDead(thread);
          } else {
            needrescan = 1;
          }
        }
        break;

      case ST_SIGNALED:
        if (signalThread(thread, 0) == -1 && errno == ESRCH) {
          ThreadList::threadIsDead(thread);
        } else {
          needrescan = 1;
        }
        break;

      case ST_SUSPINPROG:
        // The TSAN helper thread is still signaled/suspended along with
        // everyone else (for a consistent memory snapshot), but it is never
        // recreated on restart (TSAN spawns a fresh one there instead -- see
        // postRestartWork()). So it will never contribute a sem_post in
        // waitForAllRestored().  Don't count it, or that wait would hang
        // waiting for a post that never comes.
        if (! (is_tsan() && thread->is_tsan_helper)) {
          numUserThreads++;
        }
        break;

      case ST_SUSPENDED:
        if (! (is_tsan() && thread->is_tsan_helper)) {
          numUserThreads++;
        }
        break;

      case ST_CKPNTHREAD:
        break;

      default:
        ASSERT(false, "unexpected thread state while suspending: tid={} "
               "state={}",
               thread->tid, thread->state);
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

  ASSERT_NOT_NULL(activeThreads);
  TRACE("everything suspended: numUserThreads={}", numUserThreads);
}

void ThreadList::waitForExitingThreads()
{
  int needsRescan = 0;
  do {
    Thread *next;
    needsRescan = 0;

    for (Thread *thread = activeThreads; thread != NULL; thread = next) {
      next = thread->next;
      if (thread->exiting) {
        if (signalThread(thread, 0) == -1 && errno == ESRCH) {
          // Thread exited. Let's remove it from the list.
          ThreadList::threadIsDead(thread);
        } else {
          needsRescan = 1;
        }
      }
    }

    if (needsRescan) {
      usleep(1000);
    }
  } while (needsRescan);
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
  TRACE("resuming user threads");
  ASSERT_LOCK_SUCCESS(DmtcpRWLockUnlock(&threadResumeLock));
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
   *
   * 5. Normally, the user should never directly send a SIGUSR2 (ckpt signal)
   * directly to a process.  They should use the DMTCP coordinator (perhaps
   * by 'dmtcp_command'.  The coordinator then requests a ckpt from the
   * ckpt thread, and the ckpt thread sends a signal to each user thread.
   * But there are cases when DMTCP runs under some
   * other scheduler (e.g., the Slurm resource manager).  In such cases,
   * Slurm may send a ckpt signal, SIGUSR2, directly to _each_ user process,
   * and the signal may be caught by any _one_ thread of the process.
   * In this case, if we catch a signal not coming from the ckpt thread,
   * then we request the DMTCP coordinator to initiate a checkpoint.
   */

  // Case 5 above.
  if (curThread == ckptThread || curThread->state == ST_RUNNING) {
    CoordinatorAPI::connectAndSendUserCommand(DMT_CHECKPOINT);
    return;
  }

  // make sure we don't get called twice for same thread
  if (Thread_UpdateState(curThread, ST_SUSPINPROG, ST_SIGNALED)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)
    WARN_NE(-1, prctl(PR_GET_NAME, curThread->procname));
#endif  // if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)

    // --- TSAN INJECTION: PRE-CHECKPOINT ---
    if (is_tsan() && ! curThread->is_tsan_helper && ! dmtcp_is_ckpt_thread()) {
      // clang's statically linked TSAN runtime doesn't export
      // __tsan_ignore_thread_begin/_end (unlike the fiber API and
      // acquire/release). So the weak symbol resolves to NULL there. Skip
      // the bracketing when unavailable rather than segfault on a null call.
      if (__tsan_ignore_thread_begin != NULL) {
        __tsan_ignore_thread_begin();
      }
      // Ordinary threads don't need __tsan_acquire/release
      curThread->tsan_fiber_ctx = __tsan_get_current_fiber();
      if (curThread == motherofall) {
        __tsan_release((void*)curThread);
      }
      // -------------------------------------------------------
    }
    // --------------------------------------

    Thread_SaveSigState(curThread);  // save sig state (and block sig delivery)
    TLSInfo_SaveTLSState(curThread);  // save thread local storage state

    /* Set up our restart point, ie, we get jumped to here after a restore */
#ifdef SETJMP
    ASSERT(sigsetjmp(curThread->jmpbuf, 1) >= 0,
           "failed to save user-thread jump context");
#else  // ifdef SETJMP
    ASSERT_NE(-1, getcontext(&curThread->savctx));
#endif  // ifdef SETJMP
    save_sp(&curThread->saved_sp);

    TRACE("Saved user-thread checkpoint context: tid={} saved_sp={} "
          "return_address={}",
          curThread->tid, curThread->saved_sp, __builtin_return_address(0));

    if (!restoreInProgress) {
      /* We are a user thread and all context is saved.
       * Wait for ckpt thread to write ckpt, and resume.
       */

      /* Tell the checkpoint thread that we're all saved away */
      ASSERT(Thread_UpdateState(curThread, ST_SUSPENDED, ST_SUSPINPROG),
             "Failed to mark thread (tid:{}) from SUSPEND_IN_PROGRESS to "
             "SUSPENDED",
             curThread->tid);
      sem_post(&semNotifyCkptThread);

      /* Then wait for the ckpt thread to write the ckpt file then wake us up */
      TRACE("User thread suspended: tid={}", curThread->tid);

      // We can't use sem_wait here because sem_wait registers a cleanup
      // handler before going into blocking wait. The handler is popped before
      // returning from it. However, on restart, the thread will do a longjump
      // and thus will never come out of the sem_wait, thus the handler is
      // never popped. This causes a problem later on during pthread_exit. The
      // pthread_exit routine executes all registered cleanup handlers.
      // However, the sem_wait cleanup handler is now invalid and thus we get a
      // segfault.
      // The change in sem_wait behavior was first introduce in glibc 2.21.
      ASSERT_LOCK_SUCCESS(DmtcpRWLockRdLock(&threadResumeLock));

      ASSERT(Thread_UpdateState(curThread, ST_RUNNING, ST_SUSPENDED),
             "Failed to mark thread (tid:{}) from SUSPENDED to RUNNING "
             "after checkpoint",
             curThread->tid);

      ASSERT_LOCK_SUCCESS(DmtcpRWLockUnlock(&threadResumeLock));

      // --- TSAN INJECTION: RESUME ORIGINAL PROCESS ---
      if (is_tsan() && ! curThread->is_tsan_helper && !dmtcp_is_ckpt_thread()) {
        // Ordinary threads don't need __tsan_acquire/release
        // See the PRE-CHECKPOINT block above for why this is null-checked.
        if (__tsan_ignore_thread_end != NULL) {
          __tsan_ignore_thread_end();
        }
      }
      // -----------------------------------------------

    } else {
      // If the user defined DMTCP_DISABLE_PRGNAME_PREFIX, skip this prefix.
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)
# ifdef ENABLE_PRGNAME_PREFIX
      if (getenv(ENV_VAR_DISABLE_PRGNAME_PREFIX) == NULL &&
          ! Util::strStartsWith(curThread->procname, DMTCP_PRGNAME_PREFIX)) {
        // Add the "DMTCP:" prefix.
        string newName = string(DMTCP_PRGNAME_PREFIX) + curThread->procname;
        strncpy(curThread->procname,
                newName.c_str(),
                sizeof(curThread->procname));

        // Add a NULL at the end to make sure the string terminates in all cases
        curThread->procname[sizeof(curThread->procname) - 1] = '\0';
      }
# endif
      ASSERT_ERRNO(prctl(PR_SET_NAME, curThread->procname) != -1 ||
                   errno == EINVAL,
                   "prctl(PR_SET_NAME) failed");
#endif  // if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)

      ASSERT(Thread_UpdateState(curThread, ST_RUNNING, ST_SUSPENDED),
             "Failed to mark restored thread (tid:{}) from SUSPENDED to "
             "RUNNING",
             curThread->tid);

      /* Else restoreinprog >= 1;  This stuff executes to do a restart */
      ThreadList::waitForAllRestored(curThread);

      // --- TSAN INJECTION: POST-RESTART CLEANUP ---
      // See the PRE-CHECKPOINT block above for why this is null-checked.
      if (is_tsan() && ! curThread->is_tsan_helper && ! dmtcp_is_ckpt_thread() &&
          __tsan_ignore_thread_end != NULL) {
        __tsan_ignore_thread_end();
      }
      // --------------------------------------------
    }

    TRACE("User thread returning to user code: tid={} return_address={}",
          curThread->tid, __builtin_return_address(0));
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

    TRACE("before DmtcpWorker::postRestart()");

    DmtcpWorker::postRestart();

    TRACE("after DmtcpWorker::postRestart()");

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
ThreadList::postRestart(int restartPause)
{
  // This function and related ones are defined in src/tls.cpp
  TLSInfo_RestoreTLSState(motherofall);
  TLSInfo_RestoreTLSTidPid(motherofall);

  restartPauseLevel = restartPause;
  DMTCP_RESTART_PAUSE_WHILE(restartPauseLevel == 2);

  postRestartWork();
}

/*****************************************************************************
 *
 *****************************************************************************/
void
ThreadList::postRestartWork()
{
  Thread *thread;
  Thread *next;
  sigset_t tmp;

  if (TLSInfo_HaveThreadSysinfoOffset()) {
    TLSInfo_SetThreadSysinfo(saved_sysinfo);
  }

  // The PID plugin helper refreshes the virtual->real TID map and returns the
  // virtual TID that ThreadList stores in Thread::tid.
  motherofall->tid = dmtcp_update_virtual_to_real_tid(motherofall->tid);

  DMTCP_RESTART_PAUSE_WHILE(restartPauseLevel == 3);

  SharedData::postRestart();

  restoreInProgress = true;

  Util::allowGdbDebug(DEBUG_POST_RESTART);

  sigfillset(&tmp);
  for (thread = activeThreads; thread != NULL; thread = next) {
    // Precompute 'next' now, in case 'thread' is declared dead below.
    next = thread->next;

    sigandset(&sigpending_global, &tmp, &(thread->sigpending));
    tmp = sigpending_global;

    if (thread == motherofall) {
      continue;
    }

    /* Create the thread so it can finish restoring itself.
     * But remove old TSAN helper thread; it's stateless: TSAN creates new one
     */
    if (is_tsan() && thread->is_tsan_helper) {
      ThreadList::threadIsDead(thread); // Del. old TSAN helper from active list
      continue; // TSAN helper should not be restored; Stateless, TSAN creates
    }
    pid_t tid = _real_clone(restarthread,

                            // -128 for red zone
                            (void *)((char *)thread->saved_sp - 128),

                            /* Don't do CLONE_SETTLS (it'll puke).  We do it
                             * later via restoreTLSState. */
                            thread->flags & ~CLONE_SETTLS,
                            thread, thread->ptid, NULL, thread->ctid);

    ASSERT_ERRNO(tid > 0, "error recreating thread: tid={} result={}",
                 thread->tid, tid);
  TRACE("Recreated thread after restart: virtual_tid={} real_tid={}",
          thread->tid, tid);
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

  // The checkpoint thread is a special case: unlike other threads, it never
  // goes through the suspend/resume path (excluded from
  // __tsan_ignore_thread_begin/end). So its TSAN trace kept recording
  // through its own intercepted calls in writeCkpt() -- possibly torn at the
  // moment of checkpoint. Rather than resume that possibly-torn state,
  // treat this OS thread as if a fresh, unrelated one has just been
  // created: switch to a brand new TSAN fiber immediately, before anything
  // else (even a TRACE() call, a few lines below) can touch the old one.
  if (thread == ckptThread && is_tsan()) {
    ASSERT_NOT_NULL(__tsan_create_fiber,
                    "TSAN runtime is missing __tsan_create_fiber");
    void *staleFiber = thread->tsan_fiber_ctx;
    // EXPERIMENT (not working): switch to staleFiber here first, to give
    // this raw restarted thread a valid TSAN identity before creating the
    // fresh fiber -- goal was to drop the suppressions-file requirement.
    // if (staleFiber != NULL && dmtcp_is_ckpt_thread()) {
    //   __tsan_switch_to_fiber(staleFiber, 0);
    // }
    void *freshFiber = __tsan_create_fiber(0);
    ASSERT_NOT_NULL(freshFiber, "__tsan_create_fiber returned NULL");
    __tsan_switch_to_fiber(freshFiber, 0);
    if (staleFiber != NULL) {
      ASSERT_NOT_NULL(__tsan_destroy_fiber,
                      "TSAN runtime is missing __tsan_destroy_fiber");
      __tsan_destroy_fiber(staleFiber);  // now inactive; free it
    }
    thread->tsan_fiber_ctx = freshFiber;
  }

  TLSInfo_RestoreTLSTidPid(thread);

  if (TLSInfo_HaveThreadSysinfoOffset()) {
    TLSInfo_SetThreadSysinfo(saved_sysinfo);
  }

  // Keep Thread::tid in the virtual namespace while refreshing its real mapping.
  thread->tid = dmtcp_update_virtual_to_real_tid(thread->tid);

  if (thread == motherofall) {  // if this is a user thread
    DMTCP_RESTART_PAUSE_WHILE(restartPauseLevel == 4);
  }

  // --- TSAN INJECTION: RESTART BRIDGE ---
  // Earlier, we already omitted restarthread() if thread->is_tsan_helper
  //
  // Do NOT call __tsan_ignore_thread_begin() here. thread->tsan_fiber_ctx
  // was captured in stopthisthread()'s PRE-CHECKPOINT block *after* that
  // block's own __tsan_ignore_thread_begin() call. So the restored fiber
  // is already carrying "ignore" depth 1 from before checkpoint.
  if (is_tsan() && ! dmtcp_is_ckpt_thread()) {
    __tsan_switch_to_fiber(thread->tsan_fiber_ctx, 0);
    if (thread == motherofall) {
      __tsan_acquire((void*)thread);
    }
    // ---------------------------------------------------------
  }
  // --------------------------------------

  // The checkpoint thread is deliberately not touched here: it was already
  // given a fresh TSAN fiber earlier in this function, immediately after
  // TLSInfo_RestoreTLSState() -- see the comment there for why.

  /* Jump to the stopthisthread routine just after sigsetjmp/getcontext call.
   * Note that if this is the restored checkpointhread, it jumps to the
   * checkpointhread routine
   */
  TRACE("calling siglongjmp/setcontext: tid={}", thread->tid);
#ifdef SETJMP
  siglongjmp(thread->jmpbuf, 1); /* Shouldn't return */
#else  // ifdef SETJMP
  setcontext(&thread->savctx); /* Shouldn't return */
#endif  // ifdef SETJMP
  ASSERT(false, "restored thread context unexpectedly returned: tid={}",
         thread->tid);
  return 0;   /* NOTREACHED : stop compiler warning */
}

/*****************************************************************************
 *
 *****************************************************************************/
int
Thread_UpdateState(Thread *th, ThreadState newval, ThreadState oldval)
{
  int res = 0;

  ASSERT_LOCK_SUCCESS(DmtcpMutexLock(&threadStateLock),
                      "locking thread state");
  if (oldval == th->state) {
    th->state = newval;
    res = 1;
  }
  ASSERT_LOCK_SUCCESS(DmtcpMutexUnlock(&threadStateLock),
                      "unlocking thread state");
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
  ASSERT_PTHREAD_SUCCESS(
    pthread_sigmask(SIG_SETMASK, NULL, &th->sigblockmask),
    "saving thread signal mask: tid={}",
    th->tid);

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

  TRACE("restoring signal mask for thread: tid={}", th->tid);
  ASSERT_PTHREAD_SUCCESS(
    pthread_sigmask(SIG_SETMASK, &th->sigblockmask, NULL),
    "restoring thread signal mask: tid={}",
    th->tid);

  // Raise the signals which were pending for only this thread at the time of
  // checkpoint.
  for (i = SIGRTMAX; i > 0; --i) {
    if (sigismember(&th->sigpending, i) == 1 &&
        sigismember(&th->sigblockmask, i) == 1 &&
        sigismember(&sigpending_global, i) == 0 &&
        i != dmtcp_get_ckpt_signal()) {
      if (i == SIGCHLD) {
        NOTE("\n*** WARNING:  SIGCHLD was delivered prior to ckpt.\n"
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
  Thread *thread;
  Thread *next_thread;

  lock_threads();

  ASSERT(th->tid != 0, "cannot add thread with zero tid");

  // First remove duplicate descriptors.
  for (thread = activeThreads; thread != NULL; thread = next_thread) {
    next_thread = thread->next;
    if (thread != th && thread->tid == th->tid) {
      TRACE("Removing duplicate thread descriptor: tid={}", thread->tid);

      // There will be at most one duplicate descriptor.
      threadIsDead(thread);
      continue;
    }

    /* NOTE:  ST_ZOMBIE is used only for the sake of efficiency.  We
     *   test threads in state ST_ZOMBIE using tgkill to remove them
     *   early (before reaching a checkpoint) so that the
     *   threadrdescriptor list does not grow too long.
     */
    if (thread->exiting) {
      /* if no thread with this tid, then we can remove zombie descriptor */
      if (-1 == signalThread(thread, 0)) {
        TRACE("Killing zombie thread: tid={}", thread->tid);
        threadIsDead(thread);
      }
    }
  }

  th->next = activeThreads;
  th->prev = NULL;
  if (activeThreads != NULL) {
    activeThreads->prev = th;
  }
  activeThreads = th;

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
  ASSERT_NOT_NULL(thread);
  TRACE("Putting thread on freelist: tid={}", thread->tid);

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

  if (thread != &motherofallStorage) {
    JALLOC_FREE(thread);
  }
}
