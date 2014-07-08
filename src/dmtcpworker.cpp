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
#include "mtcpinterface.h"
#include "threadsync.h"
#include "processinfo.h"
#include "syscallwrappers.h"
#include "util.h"
#include "syslogwrappers.h"
#include "coordinatorapi.h"
#include "shareddata.h"
#include "threadlist.h"
#include  "../jalib/jsocket.h"
#include  "../jalib/jfilesystem.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jbuffer.h"

using namespace dmtcp;

LIB_PRIVATE int dmtcp_wrappers_initializing = 0;

LIB_PRIVATE void pthread_atfork_prepare();
LIB_PRIVATE void pthread_atfork_parent();
LIB_PRIVATE void pthread_atfork_child();

bool dmtcp::DmtcpWorker::_exitInProgress = false;

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
  //   If they directly call _real_execve to get libc symbol, they will
  //   not be part of DMTCP computation.
  // This has the advantage that our value of LD_PRELOAD will always come
  //   before any paths set by user application.
  // Also, bash likes to keep its own envp, but we will interact with bash only
  //   within the exec wrapper.
  // NOTE:  If the user called exec("ssh ..."), we currently catch this in
  //   DmtcpWorker() due to LD_PRELOAD, unset LD_PRELOAD, and edit this into
  //   exec("dmtcp_launch --ssh-slave ... ssh ..."), and re-execute.
  //   This way, we will unset LD_PRELOAD here and now, instead of at that time.
  char *preload = getenv("LD_PRELOAD");
  char *userPreload =  getenv(ENV_VAR_ORIG_LD_PRELOAD);
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
int dmtcp::DmtcpWorker::determineCkptSignal()
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
static bool dmtcpWrappersInitialized = false;
extern "C" void dmtcp_prepare_wrappers(void)
{
  if (!dmtcpWrappersInitialized) {
    // FIXME: Remove JALLOC_HELPER_... after the release.
    JALLOC_HELPER_DISABLE_LOCKS();
    dmtcp_wrappers_initializing = 1;
    initialize_libc_wrappers();
    //dmtcp::DmtcpWorker::eventHook(DMTCP_EVENT_INIT_WRAPPERS, NULL);
    dmtcp_wrappers_initializing = 0;
    initialize_libpthread_wrappers();
    JALLOC_HELPER_ENABLE_LOCKS();
    dmtcpWrappersInitialized = true;

    /* Register pthread_atfork_child() as the first post-fork handler for the
     * child process. This needs to be the first function that is called by
     * libc:fork() after the child process is created.
     *
     * Some dmtcp plugin might also call pthread_atfork and so we call it right
     * here before initializing the wrappers.
     *
     * NOTE: If this doesn't work and someone is able to call pthead_atfork
     * before this call, we might want to install a pthread_atfork() wrappers.
     */
    JASSERT(pthread_atfork(pthread_atfork_prepare,
                           pthread_atfork_parent,
                           pthread_atfork_child) == 0);
  }
}

static void calculateArgvAndEnvSize()
{
  size_t argvSize, envSize;

  dmtcp::vector<dmtcp::string> args = jalib::Filesystem::GetProgramArgs();
  argvSize = 0;
  for (size_t i = 0; i < args.size(); i++) {
    argvSize += args[i].length() + 1;
  }
  envSize = 0;
  if (environ != NULL) {
    char *ptr = environ[0];
    while (*ptr != '\0' && args[0].compare(ptr) != 0) {
      envSize += strlen(ptr) + 1;
      ptr += strlen(ptr) + 1;
    }
  }
  envSize += args[0].length();

  dmtcp::ProcessInfo::instance().argvSize(argvSize);
  dmtcp::ProcessInfo::instance().envSize(envSize);
}

static dmtcp::string getLogFilePath()
{
#ifdef DEBUG
  dmtcp::ostringstream o;
  o << "/proc/self/fd/" << PROTECTED_JASSERTLOG_FD;
  return jalib::Filesystem::ResolveSymlink(o.str());
#else
  return "";
#endif
}

static void writeCurrentLogFileNameToPrevLogFile(dmtcp::string& path)
{
#ifdef DEBUG
  dmtcp::ostringstream o;
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
    dmtcp::string prevLogFilePath = getLogFilePath();

    jalib::JBinarySerializeReaderRaw rd ("", PROTECTED_LIFEBOAT_FD);
    rd.rewind();
    UniquePid::serialize (rd);
    Util::initializeLogFile("", prevLogFilePath);

    writeCurrentLogFileNameToPrevLogFile(prevLogFilePath);

    DmtcpEventData_t edata;
    edata.serializerInfo.fd = PROTECTED_LIFEBOAT_FD;
    dmtcp::DmtcpWorker::eventHook(DMTCP_EVENT_POST_EXEC, &edata);
    _real_close(PROTECTED_LIFEBOAT_FD);
  } else {
    // Brand new process (was never under ckpt-control),
    // Initialize the log file
    Util::initializeLogFile();

    JTRACE("Root of processes tree");
    ProcessInfo::instance().setRootOfProcessTree();
  }
}

static void processRlimit()
{
#ifdef __i386__
  // Match work begun in dmtcpPrepareForExec()
# if 0
  if (getenv("DMTCP_ADDR_COMPAT_LAYOUT")) {
    _dmtcp_unsetenv("DMTCP_ADDR_COMPAT_LAYOUT");
    // DMTCP had set ADDR_COMPAT_LAYOUT.  Now unset it.
    personality((unsigned long)personality(0xffffffff) ^ ADDR_COMPAT_LAYOUT);
    JTRACE("unsetting ADDR_COMPAT_LAYOUT");
  }
# else
  { char * rlim_cur_char = getenv("DMTCP_RLIMIT_STACK");
    if (rlim_cur_char != NULL) {
      struct rlimit rlim;
      getrlimit(RLIMIT_STACK, &rlim);
      rlim.rlim_cur = atol(rlim_cur_char);
      JTRACE("rlim_cur for RLIMIT_STACK being restored.") (rlim.rlim_cur);
      setrlimit(RLIMIT_STACK, &rlim);
      _dmtcp_unsetenv("DMTCP_RLIMIT_STACK");
    }
  }
# endif
#endif
}

dmtcp::DmtcpWorker dmtcp::DmtcpWorker::theInstance ( true );
dmtcp::DmtcpWorker& dmtcp::DmtcpWorker::instance() { return theInstance; }

//called before user main()
//workerhijack.cpp initializes a static variable theInstance to DmtcpWorker obj
dmtcp::DmtcpWorker::DmtcpWorker (bool enableCheckpointing)
{
  if (!enableCheckpointing) return;
  else {
    WorkerState::setCurrentState(WorkerState::UNKNOWN);
    initializeJalib();
    dmtcp_prepare_wrappers();
    prepareLogAndProcessdDataFromSerialFile();
  }

  JTRACE("libdmtcp.so:  Running ")
    (jalib::Filesystem::GetProgramName()) (getenv ("LD_PRELOAD"));

  if (getenv(ENV_VAR_UTILITY_DIR) == NULL) {
    JNOTE("\n **** Not checkpointing this process,"
            " due to missing environment var ****")
          (getenv(ENV_VAR_UTILITY_DIR))
          (jalib::Filesystem::GetProgramName());
    return;
  }

  processRlimit();

  //This is called for side effect only.  Force this function to call
  // getenv(ENV_VAR_SIGCKPT) now and cache it to avoid getenv calls later.
  determineCkptSignal();

  // Also cache programName and arguments
  dmtcp::string programName = jalib::Filesystem::GetProgramName();
  dmtcp::vector<dmtcp::string> args = jalib::Filesystem::GetProgramArgs();

  JASSERT(programName != "dmtcp_coordinator"  &&
          programName != "dmtcp_launch"   &&
          programName != "dmtcp_nocheckpoint" &&
          programName != "dmtcp_comand"       &&
          programName != "dmtcp_restart"      &&
          programName != "mtcp_restart"       &&
          programName != "ssh")
    (programName) .Text("This program should not be run under ckpt control");

  calculateArgvAndEnvSize();
  restoreUserLDPRELOAD();

  WorkerState::setCurrentState (WorkerState::RUNNING);
  // define "Weak Symbols for each library plugin in libdmtcp.so
  eventHook(DMTCP_EVENT_INIT, NULL);

  initializeMtcpEngine();
  informCoordinatorOfRUNNINGState();
}

void dmtcp::DmtcpWorker::resetOnFork()
{
  eventHook(DMTCP_EVENT_ATFORK_CHILD, NULL);

  theInstance.cleanupWorker();

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
  new ( &theInstance ) DmtcpWorker ( false );

  ThreadList::resetOnFork();

  dmtcp::DmtcpWorker::_exitInProgress = false;

  WorkerState::setCurrentState ( WorkerState::RUNNING );

}

void dmtcp::DmtcpWorker::cleanupWorker()
{
  ThreadSync::resetLocks();
  WorkerState::setCurrentState(WorkerState::UNKNOWN);
  JTRACE("disconnecting from dmtcp coordinator");
}

void dmtcp::DmtcpWorker::interruptCkpthread()
{
  if (ThreadSync::destroyDmtcpWorkerLockTryLock() == EBUSY) {
    ThreadList::killCkpthread();
    ThreadSync::destroyDmtcpWorkerLockLock();
  }
}

//called after user main()
dmtcp::DmtcpWorker::~DmtcpWorker()
{
  if (exitInProgress()) {
    /*
     * Exit race fixed. If the user threads calls exit(), ~DmtcpWorker() is
     * called.  Now if the ckpt-thread is trying to use DmtcpWorker object
     * while it is being destroyed, there is a problem.
     *
     * The fix here is to raise the flag exitInProgress in the exit() system
     * call wrapper. Later in ~DmtcpWorker() we check if the flag has been
     * raised or not.  If the exitInProgress flag has been raised, it closes
     * the coordinator socket and tries to acquire destroyDmtcpWorker mutex.
     *
     * The ckpt-thread tries to acquire the destroyDmtcpWorker mutex before
     * writing/reading any message to/from coordinator socket while the user
     * threads are running (i.e. messages like DMT_SUSPEND, DMT_SUSPENDED
     * etc.)_. If it fails to acquire the lock, it verifies that the
     * exitInProgress has been raised and performs pthread_exit().
     *
     * As obvious, once the user threads have been suspended the ckpt-thread
     *  releases the destroyDmtcpWorker() mutex and continues normal execution.
     */
    eventHook(DMTCP_EVENT_EXIT, NULL);
    interruptCkpthread();
  }
  cleanupWorker();
}

void dmtcp::DmtcpWorker::waitForCoordinatorMsg(dmtcp::string msgStr,
                                               DmtcpMessageType type)
{
  if (dmtcp_no_coordinator()) {
    if (type == DMT_DO_SUSPEND) {
      string shmFile = jalib::Filesystem::GetDeviceName(PROTECTED_SHM_FD);
      JASSERT(!shmFile.empty());
      unlink(shmFile.c_str());
      CoordinatorAPI::instance().waitForCheckpointCommand();
      ProcessInfo::instance().numPeers(1);
      ProcessInfo::instance().compGroup(SharedData::getCompId());
    }
    return;
  }

  if (type == DMT_DO_SUSPEND) {
    if (ThreadSync::destroyDmtcpWorkerLockTryLock() != 0) {
      JTRACE("User thread is performing exit()."
               " ckpt thread exit()ing as well");
      pthread_exit(NULL);
    }
    if (exitInProgress()) {
      ThreadSync::destroyDmtcpWorkerLockUnlock();
      pthread_exit(NULL);
    }
  }

  dmtcp::DmtcpMessage msg;

  if (type == DMT_DO_SUSPEND) {
    // Make a dummy syscall to inform superior of our status before we go into
    // select. If ptrace is disabled, this call has no significant effect.
    _real_syscall(DMTCP_FAKE_SYSCALL);
  } else {
    msg.type = DMT_OK;
    msg.state = WorkerState::currentState();
    CoordinatorAPI::instance().sendMsgToCoordinator(msg);
  }

  JTRACE("waiting for " + msgStr + " message");
  CoordinatorAPI::instance().recvMsgFromCoordinator(&msg);
  if (type == DMT_DO_SUSPEND && exitInProgress()) {
    ThreadSync::destroyDmtcpWorkerLockUnlock();
    pthread_exit(NULL);
  }

  msg.assertValid();
  if (msg.type == DMT_KILL_PEER) {
    JTRACE("Received KILL message from coordinator, exiting");
    _exit (0);
  }
  JASSERT(msg.type == type) (msg.type) (type);

  // Coordinator sends some computation information along with the SUSPEND
  // message. Extracting that.
  if (type == DMT_DO_SUSPEND) {
    SharedData::updateGeneration(msg.compGroup.generation());
    JASSERT(SharedData::getCompId() == msg.compGroup.upid())
      (SharedData::getCompId()) (msg.compGroup);
  } else if (type == DMT_DO_FD_LEADER_ELECTION) {
    JTRACE("Computation information") (msg.compGroup) (msg.numPeers);
    ProcessInfo::instance().compGroup(msg.compGroup);
    ProcessInfo::instance().numPeers(msg.numPeers);
  }
}

void dmtcp::DmtcpWorker::informCoordinatorOfRUNNINGState()
{
  dmtcp::DmtcpMessage msg;

  JASSERT(WorkerState::currentState() == WorkerState::RUNNING);

  msg.type = DMT_OK;
  msg.state = WorkerState::currentState();
  CoordinatorAPI::instance().sendMsgToCoordinator(msg);
}

void dmtcp::DmtcpWorker::waitForStage1Suspend()
{
  JTRACE("running");

  WorkerState::setCurrentState (WorkerState::RUNNING);

  waitForCoordinatorMsg ("SUSPEND", DMT_DO_SUSPEND);

  JTRACE("got SUSPEND message, preparing to acquire all ThreadSync locks");
  ThreadSync::acquireLocks();

  JTRACE("Starting checkpoint, suspending...");
}

void dmtcp::DmtcpWorker::waitForStage2Checkpoint()
{
  WorkerState::setCurrentState (WorkerState::SUSPENDED);
  JTRACE("suspended");

  if (exitInProgress()) {
    ThreadSync::destroyDmtcpWorkerLockUnlock();
    pthread_exit(NULL);
  }
  ThreadSync::destroyDmtcpWorkerLockUnlock();

  ThreadSync::releaseLocks();

  SyslogCheckpointer::stopService();

  eventHook(DMTCP_EVENT_THREADS_SUSPEND, NULL);

  waitForCoordinatorMsg ("FD_LEADER_ELECTION", DMT_DO_FD_LEADER_ELECTION);

  eventHook(DMTCP_EVENT_LEADER_ELECTION, NULL);

  WorkerState::setCurrentState (WorkerState::FD_LEADER_ELECTION);

  waitForCoordinatorMsg ("DRAIN", DMT_DO_DRAIN);

  WorkerState::setCurrentState (WorkerState::DRAINED);

  eventHook(DMTCP_EVENT_DRAIN, NULL);

  waitForCoordinatorMsg ("CHECKPOINT", DMT_DO_CHECKPOINT);
  JTRACE("got checkpoint message");

  eventHook(DMTCP_EVENT_WRITE_CKPT, NULL);

  // Unmap shared area
  dmtcp::SharedData::preCkpt();
}

void dmtcp::DmtcpWorker::waitForStage3Refill(bool isRestart)
{
  DmtcpEventData_t edata;
  JTRACE("checkpointed");

  WorkerState::setCurrentState (WorkerState::CHECKPOINTED);

#ifdef COORD_NAMESERVICE
  waitForCoordinatorMsg("REGISTER_NAME_SERVICE_DATA",
                          DMT_DO_REGISTER_NAME_SERVICE_DATA);
  edata.nameserviceInfo.isRestart = isRestart;
  eventHook(DMTCP_EVENT_REGISTER_NAME_SERVICE_DATA, &edata);
  JTRACE("Key Value Pairs registered with the coordinator");
  WorkerState::setCurrentState(WorkerState::NAME_SERVICE_DATA_REGISTERED);

  waitForCoordinatorMsg("SEND_QUERIES", DMT_DO_SEND_QUERIES);
  eventHook(DMTCP_EVENT_SEND_QUERIES, &edata);
  JTRACE("Queries sent to the coordinator");
  WorkerState::setCurrentState(WorkerState::DONE_QUERYING);
#endif

  waitForCoordinatorMsg ("REFILL", DMT_DO_REFILL);

  SyslogCheckpointer::restoreService();

  edata.refillInfo.isRestart = isRestart;
  dmtcp::DmtcpWorker::eventHook(DMTCP_EVENT_REFILL, &edata);
}

void dmtcp::DmtcpWorker::waitForStage4Resume(bool isRestart)
{
  JTRACE("refilled");
  WorkerState::setCurrentState (WorkerState::REFILLED);
  waitForCoordinatorMsg ("RESUME", DMT_DO_RESUME);
  JTRACE("got resume message");
  DmtcpEventData_t edata;
  edata.resumeInfo.isRestart = isRestart;
  dmtcp::DmtcpWorker::eventHook(DMTCP_EVENT_THREADS_RESUME, &edata);
}

void dmtcp_CoordinatorAPI_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data);
void dmtcp_SharedData_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data);
void dmtcp_ProcessInfo_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data);
void dmtcp_UniquePid_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data);

void dmtcp::DmtcpWorker::eventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  static jalib::JBuffer buf(0); // To force linkage of jbuffer.cpp
  dmtcp_UniquePid_EventHook(event, data);
  dmtcp_CoordinatorAPI_EventHook(event, data);
  dmtcp_SharedData_EventHook(event, data);
  dmtcp_ProcessInfo_EventHook(event, data);
  if (dmtcp_event_hook != NULL) {
    dmtcp_event_hook(event, data);
  }
}
