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

#include "dmtcpworker.h"
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/time.h>
#include "../jalib/jbuffer.h"
#include "../jalib/jconvert.h"
#include "../jalib/jfilesystem.h"
#include "../jalib/jsocket.h"
#include "coordinatorapi.h"
#include "pluginmanager.h"
#include "processinfo.h"
#include "shareddata.h"
#include "syscallwrappers.h"
#include "syslogwrappers.h"
#include "threadlist.h"
#include "threadsync.h"
#include "util.h"

using namespace dmtcp;

LIB_PRIVATE void pthread_atfork_prepare();
LIB_PRIVATE void pthread_atfork_parent();
LIB_PRIVATE void pthread_atfork_child();

void pidVirt_pthread_atfork_child() __attribute__((weak));

/* This is defined by newer gcc version unique for each module.  */
extern void *__dso_handle __attribute__((__weak__,
                                         __visibility__("hidden")));

EXTERNC int __register_atfork(void (*prepare)(void), void (*parent)(
                                void), void (*child)(void), void *dso_handle);

EXTERNC void *ibv_get_device_list(void *) __attribute__((weak));

/* The following instance of the DmtcpWorker is just to trigger the constructor
 * to allow us to hijack the process
 */
static volatile bool exitInProgress = false;
static bool exitAfterCkpt = 0;

/* NOTE:  Please keep this function in sync with its copy at:
 *   dmtcp_nocheckpoint.cpp:restoreUserLDPRELOAD()
 */
void
restoreUserLDPRELOAD()
{
  /* A call to setenv() can result in a call to malloc(). The setenv() call may
   * also grab a low-level libc lock before calling malloc. The malloc()
   * wrapper, if present, will try to acquire the wrapper lock. This can lead
   * to a deadlock in the following scenario:
   *
   * T1 (main thread): fork() -> acquire exclusive lock
   * T2 (ckpt thread): setenv() -> acquire low-level libc lock ->
   *                   malloc -> wait for wrapper-exec lock.
   * T1: setenv() -> block on low-level libc lock (held by T2).
   *
   * The simpler solution would have been to not call setenv from DMTCP, and
   * use putenv instead. This would require larger change.
   *
   * The solution used here is to set LD_PRELOAD to "" before * user main().
   * This is as good as unsetting it.  Later, the ckpt-thread * can unset it
   * if it is still NULL, but then there is a possibility of a race between
   * user code and ckpt-thread.
   */

  // We have now successfully used LD_PRELOAD to execute prior to main()
  // Next, hide our value of LD_PRELOAD, in a global variable.
  // At checkpoint and restart time, we will no longer need our LD_PRELOAD.
  // We will need it in only one place:
  // when the user application makes an exec call:
  // If anybody calls our execwrapper, we will reset LD_PRELOAD then.
  // EXCEPTION:  If anybody directly calls _real_execve with env arg of NULL,
  // they will not be part of DMTCP computation.
  // This has the advantage that our value of LD_PRELOAD will always come
  // before any paths set by user application.
  // Also, bash likes to keep its own envp, but we will interact with bash only
  // within the exec wrapper.
  // NOTE:  If the user called exec("ssh ..."), we currently catch this in
  // src/pugin/dmtcp_ssh.cp:main(), and edit this into
  // exec("dmtcp_launch ... dmtcp_ssh ..."), and re-execute.
  // NOTE:  If the user called exec("dmtcp_nocheckpoint ..."), we will
  // reset LD_PRELOAD back to ENV_VAR_ORIG_LD_PRELOAD in dmtcp_nocheckpoint
  char *preload = getenv("LD_PRELOAD");
  char *userPreload = getenv(ENV_VAR_ORIG_LD_PRELOAD);

  JASSERT(userPreload == NULL || strlen(userPreload) <= strlen(preload));

  // Destructively modify environment variable "LD_PRELOAD" in place:
  preload[0] = '\0';
  if (userPreload == NULL) {
    // _dmtcp_unsetenv("LD_PRELOAD");
  } else {
    strcat(preload, userPreload);

    // setenv("LD_PRELOAD", userPreload, 1);
  }
  JTRACE("LD_PRELOAD") (preload) (userPreload) (getenv(ENV_VAR_HIJACK_LIBS))
    (getenv(ENV_VAR_HIJACK_LIBS_M32)) (getenv("LD_PRELOAD"));
}

// This should be visible to library only.  DmtcpWorker will call
// this to initialize tmp (ckpt signal) at startup time.  This avoids
// any later calls to getenv(), at which time the user app may have
// a wrapper around getenv, modified environ, or other tricks.
// (Matlab needs this or else it segfaults on restart, and bash plays
// similar tricks with maintaining its own environment.)
// Used in mtcpinterface.cpp and signalwrappers.cpp.
// FIXME: DO we still want it to be library visible only?
// __attribute__ ((visibility ("hidden")))
int
DmtcpWorker::determineCkptSignal()
{
  int sig = CKPT_SIGNAL;
  char *endp = NULL;
  static const char *tmp = getenv(ENV_VAR_SIGCKPT);

  if (tmp != NULL) {
    sig = strtol(tmp, &endp, 0);
    if ((errno != 0) || (tmp == endp)) {
      sig = CKPT_SIGNAL;
    }
    if (sig < 1 || sig > 31) {
      sig = CKPT_SIGNAL;
    }
  }
  return sig;
}

/* This function is called at the very beginning of the DmtcpWorker constructor
 * to do some initialization work so that DMTCP can later use _real_XXX
 * functions reliably. Read the comment at the top of syscallsreal.c for more
 * details.
 */
static void
dmtcp_prepare_atfork(void)
{
  /* Register pidVirt_pthread_atfork_child() as the first post-fork handler
   * for the child process. This needs to be the first function that is
   * called by libc:fork() after the child process is created.
   *
   * pthread_atfork_child() needs to be the second post-fork handler for the
   * child process.
   *
   * Some dmtcp plugin might also call pthread_atfork and so we call it right
   * here before initializing the wrappers.
   *
   * NOTE: If this doesn't work and someone is able to call pthread_atfork
   * before this call, we might want to install a pthread_atfork() wrapper.
   */

  /* If we use pthread_atfork here, it fails for Ubuntu 14.04 on ARM.
   * To fix it, we use __register_atfork and use the __dso_handle provided by
   * the gcc compiler.
   */
  JASSERT(__register_atfork(NULL, NULL,
                            pidVirt_pthread_atfork_child,
                            __dso_handle) == 0);

  JASSERT(pthread_atfork(pthread_atfork_prepare,
                         pthread_atfork_parent,
                         pthread_atfork_child) == 0);
}

static string
getLogFilePath()
{
#ifdef LOGGING
  ostringstream o;
  o << "/proc/self/fd/" << PROTECTED_JASSERTLOG_FD;
  return jalib::Filesystem::ResolveSymlink(o.str());

#else // ifdef LOGGING
  return "";
#endif // ifdef LOGGING
}

static void
writeCurrentLogFileNameToPrevLogFile(string &path)
{
#ifdef LOGGING
  ostringstream o;
  o << "========================================\n"
    << "This process exec()'d into a new program\n"
    << "Program Name: " << jalib::Filesystem::GetProgramName() << "\n"
    << "New JAssertLog Path: " << getLogFilePath() << "\n"
    << "========================================\n";

  int fd = open(path.c_str(), O_WRONLY | O_APPEND, 0);
  if (fd != -1) {
    Util::writeAll(fd, o.str().c_str(), o.str().length());
  }
  _real_close(fd);
#endif // ifdef LOGGING
}

static void
prepareLogAndProcessdDataFromSerialFile()
{
  if (Util::isValidFd(PROTECTED_LIFEBOAT_FD)) {
    // This process was under ckpt-control and exec()'d into a new program.
    // Find out path of previous log file so that later, we can write the name
    // of the new log file into that one.
    string prevLogFilePath = getLogFilePath();

    jalib::JBinarySerializeReaderRaw rd("", PROTECTED_LIFEBOAT_FD);
    rd.rewind();
    UniquePid::serialize(rd);
    Util::initializeLogFile(SharedData::getTmpDir().c_str(),
                            NULL,
                            prevLogFilePath.c_str());

    writeCurrentLogFileNameToPrevLogFile(prevLogFilePath);

    DmtcpEventData_t edata;
    edata.serializerInfo.fd = PROTECTED_LIFEBOAT_FD;
    PluginManager::eventHook(DMTCP_EVENT_POST_EXEC, &edata);
    _real_close(PROTECTED_LIFEBOAT_FD);
  } else {
    // Brand new process (was never under ckpt-control),
    // Initialize the log file
    Util::initializeLogFile(SharedData::getTmpDir().c_str(), NULL, NULL);

    JTRACE("Root of processes tree");
    ProcessInfo::instance().setRootOfProcessTree();
  }
}

static void
segFaultHandler(int sig, siginfo_t *siginfo, void *context)
{
  while (1) {
    sleep(1);
  }
}

static void
installSegFaultHandler()
{
  // install SIGSEGV handler
  struct sigaction act;

  memset(&act, 0, sizeof(act));
  act.sa_sigaction = segFaultHandler;
  act.sa_flags = SA_SIGINFO;
  JASSERT(sigaction(SIGSEGV, &act, NULL) == 0) (JASSERT_ERRNO);
}

// Initialize wrappers, etc.
extern "C" void
dmtcp_initialize()
{
  dmtcp_prepare_wrappers();
}

// Initialize remaining components.
extern "C" void __attribute__((constructor(101)))
dmtcp_initialize_entry_point()
{
  static bool initialized = false;

  if (initialized) {
    return;
  }

  initialized = true;

  dmtcp_initialize();

  initializeJalib();
  dmtcp_prepare_atfork();

  WorkerState::setCurrentState(WorkerState::RUNNING);

  PluginManager::initialize();

  prepareLogAndProcessdDataFromSerialFile();

  JTRACE("libdmtcp.so:  Running ")
    (jalib::Filesystem::GetProgramName()) (getenv("LD_PRELOAD"));

  if (getenv("DMTCP_SEGFAULT_HANDLER") != NULL) {
    // Install a segmentation fault handler (for debugging).
    installSegFaultHandler();
  }

  // This is called for side effect only.  Force this function to call
  // getenv(ENV_VAR_SIGCKPT) now and cache it to avoid getenv calls later.
  DmtcpWorker::determineCkptSignal();

  // Also cache programName and arguments
  string programName = jalib::Filesystem::GetProgramName();

  JASSERT(programName != "dmtcp_coordinator" &&
          programName != "dmtcp_launch" &&
          programName != "dmtcp_nocheckpoint" &&
          programName != "dmtcp_comand" &&
          programName != "dmtcp_restart" &&
          programName != "mtcp_restart" &&
          programName != "rsh" &&
          programName != "ssh")
    (programName).Text("This program should not be run under ckpt control");

  ProcessInfo::instance().calculateArgvAndEnvSize();
  restoreUserLDPRELOAD();

  if (ibv_get_device_list && !dmtcp_infiniband_enabled) {
    JNOTE("\n\n*** InfiniBand library detected."
          "  Please use dmtcp_launch --ib ***\n");
  }

  // In libdmtcp.so, notify this event for each plugin.
  PluginManager::eventHook(DMTCP_EVENT_INIT, NULL);

  ThreadSync::initMotherOfAll();
  ThreadList::init();
}

void
DmtcpWorker::resetOnFork()
{
  exitInProgress = false;

  ThreadSync::resetLocks();

  WorkerState::setCurrentState(WorkerState::RUNNING);

  ThreadSync::initMotherOfAll();

  // Some plugins might make calls that require wrapper locks, etc.
  // Therefore, it is better to call this hook after we reset all locks.
  PluginManager::eventHook(DMTCP_EVENT_ATFORK_CHILD, NULL);
}

// Called after user main() by user thread or during exit() processing.
// With a high priority, we are hoping to be called first. This would allow us
// to set the exitInProgress flag for the ckpt thread to process later on.
// There is a potential race here. If the ckpt-thread suspends the user thread
// after the user thread has called exit() but before it is able to set
// `exitInProgress` to true, the ckpt thread will go about business as usual.
// This could be problematic if the exit() handlers had destroyed some
// resources.
// A potential solution is to not rely on user-destroyable resources. That way,
// we would have everything we need in order to perform a checkpoint. On
// restart, the process will then continue through the rest of the exit
// process.
void __attribute__((destructor(65535)))
dmtcp_finalize()
{
  /* If the destructor was called, we know that we are exiting
   * After setting this, the wrapper execution locks will be ignored.
   * FIXME:  A better solution is to add a ZOMBIE state to DmtcpWorker,
   *         instead of using a separate variable, _exitInProgress.
   */
  exitInProgress = true;
  PluginManager::eventHook(DMTCP_EVENT_EXIT, NULL);

  ThreadSync::resetLocks();
  WorkerState::setCurrentState(WorkerState::UNKNOWN);

  JTRACE("Process exiting.");
}

void
DmtcpWorker::ckptThreadPerformExit()
{
  JTRACE("User thread is performing exit(). Ckpt thread exit()ing as well");

  // Ideally, we would like to perform pthread_exit(), but we are in the middle
  // of process cleanup (due to the user thread's exit() call) and as a result,
  // the static objects are being destroyed.  A call to pthread_exit() also
  // results in execution of various cleanup routines.  If the thread tries to
  // access any static objects during some cleanup routine, it will cause a
  // segfault.
  //
  // Our approach to loop here while we wait for the process to terminate.
  // This guarantees that we never access any static objects from this point
  // forward.
  while (1) {
    sleep(1);
  }
}

bool
DmtcpWorker::isExitInProgress()
{
  return exitInProgress;
}

void
DmtcpWorker::waitForPreSuspendMessage()
{
  SharedData::resetBarrierInfo();

  if (dmtcp_no_coordinator()) {
    string shmFile = jalib::Filesystem::GetDeviceName(PROTECTED_SHM_FD);
    JASSERT(!shmFile.empty());
    unlink(shmFile.c_str());
    CoordinatorAPI::waitForCheckpointCommand();
    ProcessInfo::instance().numPeers(1);
    ProcessInfo::instance().compGroup(SharedData::getCompId());
    return;
  }

  JTRACE("waiting for CHECKPOINT message");

  DmtcpMessage msg;
  CoordinatorAPI::recvMsgFromCoordinator(&msg);

  // Before validating message; make sure we are not exiting.
  if (exitInProgress) {
    ckptThreadPerformExit();
  }

  msg.assertValid();

  JASSERT(msg.type == DMT_DO_CHECKPOINT) (msg.type);

  // Coordinator sends some computation information along with the SUSPEND
  // message. Extracting that.
  SharedData::updateGeneration(msg.compGroup.computationGeneration());
  JASSERT(SharedData::getCompId() == msg.compGroup.upid())
    (SharedData::getCompId()) (msg.compGroup);

  ProcessInfo::instance().compGroup(SharedData::getCompId());
  exitAfterCkpt = msg.exitAfterCkpt;
}

void
DmtcpWorker::waitForCheckpointRequest()
{
  JTRACE("running");

  WorkerState::setCurrentState(WorkerState::RUNNING);

  waitForPreSuspendMessage();

  WorkerState::setCurrentState(WorkerState::PRESUSPEND);

  JTRACE("Procesing pre-suspend barriers");
  PluginManager::eventHook(DMTCP_EVENT_PRESUSPEND);

  JTRACE("Waiting for DMT:SUSPEND barrier");
  if (!CoordinatorAPI::waitForBarrier("DMT:SUSPEND")) {
    JASSERT(exitInProgress);
    ckptThreadPerformExit();
  }

  JTRACE("DMT:SUSPEND barrier lifted, preparing to acquire locks");
  ThreadSync::acquireLocks();

  JTRACE("Starting checkpoint, suspending threads...");
}

// now user threads are stopped
void
DmtcpWorker::preCheckpoint()
{
  JTRACE("Threads suspended");
  WorkerState::setCurrentState(WorkerState::SUSPENDED);

  ThreadSync::releaseLocks();

  if (exitInProgress) {
    // There is no reason to continue checkpointing this process as it would
    // simply die right after resume/restore.
    // Release user threads from ckpt signal handler.
    ThreadList::resumeThreads();
    ckptThreadPerformExit();
  }

  // Update generation, in case user callback calls dmtcp_get_generation().
  uint32_t computationGeneration =
    SharedData::getCompId()._computation_generation;
  ProcessInfo::instance().set_generation(computationGeneration);

  SharedData::prepareForCkpt();

  uint32_t numPeers;
  JTRACE("Waiting for DMT_CHECKPOINT barrier");
  CoordinatorAPI::waitForBarrier("DMT:CHECKPOINT", &numPeers);
  JTRACE("Computation information") (numPeers);

  ProcessInfo::instance().numPeers(numPeers);

  WorkerState::setCurrentState(WorkerState::CHECKPOINTING);
  PluginManager::eventHook(DMTCP_EVENT_PRECHECKPOINT);
}

void
DmtcpWorker::postCheckpoint()
{
  WorkerState::setCurrentState(WorkerState::CHECKPOINTED);
  CoordinatorAPI::sendCkptFilename();

  if (exitAfterCkpt) {
    JTRACE("Asked to exit after checkpoint. Exiting!");
    _exit(0);
  }

  PluginManager::eventHook(DMTCP_EVENT_RESUME);

  // Inform Coordinator of RUNNING state.
  WorkerState::setCurrentState(WorkerState::RUNNING);
  JTRACE("Informing coordinator of RUNNING status") (UniquePid::ThisProcess());
  CoordinatorAPI::sendMsgToCoordinator(DMT_WORKER_RESUMING);
}

void
DmtcpWorker::postRestart(double ckptReadTime)
{
  JTRACE("begin postRestart()");
  WorkerState::setCurrentState(WorkerState::RESTARTING);

  PluginManager::eventHook(DMTCP_EVENT_RESTART);

  JTRACE("got resume message after restart");

  // Inform Coordinator of RUNNING state.
  WorkerState::setCurrentState(WorkerState::RUNNING);
  JTRACE("Informing coordinator of RUNNING status") (UniquePid::ThisProcess());
  CoordinatorAPI::sendMsgToCoordinator(DMT_WORKER_RESUMING);
}
