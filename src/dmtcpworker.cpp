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

#include <stdlib.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "dmtcpworker.h"
#include "threadsync.h"
#include "processinfo.h"
#include "syscallwrappers.h"
#include "util.h"
#include "syslogwrappers.h"
#include "coordinatorapi.h"
#include "shareddata.h"
#include "threadlist.h"
#include "pluginmanager.h"
#include  "../jalib/jsocket.h"
#include  "../jalib/jfilesystem.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jbuffer.h"

using namespace dmtcp;

LIB_PRIVATE void pthread_atfork_prepare();
LIB_PRIVATE void pthread_atfork_parent();
LIB_PRIVATE void pthread_atfork_child();

void pidVirt_pthread_atfork_child() __attribute__((weak));

/* This is defined by newer gcc version unique for each module.  */
extern void *__dso_handle __attribute__ ((__weak__,
					  __visibility__ ("hidden")));

EXTERNC int __register_atfork(void (*prepare)(void), void (*parent)(void),
                              void (*child)(void), void *dso_handle);

EXTERNC void *ibv_get_device_list(void *) __attribute__((weak));

/* The following instance of the DmtcpWorker is just to trigger the constructor
 * to allow us to hijack the process
 */
DmtcpWorker DmtcpWorker::theInstance;
bool DmtcpWorker::_exitInProgress = false;


/* NOTE:  Please keep this function in sync with its copy at:
 *   dmtcp_nocheckpoint.cpp:restoreUserLDPRELOAD()
 */
void restoreUserLDPRELOAD()
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
  //  when the user application makes an exec call:
  //   If anybody calls our execwrapper, we will reset LD_PRELOAD then.
  //   EXCEPTION:  If anybody directly calls _real_execve with env arg of NULL,
  //   they will not be part of DMTCP computation.
  // This has the advantage that our value of LD_PRELOAD will always come
  //   before any paths set by user application.
  // Also, bash likes to keep its own envp, but we will interact with bash only
  //   within the exec wrapper.
  // NOTE:  If the user called exec("ssh ..."), we currently catch this in
  //   src/pugin/dmtcp_ssh.cp:main(), and edit this into
  //   exec("dmtcp_launch ... dmtcp_ssh ..."), and re-execute.
  // NOTE:  If the user called exec("dmtcp_nocheckpoint ..."), we will
  //   reset LD_PRELOAD back to ENV_VAR_ORIG_LD_PRELOAD in dmtcp_nocheckpoint
  char *preload = getenv("LD_PRELOAD");
  char *userPreload = getenv(ENV_VAR_ORIG_LD_PRELOAD);
  JASSERT(userPreload == NULL || strlen(userPreload) <= strlen(preload));
  // Destructively modify environment variable "LD_PRELOAD" in place:
  preload[0] = '\0';
  if (userPreload == NULL) {
    //_dmtcp_unsetenv("LD_PRELOAD");
  } else {
    strcat(preload, userPreload);
    //setenv("LD_PRELOAD", userPreload, 1);
  }
  JTRACE("LD_PRELOAD") (preload) (userPreload) (getenv(ENV_VAR_HIJACK_LIBS))
    (getenv(ENV_VAR_HIJACK_LIBS_M32)) (getenv("LD_PRELOAD"));
}

// This should be visible to library only.  DmtcpWorker will call
//   this to initialize tmp (ckpt signal) at startup time.  This avoids
//   any later calls to getenv(), at which time the user app may have
//   a wrapper around getenv, modified environ, or other tricks.
//   (Matlab needs this or else it segfaults on restart, and bash plays
//   similar tricks with maintaining its own environment.)
// Used in mtcpinterface.cpp and signalwrappers.cpp.
// FIXME: DO we still want it to be library visible only?
//__attribute__ ((visibility ("hidden")))
int DmtcpWorker::determineCkptSignal()
{
  int sig = CKPT_SIGNAL;
  char* endp = NULL;
  static const char* tmp = getenv(ENV_VAR_SIGCKPT);
  if (tmp != NULL) {
      sig = strtol(tmp, &endp, 0);
      if ((errno != 0) || (tmp == endp))
        sig = CKPT_SIGNAL;
      if (sig < 1 || sig > 31)
        sig = CKPT_SIGNAL;
  }
  return sig;
}

/* This function is called at the very beginning of the DmtcpWorker constructor
 * to do some initialization work so that DMTCP can later use _real_XXX
 * functions reliably. Read the comment at the top of syscallsreal.c for more
 * details.
 */
static void dmtcp_prepare_atfork(void)
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

static string getLogFilePath()
{
#ifdef DEBUG
  ostringstream o;
  o << "/proc/self/fd/" << PROTECTED_JASSERTLOG_FD;
  return jalib::Filesystem::ResolveSymlink(o.str());
#else
  return "";
#endif
}

static void writeCurrentLogFileNameToPrevLogFile(string& path)
{
#ifdef DEBUG
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
#endif
}

static void prepareLogAndProcessdDataFromSerialFile()
{

  if (Util::isValidFd(PROTECTED_LIFEBOAT_FD)) {
    // This process was under ckpt-control and exec()'d into a new program.
    // Find out path of previous log file so that later, we can write the name
    // of the new log file into that one.
    string prevLogFilePath = getLogFilePath();

    jalib::JBinarySerializeReaderRaw rd ("", PROTECTED_LIFEBOAT_FD);
    rd.rewind();
    UniquePid::serialize (rd);
    Util::initializeLogFile(SharedData::getTmpDir(), "", prevLogFilePath);

    writeCurrentLogFileNameToPrevLogFile(prevLogFilePath);

    DmtcpEventData_t edata;
    edata.serializerInfo.fd = PROTECTED_LIFEBOAT_FD;
    PluginManager::eventHook(DMTCP_EVENT_POST_EXEC, &edata);
    _real_close(PROTECTED_LIFEBOAT_FD);
  } else {
    // Brand new process (was never under ckpt-control),
    // Initialize the log file
    Util::initializeLogFile(SharedData::getTmpDir());

    JTRACE("Root of processes tree");
    ProcessInfo::instance().setRootOfProcessTree();
  }
}

static void segFaultHandler(int sig, siginfo_t* siginfo, void* context)
{
  while (1) sleep(1);
}

static void installSegFaultHandler()
{
  // install SIGSEGV handler
  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_sigaction = segFaultHandler;
  act.sa_flags = SA_SIGINFO;
  JASSERT (sigaction(SIGSEGV, &act, NULL) == 0) (JASSERT_ERRNO);
}

static jalib::JBuffer buf(0); // To force linkage of jbuffer.cpp

//called before user main()
//workerhijack.cpp initializes a static variable theInstance to DmtcpWorker obj
DmtcpWorker::DmtcpWorker()
{
  WorkerState::setCurrentState(WorkerState::UNKNOWN);

  dmtcp_prepare_wrappers();
  initializeJalib();
  dmtcp_prepare_atfork();
  PluginManager::initialize();
  prepareLogAndProcessdDataFromSerialFile();

  JTRACE("libdmtcp.so:  Running ")
    (jalib::Filesystem::GetProgramName()) (getenv ("LD_PRELOAD"));

  if (getenv("DMTCP_SEGFAULT_HANDLER") != NULL) {
    // Install a segmentation fault handler (for debugging).
    installSegFaultHandler();
  }

  //This is called for side effect only.  Force this function to call
  // getenv(ENV_VAR_SIGCKPT) now and cache it to avoid getenv calls later.
  determineCkptSignal();

  // Also cache programName and arguments
  string programName = jalib::Filesystem::GetProgramName();

  JASSERT(programName != "dmtcp_coordinator"  &&
          programName != "dmtcp_launch"   &&
          programName != "dmtcp_nocheckpoint" &&
          programName != "dmtcp_comand"       &&
          programName != "dmtcp_restart"      &&
          programName != "mtcp_restart"       &&
          programName != "ssh")
    (programName) .Text("This program should not be run under ckpt control");

  ProcessInfo::instance().calculateArgvAndEnvSize();
  restoreUserLDPRELOAD();

  WorkerState::setCurrentState (WorkerState::RUNNING);

  if (ibv_get_device_list && !dmtcp_infiniband_enabled) {
    JNOTE("\n\n*** InfiniBand library detected."
          "  Please use dmtcp_launch --ib ***\n");
  }

  // In libdmtcp.so, notify this event for each plugin.
  PluginManager::eventHook(DMTCP_EVENT_INIT, NULL);

  ThreadSync::initMotherOfAll();
  ThreadList::init();
}

void DmtcpWorker::resetOnFork()
{
  PluginManager::eventHook(DMTCP_EVENT_ATFORK_CHILD, NULL);

  cleanupWorker();

  WorkerState::setCurrentState ( WorkerState::RUNNING );

  /* If parent process had file connections and it fork()'d a child
   * process, the child process would consider the file connections as
   * pre-existing and hence wouldn't restore them. This is fixed by making sure
   * that when a child process is forked, it shouldn't be looking for
   * pre-existing connections because the parent has already done that.
   *
   * So, here while creating the instance, we do not want to execute everything
   * in the constructor since it's not relevant. All we need to call is
   * connectToCoordinatorWithHandshake() and initializeMtcpEngine().
   */
  //new ( &theInstance ) DmtcpWorker ( false );

  ThreadList::resetOnFork();
  ThreadSync::initMotherOfAll();

  DmtcpWorker::_exitInProgress = false;
}

void DmtcpWorker::cleanupWorker()
{
  ThreadSync::resetLocks();
  WorkerState::setCurrentState(WorkerState::UNKNOWN);
  JTRACE("disconnecting from dmtcp coordinator");
}

void DmtcpWorker::interruptCkpthread()
{
  if (ThreadSync::destroyDmtcpWorkerLockTryLock() == EBUSY) {
    ThreadList::killCkpthread();
    ThreadSync::destroyDmtcpWorkerLockLock();
  }
}

//called after user main()
DmtcpWorker::~DmtcpWorker()
{
  /* If the destructor was called, we know that we are exiting
   * After setting this, the wrapper execution locks will be ignored.
   * FIXME:  A better solution is to add a ZOMBIE state to DmtcpWorker,
   *         instead of using a separate variable, _exitInProgress.
   */
  setExitInProgress();
  PluginManager::eventHook(DMTCP_EVENT_EXIT, NULL);
  interruptCkpthread();
  cleanupWorker();
}

static void ckptThreadPerformExit()
{
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
  while (1) sleep(1);
}

void DmtcpWorker::waitForSuspendMessage()
{
  SharedData::resetBarrierInfo();
  if (dmtcp_no_coordinator()) {
    string shmFile = jalib::Filesystem::GetDeviceName(PROTECTED_SHM_FD);
    JASSERT(!shmFile.empty());
    unlink(shmFile.c_str());
    CoordinatorAPI::instance().waitForCheckpointCommand();
    ProcessInfo::instance().numPeers(1);
    ProcessInfo::instance().compGroup(SharedData::getCompId());
    return;
  }

  if (ThreadSync::destroyDmtcpWorkerLockTryLock() != 0) {
    JTRACE("User thread is performing exit()."
        " ckpt thread exit()ing as well");
    ckptThreadPerformExit();
  }
  if (exitInProgress()) {
    ThreadSync::destroyDmtcpWorkerLockUnlock();
    ckptThreadPerformExit();
  }

  // Inform Coordinator of RUNNING state.
  CoordinatorAPI::instance().sendMsgToCoordinator(DmtcpMessage(DMT_OK));

  JTRACE("waiting for SUSPEND message");

  DmtcpMessage msg;
  CoordinatorAPI::instance().recvMsgFromCoordinator(&msg);

  if (exitInProgress()) {
    ThreadSync::destroyDmtcpWorkerLockUnlock();
    ckptThreadPerformExit();
  }

  msg.assertValid();
  if (msg.type == DMT_KILL_PEER) {
    JTRACE("Received KILL message from coordinator, exiting");
    _exit (0);
  }

  JASSERT(msg.type == DMT_DO_SUSPEND) (msg.type);

  // Coordinator sends some computation information along with the SUSPEND
  // message. Extracting that.
  SharedData::updateGeneration(msg.compGroup.computationGeneration());
  JASSERT(SharedData::getCompId() == msg.compGroup.upid())
    (SharedData::getCompId()) (msg.compGroup);
}

void DmtcpWorker::acknowledgeSuspendMsg()
{
  if (dmtcp_no_coordinator()) {
    return;
  }

  JTRACE("Waiting for DMT_DO_CHECKPOINT message");
  CoordinatorAPI::instance().sendMsgToCoordinator(DmtcpMessage(DMT_OK));

  DmtcpMessage msg;
  CoordinatorAPI::instance().recvMsgFromCoordinator(&msg);
  msg.assertValid();
  if (msg.type == DMT_KILL_PEER) {
    JTRACE("Received KILL message from coordinator, exiting");
    _exit (0);
  }

  JASSERT(msg.type == DMT_COMPUTATION_INFO) (msg.type);
  JTRACE("Computation information") (msg.compGroup) (msg.numPeers);
  ProcessInfo::instance().compGroup(msg.compGroup);
  ProcessInfo::instance().numPeers(msg.numPeers);
}


void DmtcpWorker::waitForCheckpointRequest()
{
  JTRACE("running");

  WorkerState::setCurrentState (WorkerState::RUNNING);

  waitForSuspendMessage();

  JTRACE("got SUSPEND message, preparing to acquire all ThreadSync locks");
  ThreadSync::acquireLocks();

  JTRACE("Starting checkpoint, suspending...");
}

//now user threads are stopped
void DmtcpWorker::preCheckpoint()
{
  WorkerState::setCurrentState (WorkerState::SUSPENDED);
  JTRACE("suspended");

  if (exitInProgress()) {
    ThreadSync::destroyDmtcpWorkerLockUnlock();
    ckptThreadPerformExit();
  }
  ThreadSync::destroyDmtcpWorkerLockUnlock();

  ThreadSync::releaseLocks();

  // Update generation, in case user callback calls dmtcp_get_generation().
  uint32_t computationGeneration =
    SharedData::getCompId()._computation_generation;
  ProcessInfo::instance().set_generation(computationGeneration);

  SharedData::prepareForCkpt();

  acknowledgeSuspendMsg();

  WorkerState::setCurrentState(WorkerState::CHECKPOINTING);
  PluginManager::processCkptBarriers();
}

void DmtcpWorker::postCheckpoint()
{
  WorkerState::setCurrentState(WorkerState::CHECKPOINTED);
  CoordinatorAPI::instance().sendCkptFilename();

  PluginManager::processResumeBarriers();
  WorkerState::setCurrentState( WorkerState::RUNNING );
}

void DmtcpWorker::postRestart()
{
  JTRACE("begin postRestart()");
  WorkerState::setCurrentState(WorkerState::RESTARTING);

  PluginManager::processRestartBarriers();
  JTRACE("got resume message after restart");

  WorkerState::setCurrentState( WorkerState::RUNNING );
}
