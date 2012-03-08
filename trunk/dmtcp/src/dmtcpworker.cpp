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

#include <unistd.h>
#include <map>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/personality.h>
#include <netdb.h>

#include "dmtcpworker.h"
#include "threadsync.h"
#include "constants.h"
#include "dmtcpmessagetypes.h"
#include "dmtcpmodule.h"
#include "mtcpinterface.h"
#include "processinfo.h"
#include "syscallwrappers.h"
#include "protectedfds.h"
#include "connectionidentifier.h"
#include "connectionmanager.h"
#include "connectionstate.h"
#include "ckptserializer.h"
#include "remexecwrappers.h"
#include "util.h"
#include "sysvipc.h"
#include  "../jalib/jsocket.h"
#include  "../jalib/jfilesystem.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jalloc.h"

using namespace dmtcp;

LIB_PRIVATE int dmtcp_wrappers_initializing = 0;

// static dmtcp::KernelBufferDrainer* theDrainer = NULL;
static dmtcp::ConnectionState* theCheckpointState = NULL;

#ifdef EXTERNAL_SOCKET_HANDLING
static dmtcp::vector <dmtcp::TcpConnectionInfo> theTcpConnections;
LIB_PRIVATE dmtcp::vector <dmtcp::ConnectionIdentifier> externalTcpConnections;
static bool _waitingForExternalSocketsToClose = false;
#endif

LIB_PRIVATE void pthread_atfork_prepare();
LIB_PRIVATE void pthread_atfork_parent();
LIB_PRIVATE void pthread_atfork_child();

bool dmtcp::DmtcpWorker::_exitInProgress = false;

static void processDmtcpCommands(dmtcp::string programName,
                                 dmtcp::vector<dmtcp::string>& args);

// To allow linking without mtcpinterface;  Weak symbol undefined, is set to 0
void __attribute__ ((weak)) dmtcp::initializeMtcpEngine();
void __attribute__ ((weak)) dmtcp::killCkpthread();

void restoreUserLDPRELOAD()
{
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
  //   exec("dmtcp_checkpoint --ssh-slave ... ssh ..."), and re-execute.
  //   This way, we will unset LD_PRELOAD here and now, instead of at that time.
  char *preload =  getenv("LD_PRELOAD");
  char *hijackLibs = getenv(ENV_VAR_HIJACK_LIBS);
  if (preload != NULL
      || dmtcp::Util::strStartsWith(preload, hijackLibs) == false) {
    return;
  }
  if (strcmp(preload, hijackLibs) == 0) {
    _dmtcp_unsetenv("LD_PRELOAD");
  } else {
    JASSERT(dmtcp::Util::strStartsWith(preload, hijackLibs))
      (preload) (hijackLibs);
    char *userPreload = preload + strlen(hijackLibs) + 1;
    setenv("LD_PRELOAD", userPreload, 1);
  }
  JTRACE("LD_PRELOAD") (preload) (hijackLibs) (getenv("LD_PRELOAD"));
}

// FIXME:  We need a better way to get MTCP_DEFAULT_SIGNAL
//         See:  pidwrappers.cpp:get_sigckpt()
#include "../../mtcp/mtcp.h" //for MTCP_DEFAULT_SIGNAL

// This should be visible to library only.  DmtcpWorker will call
//   this to initialize tmp (ckpt signal) at startup time.  This avoids
//   any later calls to getenv(), at which time the user app may have
//   a wrapper around getenv, modified environ, or other tricks.
//   (Matlab needs this or else it segfaults on restart, and bash plays
//   similar tricks with maintaining its own environment.)
// Used in mtcpinterface.cpp and signalwrappers.cpp.
// FIXME: DO we still want it to be library visible only?
//__attribute__ ((visibility ("hidden")))
int dmtcp::DmtcpWorker::determineMtcpSignal()
{
  // this mimics the MTCP logic for determining signal number found in
  // mtcp_init()
  int sig = MTCP_DEFAULT_SIGNAL;
  char* endp = NULL;
  static const char* tmp = getenv("MTCP_SIGCKPT");
  if(tmp != NULL){
      sig = strtol(tmp, &endp, 0);
      if((errno != 0) || (tmp == endp))
        sig = MTCP_DEFAULT_SIGNAL;
      if(sig < 1 || sig > 31)
        sig = MTCP_DEFAULT_SIGNAL;
  }
  return sig;
}

/* This function is called at the very beginning of the DmtcpWorker constructor
 * to do some initialization work so that DMTCP can later use _real_XXX
 * functions reliably. Read the comment at the top of syscallsreal.c for more
 * details.
 */
extern "C" LIB_PRIVATE void prepareDmtcpWrappers()
{
  // FIXME: Remove JALLOC_HELPER_... after the release.
  JALLOC_HELPER_DISABLE_LOCKS();
  dmtcp_wrappers_initializing = 1;
  initialize_libc_wrappers();
  //dmtcp_process_event(DMTCP_EVENT_INIT_WRAPPERS, NULL);
  dmtcp_wrappers_initializing = 0;
  initialize_libpthread_wrappers();
  JALLOC_HELPER_ENABLE_LOCKS();

  /* Register pthread_atfork_child() as the first post-fork handler for the
   * child process. This needs to be the first function that is called by
   * libc:fork() after the child process is created.
   *
   * Some dmtcp module might also call pthread_atfork and so we call it right
   * here before initializing the wrappers.
   *
   * NOTE: If this doesn't work and someone is able to call pthead_atfork
   * before this call, we might want to install a pthread_atfork() wrappers.
   */
  JASSERT(pthread_atfork(pthread_atfork_prepare,
                         pthread_atfork_parent,
                         pthread_atfork_child) == 0);
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
  const char* serialFile = getenv( ENV_VAR_SERIALFILE_INITIAL );
  //dmtcp::VirtualPidTable::instance().updateMapping(getpid(), _real_getpid());
  if ( serialFile != NULL ) {
    // This process was under ckpt-control and exec()'d into a new program.
    // Find out path of previous log file so that later, we can write the name
    // of the new log file into that one.
    dmtcp::string prevLogFilePath = getLogFilePath();

    jalib::JBinarySerializeReader rd ( serialFile );
    UniquePid::serialize ( rd );
    Util::initializeLogFile("", prevLogFilePath);

    writeCurrentLogFileNameToPrevLogFile(prevLogFilePath);

    JTRACE ( "loading initial socket table from file..." ) ( serialFile );
    KernelDeviceToConnection::instance().serialize ( rd );

    ProcessInfo::instance().serialize ( rd );
    ProcessInfo::instance().postExec();
    SysVIPC::instance().serialize ( rd );
    dmtcp_process_event(DMTCP_EVENT_POST_EXEC, (void*) &rd);
    _dmtcp_unsetenv(ENV_VAR_SERIALFILE_INITIAL);
  } else {
    //dmtcp::VirtualPidTable::instance().updateMapping(getppid(), _real_getppid());
    // Brand new process (was never under ckpt-control),
    // Initialize the log file
    Util::initializeLogFile();

    if ( getenv( ENV_VAR_ROOT_PROCESS ) != NULL ) {
      JTRACE("Root of processes tree");
      ProcessInfo::instance().setRootOfProcessTree();
      _dmtcp_unsetenv(ENV_VAR_ROOT_PROCESS);
    }

    JTRACE("Checking for pre-existing sockets");
    ConnectionList::instance().scanForPreExisting();
  }

  JTRACE ("Initial socket table:");
  KernelDeviceToConnection::instance().dbgSpamFds();
}

static void processRlimit()
{
#ifdef __i386__
  // Match work begun in dmtcpPrepareForExec()
# if 0
  if (getenv("DMTCP_ADDR_COMPAT_LAYOUT")) {
    _dmtcp_unsetenv("DMTCP_ADDR_COMPAT_LAYOUT");
    // DMTCP had set ADDR_COMPAT_LAYOUT.  Now unset it.
    personality( (unsigned long)personality(0xffffffff) ^ ADDR_COMPAT_LAYOUT );
    JTRACE( "unsetting ADDR_COMPAT_LAYOUT" );
  }
# else
  { char * rlim_cur_char = getenv("DMTCP_RLIMIT_STACK");
    if ( rlim_cur_char != NULL ) {
      struct rlimit rlim;
      getrlimit(RLIMIT_STACK, &rlim);
      rlim.rlim_cur = atol(rlim_cur_char);
      JTRACE ( "rlim_cur for RLIMIT_STACK being restored." ) ( rlim.rlim_cur );
      setrlimit(RLIMIT_STACK, &rlim);
      _dmtcp_unsetenv("DMTCP_RLIMIT_STACK");
    }
  }
# endif
#endif
}

//called before user main()
//workerhijack.cpp initializes a static variable theInstance to DmtcpWorker obj
dmtcp::DmtcpWorker::DmtcpWorker ( bool enableCheckpointing )
{
  if ( !enableCheckpointing ) return;
  else {
    WorkerState::setCurrentState( WorkerState::UNKNOWN);
    initializeJalib();
    prepareDmtcpWrappers();
    prepareLogAndProcessdDataFromSerialFile();
  }

  JTRACE ( "dmtcphijack.so:  Running " )
    ( jalib::Filesystem::GetProgramName() ) ( getenv ( "LD_PRELOAD" ) );

  if (getenv(ENV_VAR_UTILITY_DIR) == NULL) {
    JNOTE ( "\n **** Not checkpointing this process,"
            " due to missing environment var ****" )
          ( getenv(ENV_VAR_UTILITY_DIR) )
          ( jalib::Filesystem::GetProgramName() );
    return;
  }

  processRlimit();

  //This is called for side effect only.  Force this function to call
  // getenv("MTCP_SIGCKPT") now and cache it to avoid getenv calls later.
  determineMtcpSignal();

  // Also cache programName and arguments
  dmtcp::string programName = jalib::Filesystem::GetProgramName();
  dmtcp::vector<dmtcp::string> args = jalib::Filesystem::GetProgramArgs();

  if (programName == "dmtcp_coordinator" || programName == "dmtcp_command" ||
      programName == "dmtcp_checkpoint"  || programName == "dmtcp_restart" ||
      programName == "mtcp_restart" ) {
    processDmtcpCommands(programName, args);
  } else if (programName == "ssh") {
    processSshCommand(programName, args);
  }
  calculateArgvAndEnvSize();

  WorkerState::setCurrentState ( WorkerState::RUNNING );

  connectToCoordinatorWithHandshake();

  // define "Weak Symbols for each library module in dmtcphijack.so
  dmtcp_process_event(DMTCP_EVENT_INIT, NULL);

  /* Acquire the lock here, so that the checkpoint-thread won't be able to
   * process CHECKPOINT request until we are done with initializeMtcpEngine()
   */
  if (initializeMtcpEngine) { // if strong symbol defined elsewhere
    //WRAPPER_EXECUTION_GET_EXCL_LOCK();
    initializeMtcpEngine();
    //WRAPPER_EXECUTION_RELEASE_EXCL_LOCK();
  } else { // else trying to call weak symbol, which is undefined
    JASSERT(false).Text("initializeMtcpEngine should not be called");
  }

  /* Now wait for Checkpoint Thread to finish initialization
   * NOTE: This should be the last thing in this constructor
   */
  while (!ThreadSync::isCheckpointThreadInitialized()) {
    struct timespec sleepTime = {0, 10*1000*1000};
    nanosleep(&sleepTime, NULL);
  }
}

void dmtcp::DmtcpWorker::cleanupWorker()
{
  ThreadSync::resetLocks();
  WorkerState::setCurrentState( WorkerState::UNKNOWN);
  JTRACE ( "disconnecting from dmtcp coordinator" );
  _coordinatorSocket.close();
}

void dmtcp::DmtcpWorker::interruptCkpthread()
{
  if (ThreadSync::destroyDmtcpWorkerLockTryLock() == EBUSY) {
    if (killCkpthread) // if strong symbol defined elsewhere
      killCkpthread();
    else // else trying to call weak symbol, which is undefined
      JASSERT(false).Text("killCkpthread should not be called");
    ThreadSync::destroyDmtcpWorkerLockLock();
  }
}

//called after user main()
dmtcp::DmtcpWorker::~DmtcpWorker()
{
  if( exitInProgress() ){
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
    dmtcp_process_event(DMTCP_EVENT_PRE_EXIT, NULL);
    JTRACE ( "exit() in progress, disconnecting from dmtcp coordinator" );
    _coordinatorSocket.close();
    interruptCkpthread();
  }
  cleanupWorker();
}

static void processDmtcpCommands(dmtcp::string programName,
                                 dmtcp::vector<dmtcp::string>& args)
{
  JASSERT (programName == "dmtcp_coordinator" ||
           programName == "dmtcp_checkpoint"  ||
           programName == "dmtcp_restart"     ||
           programName == "dmtcp_command"     ||
           programName == "mtcp_restart");

  //make sure coordinator connection is closed
  _real_close ( PROTECTED_COORD_FD );

  /*
   * When running gdb or any shell which does a waitpid() on the child
   * processes, executing dmtcp_command from within gdb session / shell results
   * in process getting hung up because:
   *   gdb shell dmtcp_command -c => hangs because gdb forks off a new process
   *   and it does a waitpid  (in which we block signals) ...
   */
  if (programName == "dmtcp_command") {
    pid_t cpid = _real_fork();
    JASSERT (cpid != -1);
    if (cpid != 0) {
      _real_exit(0);
    }
  }

  //now repack args
  char** argv = new char*[args.size() + 1];
  memset ( argv, 0, sizeof ( char* ) * ( args.size() + 1 ) );

  for ( size_t i=0; i< args.size(); ++i ) {
    argv[i] = ( char* ) args[i].c_str();
  }

  JNOTE ( "re-running without checkpointing" ) ( programName );

  //now re-call the command
  restoreUserLDPRELOAD();
  _real_execvp ( jalib::Filesystem::GetProgramPath().c_str(), argv );

  //should be unreachable
  JASSERT ( false ) (jalib::Filesystem::GetProgramPath()) ( argv[0] )
    ( JASSERT_ERRNO ) .Text ( "exec() failed" );
}

const dmtcp::UniquePid& dmtcp::DmtcpWorker::coordinatorId() const
{
  return _coordinatorId;
}


void dmtcp::DmtcpWorker::waitForCoordinatorMsg(dmtcp::string msgStr,
                                               DmtcpMessageType type )
{
  if ( type == DMT_DO_SUSPEND ) {
    if (ThreadSync::destroyDmtcpWorkerLockTryLock() != 0) {
      JTRACE ( "User thread is performing exit()."
               " ckpt thread exit()ing as well" );
      pthread_exit(NULL);
    }
    if ( exitInProgress() ) {
      ThreadSync::destroyDmtcpWorkerLockUnlock();
      pthread_exit(NULL);
    }
  }

  dmtcp::DmtcpMessage msg;

  msg.type = DMT_OK;
  msg.state = WorkerState::currentState();
  _coordinatorSocket << msg;

  JTRACE ( "waiting for " + msgStr + " message" );

  do {
    msg.poison();
    _coordinatorSocket >> msg;

    if ( type == DMT_DO_SUSPEND && exitInProgress() ) {
      ThreadSync::destroyDmtcpWorkerLockUnlock();
      pthread_exit(NULL);
    }

    msg.assertValid();

    if ( msg.type == DMT_KILL_PEER ) {
      JTRACE ( "Received KILL message from coordinator, exiting" );
      _exit ( 0 );
    }

    // The ckpt thread can receive multiple DMT_RESTORE_WAITING or
    // DMT_FORCE_RESTART messages while waiting for a DMT_DO_REFILL message, we
    // need to ignore them and wait for the DMT_DO_REFILL message to arrive.
    if ( type != DMT_DO_REFILL && type != DMT_DO_REGISTER_NAME_SERVICE_DATA &&
         type != DMT_DO_SEND_QUERIES ) {
      break;
    }

  } while((type == DMT_DO_REFILL
           || type == DMT_DO_REGISTER_NAME_SERVICE_DATA
           || type == DMT_DO_SEND_QUERIES)
          && (msg.type == DMT_RESTORE_WAITING ||
              msg.type == DMT_FORCE_RESTART));

  JASSERT ( msg.type == type ) ( msg.type ) (type);

  // Coordinator sends some computation information along with the SUSPEND
  // message. Extracting that.
  if ( type == DMT_DO_SUSPEND ) {
    UniquePid::ComputationId() = msg.compGroup;
  } else if ( type == DMT_DO_FD_LEADER_ELECTION ) {
    JTRACE ( "Computation information" ) ( msg.compGroup ) ( msg.params[0] );
    JASSERT ( theCheckpointState != NULL );
    ProcessInfo::instance().numPeers(msg.params[0]);
    JASSERT(UniquePid::ComputationId() == msg.compGroup);
    ProcessInfo::instance().compGroup(msg.compGroup);
  }
}

void dmtcp::DmtcpWorker::waitForStage1Suspend()
{
  JTRACE ( "running" );

  WorkerState::setCurrentState ( WorkerState::RUNNING );

  /*
   * Its only use is to inform the user thread (waiting in DmtcpWorker
   * constructor) that the checkpoint thread has finished initialization. This
   * is to serialize DmtcpWorker-Constructor(), mtcp_init(), checkpoint-thread
   * initialization and user main(). As obvious, this is only effective when
   * the process is being initialized.
   */
  if (!ThreadSync::isCheckpointThreadInitialized()) {
    /*
     * We should not call this function any higher in the logic because it
     * calls setenv() and if it is running under bash, then it getenv() will
     * not work between the call to setenv() and bash main().
     */
    restoreUserLDPRELOAD();
    ThreadSync::setCheckpointThreadInitialized();
  }

  if ( theCheckpointState != NULL ) {
    delete theCheckpointState;
    theCheckpointState = NULL;
  }

  theCheckpointState = new ConnectionState();

#ifdef EXTERNAL_SOCKET_HANDLING
  JASSERT ( _waitingForExternalSocketsToClose == true ||
             externalTcpConnections.empty() == true );

  while ( externalTcpConnections.empty() == false ) {
    JTRACE("Waiting for externalSockets toClose")
          (_waitingForExternalSocketsToClose);
    sleep ( 1 );
  }
  if ( _waitingForExternalSocketsToClose == true ) {
    DmtcpMessage msg ( DMT_EXTERNAL_SOCKETS_CLOSED );
    _coordinatorSocket << msg;
    _waitingForExternalSocketsToClose = false;
    JTRACE("externalSocketsClosed") (_waitingForExternalSocketsToClose);
  }
#endif

  waitForCoordinatorMsg ( "SUSPEND", DMT_DO_SUSPEND );
  UniquePid::updateCheckpointDirName();

  JTRACE("got SUSPEND message, preparing to acquire all Thread-sync locks");
  ThreadSync::acquireLocks();

  JTRACE ( "Starting checkpoint, suspending..." );
}

#ifdef EXTERNAL_SOCKET_HANDLING
bool dmtcp::DmtcpWorker::waitForStage2Checkpoint()
#else
void dmtcp::DmtcpWorker::waitForStage2Checkpoint()
#endif
{
  WorkerState::setCurrentState ( WorkerState::SUSPENDED );
  JTRACE ( "suspended" );

  if ( exitInProgress() ) {
    ThreadSync::destroyDmtcpWorkerLockUnlock();
    pthread_exit(NULL);
  }
  ThreadSync::destroyDmtcpWorkerLockUnlock();

  JASSERT(_coordinatorSocket.isValid());
  ThreadSync::releaseLocks();
  dmtcp_process_event(DMTCP_EVENT_POST_SUSPEND, NULL);

  theCheckpointState->preLockSaveOptions();

  waitForCoordinatorMsg ( "FD_LEADER_ELECTION", DMT_DO_FD_LEADER_ELECTION );

  JTRACE ( "locking..." );
  JASSERT ( theCheckpointState != NULL );
  theCheckpointState->preCheckpointFdLeaderElection();
  JTRACE ( "locked" );

  SysVIPC::instance().leaderElection();

  WorkerState::setCurrentState ( WorkerState::FD_LEADER_ELECTION );

  dmtcp_process_event(DMTCP_EVENT_POST_LEADER_ELECTION, NULL);

#ifdef EXTERNAL_SOCKET_HANDLING
  if ( waitForStage2bCheckpoint() == false ) {
    return false;
  }
#else
  waitForCoordinatorMsg ( "DRAIN", DMT_DO_DRAIN );
#endif

  JTRACE ( "draining..." );
  theCheckpointState->preCheckpointDrain();
  JTRACE ( "drained" );

  SysVIPC::instance().preCkptDrain();

  WorkerState::setCurrentState ( WorkerState::DRAINED );

  dmtcp_process_event(DMTCP_EVENT_POST_DRAIN, NULL);

  waitForCoordinatorMsg ( "CHECKPOINT", DMT_DO_CHECKPOINT );
  JTRACE ( "got checkpoint message" );

#if HANDSHAKE_ON_CHECKPOINT == 1
  //handshake is done after one barrier after drain
  JTRACE ( "beginning handshakes" );
  theCheckpointState->preCheckpointHandshakes(coordinatorId());
  JTRACE ( "handshaking done" );
#endif

  dmtcp::ProcessInfo::instance().preCheckpoint();
  SysVIPC::instance().preCheckpoint();

  dmtcp_process_event(DMTCP_EVENT_PRE_CKPT, NULL);

#ifdef EXTERNAL_SOCKET_HANDLING
  return true;
#endif
}

#ifdef EXTERNAL_SOCKET_HANDLING
bool dmtcp::DmtcpWorker::waitForStage2bCheckpoint()
{
  waitForCoordinatorMsg ( "PEER_LOOKUP", DMT_DO_PEER_LOOKUP );
  JTRACE ( "Looking up Socket Peers..." );
  theTcpConnections.clear();
  theCheckpointState->preCheckpointPeerLookup(theTcpConnections);
  sendPeerLookupRequest(theTcpConnections);
  JTRACE ( "Done Socket Peer Lookup" );


  WorkerState::setCurrentState ( WorkerState::PEER_LOOKUP_COMPLETE );

  {
    dmtcp::DmtcpMessage msg;

    msg.type = DMT_OK;
    msg.state = WorkerState::currentState();
    _coordinatorSocket << msg;

    JTRACE ( "waiting for DRAIN/RESUME message" );

    do {
      msg.poison();
      _coordinatorSocket >> msg;
      msg.assertValid();

      if ( msg.type == DMT_KILL_PEER ) {
        JTRACE ( "Received KILL message from coordinator, exiting" );
        _exit ( 0 );
      }
      JTRACE ( "received message" ) (msg.type );
      if ( msg.type != DMT_UNKNOWN_PEER )
        break;

      JTRACE ("received DMT_UNKNOWN_PEER message") (msg.conId);

      TcpConnection* con =
        (TcpConnection*) &( ConnectionList::instance() [msg.conId] );
      con->markExternal();
      externalTcpConnections.push_back(msg.conId);
      _waitingForExternalSocketsToClose = true;

    } while ( msg.type == DMT_UNKNOWN_PEER );

    JASSERT ( msg.type == DMT_DO_DRAIN || msg.type == DMT_DO_RESUME )
            ( msg.type );

    ConnectionList& connections = ConnectionList::instance();

    // Tcp Accept and Connect connection with PeerType UNKNOWN should be marked as INTERNAL
    for ( ConnectionList::iterator i = connections.begin()
        ; i!= connections.end()
        ; ++i )
    {
      Connection* con =  i->second;
      if ( con->conType() == Connection::TCP ) {
        TcpConnection* tcpCon = (TcpConnection *) con;
        if ( (tcpCon->tcpType() == TcpConnection::TCP_ACCEPT ||
             tcpCon->tcpType() == TcpConnection::TCP_CONNECT) &&
             tcpCon->peerType() == TcpConnection::PEER_UNKNOWN )
          tcpCon->markInternal();
      }
    }
    if ( msg.type == DMT_DO_RESUME ) {
      JTRACE ( "Peer Lookup not complete, skipping checkpointing \n\n\n\n\n");
      return false;
    }

    JASSERT (msg.type == DMT_DO_DRAIN);
  }
}

void dmtcp::DmtcpWorker::sendPeerLookupRequest (dmtcp::vector<TcpConnectionInfo>& conInfoTable )
{
  for (int i = 0; i < conInfoTable.size(); ++i) {
    DmtcpMessage msg;
    msg.type = DMT_PEER_LOOKUP;
    msg.localAddr    = conInfoTable[i].localAddr();
    msg.remoteAddr   = conInfoTable[i].remoteAddr();
    msg.localAddrlen = conInfoTable[i].addrlen();
    msg.conId        = conInfoTable[i].conId();

    _coordinatorSocket << msg;
  }
}

bool dmtcp::DmtcpWorker::waitingForExternalSocketsToClose() {
  return _waitingForExternalSocketsToClose;
}
#endif

void dmtcp::DmtcpWorker::writeCheckpointPrefix(int fd)
{
  CkptSerializer::writeCkptPrefix(fd, theCheckpointState);
}

void dmtcp::DmtcpWorker::sendCkptFilenameToCoordinator()
{
  // Tell coordinator to record our filename in the restart script
  dmtcp::string ckptFilename = dmtcp::UniquePid::checkpointFilename();
  dmtcp::string hostname = jalib::Filesystem::GetCurrentHostname();
  JTRACE ( "recording filenames" ) ( ckptFilename ) ( hostname );
  dmtcp::DmtcpMessage msg;
  msg.type = DMT_CKPT_FILENAME;
  msg.extraBytes = ckptFilename.length() +1 + hostname.length() +1;
  _coordinatorSocket << msg;
  _coordinatorSocket.writeAll ( ckptFilename.c_str(), ckptFilename.length() +1 );
  _coordinatorSocket.writeAll ( hostname.c_str(),     hostname.length() +1 );
}


// At restart, the HOST/PORT used by dmtcp_coordinator could be different then
// those at checkpoint time. This could cause the child processes created after
// restart to fail to connect to the coordinator.
void dmtcp::DmtcpWorker::updateCoordinatorHostAndPortEnv()
{
  struct sockaddr addr;
  socklen_t addrLen = sizeof addr;
  JASSERT (0 == getpeername(_coordinatorSocket.sockfd(), &addr, &addrLen))
    (JASSERT_ERRNO);

  /* If the current coordinator is running on a HOST/PORT other than the
   * pre-checkpoint HOST/PORT, we need to update the environment variables
   * pointing to the coordinator HOST/PORT. This is needed if the new
   * coordinator has been moved around.
   */

  const char * origCoordAddr = getenv ( ENV_VAR_NAME_HOST );
  const char * origCoordPortStr = getenv ( ENV_VAR_NAME_PORT );
  if (origCoordAddr == NULL) origCoordAddr = DEFAULT_HOST;
  int origCoordPort = origCoordPortStr==NULL ? DEFAULT_PORT : jalib::StringToInt ( origCoordPortStr );

  jalib::JSockAddr originalCoordinatorAddr(origCoordAddr, origCoordPort);
  if (addrLen != originalCoordinatorAddr.addrlen() ||
      memcmp(originalCoordinatorAddr.addr(), &addr, addrLen) != 0) {

    JASSERT (addr.sa_family == AF_INET) (addr.sa_family)
      .Text ("Coordinator socket always uses IPV4 sockets");

    char currHost[1024];
    char currPort[16];

    int res = getnameinfo(&addr, addrLen, currHost, sizeof currHost,
                          currPort, sizeof currPort, NI_NUMERICSERV);
    JASSERT (res == 0) (currHost) (currPort) (gai_strerror(res))
      .Text ("getnameinfo(... currHost, ..., currPort,...) failed");

    JTRACE ("Coordinator running at a different location")
      (origCoordAddr) (origCoordPort) (currHost) (currPort);

    JASSERT (0 == setenv (ENV_VAR_NAME_HOST, currHost, 1)) (JASSERT_ERRNO);
    JASSERT( 0 == setenv (ENV_VAR_NAME_PORT, currPort, 1)) (JASSERT_ERRNO);
  }
}

void dmtcp::DmtcpWorker::postRestart()
{
  JTRACE("begin postRestart()");

  WorkerState::setCurrentState(WorkerState::RESTARTING);
  recvCoordinatorHandshake();

  JASSERT ( theCheckpointState != NULL );
  theCheckpointState->postRestart();

  dmtcp::ProcessInfo::instance().postRestart();
  SysVIPC::instance().postRestart();
}

void dmtcp::DmtcpWorker::waitForStage3Refill( bool isRestart )
{
  JTRACE ( "checkpointed" );

  WorkerState::setCurrentState ( WorkerState::CHECKPOINTED );

#ifdef COORD_NAMESERVICE
  waitForCoordinatorMsg("REGISTER_NAME_SERVICE_DATA",
                          DMT_DO_REGISTER_NAME_SERVICE_DATA );
  dmtcp_process_event(DMTCP_EVENT_REGISTER_NAME_SERVICE_DATA, NULL);
  JTRACE("Key Value Pairs registered with the coordinator");
  WorkerState::setCurrentState(WorkerState::NAME_SERVICE_DATA_REGISTERED);

  waitForCoordinatorMsg("SEND_QUERIES", DMT_DO_SEND_QUERIES);
  dmtcp_process_event(DMTCP_EVENT_SEND_QUERIES, NULL);
  JTRACE("Queries sent to the coordinator");
  WorkerState::setCurrentState(WorkerState::DONE_QUERYING);
#endif

  waitForCoordinatorMsg ( "REFILL", DMT_DO_REFILL );

  JASSERT ( theCheckpointState != NULL );
  theCheckpointState->postCheckpoint(isRestart);
  delete theCheckpointState;
  theCheckpointState = NULL;

  SysVIPC::instance().postCheckpoint();
  if (!isRestart) {
    dmtcp_process_event(DMTCP_EVENT_POST_CKPT, NULL);
  }
}

void dmtcp::DmtcpWorker::waitForStage4Resume()
{
  JTRACE ( "refilled" );
  WorkerState::setCurrentState ( WorkerState::REFILLED );
  waitForCoordinatorMsg ( "RESUME", DMT_DO_RESUME );
  JTRACE ( "got resume message" );

  SysVIPC::instance().preResume();
}

void dmtcp::DmtcpWorker::restoreVirtualPidTable()
{
  dmtcp::ProcessInfo::instance().restoreProcessGroupInfo();
}
