/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include "mtcpinterface.h"
#include "syscallwrappers.h"
#include  "../jalib/jassert.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#ifndef EXTERNC
#define EXTERNC extern "C"
#endif

//gah!!! signals API is redundant

static bool checkpointSignalBlockedForProcess = false;
static __thread bool checkpointSignalBlockedForThread = false;


static int bannedSignalNumber(){
  int _determineMtcpSignal(); // from signalwrappers.cpp
  const int cache = _determineMtcpSignal();
  return cache;
}

static int patchBSDMask(int mask){
  const int allowedMask = ~sigmask(bannedSignalNumber());
  return mask & allowedMask;
}

static inline void patchBSDUserMask(int how, const int mask, int *oldmask)
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

static inline sigset_t patchPOSIXMask(const sigset_t* mask){
  JASSERT(mask != NULL);
  sigset_t t = *mask;

  sigdelset(&t, bannedSignalNumber());
  return t;
}

static inline void patchPOSIXUserMaskWork(int how, const sigset_t *set, sigset_t *oldset,
                                          bool checkpointSignalBlocked)
{
  if (oldset != NULL) {
    if (checkpointSignalBlocked == true) {
      sigaddset(oldset, bannedSignalNumber());
    } else {
      sigdelset(oldset, bannedSignalNumber());
    }
  }

  if (set != NULL) {
    if (how == SIG_BLOCK && sigismember(set, bannedSignalNumber())) {
      checkpointSignalBlocked = true;
    } else if (how == SIG_UNBLOCK && sigismember(set,bannedSignalNumber())) {
      checkpointSignalBlocked = false;
    } else if (how == SIG_SETMASK) {
      checkpointSignalBlocked = sigismember(set, bannedSignalNumber());
    }
  }
}

static inline void patchPOSIXUserMask(int how, const sigset_t *set, sigset_t *oldset)
{
  patchPOSIXUserMaskWork(how, set, oldset, checkpointSignalBlockedForProcess);
}

/* Multi-threaded version of the above function */
static inline void patchPOSIXUserMaskMT(int how, const sigset_t *set, sigset_t *oldset)
{
  patchPOSIXUserMaskWork(how, set, oldset, checkpointSignalBlockedForThread);
}


//set the handler
EXTERNC sighandler_t signal(int signum, sighandler_t handler){
  if(signum == bannedSignalNumber()){
    return SIG_IGN;
  }
  return _real_signal( signum, handler );
}
EXTERNC int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact){
  if(signum == bannedSignalNumber()){
    act = NULL;
  }
  return _real_sigaction( signum, act, oldact);
}
EXTERNC int rt_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact){
  return sigaction (signum, act, oldact);
  //if(signum == bannedSignalNumber()){
  //  act = NULL;
  //}
  //return _real_rt_sigaction( signum, act, oldact);
}
EXTERNC int sigvec(int signum, const struct sigvec *vec, struct sigvec *ovec){
  if(signum == bannedSignalNumber()){
    vec = NULL;
  }
  return _real_sigvec( signum, vec, ovec );
}

//set the mask
EXTERNC int sigblock(int mask){
  int oldmask = _real_sigblock( patchBSDMask(mask) );

  patchBSDUserMask(SIG_BLOCK, mask, &oldmask);

  return oldmask;
}

EXTERNC int sigsetmask(int mask){
  int oldmask = _real_sigsetmask( patchBSDMask(mask) );

  patchBSDUserMask(SIG_SETMASK, mask, &oldmask);

  return oldmask;
}

EXTERNC int siggetmask(void){
  int oldmask =  _real_siggetmask();

  patchBSDUserMask(SIG_BLOCK, 0, &oldmask);

  return oldmask;
}

EXTERNC int sigprocmask(int how, const sigset_t *set, sigset_t *oldset){
  const sigset_t *orig = set;
  if (set != NULL) {
    sigset_t tmp = patchPOSIXMask(set);
    set = &tmp;
  }

  int ret = _real_sigprocmask( how, set, oldset );

  if (ret != -1) {
    patchPOSIXUserMask(how, orig, oldset);
  }
  return ret;
}

EXTERNC int rt_sigprocmask(int how, const sigset_t *set, sigset_t *oldset){
  return sigprocmask(how, set, oldset);
//  const sigset_t *orig = set;
//  if (set != NULL) {
//    sigset_t tmp = patchPOSIXMask(set);
//    set = &tmp;
//  }
//
//  int ret = _real_rt_sigprocmask( how, set, oldset );
//
//  if (ret != -1) {
//    patchPOSIXUserMask(how, orig, oldset);
//  }
//  return ret;
}

/*
 * This wrapper should be thread safe so we use the multithreaded version of
 * patchPOSIXUserMask function. This will declare the static variables with
 * __thread to make them thread local.
 */
EXTERNC int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldmask){
  const sigset_t *orig = set;
  if (set != NULL) {
    sigset_t tmp = patchPOSIXMask(set);
    set = &tmp;
  }

  int ret = _real_pthread_sigmask( how, set, oldmask );

  if (ret != -1) {
    patchPOSIXUserMaskMT(how, orig, oldmask);
  }

  return ret;
}

/*
 * TODO: man page says that sigwait is implemented via sigtimedwait, however
 * sigtimedwait can return EINTR (acc. to man page) whereas sigwait won't.
 * Should we make the wrappers for sigwait/sigtimedwait homogenious??
 *                                                          -- Kapil
 */
EXTERNC int sigwait(const sigset_t *set, int *sig) {
  if (set != NULL) {
    sigset_t tmp = patchPOSIXMask(set);
    set = &tmp;
  }

  int ret = _real_sigwait( set, sig );

  return ret;
}

/* 
 * In sigwaitinfo and sigtimedwait, it is not possible to differentiate between
 * a MTCP_SIGCKPT and any other signal (that is outside the given signal set)
 * that might have occured while executing the system call. These system call
 * will return -1 with errno set to EINTR.
 * To deal with the situation, we do not remove the MTCP_SIGCKPT from the
 * signal set (if it is present); instead, we check the return value and if it
 * turns out to be MTCP_SIGCKPT, we raise the signal once again for this
 * thread.
 * Also note that once sigwaitinfo/sigtimedwait returns MTCP_SIGCKPT, we won't
 * be receiving another MTCP_SIGCKPT until we have called _real_tkill due to
 * obvious reasons so I believe it is safe to call _real_gettid() here.
 *                                                              -- Kapil
 *
 * Update: 
 * Another way to write this wrapper would be to remove the STOPSIGNAL from the
 * user supplied 'set' and then call sigwaitinfo and then we won't need to
 * raise the STOPSIGNAL ourselves. However, there is a catch. sigwaitinfo will
 * return 'EINTR' if the wait was interrupted by a signal handler (STOPSIGNAL
 * in our case), thus we can either call sigwaitinfo again or return the error
 * to the user code; I would like to do the former.
 *                                                              -- Kapil
 */
EXTERNC int sigwaitinfo(const sigset_t *set, siginfo_t *info)
{
  int ret;
  while ( 1 ) {
    ret = _real_sigwaitinfo( set, info );
    if ( ret != bannedSignalNumber() ) {
      break;
    }
    raise(bannedSignalNumber());
  }
  return ret;
}

EXTERNC int sigtimedwait(const sigset_t *set, siginfo_t *info,
                         const struct timespec *timeout)
{
  int ret;
  while ( 1 ) {
    ret = _real_sigtimedwait( set, info, timeout );
    if ( ret != bannedSignalNumber() ) {
      break;
    }
    raise(bannedSignalNumber());
  }
  return ret;
}
