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

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
#include "sockettable.h"
#include <unistd.h>
#include "uniquepid.h"
#include "dmtcpworker.h"
#include "virtualpidtable.h"
#include  "../jalib/jfilesystem.h"
#include  "../jalib/jconvert.h"
namespace
{
  static const char* REOPEN_MTCP = ( char* ) 0x1;

  static void* find_and_open_mtcp_so()
  {
    dmtcp::string mtcpso = jalib::Filesystem::FindHelperUtility ( "mtcp.so" );
    void* handle = dlopen ( mtcpso.c_str(), RTLD_NOW );
    JASSERT ( handle != NULL ) ( mtcpso ).Text ( "failed to load mtcp.so" );
    return handle;
  }

}

extern "C" void* _get_mtcp_symbol ( const char* name )
{
  static void* theMtcpHandle = find_and_open_mtcp_so();

  if ( name == REOPEN_MTCP )
  {
    JTRACE ( "reopening mtcp.so" ) ( theMtcpHandle );
    //must get ref count down to 0 so it is really unloaded
    for( int i=0; i<MAX_DLCLOSE_MTCP_CALLS; ++i){
      if(dlclose(theMtcpHandle) != 0){
        //failed call means it is unloaded
        JTRACE("dlclose(mtcp.so) worked");
        break;
      }else{
        JTRACE("dlclose(mtcp.so) decremented refcount");
      }
    }
    theMtcpHandle = find_and_open_mtcp_so();
    JTRACE ( "reopening mtcp.so DONE" ) ( theMtcpHandle );
    return 0;
  }

  void* tmp = dlsym ( theMtcpHandle, name );
  JASSERT ( tmp != NULL ) ( name ).Text ( "failed to find mtcp.so symbol" );

  //JTRACE("looking up mtcp.so symbol")(name);

  return tmp;
}

extern "C"
{
  typedef int ( *t_mtcp_init ) ( char const *checkpointfilename, int interval, int clonenabledefault );
  typedef void ( *t_mtcp_set_callbacks ) ( void ( *sleep_between_ckpt ) ( int sec ),
          void ( *pre_ckpt ) (),
          void ( *post_ckpt ) ( int is_restarting ),
          int  ( *ckpt_fd ) ( int fd ),
          void ( *write_ckpt_prefix ) ( int fd ));
  typedef int ( *t_mtcp_ok ) ( void );
}

static void callbackSleepBetweenCheckpoint ( int sec )
{
  dmtcp::DmtcpWorker::instance().waitForStage1Suspend();
}

static void callbackPreCheckpoint()
{
  //now user threads are stopped
  dmtcp::userHookTrampoline_preCkpt();
  dmtcp::DmtcpWorker::instance().waitForStage2Checkpoint();
}


static void callbackPostCheckpoint ( int isRestart )
{
  if ( isRestart )
  {
#ifdef DEBUG
    //logfile closed, must reopen it
    JASSERT_SET_LOGFILE ( jalib::XToString(getenv(ENV_VAR_TMPDIR))
			  + "/jassertlog." + jalib::XToString ( getpid() ) );
#endif
    dmtcp::DmtcpWorker::instance().postRestart();
  }
  else
  {
    JNOTE ( "checkpointed" ) ( dmtcp::UniquePid::checkpointFilename() );
  }
  dmtcp::DmtcpWorker::instance().waitForStage3Resume();
  //now everything but threads are restored
  dmtcp::userHookTrampoline_postCkpt(isRestart);
}

static int callbackShouldCkptFD ( int /*fd*/ )
{
  //mtcp should never checkpoint file descriptors;  dmtcp will handle it
  return 0;
}

static void callbackWriteCkptPrefix ( int fd )
{
  dmtcp::DmtcpWorker::instance().writeCheckpointPrefix(fd);
}

void dmtcp::initializeMtcpEngine()
{

  t_mtcp_set_callbacks setCallbks = ( t_mtcp_set_callbacks ) _get_mtcp_symbol ( "mtcp_set_callbacks" );

  t_mtcp_init init = ( t_mtcp_init ) _get_mtcp_symbol ( "mtcp_init" );
  t_mtcp_ok okFn = ( t_mtcp_ok ) _get_mtcp_symbol ( "mtcp_ok" );

  ( *setCallbks )( &callbackSleepBetweenCheckpoint
                 , &callbackPreCheckpoint
                 , &callbackPostCheckpoint
                 , &callbackShouldCkptFD
                 , &callbackWriteCkptPrefix);
  ( *init ) ( UniquePid::checkpointFilename(),0xBadF00d,1 );
  ( *okFn ) ();


  JTRACE ( "mtcp_init complete" ) ( UniquePid::checkpointFilename() );
}

#ifdef PID_VIRTUALIZATION
struct ThreadArg {
  int ( *fn ) ( void *arg );
  void *arg;
};

bool isConflictingTid( pid_t tid )
{
  /*  If tid is not an original tid (return same tid), then there is no conflict
   *  If tid is an original tid with the same current tid, then there 
   *   is no conflict because that's us.
   *  If tid is an original tid with a different current tid, then there 
   *   is a conflict.
   */
  if (tid == dmtcp::VirtualPidTable::Instance().originalToCurrentPid( tid ))
    return false;
  return true;
}

int thread_start(void *arg)
{
  struct ThreadArg *threadArg = (struct ThreadArg*) arg;
  pid_t tid = _real_gettid();

  if ( isConflictingTid ( tid ) ) {
    JTRACE ("Tid Conflict detected. Exiting Thread");

    return 0;
  }

  int (*fn) (void *) = threadArg->fn;

  JTRACE ( "Calling user function" );

  return (*fn) ( threadArg->arg );
}
#endif

//need to forward user clone
extern "C" int __clone ( int ( *fn ) ( void *arg ), void *child_stack, int flags, void *arg, int *parent_tidptr, struct user_desc *newtls, int *child_tidptr )
{
  typedef int ( *cloneptr ) ( int ( * ) ( void* ), void*, int, void*, int*, user_desc*, int* );
  // Don't make realclone statically initialized.  After a fork, some
  // loaders will relocate mtcp.so on REOPEN_MTCP.  And we must then
  // call _get_mtcp_symbol again on the newly relocated mtcp.so .
  cloneptr realclone = ( cloneptr ) _get_mtcp_symbol ( "__clone" );

  JTRACE ( "forwarding user's clone call to mtcp" );

#ifndef PID_VIRTUALIZATION

  return ( *realclone ) ( fn,child_stack,flags,arg,parent_tidptr,newtls,child_tidptr );
    
#else

  struct ThreadArg *threadArg = (struct ThreadArg *) malloc (sizeof (struct ThreadArg));
  threadArg->fn = fn;
  threadArg->arg = arg;

  int tid;
  
  while (1) {

    JTRACE ( "calling realclone" );

    tid = ( *realclone ) ( thread_start,child_stack,flags,threadArg,parent_tidptr,newtls,child_tidptr );
    
    if (tid == -1)
      break;

    if ( isConflictingTid ( tid ) ) {
      /* Issue a waittid for the newly created thread (if reqd.) */
      JTRACE ( "TID Conflict detected, creating a new child thread" ) ( tid );

    } else {
      JTRACE ("New Thread Created") (tid);
      dmtcp::VirtualPidTable::Instance().updateMapping( tid, tid );
      break;
    }
  }

  return tid;

#endif
}

// This is a copy of the same function in signalwrapers.cpp
// FEEL FREE TO CHANGE THIS TO USE THE ORIGINAL
//    signalwrappers.cpp:bannedSignalNumber() (but placed in dmtcp namespace ??)
#include "../../mtcp/mtcp.h" //for MTCP_DEFAULT_SIGNAL

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

  // This is called by the child process, only, via DmtcpWorker::resetOnFork().
  // We know that no one can send the SIG_CKPT signal, since if the
  //   the coordinator had requested a checkpoint, then either the
  //   the child successfully forked, or the thread of the parent process
  //   seeing the fork is processing the checkpoint signal first.  The
  //   latter case is no problem.  If the child successfully forked, then
  //   the SIG_CKPT sent by the checkpoint thread of the parent process prior
  //   to forking is too late to affect the child.  The checkpoint thread
  //   of the parent process may continue its own checkpointing, but
  //   the child process will not take part.  It's the coordinator's
  //   responsibility to then also send a checkpoint message to the checkpoint
  //   thread of the child.  DOES THE COORDINATOR DO THIS?
  // After a fork, only the child's user thread (which called fork())
  //   exists (and we know it's not our own checkpoint thread).  So, no
  //   thread is listening for a checkpoint command via the socket
  //   from the coordinator, _even_ if the coordinator decided to start
  //   the checkpoint immediately after the fork.  The child can't checkpoint
  //   until we call mtcp_init in the child, as described below.
  //   Note that resetOnFork() is the last thing done by the child before the
  //   fork wrapper returns.
  //   Jason, PLEASE VERIFY THE LOGIC ABOVE.  IT'S FOR THIS REASON, WE
  //   SHOULDN'T NEED delayCheckpointsLock.  Thanks.  - Gene

  // shutdownMtcpEngineOnFork will dlclose the old mtcp.so and will
  //   dlopen a new mtcp.so.  DmtcpWorker constructor then calls
  //   initializeMtcpEngine, which will then call mtcp_init.  We must close
  //   the old SIG_CKPT handler prior to this, so that MTCP and mtcp_init()
  //   don't think someone else is using their SIG_CKPT signal.
void dmtcp::shutdownMtcpEngineOnFork()
{
  // Remove our signal handler from our SIG_CKPT
  errno = 0;
  JWARNING (SIG_ERR != _real_signal(_determineMtcpSignal(), SIG_DFL))
           (_determineMtcpSignal())
           (JASSERT_ERRNO)
           .Text("failed to reset child's checkpoint signal on fork");
  _get_mtcp_symbol ( REOPEN_MTCP );
}

