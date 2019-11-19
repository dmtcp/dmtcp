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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "../jalib/jassert.h"
#include "dmtcpworker.h"
#include "syscallwrappers.h"

#ifndef EXTERNC
# define EXTERNC extern "C"
#endif // ifndef EXTERNC

using namespace dmtcp;

// gah!!! signals API is redundant

static bool checkpointSignalBlockedForProcess = false;
static __thread bool checkpointSignalBlockedForThread = false;
static int stopSignal = -1;


static int
bannedSignalNumber()
{
  if (stopSignal == -1) {
    stopSignal = DmtcpWorker::determineCkptSignal();

    // On some systems, the ckpt-signal may be blocked by default. Unblock it
    // now.
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, stopSignal);
    JASSERT(_real_pthread_sigmask(SIG_UNBLOCK, &set, NULL) == 0)
      (strerror(_real_pthread_sigmask(SIG_UNBLOCK, &set, NULL))) (stopSignal);
  }
  return stopSignal;
}

static int
patchBSDMask(int mask)
{
  const int allowedMask = ~sigmask(bannedSignalNumber());

  return mask & allowedMask;
}

static inline void
patchBSDUserMask(int how, const int mask, int *oldmask)
{
  const int bannedMask = sigmask(bannedSignalNumber());

  if (checkpointSignalBlockedForProcess == true) {
    *oldmask |= bannedMask;
  } else {
    *oldmask &= ~bannedMask;
  }

  if (how == SIG_BLOCK && (mask & bannedMask)) {
    checkpointSignalBlockedForProcess = true;
  } else if (how == SIG_SETMASK) {
    checkpointSignalBlockedForProcess = ((mask & bannedMask) != 0);
  }
}

static inline sigset_t
patchPOSIXMask(const sigset_t *mask)
{
  JASSERT(mask != NULL);
  sigset_t t = *mask;

  sigdelset(&t, bannedSignalNumber());
  return t;
}

static inline void
patchPOSIXUserMaskWork(int how,
                       const sigset_t *set,
                       sigset_t *oldset,
                       bool *checkpointSignalBlocked)
{
  if (oldset != NULL) {
    if (*checkpointSignalBlocked == true) {
      sigaddset(oldset, bannedSignalNumber());
    } else {
      sigdelset(oldset, bannedSignalNumber());
    }
  }

  if (set != NULL) {
    int bannedSignaIsMember = sigismember(set, bannedSignalNumber());
    if (how == SIG_BLOCK && bannedSignaIsMember) {
      *checkpointSignalBlocked = true;
    } else if (how == SIG_UNBLOCK && bannedSignaIsMember) {
      *checkpointSignalBlocked = false;
    } else if (how == SIG_SETMASK) {
      *checkpointSignalBlocked = bannedSignaIsMember;
    }
  }
}

static inline void
patchPOSIXUserMask(int how, const sigset_t *set, sigset_t *oldset)
{
  patchPOSIXUserMaskWork(how, set, oldset, &checkpointSignalBlockedForProcess);
}

/* Multi-threaded version of the above function */
static inline void
patchPOSIXUserMaskMT(int how, const sigset_t *set, sigset_t *oldset)
{
  patchPOSIXUserMaskWork(how, set, oldset, &checkpointSignalBlockedForThread);
}

// set the handler
EXTERNC sighandler_t
signal(int signum, sighandler_t handler)
{
  if (signum == bannedSignalNumber()) {
    return SIG_IGN;
  }
  return _real_signal(signum, handler);
}

EXTERNC int
sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
  if (signum == bannedSignalNumber() && act != NULL) {
    JWARNING(false)(
      "Application trying to use DMTCP's signal for it's own use.\n"
      "  You should employ a different signal by setting the\n"
      "  environment variable DMTCP_SIGCKPT to the number\n"
      "  of the signal that DMTCP should use for checkpointing.")
      (stopSignal);
    act = NULL;
  }
  return _real_sigaction(signum, act, oldact);
}

EXTERNC int
rt_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
  return sigaction(signum, act, oldact);

  // if(signum == bannedSignalNumber()) {
  // act = NULL;
  // }
  // return _real_rt_sigaction( signum, act, oldact);
}

#if !__GLIBC_PREREQ(2, 21)
EXTERNC int
sigvec(int signum, const struct sigvec *vec, struct sigvec *ovec)
{
  if (signum == bannedSignalNumber()) {
    vec = NULL;
  }
  return _real_sigvec(signum, vec, ovec);
}
#endif // if !__GLIBC_PREREQ(2, 21)

// set the mask
EXTERNC int
sigblock(int mask)
{
  int oldmask = _real_sigblock(patchBSDMask(mask));

  patchBSDUserMask(SIG_BLOCK, mask, &oldmask);
  return oldmask;
}

EXTERNC int
sigsetmask(int mask)
{
  int oldmask = _real_sigsetmask(patchBSDMask(mask));

  patchBSDUserMask(SIG_SETMASK, mask, &oldmask);
  return oldmask;
}

EXTERNC int
siggetmask(void)
{
  int oldmask = _real_siggetmask();

  patchBSDUserMask(SIG_BLOCK, 0, &oldmask);
  return oldmask;
}

EXTERNC int
sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
  sigset_t tmp;
  const sigset_t *orig = set;

  if (set != NULL) {
    tmp = patchPOSIXMask(set);
    set = &tmp;
  }

  int ret = _real_sigprocmask(how, set, oldset);

  if (ret != -1) {
    patchPOSIXUserMask(how, orig, oldset);
  }
  return ret;
}

EXTERNC int
rt_sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
  return sigprocmask(how, set, oldset);

  // sigset_t tmp;
  // const sigset_t *orig = set;
  // if (set != NULL) {
  // tmp = patchPOSIXMask(set);
  // set = &tmp;
  // }
  //
  // int ret = _real_rt_sigprocmask( how, set, oldset );
  //
  // if (ret != -1) {
  // patchPOSIXUserMask(how, orig, oldset);
  // }
  // return ret;
}

EXTERNC int
sigsuspend(const sigset_t *mask)
{
  // Suppress nonnull_compare errors.
  const sigset_t *newMask = mask;
  sigset_t tmp;

  if (newMask != NULL) {
    tmp = patchPOSIXMask(newMask);
    newMask = &tmp;
  }

  return _real_sigsuspend(newMask);
}

/* FIXME: Reverify the logic of the following four wrappers:
 *          sighold, sigignore, sigrelse, sigpause
 *        These are deprecated according to manpage.
 */
EXTERNC int
sighold(int sig)
{
  if (sig == bannedSignalNumber()) {
    return 0;
  }
  return _real_sighold(sig);
}

EXTERNC int
sigignore(int sig)
{
  if (sig == bannedSignalNumber()) {
    return 0;
  }
  return _real_sigignore(sig);
}

EXTERNC int
sigrelse(int sig)
{
  if (sig == bannedSignalNumber()) {
    return 0;
  }
  return _real_sigrelse(sig);
}

// signal.h can define sigpause as a macro expanding into __sigpause
// That takes an extra arg to handle sigmask (BSD) or signal (System V)
// So, we wrap both version.
EXTERNC int
__sigpause(int __sig_or_mask, int __is_sig)
{
  JWARNING(false)
  .Text("This function is deprecated. Use sigsuspend instead." \
        "  The DMTCP wrappers for this function may not be fully tested");
  return _real__sigpause(__sig_or_mask, __is_sig);
}

// Remove any possible macro expansion from signal.h
// sigpause must not be invoked after this in this file.
#undef sigpause
EXTERNC int
sigpause(int sig)
{
  JWARNING(false)
  .Text("This function is deprecated. Use sigsuspend instead." \
        "  The DMTCP wrappers for this function may not be fully tested");
  return _real_sigpause(sig);
}

/*
 * This wrapper should be thread safe so we use the multithreaded version of
 * patchPOSIXUserMask function. This will declare the static variables with
 * __thread to make them thread local.
 */
EXTERNC int
pthread_sigmask(int how, const sigset_t *set, sigset_t *oldmask)
{
  const sigset_t *orig = set;
  sigset_t tmp;

  if (set != NULL) {
    tmp = patchPOSIXMask(set);
    set = &tmp;
  }

  int ret = _real_pthread_sigmask(how, set, oldmask);

  if (ret != -1) {
    patchPOSIXUserMaskMT(how, orig, oldmask);
  }

  return ret;
}

/*
 * TODO: man page says that sigwait is implemented via sigtimedwait. However,
 * sigtimedwait can return EINTR (acc. to man page) whereas sigwait won't.
 * Should we make the wrappers for sigwait/sigtimedwait homogeneous??
 *                                                          -- Kapil
 */
EXTERNC int
sigwait(const sigset_t *set, int *sig)
{
  // Suppress nonnull_compare errors.
  const sigset_t *newSet = set;
  sigset_t tmp;

  if (newSet != NULL) {
    tmp = patchPOSIXMask(newSet);
    newSet = &tmp;
  }

  int ret = _real_sigwait(newSet, sig);

  return ret;
}

/*
 * In sigwaitinfo and sigtimedwait, it is not possible to differentiate between
 * a DMTCP_SIGCKPT and any other signal (that is outside the given signal set),
 * which might have occurred while executing the system call. These system calls
 * will return -1 with errno set to EINTR.
 * To deal with the situation, we do not remove the DMTCP_SIGCKPT from the
 * signal set (if it is present); instead, we check the return value and if it
 * turns out to be DMTCP_SIGCKPT, we raise the signal once again for this
 * thread.
 * Also note that once sigwaitinfo/sigtimedwait returns DMTCP_SIGCKPT, we won't
 * be receiving another DMTCP_SIGCKPT until we have called _real_tkill due to
 * obvious reasons so I believe it is safe to call _real_gettid() here.
 *                                                              -- Kapil
 *
 * Update:
 * Another way to write this wrapper would be to remove the STOPSIGNAL from the
 * user supplied 'set' and then call sigwaitinfo and then we won't need to
 * raise the STOPSIGNAL ourselves. However, there is a catch. sigwaitinfo will
 * return 'EINTR' if the wait was interrupted by a signal handler (STOPSIGNAL
 * in our case). Thus, we can either call sigwaitinfo again or return the error
 * to the user code; I would like to do the former.
 *                                                              -- Kapil
 */
EXTERNC int
sigwaitinfo(const sigset_t *set, siginfo_t *info)
{
  int ret;

  while (1) {
    ret = _real_sigwaitinfo(set, info);
    if (ret != bannedSignalNumber()) {
      break;
    }
    raise(bannedSignalNumber());
  }
  return ret;
}

EXTERNC int
sigtimedwait(const sigset_t *set,
             siginfo_t *info,
             const struct timespec *timeout)
{
  int ret;

  while (1) {
    ret = _real_sigtimedwait(set, info, timeout);
    if (ret != bannedSignalNumber()) {
      break;
    }
    raise(bannedSignalNumber());
  }
  return ret;
}
