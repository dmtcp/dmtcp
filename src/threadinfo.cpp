#include "threadinfo.h"

#include <pthread.h>
#include <unistd.h>

#include "dmtcp.h"
#include "dmtcp_assert.h"
#include "plugin/pid/pidhelpers.h"
#include "siginfo.h"
#include "syscallwrappers.h"

static DmtcpMutex threadStateLock = DMTCP_MUTEX_INITIALIZER;
extern sigset_t sigpending_global;

bool
ThreadInfo::updateState(ThreadState newState, ThreadState oldState)
{
  bool changed = false;

  ASSERT_LOCK_SUCCESS(DmtcpMutexLock(&threadStateLock),
                      "locking thread state");
  if (oldState == state) {
    state = newState;
    changed = true;
  }
  ASSERT_LOCK_SUCCESS(DmtcpMutexUnlock(&threadStateLock),
                      "unlocking thread state");
  return changed;
}

void
ThreadInfo::saveSigState()
{
  // Save signal mask and pending signals for this thread.
  ASSERT_PTHREAD_SUCCESS(
    pthread_sigmask(SIG_SETMASK, NULL, &sigblockmask),
    "saving thread signal mask: tid={}",
    tid);

  ::sigpending(&sigpending);
}

void
ThreadInfo::restoreSigState()
{
  // Restore signal mask and all pending signals.
  TRACE("restoring signal mask for thread: tid={}", tid);
  ASSERT_PTHREAD_SUCCESS(
    pthread_sigmask(SIG_SETMASK, &sigblockmask, NULL),
    "restoring thread signal mask: tid={}",
    tid);

  // Raise signals which were pending only for this thread at checkpoint time.
  for (int i = SIGRTMAX; i > 0; --i) {
    if (sigismember(&sigpending, i) == 1 &&
        sigismember(&sigblockmask, i) == 1 &&
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

int
ThreadInfo::sendSignal(int sig)
{
  return dmtcp_tgkill(getpid(), tid, sig);
}

void
ThreadInfo::markExiting()
{
  exiting = 1;
}

void
ThreadInfo::initPthreadFields()
{
  pthreadAddrs = dmtcp_pthread_get_addrs(pthread_self());
  ptid = pthreadAddrs.tid;
}

void
ThreadInfo::setSigmask()
{
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, dmtcp::SigInfo::ckptSignal());
  ASSERT_PTHREAD_SUCCESS(
    _real_pthread_sigmask(SIG_UNBLOCK, &set, NULL),
    "unblocking checkpoint signal in thread: signal={}",
    dmtcp::SigInfo::ckptSignal());
}
