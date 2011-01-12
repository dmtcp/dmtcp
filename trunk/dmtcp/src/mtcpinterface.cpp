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

#ifdef PTRACE
#include <sys/types.h>
#include <sys/ptrace.h>
#include <stdarg.h>
#include <linux/unistd.h>
#include <sys/syscall.h>
#include <fcntl.h>
#endif
#ifdef RECORD_REPLAY
#include "synchronizationlogging.h"
#endif

#ifdef RECORD_REPLAY
static inline void memfence() {  asm volatile ("mfence" ::: "memory"); }
#endif

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

// Note that mtcp.so is closed and re-opened (maybe in a different
//   location) at the time of fork.  Do not statically save the
//   return value of _get_mtcp_symbol across a fork.
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
  JASSERT ( tmp != NULL ) ( name )
    .Text ( "failed to find libmtcp.so symbol for 'name'\n"
            "Maybe try re-compiling MTCP:   (cd mtcp; make clean); make" );

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
    dmtcp::DmtcpWorker::instance().waitForStage3Refill(isRestart);
  }
  else
  {
#ifdef EXTERNAL_SOCKET_HANDLING
    if ( delayedCheckpoint == false )
#endif
    {
      dmtcp::DmtcpWorker::instance().sendCkptFilenameToCoordinator();
      dmtcp::DmtcpWorker::instance().waitForStage3Refill(isRestart);
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

#ifdef PTRACE
typedef pid_t ( *get_saved_pid_t) ( );
get_saved_pid_t get_saved_pid_ptr = NULL;

typedef int ( *get_saved_status_t) ( );
get_saved_pid_t get_saved_status_ptr = NULL;

typedef int ( *get_has_status_and_pid_t) ( );
get_has_status_and_pid_t get_has_status_and_pid_ptr = NULL;

typedef void ( *reset_pid_status_t) ( );
reset_pid_status_t reset_pid_status_ptr = NULL;

typedef void ( *set_singlestep_waited_on_t) ( pid_t superior, pid_t inferior, int value );
set_singlestep_waited_on_t set_singlestep_waited_on_ptr = NULL;

typedef int ( *get_is_waitpid_local_t ) ();
get_is_waitpid_local_t get_is_waitpid_local_ptr = NULL;

typedef int ( *get_is_ptrace_local_t ) ();
get_is_ptrace_local_t get_is_ptrace_local_ptr = NULL;

typedef void ( *unset_is_waitpid_local_t ) ();
unset_is_waitpid_local_t unset_is_waitpid_local_ptr = NULL;

typedef void ( *unset_is_ptrace_local_t ) ();
unset_is_ptrace_local_t unset_is_ptrace_local_ptr = NULL;

sigset_t signals_set;

typedef void ( *t_mtcp_init_thread_local ) ();
t_mtcp_init_thread_local mtcp_init_thread_local = NULL;

#define MTCP_DEFAULT_SIGNAL SIGUSR2
#endif 

void dmtcp::initializeMtcpEngine()
{
#ifdef PTRACE
  dmtcp::string tmpdir = dmtcp::UniquePid::getTmpDir();
  char *dir =
     (char*) _get_mtcp_symbol( "dir" );  
  sprintf(dir, "%s",  tmpdir.c_str());
#endif

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

  int *dmtcp_info_restore_working_directory =
    (int*) _get_mtcp_symbol( "dmtcp_info_restore_working_directory" );
  // DMTCP restores working dir only if --checkpoint-open-files invoked.
  // Later, we may offer the user a separate command line option for this.
  if (getenv(ENV_VAR_CKPT_OPEN_FILES))
    *dmtcp_info_restore_working_directory = 1;
  else
    *dmtcp_info_restore_working_directory = 0;

  t_mtcp_set_callbacks setCallbks =
    ( t_mtcp_set_callbacks ) _get_mtcp_symbol ( "mtcp_set_callbacks" );

  t_mtcp_init init = ( t_mtcp_init ) _get_mtcp_symbol ( "mtcp_init" );
  t_mtcp_ok okFn = ( t_mtcp_ok ) _get_mtcp_symbol ( "mtcp_ok" );

#ifdef PTRACE
  sigemptyset (&signals_set);
  // FIXME: Suppose the user did:  dmtcp_checkpoint --mtcp-checkpoint-signal ..
  sigaddset (&signals_set, MTCP_DEFAULT_SIGNAL);

  set_singlestep_waited_on_ptr =
   (set_singlestep_waited_on_t) _get_mtcp_symbol ( "set_singlestep_waited_on" );

  get_is_waitpid_local_ptr = ( get_is_waitpid_local_t ) _get_mtcp_symbol ( "get_is_waitpid_local" );

  get_is_ptrace_local_ptr = ( get_is_ptrace_local_t ) _get_mtcp_symbol ( "get_is_ptrace_local" );

  unset_is_waitpid_local_ptr = ( unset_is_waitpid_local_t ) _get_mtcp_symbol ( "unset_is_waitpid_local" );

  unset_is_ptrace_local_ptr = ( unset_is_ptrace_local_t ) _get_mtcp_symbol ( "unset_is_ptrace_local" );

  get_saved_pid_ptr = ( get_saved_pid_t ) _get_mtcp_symbol ( "get_saved_pid" );

  get_saved_status_ptr = ( get_saved_status_t ) _get_mtcp_symbol ( "get_saved_status" );

  get_has_status_and_pid_ptr = ( get_has_status_and_pid_t ) _get_mtcp_symbol ( "get_has_status_and_pid" );

  reset_pid_status_ptr = ( reset_pid_status_t ) _get_mtcp_symbol ( "reset_pid_status" );

  // Opting for the original format, not the one directly above.
  mtcp_init_thread_local = ( t_mtcp_init_thread_local ) _get_mtcp_symbol ( "init_thread_local" );
#endif

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
#ifdef RECORD_REPLAY
  long long int clone_id;
#endif
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
#ifdef RECORD_REPLAY
  if (dmtcp::WorkerState::currentState() == dmtcp::WorkerState::RUNNING) {
    my_clone_id = threadArg->clone_id;
    clone_id_to_tid_table[my_clone_id] = pthread_self();
  } else {
    JASSERT ( my_clone_id != 0 );
  }
#endif
  pid_t tid = _real_gettid();
  JTRACE ("In thread_start");

  typedef void ( *fill_in_pthread_t ) ( pid_t tid, pthread_t pth );
  // Don't make fill_in_pthread_ptr statically initialized.  After a fork, some
  // loaders will relocate libmtcp.so on REOPEN_MTCP.  And we must then
  // call _get_mtcp_symbol again on the newly relocated libmtcp.so .
  fill_in_pthread_t fill_in_pthread_ptr =
    ( fill_in_pthread_t ) _get_mtcp_symbol ( "fill_in_pthread" );

  fill_in_pthread_ptr (tid, pthread_self()); 
  
  if ( dmtcp::VirtualPidTable::isConflictingPid ( tid ) ) {
    JTRACE ("Tid Conflict detected. Exiting Thread");
    return 0;
  }

  pid_t original_tid = threadArg -> original_tid;
  int (*fn) (void *) = threadArg->fn;
  void *thread_arg = threadArg->arg;
#ifdef PTRACE
  mtcp_init_thread_local();
#endif

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
   * participate in checkpoint. Decrement the uninitializedThreadCount in
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
#ifdef RECORD_REPLAY
  reapThisThread();
#endif
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
  // Don't make _mtcp_clone_ptr statically initialized.  After a fork, some
  // loaders will relocate libmtcp.so on REOPEN_MTCP.  And we must then
  // call _get_mtcp_symbol again on the newly relocated libmtcp.so .
  cloneptr _mtcp_clone_ptr = ( cloneptr ) _get_mtcp_symbol ( "__clone" );

  //JTRACE ( "forwarding user's clone call to mtcp" );

#ifndef PID_VIRTUALIZATION
  if ( dmtcp::WorkerState::currentState() != dmtcp::WorkerState::RUNNING )
  {
    mtcpRestartThreadArg = (struct MtcpRestartThreadArg *) arg;
    arg                  = mtcpRestartThreadArg -> arg;
  }

  JTRACE ( "forwarding user's clone call to mtcp" );
  return ( *_mtcp_clone_ptr ) ( fn,child_stack,flags,arg,parent_tidptr,newtls,child_tidptr );

#else

  /* Acquire the wrapperExeution lock
   * (Make sure to unlock before returning from this function)
   * Also increment the uninitialized thread count.
   */
  WRAPPER_EXECUTION_DISABLE_CKPT();
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
#ifdef RECORD_REPLAY
  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::RUNNING ) {
    memfence();
    JTRACE ( "global_clone_counter" ) ( global_clone_counter );
    threadArg->clone_id = global_clone_counter;
    global_clone_counter++;
  }
#endif

  int tid;

  /*
   * originalTid == -1 indicates that the thread is being created for the first
   * time in the process i.e. we are not restoring from a checkpoint
   */

  while (1) {
    if (originalTid == -1) {
      /* First time thread creation */
      JTRACE ( "forwarding user's clone call to mtcp" );
      tid = ( *_mtcp_clone_ptr ) ( thread_start,child_stack,flags,threadArg,parent_tidptr,newtls,child_tidptr );
    } else {
      /* Recreating thread during restart */
      JTRACE ( "calling libc:__clone" );
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
        /* creating thread while restarting, we need to notify other processes */
        dmtcp::VirtualPidTable::instance().updateMapping ( originalTid, tid );
        dmtcp::VirtualPidTable::InsertIntoPidMapFile(originalTid, tid );
        tid = originalTid;
      } else {
        /* Newly created thread, insert mappings */
        dmtcp::VirtualPidTable::instance().updateMapping ( tid, tid );
      }
      break;
    }
  }

  /* Release the wrapperExeution lock */
  WRAPPER_EXECUTION_ENABLE_CKPT();

  return tid;

#endif
}

#ifdef RECORD_REPLAY
static int _almost_real_pthread_join (pthread_t thread, void **value_ptr)
{
  /* Wrap the call to _real_pthread_join() to make sure we call
     delete_thread_on_pthread_join(). */
  typedef void ( *delete_thread_fnc_t ) ( pthread_t );
  // Don't make delete_thread_fnc statically initialized.  After a fork, some
  // loaders will relocate libmtcp.so on REOPEN_MTCP.  And we must then
  // call _get_mtcp_symbol again on the newly relocated libmtcp.so .
  delete_thread_fnc_t delete_thread_fnc =
    (delete_thread_fnc_t) _get_mtcp_symbol("delete_thread_on_pthread_join");
  int retval = _real_pthread_join (thread, value_ptr);
  delete_thread_fnc (thread);
  return retval;
}
#endif // RECORD_REPLAY

extern "C" int pthread_join (pthread_t thread, void **value_ptr)
{
#ifdef RECORD_REPLAY
  /* We change things up a bit here. Since we don't allow the user's
     pthread_join() to have an effect, we don't call the mtcp
     "delete_thread_on_pthread_join()" function here unless we decide not to
     synchronize this call to pthread_join().

     We DO need to call it from the thread reaper reapThread(), however, which
     is in pthreadwrappers.cpp. */
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    int retval = _almost_real_pthread_join(thread, value_ptr);
    return retval;
  }

  int retval = 0;
  size_t stack_size;
  void *stack_addr;
  pthread_attr_t attr;
  log_entry_t my_entry = create_pthread_join_entry(my_clone_id,
      pthread_join_event, (unsigned long int)thread,
      (unsigned long int)value_ptr);
  log_entry_t my_return_entry = create_pthread_join_entry(my_clone_id,
      pthread_join_event_return, (unsigned long int)thread,
      (unsigned long int)value_ptr);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &pthread_join_turn_check);
    getNextLogEntry();
    while (pthread_join_retvals.find(thread) == pthread_join_retvals.end()) {
      usleep(100);
    }
    if (pthread_join_retvals.find(thread) != pthread_join_retvals.end()) {
      // We joined it as part of the thread reaping.
      if (value_ptr != NULL) {
        // If the user cares about the return value.
        retval = pthread_join_retvals[thread].retval;
        *value_ptr = pthread_join_retvals[thread].value_ptr;
        if (retval == -1) {
          errno = pthread_join_retvals[thread].my_errno;
        }
      }
      pthread_join_retvals.erase(thread);
    } else {
      JASSERT ( false ) .Text("A thread was not joined by reaper thread.");
    }
    waitForTurn(my_return_entry, &pthread_join_turn_check);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    while (pthread_join_retvals.find(thread) == pthread_join_retvals.end()) {
      usleep(100);
    }
    if (pthread_join_retvals.find(thread) != pthread_join_retvals.end()) {
      // We joined it as part of the thread reaping.
      if (value_ptr != NULL) {
        // If the user cares about the return value.
        retval = pthread_join_retvals[thread].retval;
        *value_ptr = pthread_join_retvals[thread].value_ptr;
        if (retval == -1) {
          errno = pthread_join_retvals[thread].my_errno;
        }
      }
      pthread_join_retvals.erase(thread);
    } else {
      JASSERT ( false ) .Text("A thread was not joined by reaper thread.");
    }
    addNextLogEntry(my_return_entry);
  }
  return retval;
#else
  typedef void ( *delete_thread_on_pthread_join_t) ( pthread_t pth );
  // Don't make delete_thread_on_pthread_join_ptr statically initialized.
  // After a fork, some
  // loaders will relocate libmtcp.so on REOPEN_MTCP.  And we must then
  // call _get_mtcp_symbol again on the newly relocated libmtcp.so .
  delete_thread_on_pthread_join_t delete_thread_on_pthread_join_ptr =
    ( delete_thread_on_pthread_join_t ) _get_mtcp_symbol ( "delete_thread_on_pthread_join" );
  int retval = _real_pthread_join (thread, value_ptr);
  delete_thread_on_pthread_join_ptr (thread);
  return retval;
#endif
}

#ifdef PTRACE
#ifndef PID_VIRTUALIZATION
#error "PTRACE can not be used without enabling PID-Virtualization"
#endif
// ptrace cannot work without pid virtualization.  If we're not using
// pid virtualization, then disable this wrapper around ptrace, and
// let the application call ptrace from libc.

// These constants must agree with the constants in mtcp/mtcp.c
#define PTRACE_UNSPECIFIED_COMMAND 0
#define PTRACE_SINGLESTEP_COMMAND 1
#define PTRACE_CONTINUE_COMMAND 2

// FIXME
// This is a wrapper for ptrace.  mtcpinterface.cpp is a file for DMTCP
//    to start mtcp.so (and call mtcp_init), and to stop mtcp.so
// This wrapper function should be placed with other wrappers
//    on in a new file called mtcpwrapper.cpp or ptracewrapper.cpp
//    or mtcpwrapper.c .
// Probably most of the functions from __clone to ptrace and to
//    the special __libc_memalign redirection could be moved
//    to a new file.  - Gene
extern "C" long ptrace ( enum __ptrace_request request, ... )
{
  va_list ap;
  pid_t pid;
  void *addr;
  void *data;

  pid_t superior;
  pid_t inferior;

  long ptrace_ret;

  typedef void ( *writeptraceinfo_t ) ( pid_t superior, pid_t inferior );
  // Don't make writeptraceinfo_ptr statically initialized.  After a fork, some
  // loaders will relocate libmtcp.so on REOPEN_MTCP.  And we must then
  // call _get_mtcp_symbol again on the newly relocated libmtcp.so .
  writeptraceinfo_t writeptraceinfo_ptr = ( writeptraceinfo_t ) _get_mtcp_symbol ( "writeptraceinfo" );

  typedef void ( *write_info_to_file_t ) ( int file, pid_t superior, pid_t inferior );
  // Don't make write_info_to_file_ptr statically initialized.  After a fork,
  // some loaders will relocate libmtcp.so on REOPEN_MTCP.  And we must then
  // call _get_mtcp_symbol again on the newly relocated libmtcp.so .
  write_info_to_file_t write_info_to_file_ptr = ( write_info_to_file_t ) _get_mtcp_symbol ( "write_info_to_file" );

  typedef void ( *remove_from_ptrace_pairs_t) ( pid_t superior, pid_t inferior );
  // Don't make remove_from_ptrace_pairs_ptr statically initialized.  After a
  // fork,
  // some loaders will relocate libmtcp.so on REOPEN_MTCP.  And we must then
  // call _get_mtcp_symbol again on the newly relocated libmtcp.so .
  remove_from_ptrace_pairs_t remove_from_ptrace_pairs_ptr =
                           ( remove_from_ptrace_pairs_t ) _get_mtcp_symbol ( "remove_from_ptrace_pairs" );

  typedef void ( *handle_command_t ) ( pid_t superior, pid_t inferior, int last_command );
  // Don't make handle_command_ptr statically initialized.  After a fork,
  // some loaders will relocate libmtcp.so on REOPEN_MTCP.  And we must then
  // call _get_mtcp_symbol again on the newly relocated libmtcp.so .
  handle_command_t handle_command_ptr = ( handle_command_t ) _get_mtcp_symbol ( "handle_command" );


  va_start( ap, request );
  pid = va_arg( ap, pid_t );
  addr = va_arg( ap, void * );
  data = va_arg( ap, void * );
  va_end( ap );
  superior = syscall( SYS_gettid );
  inferior = pid;

  switch (request) {
    case PTRACE_ATTACH: {
     if (!get_is_ptrace_local_ptr ()) writeptraceinfo_ptr ( superior, inferior );
      else unset_is_ptrace_local_ptr ();
      break;
    }
    case PTRACE_TRACEME: {
      superior = getppid();
      inferior = syscall( SYS_gettid );
      writeptraceinfo_ptr( superior, inferior );
      break;
    }
    case PTRACE_DETACH: {
     if (!get_is_ptrace_local_ptr ()) remove_from_ptrace_pairs_ptr ( superior, inferior );
     else unset_is_ptrace_local_ptr ();
     break;
    }
    case PTRACE_CONT: {
     if (!get_is_ptrace_local_ptr ()) handle_command_ptr ( superior, inferior, PTRACE_CONTINUE_COMMAND );
     else unset_is_ptrace_local_ptr ();
     break;
    }
    case PTRACE_SINGLESTEP: {
     pid = dmtcp::VirtualPidTable::instance().originalToCurrentPid( pid );
     if (!get_is_ptrace_local_ptr ()) {
        if (_real_pthread_sigmask (SIG_BLOCK, &signals_set, NULL) != 0) {
                perror ("waitpid wrapper");
                 exit(-1);
        }
        handle_command_ptr (superior, inferior, PTRACE_SINGLESTEP_COMMAND);
        ptrace_ret =  _real_ptrace (request, pid, addr, data);
        if (_real_pthread_sigmask (SIG_UNBLOCK, &signals_set, NULL) != 0) {
                perror ("waitpid wrapper");
                exit(-1);
        }
     }
     else {
        ptrace_ret =  _real_ptrace (request, pid, addr, data);
        unset_is_ptrace_local_ptr ();
     }
     break;
    }
    case PTRACE_SETOPTIONS: {
     write_info_to_file_ptr (1, superior, inferior);
     break;
    }
    default: {
      break;
    }
  }

  /* TODO: We might want to check the return value in certain cases */

  if ( request != PTRACE_SINGLESTEP ) {
        pid = dmtcp::VirtualPidTable::instance().originalToCurrentPid( pid );
        ptrace_ret =  _real_ptrace( request, pid, addr, data );
  }

  return ptrace_ret;
}
#endif

#ifdef PTRACE
# ifndef RECORD_REPLAY
   // RECORD_REPLAY defines its own __libc_memalign
   //   wrapper.  So, we won't interfere with it here.
#  include <malloc.h>
// This is needed to fix what is arguably a bug in libdl-2.10.so
//   (and probably extending from versions 2.4 at least through 2.11).
// In libdl-2.10.so dl-tls.c:allocate_and_init  calls __libc_memalign
//    but dl-tls.c:dl_update_slotinfo just calls free .
// So, TLS is allocated by libc malloc and can be freed by a malloc library
//    defined by user.  This is a bug.
// This happens only in a multi-threaded programs for which TLS is allocated.
// So, we intercept __libc_memalign and point it to memalign to have a match.
// We do the same for __libc_free.  libdl.so doesn't currently define
//    __libc_free, but the code must be prepared to accept this.
// An alternative to defining __libc_memalign would have been using
//    the glibc __memalign_hook() function.
extern "C"
void *__libc_memalign(size_t boundary, size_t size) {
  return memalign(boundary, size);
}
// libdl.so doesn't define __libc_free, but in case it does in the future ...
extern "C"
void __libc_free(void * ptr) {
  free(ptr);
}
# endif
#endif


// FIXME
// Starting here, we can continue with files for mtcpinterface.cpp - Gene

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
  int _determineMtcpSignal(); // from signalwrappers.cpp
  // Remove our signal handler from our SIG_CKPT
  errno = 0;
  JWARNING (SIG_ERR != _real_signal(_determineMtcpSignal(), SIG_DFL))
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
