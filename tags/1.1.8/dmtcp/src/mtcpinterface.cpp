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
#include "../jalib/jassert.h"
#include "../jalib/jalloc.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include "constants.h"
#include "sockettable.h"
#include <unistd.h>
#include "uniquepid.h"
#include "dmtcpworker.h"
#include "virtualpidtable.h"
#include "protectedfds.h"
#include "../jalib/jfilesystem.h"
#include "../jalib/jconvert.h"

namespace
{
  static const char* REOPEN_MTCP = ( char* ) 0x1;

  static void* find_and_open_mtcp_so()
  {
    dmtcp::string mtcpso = jalib::Filesystem::FindHelperUtility ( "libmtcp.so" );
    void* handle = dlopen ( mtcpso.c_str(), RTLD_NOW );
    JASSERT ( handle != NULL ) ( mtcpso ).Text ( "failed to load libmtcp.so" );
    return handle;
  }

}

#ifdef EXTERNAL_SOCKET_HANDLING
static bool delayedCheckpoint = false;
#endif

extern "C" void* _get_mtcp_symbol ( const char* name )
{
  static void* theMtcpHandle = find_and_open_mtcp_so();

  if ( name == REOPEN_MTCP )
  {
    JTRACE ( "reopening libmtcp.so" ) ( theMtcpHandle );
    //must get ref count down to 0 so it is really unloaded
    for( int i=0; i<MAX_DLCLOSE_MTCP_CALLS; ++i){
      if(dlclose(theMtcpHandle) != 0){
        //failed call means it is unloaded
        JTRACE("dlclose(libmtcp.so) worked");
        break;
      }else{
        JTRACE("dlclose(libmtcp.so) decremented refcount");
      }
    }
    theMtcpHandle = find_and_open_mtcp_so();
    JTRACE ( "reopening libmtcp.so DONE" ) ( theMtcpHandle );
    return 0;
  }

  void* tmp = dlsym ( theMtcpHandle, name );
  JASSERT ( tmp != NULL ) ( name ).Text ( "failed to find libmtcp.so symbol" );

  //JTRACE("looking up libmtcp.so symbol")(name);

  return tmp;
}

extern "C"
{
  typedef int ( *t_mtcp_init ) ( char const *checkpointFilename, int interval, int clonenabledefault );
  typedef void ( *t_mtcp_set_callbacks ) ( void ( *sleep_between_ckpt ) ( int sec ),
          void ( *pre_ckpt ) ( char ** ckptFilename ),
          void ( *post_ckpt ) ( int is_restarting ),
          int  ( *ckpt_fd ) ( int fd ),
          void ( *write_ckpt_prefix ) ( int fd ),
          void ( *restore_virtual_pid_table) ());
  typedef int ( *t_mtcp_ok ) ( void );
  typedef void ( *t_mtcp_kill_ckpthread ) ( void );
}

static void callbackSleepBetweenCheckpoint ( int sec )
{
  dmtcp::DmtcpWorker::instance().waitForStage1Suspend();

  // After acquiring this lock, there shouldn't be any
  // allocations/deallocations and JASSERT/JTRACE/JWARNING/JNOTE etc.; the
  // process can deadlock.
  JALIB_CKPT_LOCK();
}

static void callbackPreCheckpoint( char ** ckptFilename )
{
  JALIB_CKPT_UNLOCK();

  //now user threads are stopped
  dmtcp::userHookTrampoline_preCkpt();
#ifdef EXTERNAL_SOCKET_HANDLING
  if (dmtcp::DmtcpWorker::instance().waitForStage2Checkpoint() == false) {
    char *nullDevice = (char *) "/dev/null";
    *ckptFilename = nullDevice;
    delayedCheckpoint = true;
  } else 
#else
  dmtcp::DmtcpWorker::instance().waitForStage2Checkpoint();
#endif
  {
    // If we don't modify *ckptFilename, then MTCP will continue to use
    //  its default filename, which was passed to it via our call to mtcp_init()
#ifdef UNIQUE_CHECKPOINT_FILENAMES
    dmtcp::UniquePid::ThisProcess().incrementGeneration();
    *ckptFilename = const_cast<char *>(dmtcp::UniquePid::checkpointFilename());
#endif
  }
}


static void callbackPostCheckpoint ( int isRestart )
{
  if ( isRestart )
  {
    dmtcp::DmtcpWorker::instance().postRestart();
    /* FIXME: There is not need to call sendCkptFilenameToCoordinator() but if
     *        we do not call it, it exposes a bug in dmtcp_coordinator.
     * BUG: The restarting process reconnects to the coordinator and the old
     *      connection is discarded. However, the coordinator doesn't discard
     *      the old connection right away (since it can't detect if the other
     *      end of the socket is closed). It is only discarded after the next
     *      read phase (coordinator trying to read from all the connected
     *      workers) in monitorSockets() is complete.  In this read phase, an
     *      error is recorded on the closed socket and in the next iteration of
     *      verifying the _dataSockets, this socket is closed and the
     *      corresponding entry in _dataSockets is freed. 
     *
     *      The problem occurrs when some other worker sends a status messages
     *      which should take the computation to the next barrier, but since
     *      the _to_be_disconnected socket is present, the minimum state is not
     *      reached unanimously and hence the coordinator doesn't raise the
     *      barrier. 
     *
     *      The bug was observed by Kapil in gettimeofday test program. It can
     *      be seen in 1 out of 3 restart attempts.
     *
     *      The current solution is to send a dummy message to coordinator here
     *      before sending a proper request.
     */
    dmtcp::DmtcpWorker::instance().sendCkptFilenameToCoordinator();
    dmtcp::DmtcpWorker::instance().waitForStage3Refill();
  }
  else
  {
#ifdef EXTERNAL_SOCKET_HANDLING
    if ( delayedCheckpoint == false )
#endif
    {
      dmtcp::DmtcpWorker::instance().sendCkptFilenameToCoordinator();
      dmtcp::DmtcpWorker::instance().waitForStage3Refill();
      dmtcp::DmtcpWorker::instance().waitForStage4Resume();
    }

    //now everything but threads are restored
    dmtcp::userHookTrampoline_postCkpt(isRestart);

    // After this point, the user threads will be unlocked in mtcp.c and will
    // resume their computation and so it is OK to set the process state to
    // RUNNING.
    dmtcp::WorkerState::setCurrentState( dmtcp::WorkerState::RUNNING );
  }
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

static void callbackRestoreVirtualPidTable ( )
{
  dmtcp::DmtcpWorker::instance().waitForStage4Resume();
  dmtcp::DmtcpWorker::instance().restoreVirtualPidTable();

  //now everything but threads are restored
  dmtcp::userHookTrampoline_postCkpt(true);

  // After this point, the user threads will be unlocked in mtcp.c and will
  // resume their computation and so it is OK to set the process state to
  // RUNNING.
  dmtcp::WorkerState::setCurrentState( dmtcp::WorkerState::RUNNING );
} 

void dmtcp::initializeMtcpEngine()
{
  int *dmtcp_exists_ptr =
    (int*) _get_mtcp_symbol( "dmtcp_exists" );
  *dmtcp_exists_ptr = 1;

  int *dmtcp_info_pid_virtualization_enabled_ptr = 
    (int*) _get_mtcp_symbol( "dmtcp_info_pid_virtualization_enabled" );

#ifdef PID_VIRTUALIZATION
  *dmtcp_info_pid_virtualization_enabled_ptr = 1;
#else
  *dmtcp_info_pid_virtualization_enabled_ptr = 0;
#endif 

  int *dmtcp_info_stderr_fd = 
    (int*) _get_mtcp_symbol( "dmtcp_info_stderr_fd" );
  *dmtcp_info_stderr_fd = PROTECTED_STDERR_FD;

#ifdef DEBUG
  int *dmtcp_info_jassertlog_fd = 
    (int*) _get_mtcp_symbol( "dmtcp_info_jassertlog_fd" );
  *dmtcp_info_jassertlog_fd = PROTECTED_JASSERTLOG_FD;
#endif 

  t_mtcp_set_callbacks setCallbks = ( t_mtcp_set_callbacks ) _get_mtcp_symbol ( "mtcp_set_callbacks" );

  t_mtcp_init init = ( t_mtcp_init ) _get_mtcp_symbol ( "mtcp_init" );
  t_mtcp_ok okFn = ( t_mtcp_ok ) _get_mtcp_symbol ( "mtcp_ok" );

  ( *setCallbks )( &callbackSleepBetweenCheckpoint
                 , &callbackPreCheckpoint
                 , &callbackPostCheckpoint
                 , &callbackShouldCkptFD
                 , &callbackWriteCkptPrefix
                 , &callbackRestoreVirtualPidTable);
  JTRACE ("Calling mtcp_init");
  ( *init ) ( UniquePid::checkpointFilename(),0xBadF00d,1 );
  ( *okFn ) ();

  JTRACE ( "mtcp_init complete" ) ( UniquePid::checkpointFilename() );
}

#ifdef PID_VIRTUALIZATION
struct ThreadArg {
  int ( *fn ) ( void *arg );
  void *arg;
  pid_t original_tid;
};

// bool isConflictingTid( pid_t tid )
// {
//   /*  If tid is not an original tid (return same tid), then there is no conflict
//    *  If tid is an original tid with the same current tid, then there 
//    *   is no conflict because that's us.
//    *  If tid is an original tid with a different current tid, then there 
//    *   is a conflict.
//    */
//   if (tid == dmtcp::VirtualPidTable::instance().originalToCurrentPid( tid ))
//     return false;
//   return true;
// }

int thread_start(void *arg)
{
  struct ThreadArg *threadArg = (struct ThreadArg*) arg;
  pid_t tid = _real_gettid();

  if ( dmtcp::VirtualPidTable::isConflictingPid ( tid ) ) {
    JTRACE ("Tid Conflict detected. Exiting Thread");
    return 0;
  }

  pid_t original_tid = threadArg -> original_tid;
  int (*fn) (void *) = threadArg->fn;
  void *thread_arg = threadArg->arg;

  // Free the memory which was previously allocated by calling JALLOC_HELPER_MALLOC
  JALLOC_HELPER_FREE(threadArg);

  if (original_tid == -1) {
    /* 
     * original tid is not known, which means this thread never existed before
     * checkpoint, so will insert the original_tid into virtualpidtable
     */
    original_tid = syscall(SYS_gettid);
    JASSERT ( tid == original_tid ) (tid) (original_tid) 
      .Text ( "syscall(SYS_gettid) and _real_gettid() returning different values for the newly created thread!" );
    dmtcp::VirtualPidTable::instance().insertTid ( original_tid );
  }

  dmtcp::VirtualPidTable::instance().updateMapping ( original_tid, tid );

  JTRACE ( "Calling user function" ) (original_tid);

  /* Thread finished initialization, its now safe for this thread to
   * participate in checkpoint. Decrement the unInitializedThreadCount in
   * DmtcpWorker.
   */ 
  dmtcp::DmtcpWorker::decrementUninitializedThreadCount();

  // return (*(threadArg->fn)) ( threadArg->arg );
  int result = (*fn) ( thread_arg );

  JTRACE ( "Thread returned:" ) (original_tid);
  
  /* 
   * This thread has finished its execution, do some cleanup on our part.
   *  erasing the original_tid entry from virtualpidtable
   */

  dmtcp::VirtualPidTable::instance().erase ( original_tid );
  dmtcp::VirtualPidTable::instance().eraseTid ( original_tid );
   
  return result;
}
#endif

//need to forward user clone
extern "C" int __clone ( int ( *fn ) ( void *arg ), void *child_stack, int flags, void *arg, int *parent_tidptr, struct user_desc *newtls, int *child_tidptr )
{
  /* 
   * struct MtcpRestartThreadArg 
   *
   * DMTCP requires the original_tids  of the threads being created during
   *  the RESTARTING phase. We use MtcpRestartThreadArg structure to pass
   *  the original_tid of the thread being created from MTCP to DMTCP. 
   *
   * actual clone call: clone (fn, child_stack, flags, void *, ... ) 
   * new clone call   : clone (fn, child_stack, flags, (struct MtcpRestartThreadArg *), ...)
   *
   * DMTCP automatically extracts arg from this structure and passes that
   * to the _real_clone call.
   * 
   * IMPORTANT NOTE: While updating, this structure must be kept in sync
   * with the structure defined with the same name in mtcp.c 
   */
  struct MtcpRestartThreadArg { 
    void * arg;
    pid_t original_tid;
  } *mtcpRestartThreadArg;

  typedef int ( *cloneptr ) ( int ( * ) ( void* ), void*, int, void*, int*, user_desc*, int* );
  // Don't make realclone statically initialized.  After a fork, some
  // loaders will relocate libmtcp.so on REOPEN_MTCP.  And we must then
  // call _get_mtcp_symbol again on the newly relocated libmtcp.so .
  cloneptr realclone = ( cloneptr ) _get_mtcp_symbol ( "__clone" );

  //JTRACE ( "forwarding user's clone call to mtcp" );

#ifndef PID_VIRTUALIZATION
  if ( dmtcp::WorkerState::currentState() != dmtcp::WorkerState::RUNNING )
  {
    mtcpRestartThreadArg = (struct MtcpRestartThreadArg *) arg;
    arg                  = mtcpRestartThreadArg -> arg;
  }

  JTRACE ( "forwarding user's clone call to mtcp" );
  return ( *realclone ) ( fn,child_stack,flags,arg,parent_tidptr,newtls,child_tidptr );
    
#else

  /* Acquire the wrapperExeution lock 
   * (Make sure to unlock before returning from this function)
   * Also increment the uninitialized thread count.
   */
  WRAPPER_EXECUTION_LOCK_LOCK();
  dmtcp::DmtcpWorker::incrementUninitializedThreadCount();


  pid_t originalTid = -1;

  if ( dmtcp::WorkerState::currentState() != dmtcp::WorkerState::RUNNING )
  {
    mtcpRestartThreadArg = (struct MtcpRestartThreadArg *) arg;
    arg         = mtcpRestartThreadArg -> arg;
    originalTid = mtcpRestartThreadArg -> original_tid;
  }

  // We have to use DMTCP specific memory allocator because using glibc:malloc
  // can interfere with user theads
  struct ThreadArg *threadArg = (struct ThreadArg *) JALLOC_HELPER_MALLOC (sizeof (struct ThreadArg));
  threadArg->fn = fn;
  threadArg->arg = arg;
  threadArg->original_tid = originalTid;

  int tid;
  
  while (1) {

    JTRACE ( "calling realclone" );

    if (originalTid == -1) {
      JTRACE ( "forwarding user's clone call to mtcp" );
      tid = ( *realclone ) ( thread_start,child_stack,flags,threadArg,parent_tidptr,newtls,child_tidptr );
    } else {
      tid = _real_clone ( thread_start,child_stack,flags,threadArg,parent_tidptr,newtls,child_tidptr );
    }

    if (tid == -1) {
      // Free the memory which was previously allocated by calling
      // JALLOC_HELPER_MALLOC
      JALLOC_HELPER_FREE ( threadArg );

      /* If clone() failed, decrement the uninitialized thread count, since
       * there is none
       */
      dmtcp::DmtcpWorker::decrementUninitializedThreadCount();
      break;
    }

    if ( dmtcp::VirtualPidTable::isConflictingPid ( tid ) ) {
    //if ( isConflictingTid ( tid ) ) {
      /* Issue a waittid for the newly created thread (if required.) */
      JTRACE ( "TID Conflict detected, creating a new child thread" ) ( tid );
    } else {
      JTRACE ("New Thread Created") (tid);
      if (originalTid != -1)
      {
        dmtcp::VirtualPidTable::instance().updateMapping ( originalTid, tid );
        dmtcp::VirtualPidTable::InsertIntoPidMapFile(originalTid, tid );
        tid = originalTid;
      } else {
        dmtcp::VirtualPidTable::instance().updateMapping ( tid, tid );
      }
      break;
    }
  }

  /* Release the wrapperExeution lock */
  WRAPPER_EXECUTION_LOCK_UNLOCK();

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

  // shutdownMtcpEngineOnFork will dlclose the old libmtcp.so and will
  //   dlopen a new libmtcp.so.  DmtcpWorker constructor then calls
  //   initializeMtcpEngine, which will then call mtcp_init.  We must close
  //   the old SIG_CKPT handler prior to this, so that MTCP and mtcp_init()
  //   don't think someone else is using their SIG_CKPT signal.
void dmtcp::shutdownMtcpEngineOnFork()
{
  // Remove our signal handler from our SIG_CKPT
  errno = 0;
  JWARNING (SIG_ERR != mtcp_real_signal(_determineMtcpSignal(), SIG_DFL))
           (_determineMtcpSignal())
           (JASSERT_ERRNO)
           .Text("failed to reset child's checkpoint signal on fork");
  _get_mtcp_symbol ( REOPEN_MTCP );
}

void dmtcp::killCkpthread()
{
  t_mtcp_kill_ckpthread kill_ckpthread = 
    (t_mtcp_kill_ckpthread) _get_mtcp_symbol( "mtcp_kill_ckpthread" );
  kill_ckpthread();
}
