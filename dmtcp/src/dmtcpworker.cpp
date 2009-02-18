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


static pthread_mutex_t theCkptCanStart = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

bool dmtcp::DmtcpWorker::_stdErrMasked = false;
bool dmtcp::DmtcpWorker::_stdErrClosed = false;

void dmtcp::DmtcpWorker::maskStdErr()
{
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
  if ( _stdErrMasked == false ) return;
  
  int oldfd = PROTECTED_STDERR_FD;

  // if stderr fd of the process was closed before masking, then make sure to close it here.
  if ( _stdErrClosed == false ) {
    JASSERT ( _real_dup2 ( oldfd, 2 ) == 2 );
    _real_close ( oldfd );
  } else {
    _real_close ( 2 );
  }

  _stdErrMasked = false;
}

// static dmtcp::KernelBufferDrainer* theDrainer = 0;
static dmtcp::ConnectionState* theCheckpointState = 0;
static int theRestorPort = RESTORE_PORT_START;

void dmtcp::DmtcpWorker::useAlternateCoordinatorFd(){
  _coordinatorSocket = jalib::JSocket( PROTECTEDFD( 4 ) );
}

//called before user main()
dmtcp::DmtcpWorker::DmtcpWorker ( bool enableCheckpointing )
    :_coordinatorSocket ( PROTECTEDFD ( 1 ) )
    ,_restoreSocket ( PROTECTEDFD ( 3 ) )
{
  if ( !enableCheckpointing ) return;
  if ( getenv("JALIB_UTILITY_DIR") == NULL ) {
    JNOTE ( "\n **** Not checkpointing this process,"
            " due to missing environment var ****" )
          ( getenv("JALIB_UTILITY_DIR") )
          ( jalib::Filesystem::GetProgramName() );
    return;
  }

  const char* serialFile = getenv( ENV_VAR_SERIALFILE_INITIAL );

  JASSERT_INIT();
  JTRACE ( "dmtcphijack.so:  Running " ) ( jalib::Filesystem::GetProgramName() ) ( getenv ( "LD_PRELOAD" ) );
  JTRACE ( "dmtcphijack.so:  Child of pid " ) ( getppid() );

  if ( jalib::Filesystem::GetProgramName() == "ssh" )
  {
    //make sure coordinator connection is closed
    _real_close ( PROTECTEDFD ( 1 ) );

    //get prog args
    std::vector<std::string> args = jalib::Filesystem::GetProgramArgs();
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
    std::string& cmd = args[commandStart];


    const char * coordinatorAddr      = getenv ( ENV_VAR_NAME_ADDR );
    const char * coordinatorPortStr   = getenv ( ENV_VAR_NAME_PORT );
    const char * sigckpt              = getenv ( ENV_VAR_SIGCKPT );
    const char * compression          = getenv ( ENV_VAR_COMPRESSION );
    const char * ckptOpenFiles        = getenv ( ENV_VAR_CKPT_OPEN_FILES ); 
    const char * ckptDir              = getenv ( ENV_VAR_CHECKPOINT_DIR );

    //modify the command

    //std::string prefix = "env ";

    std::string prefix = DMTCP_CHECKPOINT_CMD " --ssh-slave ";


    if ( coordinatorAddr != NULL )    prefix += std::string() + "--host " + coordinatorAddr    + " ";
    if ( coordinatorPortStr != NULL ) prefix += std::string() + "--port " + coordinatorPortStr + " ";
    if ( sigckpt != NULL )            prefix += std::string() + "--mtcp-checkpoint-signal "    + sigckpt + " ";
    if ( ckptDir != NULL )            prefix += std::string() + "--dir "  + ckptDir            + " ";
    if ( ckptOpenFiles != NULL )      prefix += std::string() + "--checkpoint-open-files ";

    if ( compression != NULL ) {
      if ( strcmp ( compression, "0" ) )
        prefix += "--no-gzip ";
      else 
        prefix += "--gzip ";
    }

    cmd = prefix + cmd;

    //now repack args
    std::string newCommand = "";
    char** argv = new char*[args.size() +2];
    memset ( argv,0,sizeof ( char* ) * ( args.size() +2 ) );

    for ( size_t i=0; i< args.size(); ++i )
    {
      argv[i] = ( char* ) args[i].c_str();
      newCommand += args[i] + ' ';
    }

    //we don't want to get into an infinite loop now do we?
    unsetenv ( "LD_PRELOAD" );
    unsetenv ( ENV_VAR_HIJACK_LIB );

    JNOTE ( "re-running SSH with checkpointing" ) ( newCommand );

    //now re-call ssh
    execvp ( argv[0], argv );

    //should be unreachable
    JASSERT ( false ) ( cmd ) ( JASSERT_ERRNO ).Text ( "exec() failed" );
  }

  WorkerState::setCurrentState ( WorkerState::RUNNING );

  connectToCoordinator();

  initializeMtcpEngine();

  if ( serialFile != NULL )
  {
    JTRACE ( "loading initial socket table from file..." ) ( serialFile );

    //reset state
//         ConnectionList::Instance() = ConnectionList();
//         KernelDeviceToConnection::Instance() = KernelDeviceToConnection();

    //load file
    jalib::JBinarySerializeReader rd ( serialFile );
    KernelDeviceToConnection::Instance().serialize ( rd );

#ifdef DEBUG
    JTRACE ( "initial socket table:" );
    KernelDeviceToConnection::Instance().dbgSpamFds();
#endif

    unsetenv ( ENV_VAR_SERIALFILE_INITIAL );
  }
  else
  {
    JTRACE ( "root of processes tree, checking for pre-existing sockets" );
    ConnectionList::Instance().scanForPreExisting();
  }


// #ifdef DEBUG
//     JTRACE("listing fds");
//     KernelDeviceToConnection::Instance().dbgSpamFds();
// #endif
}

//called after user main()
dmtcp::DmtcpWorker::~DmtcpWorker()
{
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
      if ( msg.type == dmtcp::DMT_KILL_PEER ) exit ( 0 );
      msg.poison();
    }
  }
  JTRACE ( "got SUSPEND signal, waiting for lock(&theCkptCanStart)" );

  JASSERT(pthread_mutex_lock(&theCkptCanStart)==0)(JASSERT_ERRNO);
  JTRACE ( "Starting checkpoint, suspending..." );
}

void dmtcp::DmtcpWorker::waitForStage2Checkpoint()
{
  JTRACE ( "suspended" );
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

void dmtcp::DmtcpWorker::waitForStage3Resume()
{
  JTRACE ( "unmasking stderr" );
  unmaskStdErr();

  {
    // Tell coordinator to record our filename in the restart script
    std::string ckptFilename = dmtcp::UniquePid::checkpointFilename();
    std::string hostname = jalib::Filesystem::GetCurrentHostname();
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


void dmtcp::DmtcpWorker::postRestart()
{
  unmaskStdErr();
  JTRACE("begin postRestart()");

  WorkerState::setCurrentState(WorkerState::RESTARTING);
  recvCoordinatorHandshake();

  JASSERT ( theCheckpointState != NULL );
  theCheckpointState->postRestart();

  maskStdErr();
}

void dmtcp::DmtcpWorker::restoreSockets ( ConnectionState& coordinator )
{
  JTRACE ( "restoreSockets begin" );

  theRestorPort = RESTORE_PORT_START;

  //open up restore socket
  {
    jalib::JSocket restorSocket ( -1 );
    while ( !restorSocket.isValid() && theRestorPort < RESTORE_PORT_STOP )
    {
      restorSocket = jalib::JServerSocket ( jalib::JSockAddr::ANY, ++theRestorPort );
      JTRACE ( "open listen socket attempt" ) ( theRestorPort );
    }
    JASSERT ( restorSocket.isValid() ) ( RESTORE_PORT_START ).Text ( "failed to open listen socket" );
    restorSocket.changeFd ( _restoreSocket.sockfd() );
    JTRACE ( "openning listen sockets" ) ( _restoreSocket.sockfd() ) ( restorSocket.sockfd() );
    _restoreSocket = restorSocket;
  }

  //reconnect to our coordinator
  WorkerState::setCurrentState ( WorkerState::RESTARTING );
  connectToCoordinator();

  coordinator.doReconnect ( _coordinatorSocket,_restoreSocket );

  JTRACE ( "sockets restored!" );

}

void dmtcp::DmtcpWorker::delayCheckpointsLock(){
  JASSERT(pthread_mutex_lock(&theCkptCanStart)==0)(JASSERT_ERRNO);
}

void dmtcp::DmtcpWorker::delayCheckpointsUnlock(){
  JASSERT(pthread_mutex_unlock(&theCkptCanStart)==0)(JASSERT_ERRNO);
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
    sendCoordinatorHandshake(jalib::Filesystem::GetProgramName());
    recvCoordinatorHandshake();
  }else{
    JTRACE ( "connected to dmtcp coordinator, no handshake" ) ( coordinatorAddr ) ( coordinatorPort );
  }
}

void dmtcp::DmtcpWorker::sendCoordinatorHandshake(const std::string& progname){
  JTRACE("sending coordinator handshake")(UniquePid::ThisProcess());

  std::string hostname = jalib::Filesystem::GetCurrentHostname();
  dmtcp::DmtcpMessage hello_local;
  hello_local.type = dmtcp::DMT_HELLO_COORDINATOR;
  hello_local.restorePort = theRestorPort;
  hello_local.extraBytes = hostname.length() + 1 + progname.length() + 1;
  _coordinatorSocket << hello_local;
  _coordinatorSocket.writeAll( hostname.c_str(),hostname.length()+1);
  _coordinatorSocket.writeAll( progname.c_str(),progname.length()+1);
}

void dmtcp::DmtcpWorker::recvCoordinatorHandshake(){
  JTRACE("receiving coordinator handshake");

  dmtcp::DmtcpMessage hello_remote;
  hello_remote.poison();
  _coordinatorSocket >> hello_remote;
  hello_remote.assertValid();
  JASSERT ( hello_remote.type == dmtcp::DMT_HELLO_WORKER ) ( hello_remote.type );
  _coordinatorId = hello_remote.coordinator;
  DmtcpMessage::setDefaultCoordinator ( _coordinatorId );
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
    if(result[0]==0 || result[1]^isRestart){
      if(result[0] != 0)
        printf("[DMTCP] Joining existing computation of %d processes.\n", result[0]);
      exit(CS_OK);
    }else{
      printf("[DMTCP] ERROR: existing computation not in a running state, perhaps checkpoint in progress?\n");
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

    fprintf(stderr, "[DMTCP] Coordinator not found at %s:%d.\n", coordinatorAddr, coordinatorPort);

    if((modes&COORD_NEW) == 0){
      fprintf(stderr, "[DMTCP] Won't automatically start coordinator because '--join' flag is specified.\n");
      exit(1);
    }

    if(coordinatorAddr!=NULL){
      std::string s=coordinatorAddr;
      if(s!="localhost" && s!="127.0.0.1" && s!=jalib::Filesystem::GetCurrentHostname()){
        fprintf(stderr, "[DMTCP] Won't automatically start coordinator because DMTCP_HOST is set to a remote host.\n");
        exit(1);
      }
    }

    fprintf(stderr, "[DMTCP] Starting a new coordinator automatically.\n");

    if(fork()==0){
      std::string coordinator = jalib::Filesystem::FindHelperUtility("dmtcp_coordinator");
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
    if(WEXITSTATUS(coordinatorStatus) != 0){
      printf("[DMTCP] ERROR: Failed to start coordinator, port already in use.\n[DMTCP] You may use a different port by running with '-p 12345'\n");
      exit(1);
    }
  }else{
    if((modes&COORD_JOIN) == 0){
      fprintf(stderr, "[DMTCP] ERROR: Coordinator already running, but '--new' flag was given.\n");
      exit(1);
    }
  }
}

//to allow linking without mtcpinterface
void __attribute__ ((weak)) dmtcp::initializeMtcpEngine()
{
  JASSERT(false).Text("should not be called");
}
