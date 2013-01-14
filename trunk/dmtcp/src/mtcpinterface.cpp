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
#include "processinfo.h"
#include "protectedfds.h"
#include "dmtcpplugin.h"
#include "coordinatorapi.h"
#include "util.h"

#include "../jalib/jfilesystem.h"
#include "../jalib/jconvert.h"
#include "../jalib/jassert.h"
#include "../jalib/jalloc.h"
#include "../../mtcp/mtcp.h"

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

static void callbackSleepBetweenCheckpoint(int sec);
static void callbackPreCheckpoint(char **ckptFilename);
static void callbackPostCheckpoint(int isRestart,
                                   char* mtcpRestoreArgvStartAddr);
static int callbackShouldCkptFD(int /*fd*/);
static void callbackWriteCkptPrefix(int fd);

void callbackHoldsAnyLocks(int *retval);
void callbackPreSuspendUserThread();
void callbackPreResumeUserThread(int isRestart);

extern "C" int dmtcp_is_ptracing() __attribute__ ((weak));

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


  mtcp_init_dmtcp_info(pidVirtualizationEnabled,
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
  initializeDmtcpInfoInMtcp();

  mtcp_set_callbacks(&callbackSleepBetweenCheckpoint,
                     &callbackPreCheckpoint,
                     &callbackPostCheckpoint,
                     &callbackShouldCkptFD,
                     &callbackWriteCkptPrefix);

  mtcp_set_dmtcp_callbacks(&callbackHoldsAnyLocks,
                           &callbackPreSuspendUserThread,
                           &callbackPreResumeUserThread);

  JTRACE ("Calling mtcp_init");
  mtcp_init(UniquePid::getCkptFilename(), 0xBadF00d, 1);
  mtcp_ok();

  JTRACE ( "mtcp_init complete" ) ( UniquePid::getCkptFilename() );

  /* Now wait for Checkpoint Thread to finish initialization
   * NOTE: This should be the last thing in this constructor
   */
  ThreadSync::initMotherOfAll();
  while (!ThreadSync::isCheckpointThreadInitialized()) {
    struct timespec sleepTime = {0, 10*1000*1000};
    nanosleep(&sleepTime, NULL);
  }
}

void dmtcp::shutdownMtcpEngineOnFork()
{
  mtcp_reset_on_fork();
}

void dmtcp::killCkpthread()
{
  mtcp_kill_ckpthread();
}

static void callbackSleepBetweenCheckpoint ( int sec )
{
  dmtcp::ThreadSync::waitForUserThreadsToFinishPreResumeCB();
  dmtcp::DmtcpWorker::processEvent(DMTCP_EVENT_WAIT_FOR_SUSPEND_MSG, NULL);
  if (dmtcp_is_ptracing && dmtcp_is_ptracing()) {
    // FIXME: Add a test to make check that can insert a delay of a couple of
    // seconds in here. This helps testing the initialization routines of various
    // plugins.
    // Inform Coordinator of our RUNNING state;
    dmtcp::DmtcpWorker::instance().informCoordinatorOfRUNNINGState();
  }
  dmtcp::DmtcpWorker::instance().waitForStage1Suspend();

  prctlGetProcessName();
  unmapRestoreArgv();

  // After acquiring this lock, there shouldn't be any
  // allocations/deallocations and JASSERT/JTRACE/JWARNING/JNOTE etc.; the
  // process can deadlock.
  JALIB_CKPT_LOCK();
}

static void callbackPreCheckpoint( char ** ckptFilename )
{
  // All we want to do is unlock the jassert/jalloc locks, if we reset them, it
  // serves the purpose without having a callback.
  // TODO: Check for correctness.
  JALIB_CKPT_UNLOCK();

  //now user threads are stopped
  dmtcp::userHookTrampoline_preCkpt();
  dmtcp::DmtcpWorker::instance().waitForStage2Checkpoint();
  *ckptFilename = const_cast<char *>(dmtcp::UniquePid::getCkptFilename());
  JTRACE ( "MTCP is about to write checkpoint image." )(*ckptFilename);
}


extern "C" int fred_record_replay_enabled() __attribute__ ((weak));
static void callbackPostCheckpoint(int isRestart,
                                   char* mtcpRestoreArgvStartAddr)
{
  if (isRestart) {
    //restoreArgvAfterRestart(mtcpRestoreArgvStartAddr);
    prctlRestoreProcessName();

    if (fred_record_replay_enabled == 0 || !fred_record_replay_enabled()) {
      /* This calls setenv() which calls malloc. Since this is only executed on
         restart, that means it there is an extra malloc on replay. Commenting this
         until we have time to fix it. */
      dmtcp::CoordinatorAPI::instance().updateHostAndPortEnv();
    }

    dmtcp::DmtcpWorker::instance().postRestart();
    dmtcp::DmtcpWorker::processEvent(DMTCP_EVENT_POST_RESTART, NULL);
  } else {
    dmtcp::DmtcpWorker::processEvent(DMTCP_EVENT_POST_CKPT, NULL);
  }

  /* FIXME: There is no need to call sendCkptFilenameToCoordinator() but if
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
  dmtcp::CoordinatorAPI::instance().sendCkptFilename();

  dmtcp::DmtcpWorker::instance().waitForStage3Refill(isRestart);

  dmtcp::DmtcpWorker::instance().waitForStage4Resume(isRestart);

  // Set the process state to RUNNING now, in case a dmtcpaware hook
  //  calls pthread_create, thereby invoking our virtualization.
  dmtcp::WorkerState::setCurrentState( dmtcp::WorkerState::RUNNING );
  // Now everything but user threads are restored.  Call the user hook.
  dmtcp::userHookTrampoline_postCkpt(isRestart);

  if (dmtcp_is_ptracing == NULL || !dmtcp_is_ptracing()) {
    // Inform Coordinator of our RUNNING state;
    // If running under ptrace, lets do this in sleep-between-ckpt callback
    dmtcp::DmtcpWorker::instance().informCoordinatorOfRUNNINGState();
  }
  // After this, the user threads will be unlocked in mtcp.c and will resume.
}

static int callbackShouldCkptFD ( int /*fd*/ )
{
  //mtcp should never checkpoint file descriptors;  dmtcp will handle it
  return 0;
}

static void callbackWriteCkptPrefix ( int fd )
{
  // Not USED
  //DmtcpEventData_t edata;
  //edata.serializerInfo.fd = fd;
  //dmtcp::DmtcpWorker::processEvent(DMTCP_EVENT_WRITE_CKPT_PREFIX, &edata);
}

void callbackHoldsAnyLocks(int *retval)
{
  /* This callback is useful only for the ptrace plugin currently, but may be
   * used for other stuff as well.
   *
   * This is invoked as the first thing in stopthisthread() routine, which is
   * the signal handler for CKPT signal, to check if the current thread is
   * holding any of the wrapperExecLock or threadCreationLock. If the thread is
   * holding any of these locks, we return from the signal handler and wait for
   * the thread to release the lock. Once the thread has release the last lock,
   * it will send itself the CKPT signal and will return to the signal handler
   * and will proceed normally.
   */

  dmtcp::ThreadSync::unsetOkToGrabLock();
  *retval = dmtcp::ThreadSync::isThisThreadHoldingAnyLocks();
  if (*retval == TRUE) {
    JASSERT(dmtcp_is_ptracing && dmtcp_is_ptracing());
    dmtcp::ThreadSync::setSendCkptSignalOnFinalUnlock();
  }
}

void callbackPreSuspendUserThread()
{
  dmtcp::ThreadSync::incrNumUserThreads();
  dmtcp::DmtcpWorker::processEvent(DMTCP_EVENT_PRE_SUSPEND_USER_THREAD, NULL);
}

void callbackPreResumeUserThread(int isRestart)
{
  DmtcpEventData_t edata;
  edata.resumeUserThreadInfo.isRestart = isRestart;
  dmtcp::DmtcpWorker::processEvent(DMTCP_EVENT_RESUME_USER_THREAD, &edata);
  dmtcp::ThreadSync::setOkToGrabLock();
  // This should be the last significant work before returning from this
  // function.
  dmtcp::ThreadSync::processPreResumeCB();
  // Make a dummy syscall to inform superior of our status before we resume. If
  // ptrace is disabled, this call has no significant effect.
  syscall(DMTCP_FAKE_SYSCALL);
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

  size_t len;
  len = (dmtcp::ProcessInfo::instance().argvSize() + page_size) & page_mask;

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
      if (addr + args[i].length() >= startAddr + len)
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
  long page_size = sysconf(_SC_PAGESIZE);
  long page_mask = ~(page_size - 1);
  if (_mtcpRestoreArgvStartAddr != NULL) {
    JTRACE("Unmapping previously mmap()'d pages (that were mmap()'d for restoring argv");
    size_t len;
    len = (dmtcp::ProcessInfo::instance().argvSize() + page_size) & page_mask;
    JASSERT(_real_munmap(_mtcpRestoreArgvStartAddr, len) == 0)
      (_mtcpRestoreArgvStartAddr) (len)
      .Text ("Failed to munmap extra pages that were mapped during restart");
  }
}
