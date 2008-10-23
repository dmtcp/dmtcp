/***************************************************************************
 *   Copyright (C) 2008 by Jason Ansel                                     *
 *   jansel@ccs.neu.edu                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "../../mtcp/mtcp.h" //for MTCP_DEFAULT_SIGNAL

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

static bool checkpointSignalBlocked = false;

static int _determineMtcpSignal(){
  // this mimics the MTCP logic for determining signal number found in
  // mtcp_init()
  int sig = MTCP_DEFAULT_SIGNAL;
  char* endp = 0;
  const char* tmp = getenv("MTCP_SIGCKPT");
  if(tmp != NULL){
      sig = strtol(tmp, &endp, 0);
      if((errno != 0) || (tmp == endp))
        sig = MTCP_DEFAULT_SIGNAL;
      if(sig < 1 || sig > 31)
        sig = MTCP_DEFAULT_SIGNAL;
  }
  return sig;
}

static int bannedSignalNumber(){
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
  if (checkpointSignalBlocked == true) {
    *oldmask |= bannedMask;
  } else {
    *oldmask &= ~bannedMask;
  }

  if (how == SIG_BLOCK && (mask & bannedMask)) {
    checkpointSignalBlocked = true;
  } else if (how == SIG_SETMASK) {
    checkpointSignalBlocked = ((mask & bannedMask) != 0);
  }
}

static inline sigset_t patchPOSIXMask(const sigset_t* mask){
  JASSERT(mask != NULL);
  sigset_t t = *mask;

  sigdelset(&t, bannedSignalNumber());
  return t;
}

static inline void patchPOSIXUserMask(int how, const sigset_t *set, sigset_t *oldset)
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
  if(signum == bannedSignalNumber()){
    act = NULL;
  }
  return _real_rt_sigaction( signum, act, oldact);
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
  if (set != NULL) {
    sigset_t tmp = patchPOSIXMask(set);
    set = &tmp;
  }

  int ret = _real_sigprocmask( how, set, oldset );

  if (ret != -1) {
    patchPOSIXUserMask(how, set, oldset);
  }
  return ret;
}

EXTERNC int rt_sigprocmask(int how, const sigset_t *set, sigset_t *oldset){
  sigset_t tmp;
  if (set != NULL) {
    tmp = patchPOSIXMask(set);
    set = &tmp;
  }

  int ret = _real_rt_sigprocmask( how, &tmp, oldset );

  if (ret != -1) {
    patchPOSIXUserMask(how, set, oldset);
  }
  return ret;
}

EXTERNC int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldmask){
  sigset_t tmp;
  if (set != NULL) {
    tmp = patchPOSIXMask(set);
    set = &tmp;
  }

  int ret = _real_pthread_sigmask( how, &tmp, oldmask );

  /* We want a thread local version of the following code using __thread threadCheckpointSignalBlocked
     the threadPatchPOSIXUserMask function.
  */
  /*
  if (ret != -1) {
    patchPOSIXUserMask(how, set, oldmask);
  }
  */

  return ret;
}

