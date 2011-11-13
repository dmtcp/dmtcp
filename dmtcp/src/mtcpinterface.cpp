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
#include "dmtcpmodule.h"

#include "../jalib/jfilesystem.h"
#include "../jalib/jconvert.h"
#include "../jalib/jassert.h"
#include "../jalib/jalloc.h"

#ifdef __x86_64__
# define MTCP_RESTORE_STACK_BASE ((char*)0x7FFFFFFFF000L)
#else
# define MTCP_RESTORE_STACK_BASE \
    (strcmp("#CONFIG_M32","yes") == 0 ? ((char *)NULL) : ((char*)0xC0000000L))
#endif

#ifdef DEBUG
static int debugEnabled = 1;
#else
static int debugEnabled = 0;
#endif

#ifdef PID_VIRTUALIZATION
static int pidVirtualizationEnabled = 1;
#else
static int pidVirtualizationEnabled = 0;
#endif

static char prctlPrgName[22] = {0};
static void prctlGetProcessName();
static void prctlRestoreProcessName();

static char *_mtcpRestoreArgvStartAddr = NULL;
static void restoreArgvAfterRestart(char* mtcpRestoreArgvStartAddr);
static void unmapRestoreArgv();

static const char* REOPEN_MTCP = ( char* ) 0x1;

static void callbackSleepBetweenCheckpoint(int sec);
static void callbackPreCheckpoint(char **ckptFilename);
static void callbackPostCheckpoint(int isRestart,
                                   char* mtcpRestoreArgvStartAddr);
static int callbackShouldCkptFD(int /*fd*/);
static void callbackWriteCkptPrefix(int fd);
static void callbackRestoreVirtualPidTable();

void callbackPreSuspendUserThread();
void callbackPreResumeUserThread(int is_ckpt, int is_restart);
void callbackSendStopSignal(pid_t tid, int *retry_signalling, int *retval);

void callbackCkptThreadStart();
MtcpFuncPtrs_t mtcpFuncPtrs;

#ifdef EXTERNAL_SOCKET_HANDLING
static bool delayedCheckpoint = false;
#endif

static void* find_and_open_mtcp_so()
{
  dmtcp::string mtcpso = jalib::Filesystem::FindHelperUtility ( "libmtcp.so" );
  void* handle = dlopen ( mtcpso.c_str(), RTLD_NOW );
  JASSERT ( handle != NULL ) ( mtcpso ) (dlerror())
    .Text ( "failed to load libmtcp.so" );
  return handle;
}

// Note that mtcp.so is closed and re-opened (maybe in a different
//   location) at the time of fork.  Do not statically save the
//   return value of get_mtcp_symbol across a fork.
LIB_PRIVATE
void* get_mtcp_symbol ( const char* name )
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

  void* tmp = _real_dlsym ( theMtcpHandle, name );
  JASSERT ( tmp != NULL ) ( name )
    .Text ( "failed to find libmtcp.so symbol for 'name'\n"
            "Maybe try re-compiling MTCP:   (cd mtcp; make clean); make" );

  //JTRACE("looking up libmtcp.so symbol")(name);

  return tmp;
}

static void initializeMtcpFuncPtrs()
{
  mtcpFuncPtrs.init = (mtcp_init_t) get_mtcp_symbol("mtcp_init");
  mtcpFuncPtrs.ok = (mtcp_ok_t) get_mtcp_symbol("mtcp_ok");
  mtcpFuncPtrs.threadiszombie =
    (mtcp_threadiszombie) get_mtcp_symbol("mtcp_threadiszombie");
  mtcpFuncPtrs.clone = (mtcp_clone_t) get_mtcp_symbol("__clone");
  mtcpFuncPtrs.fill_in_pthread_id =
    (mtcp_fill_in_pthread_id_t) get_mtcp_symbol("mtcp_fill_in_pthread_id");
  mtcpFuncPtrs.kill_ckpthread =
    (mtcp_kill_ckpthread_t) get_mtcp_symbol("mtcp_kill_ckpthread");
  mtcpFuncPtrs.process_pthread_join =
    (mtcp_process_pthread_join_t) get_mtcp_symbol("mtcp_process_pthread_join");
  mtcpFuncPtrs.init_dmtcp_info =
    (mtcp_init_dmtcp_info_t) get_mtcp_symbol("mtcp_init_dmtcp_info");
  mtcpFuncPtrs.set_callbacks =
    (mtcp_set_callbacks_t) get_mtcp_symbol("mtcp_set_callbacks");
}

static void initializeDmtcpInfoInMtcp()
{
  int jassertlog_fd = debugEnabled ? PROTECTED_JASSERTLOG_FD : -1;

  // DMTCP restores working dir only if --checkpoint-open-files invoked.
  // Later, we may offer the user a separate command line option for this.
  int restore_working_directory = getenv(ENV_VAR_CKPT_OPEN_FILES) ? 1 : 0;

  void *clone_fptr = (void*) _real_clone;
  void *sigaction_fptr = (void*) _real_sigaction;
  // FIXME: What if jalib::JAllocDispatcher is undefined?
  void *malloc_fptr = (void*) jalib::JAllocDispatcher::malloc;
  void *free_fptr = (void*) jalib::JAllocDispatcher::free;
  JASSERT(clone_fptr != NULL);
  JASSERT(sigaction_fptr != NULL);
  JASSERT(malloc_fptr != NULL);
  JASSERT(free_fptr != NULL);


  (*mtcpFuncPtrs.init_dmtcp_info) (pidVirtualizationEnabled,
                                   PROTECTED_STDERR_FD,
                                   jassertlog_fd,
                                   restore_working_directory,
                                   clone_fptr,
                                   sigaction_fptr,
                                   malloc_fptr,
                                   free_fptr);

}

void dmtcp::initializeMtcpEngine()
{
  initializeMtcpFuncPtrs();
  initializeDmtcpInfoInMtcp();

  (*mtcpFuncPtrs.set_callbacks)(&callbackSleepBetweenCheckpoint,
                                &callbackPreCheckpoint,
                                &callbackPostCheckpoint,
                                &callbackShouldCkptFD,
                                &callbackWriteCkptPrefix,
                                &callbackRestoreVirtualPidTable,

                                &callbackPreSuspendUserThread,
                                &callbackPreResumeUserThread,
                                &callbackSendStopSignal,
                                &callbackCkptThreadStart
                               );

  JTRACE ("Calling mtcp_init");
  mtcpFuncPtrs.init(UniquePid::checkpointFilename(), 0xBadF00d, 1);
  mtcpFuncPtrs.ok();

  JTRACE ( "mtcp_init complete" ) ( UniquePid::checkpointFilename() );
}

static void callbackSleepBetweenCheckpoint ( int sec )
{
  dmtcp_process_event(DMTCP_EVENT_WAIT_FOR_SUSPEND_MSG, NULL);
  dmtcp::DmtcpWorker::instance().waitForStage1Suspend();

  prctlGetProcessName();
  unmapRestoreArgv();

  dmtcp_process_event(DMTCP_EVENT_GOT_SUSPEND_MSG,
                      (void*) dmtcp::VirtualPidTable::instance().numThreads());
  // After acquiring this lock, there shouldn't be any
  // allocations/deallocations and JASSERT/JTRACE/JWARNING/JNOTE etc.; the
  // process can deadlock.
  JALIB_CKPT_LOCK();
}

static void callbackPreCheckpoint( char ** ckptFilename )
{
  dmtcp_process_event(DMTCP_EVENT_START_PRE_CKPT_CB, NULL);

  // All we want to do is unlock the jassert/jalloc locks, if we reset them, it
  // serves the purpose without having a callback.
  // TODO: Check for correctness.
  JALIB_RESET_ON_FORK();

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
  JTRACE ( "MTCP is about to write checkpoint image." );
}


static void callbackPostCheckpoint ( int isRestart,
                                     char* mtcpRestoreArgvStartAddr)
{
  if ( isRestart )
  {
    restoreArgvAfterRestart(mtcpRestoreArgvStartAddr);
    prctlRestoreProcessName();

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
     *      The problem occurs when some other worker sends a status messages
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
      dmtcp_process_event(DMTCP_EVENT_POST_CHECKPOINT_RESUME, NULL);
    }

    // Set the process state to RUNNING now, in case a dmtcpaware hook
    //  calls pthread_create, thereby invoking our virtualization.
    dmtcp::WorkerState::setCurrentState( dmtcp::WorkerState::RUNNING );
    // Now everything but user threads are restored.  Call the user hook.
    dmtcp::userHookTrampoline_postCkpt(isRestart);
    // After this, the user threads will be unlocked in mtcp.c and will resume.
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


#ifndef RECORD_REPLAY
  /* This calls setenv() which calls malloc. Since this is only executed on
     restart, that means it there is an extra malloc on replay. Commenting this
     until we have time to fix it. */
  dmtcp::DmtcpWorker::instance().updateCoordinatorHostAndPortEnv();
#endif

  dmtcp_process_event(DMTCP_EVENT_POST_RESTART_RESUME, NULL);

  // Set the process state to RUNNING now, in case a dmtcpaware hook
  //  calls pthread_create, thereby invoking our virtualization.
  dmtcp::WorkerState::setCurrentState( dmtcp::WorkerState::RUNNING );
  // Now everything but user threads are restored.  Call the user hook.
  dmtcp::userHookTrampoline_postCkpt(true);
  // After this, the user threads will be unlocked in mtcp.c and will resume.
}

void callbackPreSuspendUserThread()
{
  dmtcp_process_event(DMTCP_EVENT_PRE_SUSPEND_USER_THREAD, NULL);
}

void callbackPreResumeUserThread(int is_ckpt, int is_restart)
{
  DmtcpResumeUserThreadInfo info;
  info.is_ckpt = is_ckpt;
  info.is_restart = is_restart;
  dmtcp_process_event(DMTCP_EVENT_RESUME_USER_THREAD, &info);
}

void callbackSendStopSignal(pid_t tid, int *retry_signalling, int *retval)
{
  DmtcpSendStopSignalInfo info;
  info.tid = tid;
  info.retry_signalling = retry_signalling;
  info.retval = retval;

  *retry_signalling = 1;
  *retval = 0;
  dmtcp_process_event(DMTCP_EVENT_SEND_STOP_SIGNAL, &info);
}

void callbackCkptThreadStart()
{
  dmtcp_process_event(DMTCP_EVENT_CKPT_THREAD_START, NULL);
}

void prctlGetProcessName()
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11)
  if (prctlPrgName[0] == '\0') {
    memset(prctlPrgName, 0, sizeof(prctlPrgName));
    strcpy(prctlPrgName, DMTCP_PRGNAME_PREFIX);
    int ret = prctl(PR_GET_NAME, &prctlPrgName[strlen(DMTCP_PRGNAME_PREFIX)]);
    if (ret != -1) {
      JTRACE("prctl(PR_GET_NAME, ...) succeeded") (prctlPrgName);
    } else {
      JASSERT(errno == EINVAL) (JASSERT_ERRNO)
        .Text ("prctl(PR_GET_NAME, ...) failed");
      JTRACE("prctl(PR_GET_NAME, ...) failed. Not supported on this kernel?");
    }
  }
#endif
}

void prctlRestoreProcessName()
{
  // Although PR_SET_NAME has been supported since 2.6.9, we wouldn't use it on
  // kernel < 2.6.11 since we didn't get the process name using PR_GET_NAME
  // which is supported on >= 2.6.11
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11)
    if (prctl(PR_SET_NAME, prctlPrgName) != -1) {
      JTRACE("prctl(PR_SET_NAME, ...) succeeded") (prctlPrgName);
    } else {
      JASSERT(errno == EINVAL) (prctlPrgName) (JASSERT_ERRNO)
        .Text ("prctl(PR_SET_NAME, ...) failed");
      JTRACE("prctl(PR_SET_NAME, ...) failed") (prctlPrgName);
    }
#endif
}

static void restoreArgvAfterRestart(char* mtcpRestoreArgvStartAddr)
{
  /*
   * The addresses where argv of mtcp_restart process starts. /proc/PID/cmdline
   * information is looked up from these addresses.  We observed that the
   * stack-base for mtcp_restart is always 0x7ffffffff000 in 64-bit system and
   * 0xc0000000 in case of 32-bit system.  Once we restore the checkpointed
   * process's memory, we will map the pages ending in these address into the
   * process's memory if they are unused i.e. not mapped by the process (which
   * is true for most processes running with ASLR).  Once we map them, we can
   * put the argv of the checkpointed process in there so that
   * /proc/self/cmdline shows the correct values.
   * Note that if compiled in 32-bit mode '-m32', the stack base address
   * is in still a different location, and so this logic is not valid.
   */
  JASSERT(mtcpRestoreArgvStartAddr != NULL);

  long page_size = sysconf(_SC_PAGESIZE);
  long page_mask = ~(page_size - 1);
  char *startAddr = (char*) ((unsigned long) mtcpRestoreArgvStartAddr & page_mask);
  char *endAddr = MTCP_RESTORE_STACK_BASE;
  size_t len = endAddr - startAddr;
#ifdef CONFIG_M32
  return;
#endif

  // Check to verify if any page in the given range is already mmap()'d.
  // It assumes that the given addresses may belong to stack only and if
  // mapped, will have read+write permissions.
  for (size_t i = 0; i < len; i += page_size) {
    int ret = mprotect ((char*) startAddr + i, page_size,
                        PROT_READ | PROT_WRITE);
    if (ret != -1 || errno != ENOMEM) {
      _mtcpRestoreArgvStartAddr = NULL;
      return;
    }
  }

  //None of the pages are mapped -- it is safe to mmap() them
  void *retAddr = mmap((void*) startAddr, len, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
  if (retAddr != MAP_FAILED) {
    JTRACE("Restoring /proc/self/cmdline")
      (mtcpRestoreArgvStartAddr) (startAddr) (len) (JASSERT_ERRNO) ;
    dmtcp::vector<dmtcp::string> args = jalib::Filesystem::GetProgramArgs();
    char *addr = mtcpRestoreArgvStartAddr;
    // Do NOT change restarted process's /proc/self/cmdline.
    //args[0] = DMTCP_PRGNAME_PREFIX + args[0];
    for ( size_t i=0; i< args.size(); ++i ) {
      if (addr + args[i].length() >= endAddr)
        break;
      strcpy(addr, args[i].c_str());
      addr += args[i].length() + 1;
    }
    _mtcpRestoreArgvStartAddr = startAddr;
  } else {
    JTRACE("Unable to restore /proc/self/cmdline") (startAddr) (len) (JASSERT_ERRNO) ;
    _mtcpRestoreArgvStartAddr = NULL;
  }
  return;
}

static void unmapRestoreArgv()
{
  if (_mtcpRestoreArgvStartAddr != NULL) {
    JTRACE("Unmapping previously mmap()'d pages (that were mmap()'d for restoring argv");
    char *endAddr = MTCP_RESTORE_STACK_BASE;
    size_t len = endAddr - _mtcpRestoreArgvStartAddr;
    JASSERT(_real_munmap(_mtcpRestoreArgvStartAddr, len) == 0)
      (_mtcpRestoreArgvStartAddr) (len)
      .Text ("Failed to munmap extra pages that were mapped during restart");
  }
}

#ifdef PID_VIRTUALIZATION
enum cloneSucceed {CLONE_UNINITIALIZED, CLONE_FAIL, CLONE_SUCCEED};
struct ThreadArg {
  int ( *fn ) ( void *arg );  // clone() calls fn that returns int
  void * ( *pthread_fn ) ( void *arg ); // pthread_create calls fn -> void *
  void *arg;
  pid_t original_tid;
  enum cloneSucceed clone_success; // Child will set to FAIL or SUCCEED
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

// Invoked via pthread_create as start_routine
// On return, it calls mtcp_threadiszombie()
static void * pthread_start(void *arg)
{
  struct ThreadArg *threadArg = (struct ThreadArg*) arg;
  void *thread_arg = threadArg->arg;
  void * (*pthread_fn) (void *) = threadArg->pthread_fn;
  JASSERT ( pthread_fn != 0x0);
  JALLOC_HELPER_FREE(arg); // Was allocated in calling thread in pthread_create
  void *result = (*pthread_fn) ( thread_arg );
  mtcpFuncPtrs.threadiszombie();
  return result;
}

// Invoked via __clone
LIB_PRIVATE
int clone_start(void *arg)
{
  dmtcp_process_event(DMTCP_EVENT_THREAD_START, NULL);

  struct ThreadArg *threadArg = (struct ThreadArg*) arg;
  pid_t tid = _real_gettid();
  JTRACE ("In clone_start");

// FIXME:  Can we delete this portion now?  It was originally needed to handle
//   tid wraparound for test/pthread1 and test/pthread2.
#ifndef PTRACE
  // Force gettid() to agree with _real_gettid().  Why can it be out of sync?
  // gettid() just caches value of _real_gettid().
  // EDIT: This call interacts badly with PTRACE, so compiling it out for now.  KA
  dmtcp_reset_gettid();
#endif

  // FIXME: Why not do this in the mtcp.c::__clone?
  mtcpFuncPtrs.fill_in_pthread_id(tid, pthread_self());

  if ( dmtcp::VirtualPidTable::isConflictingPid ( tid ) ) {
    threadArg->clone_success = CLONE_FAIL;
    JTRACE ("TID conflict detected.  Exiting thread.");
    // If we return to clone(), clone will call __GI_exit(), and process exits.
    // We emulate glibc pthread_create.c:start_thread(), which makes
    //   call below in order to kill this thread only.
    _real_syscall(SYS_exit, 0);
    return 0; // Not reached.  Done to avoid compiler warnings.
  } else {
    threadArg->clone_success = CLONE_SUCCEED;
  }

  pid_t original_tid = threadArg -> original_tid;
  int (*fn) (void *) = threadArg->fn;
  void *thread_arg = threadArg->arg;

  // Free memory previously allocated through JALLOC_HELPER_MALLOC in __clone
  JALLOC_HELPER_FREE(threadArg);

  if (original_tid == -1) {
    /*
     * original tid is not known, which means this thread never existed before
     * checkpoint, so will insert the original_tid into virtualpidtable
     *
     * No danger in calling gettid() because it will call _real_gettid() only
     * _once_ and then cache the return value.
     */
    original_tid = gettid();
    JASSERT ( tid == original_tid ) (tid) (original_tid)
      .Text ( "syscall(SYS_gettid) and _real_gettid() returning different values for the newly created thread!" );
    dmtcp::VirtualPidTable::instance().insertTid ( original_tid );
  }

  dmtcp::VirtualPidTable::instance().updateMapping ( original_tid, tid );

  JTRACE ( "Calling user function" ) (original_tid);

  /* Thread finished initialization.  It's now safe for this thread to
   * participate in checkpoint.  Decrement the uninitializedThreadCount in
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

  dmtcp_process_event(DMTCP_EVENT_THREAD_EXIT, NULL);

  return result;
}
#endif

extern "C" int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                              void *(*start_routine)(void*), void *arg)
{
  int retval;
  // We have to use DMTCP-specific memory allocator because using glibc:malloc
  // can interfere with user threads.
  // We use JALLOC_HELPER_FREE to free this memory in two places:
  // 1. near the beginning of pthread_start (wrapper for start_routine),
  //     providing that the __clone call succeeds with no tid conflict.
  // 2. if the call to _real_pthread_create fails, then free memory
  //     near the end of this function.
  // We use MALLOC/FREE so that pthread_create() can be called again, without
  // waiting for the new thread to give up the buffer in pthread_start().
  struct ThreadArg *threadArg =
    (struct ThreadArg *) JALLOC_HELPER_MALLOC (sizeof (struct ThreadArg));
  threadArg->pthread_fn = start_routine;
  threadArg->arg = arg;
  WRAPPER_EXECUTION_DISABLE_CKPT();
  retval = _real_pthread_create(thread, attr, pthread_start, threadArg);
  WRAPPER_EXECUTION_ENABLE_CKPT();
  if (retval != 0) { // if we failed to create new pthread
    JALLOC_HELPER_FREE(threadArg);
  }
  return retval;
}

//need to forward user clone
extern "C" int __clone ( int ( *fn ) ( void *arg ), void *child_stack, int flags, void *arg, int *parent_tidptr, struct user_desc *newtls, int *child_tidptr )
{
  /*
   * struct MtcpRestartThreadArg
   *
   * DMTCP requires the original_tids of the threads being created during
   *  the RESTARTING phase.  We use an MtcpRestartThreadArg structure to pass
   *  the original_tid of the thread being created from MTCP to DMTCP.
   *
   * actual clone call: clone (fn, child_stack, flags, void *, ... )
   * new clone call   : clone (fn, child_stack, flags, (struct MtcpRestartThreadArg *), ...)
   *
   * DMTCP automatically extracts arg from this structure and passes that
   * to the _real_clone call.
   *
   * IMPORTANT NOTE: While updating, this struct must be kept in sync
   * with the struct of the same name in mtcp.c
   */
  struct MtcpRestartThreadArg {
    void * arg;
    pid_t original_tid;
  } *mtcpRestartThreadArg;

  //JTRACE ( "forwarding user's clone call to mtcp" );

#ifndef PID_VIRTUALIZATION
  if ( dmtcp::WorkerState::currentState() != dmtcp::WorkerState::RUNNING )
  {
    mtcpRestartThreadArg = (struct MtcpRestartThreadArg *) arg;
    arg                  = mtcpRestartThreadArg -> arg;
  }

  JTRACE ( "Forwarding user's clone call to mtcp" );
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

  // We have to use DMTCP-specific memory allocator because using glibc:malloc
  // can interfere with user threads.
  // We use JALLOC_HELPER_FREE to free this memory in two places:
  //   1.  later in this function in case of failure on call to __clone; and
  //   2.  near the beginnging of clone_start (wrapper for start_routine).
  struct ThreadArg *threadArg =
    (struct ThreadArg *) JALLOC_HELPER_MALLOC (sizeof (struct ThreadArg));
  threadArg->fn = fn;
  threadArg->arg = arg;
  threadArg->original_tid = originalTid;
  threadArg->clone_success = CLONE_UNINITIALIZED;

  int tid;

  /*
   * originalTid == -1 indicates that the thread is being created for the first
   * time in the process i.e. we are not restoring from a checkpoint
   */

  while (1) {
    if (originalTid == -1) {
      /* First time thread creation */
      JTRACE ( "Forwarding user's clone call to mtcp" );
      tid = mtcpFuncPtrs.clone(clone_start, child_stack, flags, threadArg,
                               parent_tidptr, newtls, child_tidptr);
      // FIXME:  What happens here if tid confliCT happens?
      dmtcp_process_event(DMTCP_EVENT_THREAD_CREATED,
                          (void*) (unsigned long) tid);
    } else {
      /* Recreating thread during restart */
      JTRACE ( "Calling libc:__clone" );
      tid = _real_clone ( clone_start, child_stack, flags, threadArg,
			  parent_tidptr, newtls, child_tidptr );
    }

    if (tid == -1) { // if the call to clone failed
      JTRACE("Clone call failed")(JASSERT_ERRNO);
      // Free the memory which was previously allocated by calling
      // JALLOC_HELPER_MALLOC inside __clone wrapper
      // FIXME:  We free the threadArg here, and then if originalTid == -1
      //         (still), we use uninitialized memory.  WHY?
      JALLOC_HELPER_FREE ( threadArg );

      /* If clone() failed, decrement the uninitialized thread count, since
       * there is none
       */
      // FIXME:  We decrement even though we will try to do this again!  WHY?
      dmtcp::DmtcpWorker::decrementUninitializedThreadCount();
      break;
    }

    if ( dmtcp::VirtualPidTable::isConflictingPid ( tid ) ) {
      // We will clone a new thread.  So, we don't free threadArg.
      // We will re-use it.

      JTRACE ( "TID conflict detected, creating a new child thread" ) ( tid );
      // Wait for child thread to acknowledge failure and quiesce itself.
      const struct timespec busywait = {(time_t) 0, (long)1000*1000};
      while (threadArg->clone_success != CLONE_FAIL) {
         nanosleep(&busywait, NULL);
      } // Will now continue again around the while loop.
    } else {
      JTRACE ("New thread created") (tid);
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

extern "C" void pthread_exit (void * retval)
{
  mtcpFuncPtrs.threadiszombie();
  _real_pthread_exit (retval);
}

extern "C" int pthread_join (pthread_t thread, void **value_ptr)
{
  /* Wrap the call to _real_pthread_join() to make sure we call
     delete_thread_on_pthread_join(). */
  int retval = _real_pthread_join (thread, value_ptr);
  if (retval == 0) {
    mtcpFuncPtrs.process_pthread_join(thread);
  }
  return retval;
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
  // Remove our signal handler from our SIG_CKPT
  errno = 0;
  JWARNING (SIG_ERR != _real_signal(dmtcp::DmtcpWorker::determineMtcpSignal(),
                                    SIG_DFL))
           (dmtcp::DmtcpWorker::determineMtcpSignal())
           (JASSERT_ERRNO)
           .Text("failed to reset child's checkpoint signal on fork");
  get_mtcp_symbol ( REOPEN_MTCP );
}

void dmtcp::killCkpthread()
{
  mtcpFuncPtrs.kill_ckpthread();
}
