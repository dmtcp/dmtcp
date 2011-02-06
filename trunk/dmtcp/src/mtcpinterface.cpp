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

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/prctl.h>

#include "constants.h"
#include "mtcpinterface.h"
#include "syscallwrappers.h"
#include "uniquepid.h"
#include "dmtcpworker.h"
#include "virtualpidtable.h"
#include "protectedfds.h"
#include "sockettable.h"
#include "ptracewrapper.h"

#include "../jalib/jfilesystem.h"
#include "../jalib/jconvert.h"
#include "../jalib/jassert.h"
#include "../jalib/jalloc.h"


#ifdef RECORD_REPLAY
#include "synchronizationlogging.h"
#endif


#ifdef RECORD_REPLAY
static inline void memfence() {  asm volatile ("mfence" ::: "memory"); }
#endif

#ifdef __x86_64
# define MTCP_RESTORE_STACK_BASE ((char*)0x7FFFFFFFF000L);
#else
# define MTCP_RESTORE_STACK_BASE ((char*)0xC0000000L);
#endif

static char prctlPrgName[22] = {0};
static void prctlGetProcessName();
static void prctlRestoreProcessName();

static char *_mtcpRestoreArgvStartAddr = NULL;
static bool restoreArgvAfterRestart(char* mtcpRestoreArgvStartAddr);
static void unmapRestoreArgv();

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

#ifdef PTRACE
__attribute__ ((visibility ("hidden"))) t_mtcp_get_ptrace_waitpid_info
  mtcp_get_ptrace_waitpid_info = NULL;

__attribute__ ((visibility ("hidden"))) t_mtcp_init_thread_local
  mtcp_init_thread_local;

__attribute__ ((visibility ("hidden"))) sigset_t signals_set;
#endif

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
  typedef int ( *t_mtcp_init ) ( char const *checkpointFilename,
                                 int interval,
                                 int clonenabledefault );
  typedef void ( *t_mtcp_set_callbacks ) (
          void ( *sleep_between_ckpt ) ( int sec ),
          void ( *pre_ckpt ) ( char ** ckptFilename ),
          void ( *post_ckpt ) ( int isRestarting,
                                char* mtcpRestoreArgvStartAddr),
          int  ( *ckpt_fd ) ( int fd ),
          void ( *write_ckpt_prefix ) ( int fd ),
          void ( *restore_virtual_pid_table) ()
#ifdef PTRACE
        , struct ptrace_info (*get_next_ptrace_info)(int index),
          void (*ptrace_info_list_command)(struct cmd_info cmd),
          void (*jalib_ckpt_unlock)()
#endif
  );
  typedef int ( *t_mtcp_ok ) ( void );
  typedef void ( *t_mtcp_kill_ckpthread ) ( void );
}

static void callbackSleepBetweenCheckpoint ( int sec )
{
  dmtcp::DmtcpWorker::instance().waitForStage1Suspend();

  prctlGetProcessName();
  unmapRestoreArgv();

  // After acquiring this lock, there shouldn't be any
  // allocations/deallocations and JASSERT/JTRACE/JWARNING/JNOTE etc.; the
  // process can deadlock.
  JALIB_CKPT_LOCK();
}

#ifdef PTRACE
static struct ptrace_info callbackGetNextPtraceInfo (int index)
{
  return get_next_ptrace_info(index);
}

static void callbackPtraceInfoListCommand (struct cmd_info cmd)
{
  ptrace_info_list_command(cmd);
}

static void callbackJalibCkptUnlock ()
{
  JALIB_CKPT_UNLOCK();
}
#endif

static void callbackPreCheckpoint( char ** ckptFilename )
{
/* In the case of PTRACE, we have already called JALIB_CKPT_UNLOCK. */
#ifndef PTRACE
  JALIB_CKPT_UNLOCK();
#endif

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


static void callbackPostCheckpoint ( int isRestart,
                                     char* mtcpRestoreArgvStartAddr)
{
  if ( isRestart )
  {
    if (restoreArgvAfterRestart(mtcpRestoreArgvStartAddr)) {
      prctlRestoreProcessName();
    }


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

void dmtcp::initializeMtcpEngine()
{
#ifdef PTRACE
  dmtcp::string tmpdir = dmtcp::UniquePid::getTmpDir();
  char *dmtcp_tmp_dir =
     (char*) _get_mtcp_symbol( "dmtcp_tmp_dir" );  
  sprintf(dmtcp_tmp_dir, "%s",  tmpdir.c_str());
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

  mtcp_get_ptrace_waitpid_info = ( t_mtcp_get_ptrace_waitpid_info )
    _get_mtcp_symbol ( "get_ptrace_waitpid_info" );

  mtcp_init_thread_local = ( t_mtcp_init_thread_local ) 
    _get_mtcp_symbol ( "init_thread_local" );
#endif

  ( *setCallbks )( &callbackSleepBetweenCheckpoint
                 , &callbackPreCheckpoint
                 , &callbackPostCheckpoint
                 , &callbackShouldCkptFD
                 , &callbackWriteCkptPrefix
                 , &callbackRestoreVirtualPidTable
#ifdef PTRACE
                 , &callbackGetNextPtraceInfo
                 , &callbackPtraceInfoListCommand
                 , &callbackJalibCkptUnlock
#endif
                 );
  JTRACE ("Calling mtcp_init");
  ( *init ) ( UniquePid::checkpointFilename(),0xBadF00d,1 );
  ( *okFn ) ();

  JTRACE ( "mtcp_init complete" ) ( UniquePid::checkpointFilename() );
}

void prctlGetProcessName()
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11)
  if (prctlPrgName[0] == '\0') {
    bzero((void*) prctlPrgName, 22);
    strcpy(prctlPrgName, "dmtcp:");
    JASSERT(prctl(PR_GET_NAME, &prctlPrgName[6]) != -1) (JASSERT_ERRNO)
      .Text ("prctl() failed");
    JTRACE("prctl(PR_GET_NAME, ...) succeeded") (prctlPrgName);
  }
#endif
}

void prctlRestoreProcessName()
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11)
    JASSERT(prctl(PR_SET_NAME, prctlPrgName) != -1) (prctlPrgName) (JASSERT_ERRNO)
      .Text ("prctl() failed");
    JTRACE("prctl(PR_SET_NAME, ...) succeeded") (prctlPrgName);
#endif
}

static bool restoreArgvAfterRestart(char* mtcpRestoreArgvStartAddr)
{
  /*
   * The addresses where argv of mtcp_restart process start. /proc/pid/cmdline
   * information is looked up from these addresses.  We observed that the
   * stack-base for mtcp_restart is always 0x7ffffffff000 in 64-bit system and
   * 0xc0000000 in case of 32-bit system.  Once we restore the checkpointed
   * process' memory, we will map the pages ending in these address into
   * process' memory if they are unused i.e. not mapped by the process (which
   * is true for most processes running with ASLR).  Once we map them, we can
   * put the argv of the checkpointed process in there so that
   * /proc/self/cmdline show the correct values.
   */
  JASSERT(mtcpRestoreArgvStartAddr != NULL);

  char *startAddr = (char*) ((unsigned long) mtcpRestoreArgvStartAddr & PAGE_MASK);
  char *endAddr = MTCP_RESTORE_STACK_BASE;
  size_t len = endAddr - startAddr;

  void *retAddr = mmap((void*) startAddr, len, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
  if (retAddr != MAP_FAILED) {
    JTRACE("Restoring /proc/self/cmdline") (mtcpRestoreArgvStartAddr) (startAddr) (len) (JASSERT_ERRNO) ;
    dmtcp::vector<dmtcp::string> args = jalib::Filesystem::GetProgramArgs();
    char *addr = mtcpRestoreArgvStartAddr;
    args[0] = "DMTCP:" + args[0];
    for ( size_t i=0; i< args.size(); ++i ) {
      if (addr + args[i].length() >= endAddr)
        break;
      strcpy(addr, args[0].c_str());
      addr += args[0].length() + 1;
    }
    _mtcpRestoreArgvStartAddr = startAddr;
    return true;
  } else {
    JTRACE("Unable to restore /proc/self/cmdline") (startAddr) (len) (JASSERT_ERRNO) ;
    _mtcpRestoreArgvStartAddr = NULL;
  }
  return false;
}

static void unmapRestoreArgv()
{
  if (_mtcpRestoreArgvStartAddr != NULL) {
    char *endAddr = MTCP_RESTORE_STACK_BASE;
    size_t len = endAddr - _mtcpRestoreArgvStartAddr;
    JASSERT(munmap(_mtcpRestoreArgvStartAddr, len) == 0)
      (_mtcpRestoreArgvStartAddr) (len)
      .Text ("Failed to munmap extra pages that were mapped during restart");
  }
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
