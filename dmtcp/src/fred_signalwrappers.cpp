/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include "dmtcpworker.h"
#include "mtcpinterface.h"
#include "syscallwrappers.h"
#include  "../jalib/jassert.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "dmtcpmodule.h"
#ifdef RECORD_REPLAY
#include "fred_wrappers.h"
#include "synchronizationlogging.h"
#include "../jalib/jfilesystem.h"

#ifndef EXTERNC
#define EXTERNC extern "C"
#endif

static dmtcp::map<int, sighandler_t> user_sig_handlers;

static inline sigset_t patchPOSIXMask(const sigset_t* mask){
  JASSERT(mask != NULL);
  sigset_t t = *mask;

  sigdelset(&t, dmtcp_get_ckpt_signal());
  return t;
}

static void sig_handler_wrapper(int sig)
{
  // FIXME: Why is the following  commented out?
  /*void *return_addr = GET_RETURN_ADDRESS();
if (!shouldSynchronize(return_addr)) {
    kill(getpid(), SIGSEGV);
    return (*user_sig_handlers[sig]) (sig);
    }*/
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    JASSERT ( false ) .Text("don't want this");
    return (*user_sig_handlers[sig]) (sig);
  }
  int retval = 0;
  log_entry_t my_entry = create_signal_handler_entry(my_clone_id,
                                                     signal_handler_event, sig);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY(signal_handler);
    (*user_sig_handlers[sig]) (sig);
  } else if (SYNC_IS_RECORD) {
    (*user_sig_handlers[sig]) (sig);
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
}

//set the handler
EXTERNC sighandler_t signal(int signum, sighandler_t handler)
{
  if(signum == dmtcp_get_ckpt_signal()){
    return SIG_IGN;
  }
  WRAPPER_HEADER_RAW(sighandler_t, signal, _real_signal, signum, handler);
  // We don't need to log and replay this call, we just need to note the user's
  // signal handler so that our signal handler wrapper can call that function.
  user_sig_handlers[signum] = handler;
  return _real_signal( signum, sig_handler_wrapper );
}


EXTERNC sighandler_t sigset(int sig, sighandler_t disp)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr) ||
      jalib::Filesystem::GetProgramName() == "gdb") {
    // Don't use our wrapper for non-user signal() calls:
    return _real_sigset (sig, disp);
  } else {
    // We don't need to log and replay this call, we just need to note the
    // user's  signal handler so that our signal handler wrapper can call that
    // function.
    user_sig_handlers[sig] = disp;
    return _real_sigset( sig, sig_handler_wrapper );
  }
}

EXTERNC int sigaction(int signum, const struct sigaction *act,
                      struct sigaction *oldact)
{
  if(signum == dmtcp_get_ckpt_signal()){
    act = NULL;
  }
  void *return_addr = GET_RETURN_ADDRESS();
  if (act != NULL && shouldSynchronize(return_addr) &&
      jalib::Filesystem::GetProgramName() != "gdb") {
    struct sigaction newact;
    memset(&newact, 0, sizeof(struct sigaction));
    if (act->sa_handler == SIG_DFL || act->sa_handler == SIG_IGN) {
      // Remove it from our map.
      user_sig_handlers.erase(signum);
    } else {
      // Save user's signal handler
      if (act->sa_flags & SA_SIGINFO) {
        JASSERT ( false ).Text("Unimplemented.");
        //user_sig_handlers[signum] = act->sa_sigaction;
        //newact.sa_sigaction = act->sa_sigaction;
      } else {
        user_sig_handlers[signum] = act->sa_handler;
        newact.sa_handler = &sig_handler_wrapper;
      }
      // Create our own action with our own signal handler, but copy user's
      // other fields.
      newact.sa_mask = act->sa_mask;
      newact.sa_flags = act->sa_flags;
      newact.sa_restorer = act->sa_restorer;
    }
    return _real_sigaction( signum, &newact, oldact);
  } else {
    return _real_sigaction( signum, act, oldact);
  }
}

EXTERNC int sigwait(const sigset_t *set, int *sig)
{
  if (set != NULL) {
    sigset_t tmp = patchPOSIXMask(set);
    set = &tmp;
  }
  WRAPPER_HEADER(int, sigwait, _real_sigwait, set, sig);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(sigwait);
    if (sig != NULL) {
      *sig = GET_FIELD(currentLogEntry, sigwait, sig);
    }
    WRAPPER_REPLAY_END(sigwait);
  } else if (SYNC_IS_RECORD) {
    retval = _real_sigwait(set, sig);
    if (sig != NULL) {
      SET_FIELD2(my_entry, sigwait, sig, *sig);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}
#endif
