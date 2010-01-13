/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>


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

/* 
 * WrapperProtectionLock is used to make the checkpoint safe by making sure that
 *   no user-thread is executing any DMTCP wrapper code when it receives the
 *   checkpoint signal. 
 * Working: 
 *   On entering the wrapper in DMTCP, the user-thread acquires the read lock,
 *     and releases it before leaving the wrapper.
 *   When the Checkpoint-thread wants to send the SUSPEND signal to user
 *     threads, it must acquire the write lock. It is blocked until all the
 *     existing read-locks by user threads have been released. NOTE that this is
 *     a WRITER-PREFERRED lock.
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

bool dmtcp::DmtcpWorker::_stdErrMasked = false;
bool dmtcp::DmtcpWorker::_stdErrClosed = false;

void dmtcp::DmtcpWorker::maskStdErr()
{
  return;
  if ( _stdErrMasked == true ) return;

  // if the stderr fd is already closed, we don't want to protect it
  if ( fcntl( 2, F_GETFD) == -1 )
    _stdErrClosed = true;
  else
    _stdErrClosed = false;

  if ( _stdErrClosed == false) {
    int newfd = PROTECTED_STDERR_FD;
    JASSERT ( _real_dup2 ( 2, newfd ) == newfd );
  }
  JASSERT ( _real_dup2 ( JASSERT_STDERR_FD, 2 ) == 2 );
  _stdErrMasked = true;
}

void dmtcp::DmtcpWorker::unmaskStdErr()
{
  return;
  if ( _stdErrMasked == false ) return;
  
  int oldfd = PROTECTED_STDERR_FD;

  // if stderr fd of the process was closed before masking, then make sure to close it here.
  if ( _stdErrClosed == false ) {
    JASSERT ( _real_dup2 ( oldfd, 2 ) == 2 ) (oldfd);
    _real_close ( oldfd );
  } else {
    _real_close ( 2 );
  }

  _stdErrMasked = false;
}

// static dmtcp::KernelBufferDrainer* theDrainer = 0;
static dmtcp::ConnectionState* theCheckpointState = 0;
static int theRestorePort = RESTORE_PORT_START;

void dmtcp::DmtcpWorker::useAlternateCoordinatorFd(){
  _coordinatorSocket = jalib::JSocket( PROTECTEDFD( 4 ) );
}

//called before user main()
dmtcp::DmtcpWorker::DmtcpWorker ( bool enableCheckpointing )
    :_coordinatorSocket ( PROTECTEDFD ( 1 ) )
    ,_restoreSocket ( PROTECTEDFD ( 3 ) )
{
  if ( !enableCheckpointing ) return;

  WorkerState::setCurrentState( WorkerState::UNKNOWN); 

#ifdef DEBUG
  /* Disable Jassert Logging */
  dmtcp::UniquePid::ThisProcess(true);

  dmtcp::ostringstream o;
  o << getenv(ENV_VAR_TMPDIR) << "/jassertlog." << dmtcp::UniquePid::ThisProcess();
  JASSERT_SET_LOGFILE (o.str());
  JASSERT_INIT();

  JTRACE ( "recalculated process UniquePid..." ) ( dmtcp::UniquePid::ThisProcess() );
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

  const char* serialFile = getenv( ENV_VAR_SERIALFILE_INITIAL );
  
  JTRACE ( "dmtcphijack.so:  Running " ) ( jalib::Filesystem::GetProgramName() ) ( getenv ( "LD_PRELOAD" ) );
  JTRACE ( "dmtcphijack.so:  Child of pid " ) ( getppid() );

  if ( jalib::Filesystem::GetProgramName() == "ssh" )
  {
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


    if ( coordinatorAddr != NULL )    prefix += dmtcp::string() + "--host " + coordinatorAddr    + " ";
    if ( coordinatorPortStr != NULL ) prefix += dmtcp::string() + "--port " + coordinatorPortStr + " ";
    if ( sigckpt != NULL )            prefix += dmtcp::string() + "--mtcp-checkpoint-signal "    + sigckpt + " ";
    if ( ckptDir != NULL )            prefix += dmtcp::string() + "--ckptdir " + ckptDir         + " ";
    if ( tmpDir != NULL )             prefix += dmtcp::string() + "--tmpdir " + tmpDir           + " ";
    if ( ckptOpenFiles != NULL )      prefix += dmtcp::string() + "--checkpoint-open-files"      + " ";

    if ( compression != NULL ) {
      if ( strcmp ( compression, "0" ) == 0 )
        prefix += "--no-gzip ";
      else 
        prefix += "--gzip ";
    }

    cmd = prefix + cmd;

    //now repack args
    dmtcp::string newCommand = "";
    char** argv = new char*[args.size() +2];
    memset ( argv,0,sizeof ( char* ) * ( args.size() +2 ) );

    for ( size_t i=0; i< args.size(); ++i )
    {
      argv[i] = ( char* ) args[i].c_str();
      newCommand += args[i] + ' ';
    }

    //we don't want to get into an infinite loop now do we?
    _dmtcp_unsetenv ( "LD_PRELOAD" );
    _dmtcp_unsetenv ( ENV_VAR_HIJACK_LIB );

    JNOTE ( "re-running SSH with checkpointing" ) ( newCommand );

    //now re-call ssh
    execvp ( argv[0], argv );

    //should be unreachable
    JASSERT ( false ) ( cmd ) ( JASSERT_ERRNO ).Text ( "exec() failed" );
  }

  WorkerState::setCurrentState ( WorkerState::RUNNING );

  if ( serialFile != NULL )
  {
    JTRACE ( "loading initial socket table from file..." ) ( serialFile );

    //reset state
//         ConnectionList::Instance() = ConnectionList();
//         KernelDeviceToConnection::Instance() = KernelDeviceToConnection();

    //load file
    jalib::JBinarySerializeReader rd ( serialFile );
    UniquePid::serialize ( rd );
    KernelDeviceToConnection::Instance().serialize ( rd );

#ifdef PID_VIRTUALIZATION
    VirtualPidTable::Instance().serialize ( rd );
#endif 

#ifdef DEBUG
    JTRACE ( "initial socket table:" );
    KernelDeviceToConnection::Instance().dbgSpamFds();
#endif

    _dmtcp_unsetenv ( ENV_VAR_SERIALFILE_INITIAL );
  }
  else
  {
    JTRACE ( "root of processes tree, checking for pre-existing sockets" );

#ifdef PID_VIRTUALIZATION
    if ( getenv( ENV_VAR_ROOT_PROCESS ) != NULL ) {
      dmtcp::VirtualPidTable::Instance().setRootOfProcessTree();
      _dmtcp_unsetenv( ENV_VAR_ROOT_PROCESS );
    }
#endif 

    ConnectionList::Instance().scanForPreExisting();
  }

  connectToCoordinator();

  initializeMtcpEngine();

// #ifdef DEBUG
//     JTRACE("listing fds");
//     KernelDeviceToConnection::Instance().dbgSpamFds();
// #endif
}

//called after user main()
dmtcp::DmtcpWorker::~DmtcpWorker()
{
  pthread_rwlock_t newLock = PTHREAD_RWLOCK_WRITER_NONRECURSIVE_INITIALIZER_NP;
  pthread_mutex_t newCountLock = PTHREAD_MUTEX_INITIALIZER;
  theWrapperExecutionLock = newLock;
  unInitializedThreadCountLock = newCountLock;
  unInitializedThreadCount = 0;
  WorkerState::setCurrentState( WorkerState::UNKNOWN); 
  JTRACE ( "disconnecting from dmtcp coordinator" );
  _coordinatorSocket.close();
}




const dmtcp::UniquePid& dmtcp::DmtcpWorker::coordinatorId() const
{
  return _coordinatorId;
}

void dmtcp::DmtcpWorker::waitForStage1Suspend()
{
  JTRACE ( "running" );
  WorkerState::setCurrentState ( WorkerState::RUNNING );
  {
    dmtcp::DmtcpMessage msg;
    msg.type = DMT_OK;
    msg.state = WorkerState::RUNNING;
    _coordinatorSocket << msg;
  }
  JTRACE ( "waiting for SUSPEND signal" );
  {
    dmtcp::DmtcpMessage msg;
    msg.poison();
    while ( msg.type != dmtcp::DMT_DO_SUSPEND )
    {
      _coordinatorSocket >> msg;
      msg.assertValid();
      JTRACE ( "got MSG from coordinator" ) ( msg.type );
      if ( msg.type == dmtcp::DMT_KILL_PEER )
        exit ( 0 );
      msg.poison();
    }
  }

  JTRACE ( "got SUSPEND signal, waiting for dmtcp_lock(): to get synchronized with _runCoordinatorCmd if we use DMTCP API" );
  _dmtcp_lock();
  // TODO: may be it is better to move unlock to more appropriate place. 
  // For example after suspending all threads
  _dmtcp_unlock();


  JTRACE ( "got SUSPEND signal, waiting for lock(&theCkptCanStart)" );
  JASSERT(pthread_mutex_lock(&theCkptCanStart)==0)(JASSERT_ERRNO);

  JTRACE ( "got SUSPEND signal, waiting for other threads to exit DMTCP-Wrappers" );
  JASSERT(pthread_rwlock_wrlock(&theWrapperExecutionLock) == 0)(JASSERT_ERRNO);
  JTRACE ( "got SUSPEND signal, waiting for newly created threads to finish initialization" )(unInitializedThreadCount);
  waitForThreadsToFinishInitialization();

  JTRACE ( "Starting checkpoint, suspending..." );
}

void dmtcp::DmtcpWorker::waitForStage2Checkpoint()
{
  JTRACE ( "suspended" );

  JASSERT(pthread_rwlock_unlock(&theWrapperExecutionLock) == 0)(JASSERT_ERRNO);

  JASSERT(pthread_mutex_unlock(&theCkptCanStart)==0)(JASSERT_ERRNO);

  WorkerState::setCurrentState ( WorkerState::SUSPENDED );
  {
    dmtcp::DmtcpMessage msg;
    msg.type = DMT_OK;
    msg.state = WorkerState::SUSPENDED;
    _coordinatorSocket << msg;
  }
  JTRACE ( "waiting for lock signal" );
  {
    dmtcp::DmtcpMessage msg;
    msg.poison();
    _coordinatorSocket >> msg;
    msg.assertValid();
    JASSERT ( msg.type == dmtcp::DMT_DO_LOCK_FDS ) ( msg.type );
  }
  JTRACE ( "locking..." );
  JASSERT ( theCheckpointState == 0 );
  theCheckpointState = new ConnectionState();
  theCheckpointState->preCheckpointLock();
  JTRACE ( "locked" );
  WorkerState::setCurrentState ( WorkerState::LOCKED );
  {
    dmtcp::DmtcpMessage msg;
    msg.type = DMT_OK;
    msg.state = WorkerState::LOCKED;
    _coordinatorSocket << msg;
  }
  JTRACE ( "waiting for drain signal" );
  {
    dmtcp::DmtcpMessage msg;
    msg.poison();
    _coordinatorSocket >> msg;
    msg.assertValid();
    JASSERT ( msg.type == dmtcp::DMT_DO_DRAIN ) ( msg.type );
  }
  JTRACE ( "draining..." );
  theCheckpointState->preCheckpointDrain();
  JTRACE ( "drained" );
  WorkerState::setCurrentState ( WorkerState::DRAINED );
  {
    dmtcp::DmtcpMessage msg;
    msg.type = DMT_OK;
    msg.state = WorkerState::DRAINED;
    _coordinatorSocket << msg;
  }
  JTRACE ( "waiting for checkpoint signal" );
  {
    dmtcp::DmtcpMessage msg;
    msg.poison();
    _coordinatorSocket >> msg;
    msg.assertValid();
    JASSERT ( msg.type == dmtcp::DMT_DO_CHECKPOINT ) ( msg.type );
    JTRACE("===================")(msg.params[0])("===================");
    theCheckpointState->numPeers(msg.params[0]);
    theCheckpointState->compGroup(msg.compGroup);
  }
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
  dmtcp::VirtualPidTable::Instance().preCheckpoint();
#endif

  JTRACE ( "masking stderr from mtcp" );
  //because MTCP spams, and the user may have a socket for stderr
  maskStdErr();
}

void dmtcp::DmtcpWorker::writeCheckpointPrefix(int fd){
  unmaskStdErr();

  const int len = strlen(DMTCP_FILE_HEADER);
  JASSERT(write(fd, DMTCP_FILE_HEADER, len)==len);

  theCheckpointState->outputDmtcpConnectionTable(fd);

  maskStdErr();
}

void dmtcp::DmtcpWorker::waitForStage3Resume(int isRestart)
{
  JTRACE ( "unmasking stderr" );
  unmaskStdErr();

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

  JTRACE ( "checkpointed" );
  WorkerState::setCurrentState ( WorkerState::CHECKPOINTED );
  {
    // Tell coordinator we are done checkpointing
    dmtcp::DmtcpMessage msg;
    msg.type = DMT_OK;
    msg.state = WorkerState::CHECKPOINTED;
    _coordinatorSocket << msg;
  }

  JTRACE ( "waiting for refill signal" );
  {
    dmtcp::DmtcpMessage msg;
    do
    {
      msg.poison();
      _coordinatorSocket >> msg;
      msg.assertValid();
    }
    while ( msg.type == DMT_RESTORE_WAITING || msg.type == DMT_FORCE_RESTART );
    JASSERT ( msg.type == dmtcp::DMT_DO_REFILL ) ( msg.type );
  }
  JASSERT ( theCheckpointState != 0 );
  theCheckpointState->postCheckpoint();
  delete theCheckpointState;
  theCheckpointState = 0;

  if (!isRestart) {

    JTRACE ( "refilled" );
    WorkerState::setCurrentState ( WorkerState::REFILLED );
    {
      dmtcp::DmtcpMessage msg;
      msg.type = DMT_OK;
      msg.state = WorkerState::REFILLED;
      _coordinatorSocket << msg;
    }
    JTRACE ( "waiting for resume signal" );
    {
      dmtcp::DmtcpMessage msg;
      msg.poison();
      _coordinatorSocket >> msg;
      msg.assertValid();
      JASSERT ( msg.type == dmtcp::DMT_DO_RESUME ) ( msg.type );
    }
    JTRACE ( "got resume signal" );
  }
  JTRACE ( "masking stderr" );
  maskStdErr();
}

void dmtcp::DmtcpWorker::writeTidMaps()
{
  unmaskStdErr();

  JTRACE ( "refilled" );
  WorkerState::setCurrentState ( WorkerState::REFILLED );
  {
    dmtcp::DmtcpMessage msg;
    msg.type = DMT_OK;
    msg.state = WorkerState::REFILLED;
    _coordinatorSocket << msg;
  }
  JTRACE ( "waiting for resume signal" );
  {
    dmtcp::DmtcpMessage msg;
    msg.poison();
    _coordinatorSocket >> msg;
    msg.assertValid();
    JASSERT ( msg.type == dmtcp::DMT_DO_RESUME ) ( msg.type );
  }
  JTRACE ( "got resume signal" );

#ifdef PID_VIRTUALIZATION
  dmtcp::VirtualPidTable::Instance().postRestart2();
#endif

  // After this point, the user threads will be unlocked in mtcp.c and will
  // resume their computation and so it is OK to set the process state to
  // RUNNING.
  dmtcp::WorkerState::setCurrentState( dmtcp::WorkerState::RUNNING );

  maskStdErr();
}

void dmtcp::DmtcpWorker::postRestart()
{
  unmaskStdErr();
  JTRACE("begin postRestart()");

  WorkerState::setCurrentState(WorkerState::RESTARTING);
  recvCoordinatorHandshake();

  JASSERT ( theCheckpointState != NULL );
  theCheckpointState->postRestart();

#ifdef PID_VIRTUALIZATION
  dmtcp::VirtualPidTable::Instance().postRestart();
#endif

  maskStdErr();
}

void dmtcp::DmtcpWorker::restoreSockets(ConnectionState& coordinator,
    dmtcp::UniquePid compGroup,
    int numPeers,
    int &coordTstamp)
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
    JTRACE ( "openning listen sockets" ) ( _restoreSocket.sockfd() ) ( restoreSocket.sockfd() );
    _restoreSocket = restoreSocket;
  }

  //reconnect to our coordinator
  connectToCoordinator(false);
  sendCoordinatorHandshake(jalib::Filesystem::GetProgramName(),compGroup,numPeers);
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
bool dmtcp::DmtcpWorker::wrapperExecutionLockLock()
{
  int saved_errno = errno;
  bool lockAcquired = false;
  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::RUNNING ) {
    int retVal = pthread_rwlock_rdlock(&theWrapperExecutionLock);
    JASSERT(retVal == 0 || retVal == EDEADLK) (retVal) (strerror(retVal))
      .Text("Failed to acquire rdlock");
    lockAcquired = retVal == 0 ? true : false;
  }
  errno = saved_errno;
  return lockAcquired;
}

void dmtcp::DmtcpWorker::wrapperExecutionLockUnlock()
{
  int saved_errno = errno;
  JASSERT( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::RUNNING )
    .Text( "This implies process is not in running state and yet this thread managed to acquire the wrapperExecutionLock. This is wrong." );
  JASSERT(pthread_rwlock_unlock(&theWrapperExecutionLock) == 0)(JASSERT_ERRNO);
  errno = saved_errno;
}

void dmtcp::DmtcpWorker::waitForThreadsToFinishInitialization()
{
  JTRACE(":") (unInitializedThreadCount);
  while (unInitializedThreadCount != 0) {
    struct timespec sleepTime = {0, 10*1000*1000};
    nanosleep(&sleepTime, NULL);
  }
}

void dmtcp::DmtcpWorker::incrementUnInitializedThreadCount()
{
  int saved_errno = errno;
  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::RUNNING ) {
    JASSERT(pthread_mutex_lock(&unInitializedThreadCountLock) == 0) (JASSERT_ERRNO);
    unInitializedThreadCount++;
    JTRACE(":") (unInitializedThreadCount);
    JASSERT(pthread_mutex_unlock(&unInitializedThreadCountLock) == 0) (JASSERT_ERRNO);
  }
  errno = saved_errno;
}

void dmtcp::DmtcpWorker::decrementUnInitializedThreadCount()
{
  int saved_errno = errno;
  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::RUNNING ) {
    JASSERT(pthread_mutex_lock(&unInitializedThreadCountLock) == 0) (JASSERT_ERRNO);
    JASSERT(unInitializedThreadCount > 0) (unInitializedThreadCount);
    unInitializedThreadCount--;
    JTRACE(":") (unInitializedThreadCount);
    JASSERT(pthread_mutex_unlock(&unInitializedThreadCountLock) == 0) (JASSERT_ERRNO);
  }
  errno = saved_errno;
}

void dmtcp::DmtcpWorker::connectAndSendUserCommand(char c, int* result /*= NULL*/)
{
  //prevent checkpoints from starting
  delayCheckpointsLock();
  {
    connectToCoordinator(false);
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
void dmtcp::DmtcpWorker::connectToCoordinator(bool doHanshaking)
{

  const char * coordinatorAddr = getenv ( ENV_VAR_NAME_ADDR );
  const char * coordinatorPortStr = getenv ( ENV_VAR_NAME_PORT );
  dmtcp::UniquePid zeroGroup;

  if ( coordinatorAddr == NULL ) coordinatorAddr = DEFAULT_HOST;
  int coordinatorPort = coordinatorPortStr==NULL ? DEFAULT_PORT : jalib::StringToInt ( coordinatorPortStr );

  jalib::JSocket oldFd = _coordinatorSocket;

  _coordinatorSocket = jalib::JClientSocket ( coordinatorAddr,coordinatorPort );

  JASSERT ( _coordinatorSocket.isValid() )
  ( coordinatorAddr )
  ( coordinatorPort )
  .Text ( "Failed to connect to DMTCP coordinator" );

  if ( oldFd.isValid() )
  {
    JTRACE ( "restoring old coordinatorsocket fd" )
    ( oldFd.sockfd() )
    ( _coordinatorSocket.sockfd() );

    _coordinatorSocket.changeFd ( oldFd.sockfd() );
  }


  if(doHanshaking)
  {
    JTRACE("\n------------\n\nCONNECT TO coordinator\n\n--------------\n");
    sendCoordinatorHandshake(jalib::Filesystem::GetProgramName());
    recvCoordinatorHandshake();
  }else{
    JTRACE ( "connected to dmtcp coordinator, no handshake" ) ( coordinatorAddr ) ( coordinatorPort );
  }
}

void dmtcp::DmtcpWorker::sendCoordinatorHandshake(const dmtcp::string& progname,UniquePid compGroup,int np){
  JTRACE("sending coordinator handshake")(UniquePid::ThisProcess());

  dmtcp::string hostname = jalib::Filesystem::GetCurrentHostname();
  dmtcp::DmtcpMessage hello_local;
  hello_local.type = dmtcp::DMT_HELLO_COORDINATOR;
  hello_local.params[0] = np;
  hello_local.compGroup = compGroup;
  hello_local.restorePort = theRestorePort;
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
  JASSERT ( hello_remote.type == dmtcp::DMT_HELLO_WORKER ) ( hello_remote.type );
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
  //fork a child process to probe the coordinator
  if(fork()==0){
    //fork so if we hit an error parent wont die
    dup2(2,1);                          //copy stderr to stdout
    dup2(open("/dev/null",O_RDWR), 2);  //close stderr
    int result[DMTCPMESSAGE_NUM_PARAMS];
    dmtcp::DmtcpWorker worker(false);
    worker.connectAndSendUserCommand('s', result);
    if(result[0]==0 || result[1] ^ isRestart){
      if(result[0] != 0) {
        int num_processes = result[0];
        JTRACE("Joining existing computation.") (num_processes);
      }
      exit(CS_OK);
    }else{
      JTRACE("Existing computation not in a running state, perhaps checkpoint in progress?");
      exit(CS_NO);
    }
  }
  errno = 0;
  JASSERT(::wait(&coordinatorStatus)>0)(JASSERT_ERRNO);

  //is coordinator running?
  if(WEXITSTATUS(coordinatorStatus) != CS_OK){
    //is coordinator in funny state?
    if(WEXITSTATUS(coordinatorStatus) == CS_NO){
      exit(1);
    }

    //get location of coordinator
    const char * coordinatorAddr = getenv ( ENV_VAR_NAME_ADDR );
    if(coordinatorAddr==NULL) coordinatorAddr = "localhost";
    const char * coordinatorPortStr = getenv ( ENV_VAR_NAME_PORT );
    int coordinatorPort = coordinatorPortStr==NULL ? DEFAULT_PORT : jalib::StringToInt(coordinatorPortStr);

    JTRACE("Coordinator not found.") (coordinatorAddr) (coordinatorPort);

    JASSERT( modes & COORD_NEW )
      .Text("Won't automatically start coordinator because '--join' flag is specified.");

    if(coordinatorAddr!=NULL){
      dmtcp::string s=coordinatorAddr;
      if(s!="localhost" && s!="127.0.0.1" && s!=jalib::Filesystem::GetCurrentHostname()){
        JASSERT(false)
          .Text("Won't automatically start coordinator because DMTCP_HOST is set to a remote host.");
        exit(1);
      }
    }

    JTRACE("Starting a new coordinator automatically.");

    if(fork()==0){
      dmtcp::string coordinator = jalib::Filesystem::FindHelperUtility("dmtcp_coordinator");
      char * args[] = {
        (char*)coordinator.c_str(),
        (char*)"--exit-on-last",
        (char*)"--background",
        NULL
      };
      execv(args[0], args);
      JASSERT(false)(coordinator)(JASSERT_ERRNO).Text("exec(dmtcp_coordinator) failed");
    }
    errno = 0;

    JASSERT(::wait(&coordinatorStatus)>0)(JASSERT_ERRNO);

    JASSERT(WEXITSTATUS(coordinatorStatus) == 0)
      .Text("Failed to start coordinator, port already in use. You may use a different port by running with \'-p 12345\'\n");

  }else{
    JASSERT( modes & COORD_JOIN )
      .Text("Coordinator already running, but '--new' flag was given.");
  }
}

//to allow linking without mtcpinterface
void __attribute__ ((weak)) dmtcp::initializeMtcpEngine()
{
  JASSERT(false).Text("should not be called");
}
