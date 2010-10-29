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

#include "dmtcpworker.h"
#include "constants.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jalloc.h"
#include "dmtcpmessagetypes.h"
#include <stdlib.h>
#include "mtcpinterface.h"
#include <unistd.h>
#include "sockettable.h"
#include  "../jalib/jsocket.h"
#include <map>
#include "kernelbufferdrainer.h"
#include  "../jalib/jfilesystem.h"
#include "syscallwrappers.h"
#include "protectedfds.h"
#include "connectionidentifier.h"
#include "connectionmanager.h"
#include "connectionstate.h"
#include "dmtcp_coordinator.h"
#include "sysvipc.h"
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/personality.h>


/* Read-write lock initializers.  */
#ifdef __USE_GNU
# if __WORDSIZE == 64
#  define PTHREAD_RWLOCK_PREFER_WRITER_RECURSIVE_INITIALIZER_NP \
 { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,					      \
       PTHREAD_RWLOCK_PREFER_WRITER_NP } }
# else
#  if __BYTE_ORDER == __LITTLE_ENDIAN
#   define PTHREAD_RWLOCK_PREFER_WRITER_RECURSIVE_INITIALIZER_NP \
 { { 0, 0, 0, 0, 0, 0, PTHREAD_RWLOCK_PREFER_WRITER_NP, \
     0, 0, 0, 0 } }
#  else
#   define PTHREAD_RWLOCK_PREFER_WRITER_RECURSIVE_INITIALIZER_NP \
 { { 0, 0, 0, 0, 0, 0, 0, 0, 0, PTHREAD_RWLOCK_PREFER_WRITER_NP,\
     0 } }
#  endif
# endif
#endif


static pthread_mutex_t theCkptCanStart = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static pthread_mutex_t destroyDmtcpWorker = PTHREAD_MUTEX_INITIALIZER;

/*
 * WrapperProtectionLock is used to make the checkpoint safe by making sure
 *   that no user-thread is executing any DMTCP wrapper code when it receives
 *   the checkpoint signal.
 * Working:
 *   On entering the wrapper in DMTCP, the user-thread acquires the read lock,
 *     and releases it before leaving the wrapper.
 *   When the Checkpoint-thread wants to send the SUSPEND signal to user
 *     threads, it must acquire the write lock. It is blocked until all the
 *     existing read-locks by user threads have been released. NOTE that this
 *     is a WRITER-PREFERRED lock.
 *
 * There is a corner case too -- the newly created thread that has not been
 *   initialized yet; we need to take some extra efforts for that.
 * Here are the steps to handle the newly created uninitialized thread:
 *   A counter for the number of newly created uninitialized threads is kept.
 *     The counter is made thread safe by using a mutex.
 *   The calling thread (parent) increments the counter before calling clone.
 *   The newly created child thread decrements the counter at the end of
 *     initialization in MTCP/DMTCP.
 *   After acquiring the Write lock, the checkpoint thread waits until the
 *     number of uninitialized threads is zero. At that point, no thread is
 *     executing in the clone wrapper and it is safe to do a checkpoint.
 *
 * XXX: Currently this security is provided only for the clone wrapper; this
 * should be extended to other calls as well.           -- KAPIL
 */
static pthread_rwlock_t theWrapperExecutionLock = PTHREAD_RWLOCK_WRITER_NONRECURSIVE_INITIALIZER_NP;
static pthread_mutex_t unInitializedThreadCountLock = PTHREAD_MUTEX_INITIALIZER;
static int unInitializedThreadCount = 0;
static dmtcp::UniquePid compGroup;

// static dmtcp::KernelBufferDrainer* theDrainer = NULL;
static dmtcp::ConnectionState* theCheckpointState = NULL;

#ifdef EXTERNAL_SOCKET_HANDLING
static dmtcp::vector <dmtcp::TcpConnectionInfo> theTcpConnections;
dmtcp::vector <dmtcp::ConnectionIdentifier> externalTcpConnections;
static bool _waitingForExternalSocketsToClose = false;
#endif

static int theRestorePort = RESTORE_PORT_START;

bool dmtcp::DmtcpWorker::_exitInProgress = false;

void processDmtcpCommands(dmtcp::string programName);
static void processSshCommand(dmtcp::string programName);

void dmtcp::DmtcpWorker::useAlternateCoordinatorFd(){
  _coordinatorSocket = jalib::JSocket( PROTECTEDFD( 4 ) );
}

const unsigned int dmtcp::DmtcpWorker::ld_preload_c_len;
char dmtcp::DmtcpWorker::ld_preload_c[dmtcp::DmtcpWorker::ld_preload_c_len];

bool _checkpointThreadInitialized = false;
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
  char * preload =  getenv("LD_PRELOAD");
  char * preload_rest = strstr(preload, ":");
  if (preload_rest) {
    *preload_rest = '\0'; // Now preload is just our preload string
    preload_rest++;
  }
  JTRACE("LD_PRELOAD")(preload);
  JASSERT(strlen(preload) < dmtcp::DmtcpWorker::ld_preload_c_len)
	 (preload) (dmtcp::DmtcpWorker::ld_preload_c_len)
	 .Text("preload string is longer than ld_preload_c_len");
  strcpy(dmtcp::DmtcpWorker::ld_preload_c, preload);  // Don't malloc
  if (preload_rest) {
    setenv("LD_PRELOAD", preload_rest, 1);
  } else {
    _dmtcp_unsetenv("LD_PRELOAD");
  }
}

#include "../../mtcp/mtcp.h" //for MTCP_DEFAULT_SIGNAL

// This shold be visible to library only.  DmtcpWorker will call
//   this to initialize tmp (ckpt signal) at startup time.  This avoids
//   any later calls to getenv(), at which time the user app may have
//   a wrapper around getenv, modified environ, or other tricks.
//   (Matlab needs this or else it segfaults on restart, and bash plays
//   similar tricks with maintaining its own environmnet.)
// Used in mtcpinterface.cpp and signalwrappers.cpp.
__attribute__ ((visibility ("hidden")))
int _determineMtcpSignal(){
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

//called before user main()
//workerhijack.cpp initializes a static variable theInstance to DmtcpWorker obj
dmtcp::DmtcpWorker::DmtcpWorker ( bool enableCheckpointing )
    :_coordinatorSocket ( PROTECTEDFD ( 1 ) )
    ,_restoreSocket ( PROTECTEDFD ( 3 ) )
{
  if ( !enableCheckpointing ) return;

  WorkerState::setCurrentState( WorkerState::UNKNOWN);

  /* DO NOT PUT ANYTHING BEFORE THE FOLLOWING BLOCK OF CODE (#ifdef .... #endif) */
#ifdef DEBUG
  /* Disable Jassert Logging */
  dmtcp::UniquePid::ThisProcess(true);

  dmtcp::ostringstream o;
  o << dmtcp::UniquePid::getTmpDir() << "/jassertlog." << dmtcp::UniquePid::ThisProcess()
    << "_" << jalib::Filesystem::GetProgramName();
  JASSERT_INIT (o.str());

  JTRACE ( "recalculated process UniquePid..." ) ( dmtcp::UniquePid::ThisProcess() );
#endif

  //This is called for side effect only.  Force this function to call
  // getenv("MTCP_SIGCKPT") now and cache it to avoid getenv calls later.
  _determineMtcpSignal();

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

  if ( getenv(ENV_VAR_UTILITY_DIR) == NULL ) {
    JNOTE ( "\n **** Not checkpointing this process,"
            " due to missing environment var ****" )
          ( getenv(ENV_VAR_UTILITY_DIR) )
          ( jalib::Filesystem::GetProgramName() );
    return;
  }
  if (! getenv(ENV_VAR_QUIET))
    setenv(ENV_VAR_QUIET, "0", 0);
  jassert_quiet = *getenv(ENV_VAR_QUIET) - '0';


  JTRACE ( "dmtcphijack.so:  Running " ) ( jalib::Filesystem::GetProgramName() )                                         ( getenv ( "LD_PRELOAD" ) );
  JTRACE ( "dmtcphijack.so:  Child of pid " ) ( getppid() );

  dmtcp::string programName = jalib::Filesystem::GetProgramName();

  if ( programName == "dmtcp_coordinator"  ||
       programName == "dmtcp_checkpoint"   ||
       programName == "dmtcp_restart"      ||
       programName == "dmtcp_command"      ||
       programName == "mtcp_restart" ) {
    processDmtcpCommands(programName);
  }
  if ( programName == "ssh" ) {
    processSshCommand(programName);
  }

  WorkerState::setCurrentState ( WorkerState::RUNNING );

  const char* serialFile = getenv( ENV_VAR_SERIALFILE_INITIAL );
  if ( serialFile != NULL )
  {
    JTRACE ( "loading initial socket table from file..." ) ( serialFile );

    jalib::JBinarySerializeReader rd ( serialFile );
    UniquePid::serialize ( rd );
    KernelDeviceToConnection::instance().serialize ( rd );

#ifdef PID_VIRTUALIZATION
    VirtualPidTable::instance().serialize ( rd );
    VirtualPidTable::instance().postExec();
#endif
    SysVIPC::instance().serialize ( rd );

#ifdef DEBUG
    JTRACE ( "initial socket table:" );
    KernelDeviceToConnection::instance().dbgSpamFds();
#endif

    _dmtcp_unsetenv ( ENV_VAR_SERIALFILE_INITIAL );
  }
  else
  {
    JTRACE ( "root of processes tree, checking for pre-existing sockets" );

#ifdef PID_VIRTUALIZATION
    if ( getenv( ENV_VAR_ROOT_PROCESS ) != NULL ) {
      dmtcp::VirtualPidTable::instance().setRootOfProcessTree();
      _dmtcp_unsetenv( ENV_VAR_ROOT_PROCESS );
    }
#endif

    ConnectionList::instance().scanForPreExisting();
  }

  connectToCoordinatorWithHandshake();

  WorkerState::setCurrentState ( WorkerState::RUNNING );

  /* Acquire the lock here, so that the checkpoint-thread won't be able to
   * process CHECKPOINT request until we are done with initializeMtcpEngine()
   */
  WRAPPER_EXECUTION_DISABLE_CKPT();
  initializeMtcpEngine();
  WRAPPER_EXECUTION_ENABLE_CKPT();

  /* 
   * Now wait for Checkpoint Thread to finish initialization 
   * XXX: This should be the last thing in this constructor
   */
  while (!_checkpointThreadInitialized) {
    struct timespec sleepTime = {0, 10*1000*1000};
    nanosleep(&sleepTime, NULL);
  }
}

void dmtcp::DmtcpWorker::cleanupWorker()
{
  pthread_rwlock_t newLock = PTHREAD_RWLOCK_WRITER_NONRECURSIVE_INITIALIZER_NP;
  theWrapperExecutionLock = newLock;

  pthread_mutex_t newCountLock = PTHREAD_MUTEX_INITIALIZER;
  unInitializedThreadCountLock = newCountLock;

  pthread_mutex_t newDestroyDmtcpWorker = PTHREAD_MUTEX_INITIALIZER;
  destroyDmtcpWorker = newDestroyDmtcpWorker;

  unInitializedThreadCount = 0;
  WorkerState::setCurrentState( WorkerState::UNKNOWN);
  JTRACE ( "disconnecting from dmtcp coordinator" );
  _coordinatorSocket.close();
}

void dmtcp::DmtcpWorker::interruptCkpthread()
{
  if (pthread_mutex_trylock(&destroyDmtcpWorker) == EBUSY) {
    killCkpthread();
    pthread_mutex_lock(&destroyDmtcpWorker);
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
    JTRACE ( "exit() in progress, disconnecting from dmtcp coordinator" );
    _coordinatorSocket.close();
    interruptCkpthread();
  }
  cleanupWorker();
}

void processDmtcpCommands(dmtcp::string programName)
{
  JASSERT ( programName == "dmtcp_coordinator"  ||
      programName == "dmtcp_checkpoint"   ||
      programName == "dmtcp_restart"      ||
      programName == "dmtcp_command"      ||
      programName == "mtcp_restart" );

  //make sure coordinator connection is closed
  _real_close ( PROTECTEDFD ( 1 ) );

  //get program args
  dmtcp::vector<dmtcp::string> args = jalib::Filesystem::GetProgramArgs();

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

static void processSshCommand(dmtcp::string programName)
{
  JASSERT ( jalib::Filesystem::GetProgramName() == "ssh" );
  //make sure coordinator connection is closed
  _real_close ( PROTECTEDFD ( 1 ) );

  //get prog args
  dmtcp::vector<dmtcp::string> args = jalib::Filesystem::GetProgramArgs();
  JASSERT ( args.size() >= 3 ) ( args.size() ).Text ( "ssh must have at least 3 args to be wrapped (ie: ssh host cmd)" );

  //find command part
  size_t commandStart = 2;
  for ( size_t i = 1; i < args.size(); ++i )
  {
    if ( args[i][0] != '-' )
    {
      commandStart = i + 1;
      break;
    }
  }
  JASSERT ( commandStart < args.size() && args[commandStart][0] != '-' )
    ( commandStart ) ( args.size() ) ( args[commandStart] )
    .Text ( "failed to parse ssh command line" );

  //find the start of the command
  dmtcp::string& cmd = args[commandStart];


  const char * coordinatorAddr      = getenv ( ENV_VAR_NAME_ADDR );
  const char * coordinatorPortStr   = getenv ( ENV_VAR_NAME_PORT );
  const char * sigckpt              = getenv ( ENV_VAR_SIGCKPT );
  const char * compression          = getenv ( ENV_VAR_COMPRESSION );
  const char * ckptOpenFiles        = getenv ( ENV_VAR_CKPT_OPEN_FILES );
  const char * ckptDir              = getenv ( ENV_VAR_CHECKPOINT_DIR );
  const char * tmpDir               = getenv ( ENV_VAR_TMPDIR );
  jassert_quiet                     = *getenv ( ENV_VAR_QUIET ) - '0';

  //modify the command

  //dmtcp::string prefix = "env ";

  dmtcp::string prefix = DMTCP_CHECKPOINT_CMD " --ssh-slave ";


  if ( coordinatorAddr != NULL )
    prefix += dmtcp::string() + "--host " + coordinatorAddr    + " ";
  if ( coordinatorPortStr != NULL )
    prefix += dmtcp::string() + "--port " + coordinatorPortStr + " ";
  if ( sigckpt != NULL )
    prefix += dmtcp::string() + "--mtcp-checkpoint-signal "    + sigckpt + " ";
  if ( ckptDir != NULL )
    prefix += dmtcp::string() + "--ckptdir " + ckptDir         + " ";
  if ( tmpDir != NULL )
    prefix += dmtcp::string() + "--tmpdir " + tmpDir           + " ";
  if ( ckptOpenFiles != NULL )
    prefix += dmtcp::string() + "--checkpoint-open-files"      + " ";

  if ( compression != NULL ) {
    if ( strcmp ( compression, "0" ) == 0 )
      prefix += "--no-gzip ";
    else
      prefix += "--gzip ";
  }

  // process command
  size_t semipos, pos;
  size_t actpos = dmtcp::string::npos;
  for(semipos = 0; (pos = cmd.find(';',semipos+1)) != dmtcp::string::npos;
      semipos = pos, actpos = pos);

  if( actpos > 0 && actpos != dmtcp::string::npos ){
    cmd = cmd.substr(0,actpos+1) + prefix + cmd.substr(actpos+1);
  } else {
    cmd = prefix + cmd;
  }

  //now repack args
  dmtcp::string newCommand = "";
  char** argv = new char*[args.size() +2];
  memset ( argv,0,sizeof ( char* ) * ( args.size() +2 ) );

  for ( size_t i=0; i< args.size(); ++i )
  {
    argv[i] = ( char* ) args[i].c_str();
    newCommand += args[i] + ' ';
  }

  JNOTE ( "re-running SSH with checkpointing" ) ( newCommand );

  restoreUserLDPRELOAD();
  //now re-call ssh
  _real_execvp ( argv[0], argv );

  //should be unreachable
  JASSERT ( false ) ( cmd ) ( JASSERT_ERRNO ).Text ( "exec() failed" );
}


const dmtcp::UniquePid& dmtcp::DmtcpWorker::coordinatorId() const
{
  return _coordinatorId;
}


void dmtcp::DmtcpWorker::waitForCoordinatorMsg(dmtcp::string signalStr,
                                               DmtcpMessageType type )
{
  if ( type == DMT_DO_SUSPEND ) {
    if ( pthread_mutex_trylock(&destroyDmtcpWorker) != 0 ) {
      JTRACE ( "User thread is performing exit()."
               " ckpt thread exit()ing as well" );
      pthread_exit(NULL);
    }
    if ( exitInProgress() ) {
      JASSERT(pthread_mutex_unlock(&destroyDmtcpWorker)==0)(JASSERT_ERRNO);
      pthread_exit(NULL);
    }
  }

  dmtcp::DmtcpMessage msg;

  msg.type = DMT_OK;
  msg.state = WorkerState::currentState();
  _coordinatorSocket << msg;

  JTRACE ( "waiting for " + signalStr + " Signal" );

  do {
    msg.poison();
    _coordinatorSocket >> msg;

    if ( type == DMT_DO_SUSPEND && exitInProgress() ) {
      JASSERT(pthread_mutex_unlock(&destroyDmtcpWorker)==0)(JASSERT_ERRNO);
      pthread_exit(NULL);
    }

    msg.assertValid();

    if ( msg.type == DMT_KILL_PEER ) {
      JTRACE ( "Received KILL Message from coordinator, exiting" );
      _exit ( 0 );
    }

    // The ckpt thread can receive multiple DMT_RESTORE_WAITING or
    // DMT_FORCE_RESTART messages while waiting for a DMT_DO_REFILL message, we
    // need to ignore them and wait for the DMT_DO_REFILL message to arrive.
    if ( type != DMT_DO_REFILL ) {
      break;
    }

  } while ( type == DMT_DO_REFILL &&
            ( msg.type == DMT_RESTORE_WAITING || msg.type == DMT_FORCE_RESTART ) );

  JASSERT ( msg.type == type ) ( msg.type );

  // Coordinator sends some computation information along with the SUSPEND
  // message. Extracting that.
  if ( type == DMT_DO_SUSPEND ) {
    JTRACE ( "Computation information" ) ( msg.compGroup ) ( msg.params[0] );
    JASSERT ( theCheckpointState != NULL );
    theCheckpointState->numPeers(msg.params[0]);
    theCheckpointState->compGroup(msg.compGroup);
    compGroup = msg.compGroup;
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
  if (!_checkpointThreadInitialized) {
    /*
     * We should not call this function any higher in the logic because it
     * calls setenv() and if it is running under bash, then it getenv() will
     * not work between the call to setenv() and bash main().
     */
    restoreUserLDPRELOAD();
    _checkpointThreadInitialized = true;
  }

  if ( compGroup != UniquePid() ) {
    dmtcp::string signatureFile = UniquePid::getTmpDir() + "/"
                                + compGroup.toString() + "-"
#ifdef PID_VIRTUALIZATION
                                + jalib::XToString ( _real_getppid() );
#else
                                + jalib::XToString ( getppid() );
#endif
    JTRACE("creating signature file") (signatureFile)(_real_getpid());
    int fd = _real_open ( signatureFile.c_str(), O_CREAT|O_WRONLY, 0600 );
    JASSERT ( fd != -1 ) ( fd ) ( signatureFile )
      .Text ( "Unable to create signature file" );
    dmtcp::string pidstr = jalib::XToString(_real_getpid());
    // FIXME: This assumes write is small, always completes
    JASSERT( pidstr.length()+1
	     == write(fd, pidstr.c_str(), pidstr.length()+1) )
      ( pidstr.length()+1 );
    _real_close(fd);
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

  JTRACE ( "got SUSPEND signal, waiting for dmtcp_lock():"
	   " to get synchronized with _runCoordinatorCmd if we use DMTCP API" );
  _dmtcp_lock();
  // TODO: may be it is better to move unlock to more appropriate place.
  // For example after suspending all threads
  _dmtcp_unlock();


  JTRACE ( "got SUSPEND signal, waiting for lock(&theCkptCanStart)" );
  JASSERT(pthread_mutex_lock(&theCkptCanStart)==0)(JASSERT_ERRNO);

  JTRACE ( "got SUSPEND signal,"
           " waiting for other threads to exit DMTCP-Wrappers" );
  JASSERT(pthread_rwlock_wrlock(&theWrapperExecutionLock) == 0)(JASSERT_ERRNO);
  JTRACE ( "got SUSPEND signal,"
           " waiting for newly created threads to finish initialization" )
         (unInitializedThreadCount);
  waitForThreadsToFinishInitialization();

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
    JASSERT(pthread_mutex_unlock(&destroyDmtcpWorker)==0)(JASSERT_ERRNO);
    pthread_exit(NULL);
  }

  JASSERT(_coordinatorSocket.isValid());
  JASSERT(pthread_mutex_unlock(&destroyDmtcpWorker)==0)(JASSERT_ERRNO);

  JASSERT(pthread_rwlock_unlock(&theWrapperExecutionLock) == 0)(JASSERT_ERRNO);

  JASSERT(pthread_mutex_unlock(&theCkptCanStart)==0)(JASSERT_ERRNO);

  theCheckpointState->preLockSaveOptions();

  waitForCoordinatorMsg ( "LOCK", DMT_DO_LOCK_FDS );

  JTRACE ( "locking..." );
  JASSERT ( theCheckpointState != NULL );
  theCheckpointState->preCheckpointLock();
  JTRACE ( "locked" );

  /*
   * Save first 2 * sizeof(pid_t) bytes of each shared memory area and fill it
   * with all zeros.
   */
  SysVIPC::instance().prepareForLeaderElection();

  WorkerState::setCurrentState ( WorkerState::FD_LEADER_ELECTION );

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

  /*
   * write pid at offset 0. Also write pid at offset sizeof(pid_t) if this
   * process is the creator of this memory area. After the leader election
   * barrier, the leader of the shared-memory object is the creater of the
   * object. If the creater process is missing, then the leader process is the
   * process whose pid is stored at offset 0
   */
  SysVIPC::instance().leaderElection();

  WorkerState::setCurrentState ( WorkerState::DRAINED );

  waitForCoordinatorMsg ( "CHECKPOINT", DMT_DO_CHECKPOINT );
  JTRACE ( "got checkpoint signal" );

#if HANDSHAKE_ON_CHECKPOINT == 1
  //handshake is done after one barrier after drain
  JTRACE ( "beginning handshakes" );
  theCheckpointState->preCheckpointHandshakes(coordinatorId());
  JTRACE ( "handshaking done" );
#endif

//   JTRACE("writing *.dmtcp file");
//   theCheckpointState->outputDmtcpConnectionTable();

#ifdef PID_VIRTUALIZATION
  dmtcp::VirtualPidTable::instance().preCheckpoint();
#endif

  SysVIPC::instance().preCheckpoint();

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

    JTRACE ( "waiting for DRAIN/RESUME Signal" );

    do {
      msg.poison();
      _coordinatorSocket >> msg;
      msg.assertValid();

      if ( msg.type == DMT_KILL_PEER ) {
        JTRACE ( "Received KILL Message from coordinator, exiting" );
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

void dmtcp::DmtcpWorker::writeCheckpointPrefix ( int fd )
{
  const int len = strlen(DMTCP_FILE_HEADER);
  JASSERT(write(fd, DMTCP_FILE_HEADER, len)==len);

  theCheckpointState->outputDmtcpConnectionTable(fd);
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

void dmtcp::DmtcpWorker::postRestart()
{
  JTRACE("begin postRestart()");

  WorkerState::setCurrentState(WorkerState::RESTARTING);
  recvCoordinatorHandshake();

  JASSERT ( theCheckpointState != NULL );
  theCheckpointState->postRestart();

  if ( jalib::Filesystem::GetProgramName() == "screen" )
    send_sigwinch = 1;
  // With hardstatus (bottom status line), screen process has diff. size window
  // Must send SIGWINCH to adjust it.
  // MTCP will send SIGWINCH to process on restart.  This will force 'screen'
  // to execute ioctl wrapper.  The wrapper will report a changed winsize,
  // so that 'screen' must re-initialize the screen (scrolling resions, etc.).
  // The wrapper will also send a second SIGWINCH.  Then 'screen' will
  // call ioctl and get the correct window size and resize again.
  // We can't just send two SIGWINCH's now, since window size has not
  // changed yet, and 'screen' will assume that there's nothing to do. 

#ifdef PID_VIRTUALIZATION
  dmtcp::VirtualPidTable::instance().postRestart();
#endif
  SysVIPC::instance().postRestart();
}

void dmtcp::DmtcpWorker::waitForStage3Refill()
{
  JTRACE ( "checkpointed" );

  WorkerState::setCurrentState ( WorkerState::CHECKPOINTED );

  waitForCoordinatorMsg ( "REFILL", DMT_DO_REFILL );

  JASSERT ( theCheckpointState != NULL );
  theCheckpointState->postCheckpoint();
  delete theCheckpointState;
  theCheckpointState = NULL;

  SysVIPC::instance().postCheckpoint();
}

void dmtcp::DmtcpWorker::waitForStage4Resume()
{
  JTRACE ( "refilled" );
  WorkerState::setCurrentState ( WorkerState::REFILLED );
  waitForCoordinatorMsg ( "RESUME", DMT_DO_RESUME );
  JTRACE ( "got resume signal" );

  SysVIPC::instance().preResume();
}

void dmtcp::DmtcpWorker::restoreVirtualPidTable()
{
#ifdef PID_VIRTUALIZATION
  dmtcp::VirtualPidTable::instance().readPidMapsFromFile();
  dmtcp::VirtualPidTable::instance().restoreProcessGroupInfo();
#endif
}

void dmtcp::DmtcpWorker::restoreSockets(ConnectionState& coordinator,
                                        dmtcp::UniquePid compGroup,
                                        int              numPeers,
                                        int&             coordTstamp)
{
  JTRACE ( "restoreSockets begin" );

  theRestorePort = RESTORE_PORT_START;

  //open up restore socket
  {
    jalib::JSocket restoreSocket ( -1 );
    while ( !restoreSocket.isValid() && theRestorePort < RESTORE_PORT_STOP )
    {
      restoreSocket = jalib::JServerSocket ( jalib::JSockAddr::ANY, ++theRestorePort );
      JTRACE ( "open listen socket attempt" ) ( theRestorePort );
    }
    JASSERT ( restoreSocket.isValid() ) ( RESTORE_PORT_START ).Text ( "failed to open listen socket" );
    restoreSocket.changeFd ( _restoreSocket.sockfd() );
    JTRACE ( "opening listen sockets" ) ( _restoreSocket.sockfd() ) ( restoreSocket.sockfd() );
    _restoreSocket = restoreSocket;
  }

  //reconnect to our coordinator
  connectToCoordinatorWithoutHandshake();
  sendCoordinatorHandshake(jalib::Filesystem::GetProgramName(),
			   compGroup, numPeers, DMT_RESTART_PROCESS);
  recvCoordinatorHandshake(&coordTstamp);
  JTRACE("Connected to coordinator")(coordTstamp);

  // finish sockets restoration
  coordinator.doReconnect ( _coordinatorSocket,_restoreSocket );

  JTRACE ( "sockets restored!" );

}

void dmtcp::DmtcpWorker::delayCheckpointsLock(){
  JASSERT(pthread_mutex_lock(&theCkptCanStart)==0)(JASSERT_ERRNO);
}

void dmtcp::DmtcpWorker::delayCheckpointsUnlock(){
  JASSERT(pthread_mutex_unlock(&theCkptCanStart)==0)(JASSERT_ERRNO);
}

// XXX: Handle deadlock error code
// NOTE: Don't do any fancy stuff in this wrapper which can cause the process to go into DEADLOCK
bool dmtcp::DmtcpWorker::wrapperExecutionLockLock()
{
  int saved_errno = errno;
  bool lockAcquired = false;
  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::RUNNING ) {
    int retVal = pthread_rwlock_rdlock(&theWrapperExecutionLock);
    if ( retVal != 0 && retVal != EDEADLK ) {
      perror ( "ERROR DmtcpWorker::wrapperExecutionLockLock: Failed to acquire lock" );
      _exit(1);
    }
    // retVal should always be 0 (success) here.
    lockAcquired = retVal == 0 ? true : false;
  }
  errno = saved_errno;
  return lockAcquired;
}

// NOTE: Don't do any fancy stuff in this wrapper which can cause the process to go into DEADLOCK
void dmtcp::DmtcpWorker::wrapperExecutionLockUnlock()
{
  int saved_errno = errno;
  if ( dmtcp::WorkerState::currentState() != dmtcp::WorkerState::RUNNING ) {
    printf ( "ERROR: DmtcpWorker::wrapperExecutionLockUnlock: This process is not in \n"
             "RUNNING state and yet this thread managed to acquire the wrapperExecutionLock.\n"
             "This should not be happening, something is wrong." );
    _exit(1);
  }
  if ( pthread_rwlock_unlock(&theWrapperExecutionLock) != 0) {
    perror ( "ERROR DmtcpWorker::wrapperExecutionLockUnlock: Failed to release lock" );
    _exit(1);
    }
  errno = saved_errno;
}

void dmtcp::DmtcpWorker::waitForThreadsToFinishInitialization()
{
  while (unInitializedThreadCount != 0) {
    struct timespec sleepTime = {0, 10*1000*1000};
    nanosleep(&sleepTime, NULL);
  }
}

void dmtcp::DmtcpWorker::incrementUninitializedThreadCount()
{
  int saved_errno = errno;
  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::RUNNING ) {
    JASSERT(pthread_mutex_lock(&unInitializedThreadCountLock) == 0) (JASSERT_ERRNO);
    unInitializedThreadCount++;
    //JTRACE(":") (unInitializedThreadCount);
    JASSERT(pthread_mutex_unlock(&unInitializedThreadCountLock) == 0) (JASSERT_ERRNO);
  }
  errno = saved_errno;
}

void dmtcp::DmtcpWorker::decrementUninitializedThreadCount()
{
  int saved_errno = errno;
  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::RUNNING ) {
    JASSERT(pthread_mutex_lock(&unInitializedThreadCountLock) == 0) (JASSERT_ERRNO);
    JASSERT(unInitializedThreadCount > 0) (unInitializedThreadCount);
    unInitializedThreadCount--;
    //JTRACE(":") (unInitializedThreadCount);
    JASSERT(pthread_mutex_unlock(&unInitializedThreadCountLock) == 0) (JASSERT_ERRNO);
  }
  errno = saved_errno;
}

void dmtcp::DmtcpWorker::connectAndSendUserCommand(char c, int* result /*= NULL*/)
{
  //prevent checkpoints from starting
  delayCheckpointsLock();
  {
    if ( tryConnectToCoordinator() == false ) {
      *result = DmtcpCoordinator::ERROR_COORDINATOR_NOT_FOUND;
      return;
    }
    sendUserCommand(c,result);
    _coordinatorSocket.close();
  }
  delayCheckpointsUnlock();
}

//tell the coordinator to run given user command
void dmtcp::DmtcpWorker::sendUserCommand(char c, int* result /*= NULL*/)
{
  DmtcpMessage msg,reply;

  //send
  msg.type = DMT_USER_CMD;
  msg.params[0] = c;

  if (c == 'i') {
    const char* interval = getenv ( ENV_VAR_CKPT_INTR );
    if ( interval != NULL )
      msg.theCheckpointInterval = jalib::StringToInt ( interval );
  }

  _coordinatorSocket << msg;

  //the coordinator will violently close our socket...
  if(c=='q' || c=='Q'){
    result[0]=0;
    return;
  }

  //receive REPLY
  reply.poison();
  _coordinatorSocket >> reply;
  reply.assertValid();
  JASSERT ( reply.type == DMT_USER_CMD_RESULT );

  if(result!=NULL){
    memcpy( result, reply.params, sizeof(reply.params) );
  }
}


/*!
    \fn dmtcp::DmtcpWorker::connectToCoordinator()
 */
bool dmtcp::DmtcpWorker::tryConnectToCoordinator()
{
  return connectToCoordinator ( false );
}

void dmtcp::DmtcpWorker::connectToCoordinatorWithoutHandshake()
{
  connectToCoordinator ( );
}

void dmtcp::DmtcpWorker::connectToCoordinatorWithHandshake()
{
  connectToCoordinator ( );
  JTRACE("CONNECT TO coordinator, trying to handshake");
  sendCoordinatorHandshake(jalib::Filesystem::GetProgramName());
  recvCoordinatorHandshake();
}

bool dmtcp::DmtcpWorker::connectToCoordinator(bool dieOnError /*= true*/)
{

  const char * coordinatorAddr = getenv ( ENV_VAR_NAME_ADDR );
  const char * coordinatorPortStr = getenv ( ENV_VAR_NAME_PORT );
  dmtcp::UniquePid zeroGroup;

  if ( coordinatorAddr == NULL ) coordinatorAddr = DEFAULT_HOST;
  int coordinatorPort = coordinatorPortStr==NULL ? DEFAULT_PORT : jalib::StringToInt ( coordinatorPortStr );

  jalib::JSocket oldFd = _coordinatorSocket;

  _coordinatorSocket = jalib::JClientSocket ( coordinatorAddr,coordinatorPort );

  if ( ! _coordinatorSocket.isValid() && ! dieOnError ) {
    return false;
  }

  JASSERT ( _coordinatorSocket.isValid() )
    ( coordinatorAddr ) ( coordinatorPort )
    .Text ( "Failed to connect to DMTCP coordinator" );

  JTRACE ( "connected to dmtcp coordinator, no handshake" )
    ( coordinatorAddr ) ( coordinatorPort );

  if ( oldFd.isValid() )
  {
    JTRACE ( "restoring old coordinatorsocket fd" )
      ( oldFd.sockfd() ) ( _coordinatorSocket.sockfd() );

    _coordinatorSocket.changeFd ( oldFd.sockfd() );
  }
  return true;
}

void dmtcp::DmtcpWorker::sendCoordinatorHandshake(const dmtcp::string& progname,
                                                  UniquePid compGroup /*= UniquePid()*/,
                                                  int np /*= -1*/,
                                                  DmtcpMessageType msgType /*= DMT_HELLO_COORDINATOR*/)
{
  JTRACE("sending coordinator handshake")(UniquePid::ThisProcess());

  dmtcp::string hostname = jalib::Filesystem::GetCurrentHostname();
  dmtcp::DmtcpMessage hello_local;
  hello_local.type = msgType;
  hello_local.params[0] = np;
  hello_local.compGroup = compGroup;
  hello_local.restorePort = theRestorePort;

  const char* interval = getenv ( ENV_VAR_CKPT_INTR );
  if ( interval != NULL )
    hello_local.theCheckpointInterval = jalib::StringToInt ( interval );

  hello_local.extraBytes = hostname.length() + 1 + progname.length() + 1;
  _coordinatorSocket << hello_local;
  _coordinatorSocket.writeAll( hostname.c_str(),hostname.length()+1);
  _coordinatorSocket.writeAll( progname.c_str(),progname.length()+1);
}

void dmtcp::DmtcpWorker::recvCoordinatorHandshake( int *param1 ){
  JTRACE("receiving coordinator handshake");

  dmtcp::DmtcpMessage hello_remote;
  hello_remote.poison();
  _coordinatorSocket >> hello_remote;
  hello_remote.assertValid();

  if ( param1 == NULL )
    JASSERT ( hello_remote.type == dmtcp::DMT_HELLO_WORKER ) ( hello_remote.type );
  else
    JASSERT ( hello_remote.type == dmtcp::DMT_RESTART_PROCESS_REPLY ) ( hello_remote.type );

  _coordinatorId = hello_remote.coordinator;
  DmtcpMessage::setDefaultCoordinator ( _coordinatorId );
  if( param1 ){
    *param1 = hello_remote.params[0];
  }
  JTRACE("Coordinator handshake RECEIVED!!!!!");
}

void dmtcp::DmtcpWorker::startCoordinatorIfNeeded(int modes, int isRestart){
  const static int CS_OK = 91;
  const static int CS_NO = 92;
  int coordinatorStatus = -1;

  if (modes & COORD_BATCH) {
    startNewCoordinator ( modes, isRestart );
    return;
  }
  //fork a child process to probe the coordinator
  if (fork()==0) {
    //fork so if we hit an error parent won't die
    dup2(2,1);                          //copy stderr to stdout
    dup2(open("/dev/null",O_RDWR), 2);  //close stderr
    int result[DMTCPMESSAGE_NUM_PARAMS];
    dmtcp::DmtcpCoordinatorAPI coordinatorAPI;
    {
      if ( coordinatorAPI.tryConnectToCoordinator() == false ) {
        JTRACE("Coordinator not found.  Will try to start a new one.");
        _real_exit(1);
      }
    }

    coordinatorAPI.sendUserCommand('s',result);
    coordinatorAPI._coordinatorSocket.close();

    // result[0] == numPeers of coord;  bool result[1] == computation is running
    if(result[0]==0 || result[1] ^ isRestart){
      if(result[0] != 0) {
        int num_processes = result[0];
        JTRACE("Joining existing computation.") (num_processes);
      }
      _real_exit(CS_OK);
    }else{
      JTRACE("Existing computation not in a running state," \
	     " perhaps checkpoint in progress?");
      _real_exit(CS_NO);
    }
  }
  errno = 0;
  // FIXME:  wait() could return -1 if a signal happened before child exits
  JASSERT(::wait(&coordinatorStatus)>0)(JASSERT_ERRNO);
  JASSERT(WIFEXITED(coordinatorStatus));

  //is coordinator running?
  if (WEXITSTATUS(coordinatorStatus) != CS_OK) {
    //is coordinator in funny state?
    if(WEXITSTATUS(coordinatorStatus) == CS_NO){
      JASSERT (false) (isRestart)
	 .Text ("Coordinator in a funny state.  Peers exist, not restarting," \
		"\n but not in a running state.  Checkpointing?" \
		"\n Or maybe restarting and running with peers existing?");
    }else{
      JTRACE("Bad result found for coordinator.  Try a new one.");
    }

    JTRACE("Coordinator not found.  Starting a new one.");
    startNewCoordinator ( modes, isRestart );

  }else{
    if (modes & COORD_FORCE_NEW) {
      JTRACE("Forcing new coordinator.  --new-coordinator flag given.");
      startNewCoordinator ( modes, isRestart );
      return;
    }
    JASSERT( modes & COORD_JOIN )
      .Text("Coordinator already running, but '--new' flag was given.");
  }
}

void dmtcp::DmtcpWorker::startNewCoordinator(int modes, int isRestart)
{
  int coordinatorStatus = -1;
  //get location of coordinator
  const char * coordinatorAddr = getenv ( ENV_VAR_NAME_ADDR );
  if(coordinatorAddr==NULL) coordinatorAddr = DEFAULT_HOST;
  const char * coordinatorPortStr = getenv ( ENV_VAR_NAME_PORT );
  int coordinatorPort = coordinatorPortStr==NULL ? DEFAULT_PORT
                                                 : jalib::StringToInt(coordinatorPortStr);

  dmtcp::string s=coordinatorAddr;
  if(s!="localhost" && s!="127.0.0.1" && s!=jalib::Filesystem::GetCurrentHostname()){
    JASSERT(false)
      .Text("Won't automatically start coordinator because DMTCP_HOST is set to a remote host.");
    _real_exit(1);
  }

  if ( modes & COORD_BATCH || modes & COORD_FORCE_NEW ) {
    // Create a socket and bind it to an unused port.
    jalib::JServerSocket coordinatorListenerSocket ( jalib::JSockAddr::ANY, 0 );
    errno = 0;
    JASSERT ( coordinatorListenerSocket.isValid() )
      ( coordinatorListenerSocket.port() ) ( JASSERT_ERRNO )
      .Text ( "Failed to create listen socket."
          "\nIf msg is \"Address already in use\", this may be an old coordinator."
          "\nKill other coordinators and try again in a minute or so." );
    // Now dup the sockfd to
    coordinatorListenerSocket.changeFd(PROTECTEDFD(1));

    coordinatorPortStr = jalib::XToString(coordinatorListenerSocket.port()).c_str();
    setenv ( ENV_VAR_NAME_PORT, coordinatorPortStr, 1 );
  }

  JTRACE("Starting a new coordinator automatically.") (coordinatorPortStr);

  if(fork()==0){
    dmtcp::string coordinator = jalib::Filesystem::FindHelperUtility("dmtcp_coordinator");
    char *modeStr = (char *)"--background";
    if ( modes & COORD_BATCH ) {
      modeStr = (char *)"--batch";
    }
    char * args[] = {
      (char*)coordinator.c_str(),
      (char*)"--exit-on-last",
      modeStr,
      NULL
    };
    execv(args[0], args);
    JASSERT(false)(coordinator)(JASSERT_ERRNO).Text("exec(dmtcp_coordinator) failed");
  } else {
    _real_close ( PROTECTEDFD (1) );
  }

  errno = 0;

  if ( modes & COORD_BATCH ) {
    // FIXME: If running in batch Mode, we sleep here for 5 seconds to let
    // the coordinator get started up.  We need to fix this in future.
    sleep(5);
  } else {
    JASSERT(::wait(&coordinatorStatus)>0)(JASSERT_ERRNO);

    JASSERT(WEXITSTATUS(coordinatorStatus) == 0)
      .Text("Failed to start coordinator, port already in use.  You may use a different port by running with \'-p 12345\'\n");
  }
}

//to allow linking without mtcpinterface
void __attribute__ ((weak)) dmtcp::initializeMtcpEngine()
{
  JASSERT(false).Text("should not be called");
}

void __attribute__ ((weak)) dmtcp::killCkpthread()
{
  JASSERT(false).Text("should not be called");
}
