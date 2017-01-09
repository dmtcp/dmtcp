#include "siginfo.h"
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include "jassert.h"
#include "constants.h"
#include "dmtcp.h"
#include "syscallwrappers.h"
#include "threadlist.h"

using namespace dmtcp;

static int STOPSIGNAL;

/* NOTE:  NSIG == SIGRTMAX+1 == 65 on Linux; NSIG is const, SIGRTMAX isn't */
static struct sigaction sigactions[NSIG];

int
SigInfo::ckptSignal() { return STOPSIGNAL; }

extern "C" int
SigInfo_ckptSignal() { return STOPSIGNAL; }

/*****************************************************************************
 *
 *  Set the thread's STOPSIGNAL handler.  Threads are sent STOPSIGNAL when they
 *  are to suspend execution the application, save their state and wait for the
 *  checkpointhread to write the checkpoint file.
 *
 *    Output:
 *
 *	Calling thread will call stopthisthread() when sent a STOPSIGNAL
 *
 *****************************************************************************/
void
SigInfo::setupCkptSigHandler(sighandler_t handler)
{
  static int init = 0;

  if (!init) {
    char *tmp, *endp;
    init = 1;

    /* If the user has defined a signal, use that to suspend.  Otherwise, use
     * CKPT_SIGNAL */
    tmp = getenv("DMTCP_SIGCKPT");
    if (tmp == NULL) {
      STOPSIGNAL = CKPT_SIGNAL;
    } else {
      errno = 0;
      STOPSIGNAL = strtol(tmp, &endp, 0);

      if ((errno != 0) || (tmp == endp)) {
        JWARNING(false) (getenv("DMTCP_SIGCKPT")) (CKPT_SIGNAL)
        .Text("Your chosen SIGCKPT does not translate to a number, and cannot "
              "be used.  Default signal will be used instead");
        STOPSIGNAL = CKPT_SIGNAL;
      } else if (STOPSIGNAL < 1 || STOPSIGNAL > 31) {
        JNOTE("Your chosen SIGCKPT is not a valid signal, and cannot be used."
              " Default signal will be used instead.")
          (STOPSIGNAL) (CKPT_SIGNAL);
        STOPSIGNAL = CKPT_SIGNAL;
      }
    }
  }

  struct sigaction act, old_act;
  memset(&act, 0, sizeof act);
  act.sa_handler = handler;
  sigfillset(&act.sa_mask);
  act.sa_flags = SA_RESTART;

  // We can't use standard sigaction here, because DMTCP has a wrapper around
  // it that will not allow anyone to set a signal handler for SIGUSR2.
  JASSERT(_real_sigaction(STOPSIGNAL, &act, &old_act) != -1) (JASSERT_ERRNO)
  .Text("Error setting up signal handler");

  if ((old_act.sa_handler != SIG_IGN) && (old_act.sa_handler != SIG_DFL) &&
      (old_act.sa_handler != handler)) {
    JASSERT(false) (STOPSIGNAL) (old_act.sa_handler)
    .Text("\nSignal handler already in use. You may employ a different\n"
          "signal by setting the environment variable DMTCP_SIGCKPT to the\n"
          "number of the signal that DMTCP should use for checkpointing.\n");
  }
}

/*****************************************************************************
 *
 *  Save all signal handlers
 *
 *****************************************************************************/
void
SigInfo::saveSigHandlers()
{
  int sig;
  struct sigaction old_act, act;

  /* Remove STOPSIGNAL from pending signals list:
   * Under Ptrace, STOPSIGNAL is sent to the inferior threads once by the
   * superior thread and once by the ckpt-thread of the inferior. STOPSIGNAL
   * is blocked while the inferior thread is executing the signal handler and
   * so the signal is becomes pending and is delivered right after returning
   * from stopthisthread.
   * To tackle this, we disable/re-enable signal handler for STOPSIGNAL.
   */
  memset(&act, 0, sizeof act);
  act.sa_handler = SIG_IGN;

  // Remove signal handler
  JASSERT(_real_sigaction(STOPSIGNAL, &act, &old_act) != -1) (JASSERT_ERRNO)
  .Text("Error setting up signal handler");

  // Reinstall the previous handler
  JASSERT(_real_sigaction(STOPSIGNAL, &old_act, NULL) != -1) (JASSERT_ERRNO)
  .Text("Error setting up signal handler");

  /* Now save all the signal handlers */
  JTRACE("saving signal handlers");
  for (sig = SIGRTMAX; sig > 0; --sig) {
    if (_real_syscall(SYS_rt_sigaction, sig, NULL, &sigactions[sig],
                      _NSIG / 8) < 0) {
      JASSERT(errno == EINVAL) (sig) (JASSERT_ERRNO)
      .Text("error saving signal action");
      memset(&sigactions[sig], 0, sizeof sigactions[sig]);
    }

    if (sigactions[sig].sa_handler != SIG_DFL) {
      JTRACE("saving signal handler (non-default) for") (sig);
    }
  }
}

/*****************************************************************************
 *
 *  Restore all saved signal handlers
 *
 *****************************************************************************/
void
SigInfo::restoreSigHandlers()
{
  int sig;

  JTRACE("restoring signal handlers");
  for (sig = SIGRTMAX; sig > 0; --sig) {
#ifdef VERBOSE_LOGGING
    JTRACE("restore signal handler for") (sig);
#endif // ifdef VERBOSE_LOGGING

    JASSERT(_real_syscall(SYS_rt_sigaction, sig, &sigactions[sig], NULL,
                          _NSIG / 8) == 0 || errno == EINVAL)
      (sig) (JASSERT_ERRNO)
    .Text("error restoring signal handler");
  }
}
