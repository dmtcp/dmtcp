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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#ifndef EXTERNC
#define EXTERNC extern "C"
#endif

//gah!!! signals API is redundant


static int _determineMtcpSignal(){
  // this mimicks the MTCP logic for determining signal number found in
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
  static const int cache = _determineMtcpSignal();
  return cache;
}

static int patchBSDMask(int mask){
  static const int allowedMask = ~sigmask(bannedSignalNumber());
  return mask & allowedMask;
}

static inline sigset_t patchPOSIXMask(const sigset_t* mask){
  sigset_t t = *mask;
  sigdelset(&t, bannedSignalNumber());
  return t;
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
  return _real_sigblock( patchBSDMask(mask) );
}
EXTERNC int sigsetmask(int mask){
  return _real_sigsetmask( patchBSDMask(mask) );
}
EXTERNC int sigprocmask(int how, const sigset_t *set, sigset_t *oldset){
  sigset_t tmp = patchPOSIXMask(set);
  return _real_sigprocmask( how, &tmp, oldset );
}
EXTERNC int rt_sigprocmask(int how, const sigset_t *set, sigset_t *oldset){
  sigset_t tmp = patchPOSIXMask(set);
  return _real_rt_sigprocmask( how, &tmp, oldset );
}
EXTERNC int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldmask){
  sigset_t tmp = patchPOSIXMask(set);
  return _real_pthread_sigmask( how, &tmp, oldmask );
}


