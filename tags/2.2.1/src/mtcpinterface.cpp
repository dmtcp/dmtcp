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
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <fenv.h>
#include <termios.h>
#include <unistd.h>

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
// FIXME: Linux prctl for PR_GET_NAME/PR_SET_NAME is on a per-thread basis.
//   If we want to be really accurate, we should make this thread-local.
static char prctlPrgName[16+sizeof(DMTCP_PRGNAME_PREFIX)-1] = {0};
static void prctlGetProcessName();
static void prctlRestoreProcessName();
static void save_term_settings();
static void restore_term_settings();

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
  dmtcp::ThreadSync::waitForUserThreadsToFinishPreResumeCB();
  dmtcp::DmtcpWorker::eventHook(DMTCP_EVENT_WAIT_FOR_SUSPEND_MSG, NULL);
  if (dmtcp_is_ptracing && dmtcp_is_ptracing()) {
    // FIXME: Add a test to make check that can insert a delay of a couple of
    // seconds in here. This helps testing the initialization routines of various
    // plugins.
    // Inform Coordinator of our RUNNING state;
    dmtcp::DmtcpWorker::instance().informCoordinatorOfRUNNINGState();
  }
  dmtcp::DmtcpWorker::instance().waitForStage1Suspend();

  unmapRestoreArgv();

  /* After acquiring this lock, there shouldn't be any
   * allocations/deallocations and JASSERT/JTRACE/JWARNING/JNOTE etc.; the
   * process can deadlock.
   *
   * The corresponding JALIB_CKPT_UNLOCK is called from within suspendThreads()
   * function after all user threads have acknowledged the ckpt-signal.
   */
  JALIB_CKPT_LOCK();
}

void dmtcp::callbackPreCheckpoint()
{
  //now user threads are stopped
  dmtcp::userHookTrampoline_preCkpt();
  dmtcp::DmtcpWorker::instance().waitForStage2Checkpoint();
  dmtcp::prepareForCkpt();
}

void dmtcp::callbackPostCheckpoint(int isRestart,
                                   char* mtcpRestoreArgvStartAddr)
{
  if (isRestart) {
    //restoreArgvAfterRestart(mtcpRestoreArgvStartAddr);
    prctlRestoreProcessName();
    fesetround(rounding_mode);

    JTRACE("begin postRestart()");
    WorkerState::setCurrentState(WorkerState::RESTARTING);
    if (dmtcp_update_ppid) {
      dmtcp_update_ppid();
    }
    dmtcp::DmtcpWorker::eventHook(DMTCP_EVENT_RESTART, NULL);
  } else {
    dmtcp::DmtcpWorker::eventHook(DMTCP_EVENT_RESUME, NULL);
  }

  dmtcp::DmtcpWorker::instance().waitForStage3Refill(isRestart);

  dmtcp::DmtcpWorker::instance().waitForStage4Resume(isRestart);
  if (isRestart) {
    restore_term_settings();
  }

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

  dmtcp::ThreadSync::unsetOkToGrabLock();
  *retval = dmtcp::ThreadSync::isThisThreadHoldingAnyLocks();
  if (*retval == TRUE) {
    JASSERT(dmtcp_is_ptracing && dmtcp_is_ptracing());
    dmtcp::ThreadSync::setSendCkptSignalOnFinalUnlock();
  }
}

void dmtcp::callbackPreSuspendUserThread()
{
  dmtcp::ThreadSync::incrNumUserThreads();
  dmtcp::DmtcpWorker::eventHook(DMTCP_EVENT_PRE_SUSPEND_USER_THREAD, NULL);
  if (gettid() == getpid()) {
    prctlGetProcessName();
  }
}

void dmtcp::callbackPreResumeUserThread(int isRestart)
{
  if (isRestart) {
    prctlRestoreProcessName();
  }
  DmtcpEventData_t edata;
  edata.resumeUserThreadInfo.isRestart = isRestart;
  dmtcp::DmtcpWorker::eventHook(DMTCP_EVENT_RESUME_USER_THREAD, &edata);
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
#endif

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

void dmtcp::prepareForCkpt()
{
  /* Drain stdin and stdout before checkpoint */
  tcdrain(STDOUT_FILENO);
  tcdrain(STDERR_FILENO);

  save_term_settings();
  rounding_mode = fegetround();
}
/*************************************************************************
 *
 *  Save and restore terminal settings.
 *
 *************************************************************************/

static int saved_termios_exists = 0;
static struct termios saved_termios;
static struct winsize win;

static void save_term_settings()
{
  saved_termios_exists = ( isatty(STDIN_FILENO)
  		           && tcgetattr(STDIN_FILENO, &saved_termios) >= 0 );
  if (saved_termios_exists)
    ioctl (STDIN_FILENO, TIOCGWINSZ, (char *) &win);
}

static int safe_tcsetattr(int fd, int optional_actions,
                          const struct termios *termios_p)
{
  struct termios old_termios, new_termios;
  /* We will compare old and new, and we don't want uninitialized data */
  memset(&new_termios, 0, sizeof(new_termios));
  /* tcgetattr returns success as long as at least one of requested
   * changes was executed.  So, repeat until no more changes.
   */
  do {
    memcpy(&old_termios, &new_termios, sizeof(new_termios));
    if (tcsetattr(fd, TCSANOW, termios_p) == -1) return -1;
    if (tcgetattr(fd, &new_termios) == -1) return -1;
  } while (memcmp(&new_termios, &old_termios, sizeof(new_termios)) != 0);
  return 0;
}

// FIXME: Handle Virtual Pids
static void restore_term_settings()
{
  if (saved_termios_exists){
    /* First check if we are in foreground. If not, skip this and print
     *   warning.  If we try to call tcsetattr in background, we will hang up.
     */
    int foreground = (tcgetpgrp(STDIN_FILENO) == getpgrp());
    JTRACE("restore terminal attributes, check foreground status first")
      (foreground);
    if (foreground) {
      if ( ( ! isatty(STDIN_FILENO)
             || safe_tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios) == -1) )
        JWARNING(false) .Text("failed to restore terminal");
      else {
        struct winsize cur_win;
        JTRACE("restored terminal");
        ioctl (STDIN_FILENO, TIOCGWINSZ, (char *) &cur_win);
	/* ws_row/ws_col was probably not 0/0 prior to checkpoint.  We change
	 * it back to last known row/col prior to checkpoint, and then send a
	 * SIGWINCH (see below) to notify process that window might have changed
	 */
        if (cur_win.ws_row == 0 && cur_win.ws_col == 0)
          ioctl (STDIN_FILENO, TIOCSWINSZ, (char *) &win);
      }
    } else {
      JWARNING(false)
        .Text(":skip restore terminal step -- we are in BACKGROUND");
    }
  }
  if (kill(getpid(), SIGWINCH) == -1) {}  /* No remedy if error */
}

