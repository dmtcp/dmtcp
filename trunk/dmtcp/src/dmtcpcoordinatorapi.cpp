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

// CAN REMOVE BOOL enableCheckpointing ARG OF DmtcpWorker WHEN WE'RE DONE.
// DmtcpWorker CAN INHERIT THIS CLASS, DmtcpCoordinatorAPI

#include "dmtcpcoordinatorapi.h"
#include "protectedfds.h"
#include "syscallwrappers.h"
#include  "../jalib/jsocket.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jfilesystem.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

dmtcp::DmtcpCoordinatorAPI::DmtcpCoordinatorAPI (int sockfd)
  : _coordinatorSocket(sockfd)
  , _restoreSocket(PROTECTED_RESTORE_SOCK_FD)
{
  return;
}

void dmtcp::DmtcpCoordinatorAPI::useAlternateCoordinatorFd(){
  _coordinatorSocket = jalib::JSocket( PROTECTED_COORD_ALT_FD );
}

void dmtcp::DmtcpCoordinatorAPI::connectAndSendUserCommand(char c, int* result /*= NULL*/)
{
  if ( tryConnectToCoordinator() == false ) {
    *result = ERROR_COORDINATOR_NOT_FOUND;
    return;
  }
  sendUserCommand(c,result);
  _coordinatorSocket.close();
}

/*!
    \fn dmtcp::DmtcpCoordinatorAPI::connectAndSendUserCommand()
 */
bool dmtcp::DmtcpCoordinatorAPI::tryConnectToCoordinator()
{
  return connectToCoordinator ( false );
}

jalib::JSocket
  dmtcp::DmtcpCoordinatorAPI::createNewConnectionToCoordinator (bool dieOnError)
{
  const char * coordinatorAddr = getenv(ENV_VAR_NAME_HOST);
  const char * coordinatorPortStr = getenv(ENV_VAR_NAME_PORT);

  if ( coordinatorAddr == NULL ) coordinatorAddr = DEFAULT_HOST;
  int coordinatorPort = coordinatorPortStr == NULL
                          ? DEFAULT_PORT
                          : jalib::StringToInt(coordinatorPortStr);

  jalib::JSocket fd = jalib::JClientSocket(coordinatorAddr, coordinatorPort);

  if (!fd.isValid() && !dieOnError) {
    return fd;
  }

  JASSERT(fd.isValid()) (coordinatorAddr) (coordinatorPort)
    .Text("Failed to connect to DMTCP coordinator");

  JTRACE("connected to dmtcp coordinator, no handshake")
    (coordinatorAddr) (coordinatorPort);

  return fd;
}

bool dmtcp::DmtcpCoordinatorAPI::connectToCoordinator(bool dieOnError /*= true*/)
{
  jalib::JSocket oldFd = _coordinatorSocket;

  _coordinatorSocket = createNewConnectionToCoordinator(dieOnError);
  if (!_coordinatorSocket.isValid() && !dieOnError) {
    return false;
  }

  if (oldFd.isValid()) {
    JTRACE("restoring old coordinatorsocket fd")
      (oldFd.sockfd()) (_coordinatorSocket.sockfd());

    _coordinatorSocket.changeFd (oldFd.sockfd());
  }
  return true;
}

void dmtcp::DmtcpCoordinatorAPI::createNewConnectionBeforeFork(dmtcp::string& progName)
{
  JTRACE("Informing coordinator of a to-be-created process/program")
    (progName) (UniquePid::ThisProcess());
  _coordinatorSocket = createNewConnectionToCoordinator();
  JASSERT(_coordinatorSocket.isValid());

  sendCoordinatorHandshake(progName);
  recvCoordinatorHandshake();
}

void dmtcp::DmtcpCoordinatorAPI::informCoordinatorOfNewProcessOnFork
  (jalib::JSocket& coordSock)
{
  JASSERT(coordSock.isValid());
  JASSERT(coordSock.sockfd() != PROTECTED_COORD_FD);
  _coordinatorSocket = coordSock;
  _coordinatorSocket.changeFd(PROTECTED_COORD_FD);

  JTRACE("Informing coordinator of new process") (UniquePid::ThisProcess());
  sendCoordinatorHandshake(jalib::Filesystem::GetProgramName() + "_(forked)",
                           UniquePid::ComputationId(),
                           -1,
                           DMT_UPDATE_PROCESS_INFO_AFTER_FORK);
}

void dmtcp::DmtcpCoordinatorAPI::connectToCoordinatorWithHandshake()
{
  connectToCoordinator ( );
  JTRACE("CONNECT TO coordinator, trying to handshake");
  sendCoordinatorHandshake(jalib::Filesystem::GetProgramName());
  recvCoordinatorHandshake();
}

void dmtcp::DmtcpCoordinatorAPI::connectToCoordinatorWithoutHandshake()
{
  connectToCoordinator ( );
}

// FIXME:
static int theRestorePort = RESTORE_PORT_START;
void dmtcp::DmtcpCoordinatorAPI::sendCoordinatorHandshake (
  const dmtcp::string& progname,
  UniquePid compGroup /*= UniquePid()*/,
  int np /*= -1*/,
  DmtcpMessageType msgType /*= DMT_HELLO_COORDINATOR*/)
{
  JTRACE("sending coordinator handshake")(UniquePid::ThisProcess());

  dmtcp::string hostname = jalib::Filesystem::GetCurrentHostname();
  const char *prefixPathEnv = getenv(ENV_VAR_PREFIX_PATH);
  dmtcp::string prefixDir;
  DmtcpMessage hello_local;
  hello_local.type = msgType;
  hello_local.params[0] = np;
  hello_local.compGroup = compGroup;
  hello_local.restorePort = theRestorePort;

  if (getenv(ENV_VAR_VIRTUAL_PID) == NULL) {
    hello_local.virtualPid = -1;
  } else {
    hello_local.virtualPid = (pid_t) atoi(getenv(ENV_VAR_VIRTUAL_PID));
  }

  const char* interval = getenv ( ENV_VAR_CKPT_INTR );
  /* DmtcpMessage constructor default:
   *   hello_local.theCheckpointInterval: DMTCPMESSAGE_SAME_CKPT_INTERVAL
   */
  if ( interval != NULL )
    hello_local.theCheckpointInterval = jalib::StringToInt ( interval );
  // Tell the coordinator the ckpt interval only once.  It can change later.
  _dmtcp_unsetenv ( ENV_VAR_CKPT_INTR );

  hello_local.extraBytes = hostname.length() + 1 + progname.length() + 1;

  if (prefixPathEnv != NULL) {
    /* If --prefix was defined then this process is either running on the local
     * node (the home of first process in the comptation) or a remote node.
     *
     * If the process is running on the local node, the prefix-path-env may be
     * different from the prefix-dir of this binary, in which case, we want to
     * send the prefix-path of this binary to the coordinator and the
     * coordinator will save it as the local-prefix.
     *
     * However, if this is running on a remote node, the prefix-path-env would
     * be the same as the prefix-path of this binary and we should send the
     * prefix-path-env to the coordinator and the coordinator will note this as
     * the remote-prefix.
     */
    dmtcp::string utilDirPrefix =
      jalib::Filesystem::DirName(getenv(ENV_VAR_UTILITY_DIR));
    if (utilDirPrefix == jalib::Filesystem::ResolveSymlink(prefixPathEnv)) {
      prefixDir = prefixPathEnv;
    } else {
      prefixDir = utilDirPrefix;
    }
    hello_local.extraBytes += prefixDir.length() + 1;
  }

  _coordinatorSocket << hello_local;
  _coordinatorSocket.writeAll( hostname.c_str(),hostname.length()+1);
  _coordinatorSocket.writeAll( progname.c_str(),progname.length()+1);
  if (!prefixDir.empty()) {
    _coordinatorSocket.writeAll(prefixDir.c_str(), prefixDir.length()+1);
  }
}

void dmtcp::DmtcpCoordinatorAPI::recvCoordinatorHandshake(int *param1)
{
  JTRACE("receiving coordinator handshake");

  DmtcpMessage hello_remote;
  hello_remote.poison();
  _coordinatorSocket >> hello_remote;
  hello_remote.assertValid();

  if (hello_remote.type == DMT_KILL_PEER) {
    JTRACE ( "Received KILL message from coordinator, exiting" );
    _exit ( 0 );
  }

  if ( param1 == NULL ) {
    JASSERT(hello_remote.type == DMT_HELLO_WORKER) (hello_remote.type);
  } else {
    JASSERT(hello_remote.type == DMT_RESTART_PROCESS_REPLY) (hello_remote.type);
  }

  _coordinatorId = hello_remote.coordinator;
  DmtcpMessage::setDefaultCoordinator(_coordinatorId);
  UniquePid::ComputationId() = hello_remote.compGroup;
  if (param1) {
    *param1 = hello_remote.params[0];
  }
  _virtualPid = hello_remote.virtualPid;
  JTRACE("Coordinator handshake RECEIVED!!!!!");
}

//tell the coordinator to run given user command
void dmtcp::DmtcpCoordinatorAPI::sendUserCommand(char c, int* result /*= NULL*/)
{
  DmtcpMessage msg, reply;

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

pid_t dmtcp::DmtcpCoordinatorAPI::getVirtualPidFromCoordinator()
{
  connectToCoordinator();
  DmtcpMessage msg(DMT_GET_VIRTUAL_PID);
  _coordinatorSocket << msg;

  DmtcpMessage reply;
  reply.poison();
  _coordinatorSocket >> reply;
  reply.assertValid();
  JASSERT(reply.type == DMT_GET_VIRTUAL_PID_RESULT) (reply.type);
  JASSERT(reply.virtualPid != -1);

  _coordinatorSocket.close();
  return reply.virtualPid;
}

void dmtcp::DmtcpCoordinatorAPI::startCoordinatorIfNeeded(int modes,
                                                          int isRestart)
{
  const static int CS_OK = DMTCP_FAIL_RC+1;
  const static int CS_NO = DMTCP_FAIL_RC+2;
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
    DmtcpCoordinatorAPI coordinatorAPI;
    {
      if ( coordinatorAPI.tryConnectToCoordinator() == false ) {
        JTRACE("Coordinator not found.  Will try to start a new one.");
        _real_exit(DMTCP_FAIL_RC);
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
		"\n but not in a running state.  Often this means you are" \
		" connecting to\n a stale coordinator from a previous" \
		" computation.\n Try killing the other coordinator," \
		" or using a different port for the new comp.");
    }else if (WEXITSTATUS(coordinatorStatus) == DMTCP_FAIL_RC) {
      JTRACE("Coordinator not found.  Starting a new one.");
    }else{
      JTRACE("Bad result found for coordinator.  Will try start a new one.");
    }

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

void dmtcp::DmtcpCoordinatorAPI::startNewCoordinator(int modes, int isRestart)
{
  int coordinatorStatus = -1;
  //get location of coordinator
  const char *coordinatorAddr = getenv ( ENV_VAR_NAME_HOST );
  if(coordinatorAddr == NULL) coordinatorAddr = DEFAULT_HOST;
  const char *coordinatorPortStr = getenv ( ENV_VAR_NAME_PORT );

  dmtcp::string s = coordinatorAddr;
  if(s != "localhost" && s != "127.0.0.1" &&
     s != jalib::Filesystem::GetCurrentHostname()){
    JASSERT(false)(s)(jalib::Filesystem::GetCurrentHostname())
      .Text("Won't automatically start coordinator because DMTCP_HOST"
            " is set to a remote host.");
    _real_exit(DMTCP_FAIL_RC);
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
    coordinatorListenerSocket.changeFd(PROTECTED_COORD_FD);
    dmtcp::string coordPort= jalib::XToString(coordinatorListenerSocket.port());
    setenv ( ENV_VAR_NAME_PORT, coordPort.c_str(), 1 );
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
    _real_close ( PROTECTED_COORD_FD );
  }

  errno = 0;

  if ( modes & COORD_BATCH ) {
    // FIXME: If running in batch Mode, we sleep here for 5 seconds to let
    // the coordinator get started up.  We need to fix this in future.
    sleep(5);
  } else {
    JASSERT(wait(&coordinatorStatus)>0)(JASSERT_ERRNO);

    JASSERT(WEXITSTATUS(coordinatorStatus) == 0)
      .Text("Failed to start coordinator, port already in use.  You may use a different port by running with \'-p 12345\'\n");
  }
}

jalib::JSocket& dmtcp::DmtcpCoordinatorAPI::openRestoreSocket()
{
  JTRACE ("restoreSockets begin");

  theRestorePort = RESTORE_PORT_START;

  jalib::JSocket restoreSocket (-1);
  while (!restoreSocket.isValid() && theRestorePort < RESTORE_PORT_STOP) {
    restoreSocket = jalib::JServerSocket(jalib::JSockAddr::ANY,
                                         ++theRestorePort);
    JTRACE ("open listen socket attempt") (theRestorePort);
  }
  JASSERT (restoreSocket.isValid()) (RESTORE_PORT_START)
    .Text("failed to open listen socket");
  restoreSocket.changeFd(_restoreSocket.sockfd());
  JTRACE ("opening listen sockets")
    (_restoreSocket.sockfd()) (restoreSocket.sockfd());
  _restoreSocket = restoreSocket;
  return _restoreSocket;
}
