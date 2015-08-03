/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/
#include "constants.h"
#include "mtcpinterface.h"
#include "syscallwrappers.h"
#include "dmtcpworker.h"
#include "processinfo.h"
#include "dmtcpmessagetypes.h"
#include "util.h"
#include "threadsync.h"
#include "ckptserializer.h"
#include "protectedfds.h"
#include "shareddata.h"
#include "threadlist.h"

#include "../jalib/jfilesystem.h"
#include "../jalib/jconvert.h"
#include "../jalib/jassert.h"
#include "../jalib/jalloc.h"

using namespace dmtcp;

int rounding_mode = 1;

static char *_mtcpRestoreArgvStartAddr = NULL;
#ifdef RESTORE_ARGV_AFTER_RESTART
static void restoreArgvAfterRestart(char* mtcpRestoreArgvStartAddr);
#endif
static void unmapRestoreArgv();


extern "C" int dmtcp_is_ptracing() __attribute__ ((weak));
extern "C" int dmtcp_update_ppid() __attribute__ ((weak));

void dmtcp::initializeMtcpEngine()
{
  ThreadSync::initMotherOfAll();
  ThreadList::init();
}

void dmtcp::callbackSleepBetweenCheckpoint ( int sec )
{
  ThreadSync::waitForUserThreadsToFinishPreResumeCB();
  DmtcpWorker::eventHook(DMTCP_EVENT_WAIT_FOR_SUSPEND_MSG, NULL);
  if (dmtcp_is_ptracing && dmtcp_is_ptracing()) {
    // FIXME: Add a test to make check that can insert a delay of a couple of
    //        seconds in here. This helps testing the initialization routines
    //        of various plugins.
    // Inform coordinator of our RUNNING state;
    DmtcpWorker::informCoordinatorOfRUNNINGState();
  }
  DmtcpWorker::waitForStage1Suspend();

  unmapRestoreArgv();
}

void dmtcp::callbackPreCheckpoint()
{
  //now user threads are stopped
  DmtcpWorker::waitForStage2Checkpoint();
}

void dmtcp::callbackPostCheckpoint(int isRestart,
                                   char* mtcpRestoreArgvStartAddr)
{
  if (isRestart) {
    //restoreArgvAfterRestart(mtcpRestoreArgvStartAddr);

    JTRACE("begin postRestart()");
    WorkerState::setCurrentState(WorkerState::RESTARTING);
    if (dmtcp_update_ppid) {
      dmtcp_update_ppid();
    }
    DmtcpWorker::eventHook(DMTCP_EVENT_RESTART, NULL);
  } else {
    DmtcpWorker::eventHook(DMTCP_EVENT_RESUME, NULL);
  }

  DmtcpWorker::waitForStage3Refill(isRestart);

  DmtcpWorker::waitForStage4Resume(isRestart);

  WorkerState::setCurrentState( WorkerState::RUNNING );

  if (dmtcp_is_ptracing == NULL || !dmtcp_is_ptracing()) {
    // Inform coordinator of our RUNNING state;
    // If running under ptrace, let's do this in sleep-between-ckpt callback.
    DmtcpWorker::informCoordinatorOfRUNNINGState();
  }
  // After this, the user threads will be unlocked in mtcp.c and will resume.
}

void dmtcp::callbackHoldsAnyLocks(int *retval)
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

  ThreadSync::unsetOkToGrabLock();
  *retval = ThreadSync::isThisThreadHoldingAnyLocks();
  if (*retval) {
    JASSERT(dmtcp_is_ptracing && dmtcp_is_ptracing());
    ThreadSync::setSendCkptSignalOnFinalUnlock();
  }
}

void dmtcp::callbackPreSuspendUserThread()
{
  ThreadSync::incrNumUserThreads();
  DmtcpWorker::eventHook(DMTCP_EVENT_PRE_SUSPEND_USER_THREAD, NULL);
}

void dmtcp::callbackPreResumeUserThread(int isRestart)
{
  DmtcpEventData_t edata;
  edata.resumeUserThreadInfo.isRestart = isRestart;
  DmtcpWorker::eventHook(DMTCP_EVENT_RESUME_USER_THREAD, &edata);
  ThreadSync::setOkToGrabLock();
  // This should be the last significant work before returning from this
  // function.
  ThreadSync::processPreResumeCB();
  // Make a dummy syscall to inform superior of our status before we resume. If
  // ptrace is disabled, this call has no significant effect.
  syscall(DMTCP_FAKE_SYSCALL);
}

#ifdef RESTORE_ARGV_AFTER_RESTART
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
  len = (ProcessInfo::instance().argvSize() + page_size) & page_mask;

  // Check to verify if any page in the given range is already mmap()'d.
  // It assumes that the given addresses may belong to stack only, and if
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
    vector<string> args = jalib::Filesystem::GetProgramArgs();
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
#endif

static void unmapRestoreArgv()
{
  long page_size = sysconf(_SC_PAGESIZE);
  long page_mask = ~(page_size - 1);
  if (_mtcpRestoreArgvStartAddr != NULL) {
    JTRACE("Unmapping previously mmap()'d pages (that were mmap()'d for restoring argv");
    size_t len;
    len = (ProcessInfo::instance().argvSize() + page_size) & page_mask;
    JASSERT(_real_munmap(_mtcpRestoreArgvStartAddr, len) == 0)
      (_mtcpRestoreArgvStartAddr) (len)
      .Text ("Failed to munmap extra pages that were mapped during restart");
  }
}
