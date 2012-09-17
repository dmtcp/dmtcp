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
// DmtcpWorker CAN INHERIT THIS CLASS, CoordinatorAPI

#include "coordinatorapi.h"
#include "protectedfds.h"
#include "syscallwrappers.h"
#include  "../jalib/jsocket.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jfilesystem.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

using namespace dmtcp;

dmtcp::CoordinatorAPI::CoordinatorAPI (int sockfd)
  : _coordinatorSocket(sockfd)
  , _restoreSocket(PROTECTED_RESTORE_SOCK_FD)
{
  return;
}

void dmtcp::CoordinatorAPI::setupVirtualCoordinator()
{
  jalib::JSockAddr addr;
  const char *portStr = getenv(ENV_VAR_NAME_PORT);
  int port = (portStr == NULL) ? DEFAULT_PORT : jalib::StringToInt(portStr);
  jalib::JServerSocket virtCoordSock (addr, port);
  JASSERT(virtCoordSock.isValid()) (port) (JASSERT_ERRNO)
    .Text("Failed to create listen socket.");
  virtCoordSock.changeFd(PROTECTED_VIRT_COORD_FD);
}

void dmtcp::CoordinatorAPI::waitForCheckpointCommand()
{
  jalib::JSocket cmdSock(-1);
  jalib::JServerSocket virtCoordSock (PROTECTED_VIRT_COORD_FD);
  dmtcp::DmtcpMessage msg;
  dmtcp::DmtcpMessage reply(DMT_USER_CMD_RESULT);
  do {
    cmdSock.close();
    cmdSock = virtCoordSock.accept();
    msg.poison();
    JTRACE("Reading from incoming connection...");
    cmdSock >> msg;
  } while (!cmdSock.isValid());

  JASSERT(msg.type == DMT_USER_CMD) (msg.type)
    .Text("Unexpected connection.");

  reply.coordErrorCode = CoordinatorAPI::NOERROR;

  switch (msg.coordCmd) {
//    case 'b': case 'B':  // prefix blocking command, prior to checkpoint command
//      JTRACE("blocking checkpoint beginning...");
//      blockUntilDone = true;
//      break;
    case 'c': case 'C':
      JTRACE("checkpointing...");
      break;
    case 'k': case 'K':
    case 'q': case 'Q':
      JTRACE("Received KILL command from user, exiting");
      _exit(0);
      break;
    default:
      JTRACE("unhandled user command") (msg.coordCmd);
      reply.coordErrorCode = CoordinatorAPI::ERROR_INVALID_COMMAND;
  }
  cmdSock << reply;
  cmdSock.close();
  return;
}

bool dmtcp::CoordinatorAPI::noCoordinator()
{
  static int virtualCoordinator = -1;
  if (virtualCoordinator == -1) {
    int optVal = -1;
    socklen_t optLen = sizeof(optVal);
    int ret = _real_getsockopt(PROTECTED_VIRT_COORD_FD, SOL_SOCKET,
                               SO_ACCEPTCONN, &optVal, &optLen);
    if (ret == 0) {
      JASSERT(optVal == 1);
      virtualCoordinator = 1;
    } else {
      virtualCoordinator = 0;
    }
  }
  return virtualCoordinator;
}

void dmtcp::CoordinatorAPI::useAlternateCoordinatorFd() {
  _coordinatorSocket = jalib::JSocket(PROTECTED_COORD_ALT_FD);
}

void dmtcp::CoordinatorAPI::sendMsgToCoordinator(dmtcp::DmtcpMessage msg,
                                                 const void *extraData,
                                                 size_t len)
{
  _coordinatorSocket << msg;
  if (msg.extraBytes > 0) {
    JASSERT(extraData != NULL);
    JASSERT(len == msg.extraBytes);
    _coordinatorSocket.writeAll((const char *)extraData, msg.extraBytes);
  }
}

void dmtcp::CoordinatorAPI::recvMsgFromCoordinator(dmtcp::DmtcpMessage *msg,
                                                   void **extraData)
{
  msg->poison();
  _coordinatorSocket >> (*msg);

  if (extraData != NULL) {
    msg->assertValid();
    JASSERT(msg->extraBytes > 0);
    // Caller must free this buffer
    void *buf = JALLOC_HELPER_MALLOC(msg->extraBytes);
    _coordinatorSocket.readAll((char*)buf, msg->extraBytes);
    JASSERT(extraData != NULL);
    *extraData = buf;
  }
}

void dmtcp::CoordinatorAPI::connectAndSendUserCommand(char c,
                                                      int *coordErrorCode,
                                                      int *numPeers,
                                                      int *running)
{
  if (tryConnectToCoordinator() == false) {
    *coordErrorCode = ERROR_COORDINATOR_NOT_FOUND;
    return;
  }
  sendUserCommand(c, coordErrorCode, numPeers, running);
  _coordinatorSocket.close();
}

/*!
    \fn dmtcp::CoordinatorAPI::connectAndSendUserCommand()
 */
bool dmtcp::CoordinatorAPI::tryConnectToCoordinator()
{
  return connectToCoordinator (false);
}

jalib::JSocket
  dmtcp::CoordinatorAPI::createNewConnectionToCoordinator (bool dieOnError)
{
  const char * coordinatorAddr = getenv(ENV_VAR_NAME_HOST);
  const char * coordinatorPortStr = getenv(ENV_VAR_NAME_PORT);

  JASSERT(!noCoordinator());

  if (coordinatorAddr == NULL) coordinatorAddr = DEFAULT_HOST;
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

bool dmtcp::CoordinatorAPI::connectToCoordinator(bool dieOnError /*= true*/)
{
  if (noCoordinator()) return true;
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

void dmtcp::CoordinatorAPI::createNewConnectionBeforeFork(dmtcp::string& progName)
{
  JASSERT(!noCoordinator());
  JTRACE("Informing coordinator of a to-be-created process/program")
    (progName) (UniquePid::ThisProcess());
  _coordinatorSocket = createNewConnectionToCoordinator();
  JASSERT(_coordinatorSocket.isValid());

  sendCoordinatorHandshake(progName, UniquePid(), -1, DMT_HELLO_COORDINATOR,
                           true);
  recvCoordinatorHandshake();
  JASSERT(_virtualPid != -1);
}

void dmtcp::CoordinatorAPI::informCoordinatorOfNewProcessOnFork
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

void dmtcp::CoordinatorAPI::connectToCoordinatorWithHandshake()
{
  connectToCoordinator ();
  JTRACE("CONNECT TO coordinator, trying to handshake");
  sendCoordinatorHandshake(jalib::Filesystem::GetProgramName());
  recvCoordinatorHandshake();
}

void dmtcp::CoordinatorAPI::connectToCoordinatorWithoutHandshake()
{
  connectToCoordinator ();
}

// FIXME:
static int theRestorePort = RESTORE_PORT_START;
void dmtcp::CoordinatorAPI::sendCoordinatorHandshake(
                           const dmtcp::string& progname,
                           UniquePid compGroup /*= UniquePid()*/,
                           int np /*= -1*/,
                           DmtcpMessageType msgType /*= DMT_HELLO_COORDINATOR*/,
                           bool preForkHandshake /* = false*/)
{
  if (noCoordinator()) return;
  JTRACE("sending coordinator handshake")(UniquePid::ThisProcess());

  dmtcp::string hostname = jalib::Filesystem::GetCurrentHostname();
  const char *prefixPathEnv = getenv(ENV_VAR_PREFIX_PATH);
  dmtcp::string prefixDir;
  DmtcpMessage hello_local;
  hello_local.type = msgType;
  hello_local.numPeers = np;
  hello_local.compGroup = compGroup;
  hello_local.restorePort = theRestorePort;

  if (preForkHandshake || getenv(ENV_VAR_VIRTUAL_PID) == NULL) {
    hello_local.virtualPid = -1;
  } else {
    hello_local.virtualPid = (pid_t) atoi(getenv(ENV_VAR_VIRTUAL_PID));
  }

  const char* interval = getenv (ENV_VAR_CKPT_INTR);
  /* DmtcpMessage constructor default:
   *   hello_local.theCheckpointInterval: DMTCPMESSAGE_SAME_CKPT_INTERVAL
   */
  if (interval != NULL)
    hello_local.theCheckpointInterval = jalib::StringToInt (interval);
  // Tell the coordinator the ckpt interval only once.  It can change later.
  _dmtcp_unsetenv (ENV_VAR_CKPT_INTR);

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
  _coordinatorSocket.writeAll(hostname.c_str(),hostname.length()+1);
  _coordinatorSocket.writeAll(progname.c_str(),progname.length()+1);
  if (!prefixDir.empty()) {
    _coordinatorSocket.writeAll(prefixDir.c_str(), prefixDir.length()+1);
  }
}

void dmtcp::CoordinatorAPI::recvCoordinatorHandshake()
{
  if (noCoordinator()) return;
  JTRACE("receiving coordinator handshake");

  DmtcpMessage hello_remote;
  hello_remote.poison();
  _coordinatorSocket >> hello_remote;
  hello_remote.assertValid();

  if (hello_remote.type == DMT_KILL_PEER) {
    JTRACE("Received KILL message from coordinator, exiting");
    _exit (0);
  }

  JASSERT(hello_remote.type == DMT_HELLO_WORKER ||
          hello_remote.type == DMT_RESTART_PROCESS_REPLY)
    (hello_remote.type);

  _coordinatorId = hello_remote.coordinator;
  DmtcpMessage::setDefaultCoordinator(_coordinatorId);
  UniquePid::ComputationId() = hello_remote.compGroup;
  _coordTimeStamp = hello_remote.coordTimeStamp;
  _virtualPid = hello_remote.virtualPid;
  JTRACE("Coordinator handshake RECEIVED!!!!!");
}

//tell the coordinator to run given user command
void dmtcp::CoordinatorAPI::sendUserCommand(char c, int* coordErrorCode /*= NULL*/,
                                            int *numPeers, int *isRunning)
{
  DmtcpMessage msg, reply;

  //send
  msg.type = DMT_USER_CMD;
  msg.coordCmd = c;

  if (c == 'i') {
    const char* interval = getenv (ENV_VAR_CKPT_INTR);
    if (interval != NULL)
      msg.theCheckpointInterval = jalib::StringToInt (interval);
  }

  _coordinatorSocket << msg;

  //the coordinator will violently close our socket...
  if (c=='q' || c=='Q') {
    *coordErrorCode = CoordinatorAPI::NOERROR;
    return;
  }

  //receive REPLY
  reply.poison();
  _coordinatorSocket >> reply;
  reply.assertValid();
  JASSERT(reply.type == DMT_USER_CMD_RESULT);

  if (coordErrorCode != NULL) {
    *coordErrorCode =  reply.coordErrorCode;
  }
  if (numPeers != NULL) {
    *numPeers =  reply.numPeers;
  }
  if (isRunning != NULL) {
    *isRunning = reply.isRunning;
  }
}

pid_t dmtcp::CoordinatorAPI::getVirtualPidFromCoordinator()
{
  if (noCoordinator()) {
    return getpid();
  }
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

void dmtcp::CoordinatorAPI::startCoordinatorIfNeeded(dmtcp::CoordinatorAPI::CoordinatorMode mode,
                                                     int isRestart)
{
  const static int CS_OK = DMTCP_FAIL_RC+1;
  const static int CS_NO = DMTCP_FAIL_RC+2;
  int coordinatorStatus = -1;

  if (mode & COORD_NONE) {
    setupVirtualCoordinator();
    return;
  }
  if (mode & COORD_BATCH) {
    startNewCoordinator (mode, isRestart);
    return;
  }
  //fork a child process to probe the coordinator
  if (fork()==0) {
    //fork so if we hit an error parent won't die
    dup2(2,1);                          //copy stderr to stdout
    dup2(open("/dev/null",O_RDWR), 2);  //close stderr
    int numPeers;
    int isRunning;
    int coordErrorCode;
    CoordinatorAPI coordinatorAPI;
    {
      if (coordinatorAPI.tryConnectToCoordinator() == false) {
        JTRACE("Coordinator not found.  Will try to start a new one.");
        _real_exit(DMTCP_FAIL_RC);
      }
    }

    coordinatorAPI.sendUserCommand('s', &coordErrorCode, &numPeers, &isRunning);
    coordinatorAPI._coordinatorSocket.close();

    if (numPeers == 0 || (isRunning ^ isRestart)) {
      if (numPeers != 0) {
        JTRACE("Joining existing computation.") (numPeers);
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
    if (WEXITSTATUS(coordinatorStatus) == CS_NO) {
      JASSERT(false) (isRestart)
	 .Text("Coordinator in a funny state.  Peers exist, not restarting," \
		"\n but not in a running state.  Often this means you are" \
		" connecting to\n a stale coordinator from a previous" \
		" computation.\n Try killing the other coordinator," \
		" or using a different port for the new comp.");
    }else if (WEXITSTATUS(coordinatorStatus) == DMTCP_FAIL_RC) {
      JTRACE("Coordinator not found.  Starting a new one.");
    }else{
      JTRACE("Bad result found for coordinator.  Will try start a new one.");
    }

    startNewCoordinator (mode, isRestart);

  }else{
    if (mode & COORD_FORCE_NEW) {
      JTRACE("Forcing new coordinator.  --new-coordinator flag given.");
      startNewCoordinator (mode, isRestart);
      return;
    }
    JASSERT(mode & COORD_JOIN)
      .Text("Coordinator already running, but '--new' flag was given.");
  }
}

void dmtcp::CoordinatorAPI::startNewCoordinator(dmtcp::CoordinatorAPI::CoordinatorMode mode,
                                                int isRestart)
{
  int coordinatorStatus = -1;
  //get location of coordinator
  const char *coordinatorAddr = getenv (ENV_VAR_NAME_HOST);
  if (coordinatorAddr == NULL) coordinatorAddr = DEFAULT_HOST;
  const char *coordinatorPortStr = getenv (ENV_VAR_NAME_PORT);

  dmtcp::string s = coordinatorAddr;
  if (s != "localhost" && s != "127.0.0.1" &&
     s != jalib::Filesystem::GetCurrentHostname()) {
    JASSERT(false)(s)(jalib::Filesystem::GetCurrentHostname())
      .Text("Won't automatically start coordinator because DMTCP_HOST"
            " is set to a remote host.");
    _real_exit(DMTCP_FAIL_RC);
  }

  if (mode & COORD_BATCH || mode & COORD_FORCE_NEW) {
    // Create a socket and bind it to an unused port.
    jalib::JServerSocket coordinatorListenerSocket (jalib::JSockAddr::ANY, 0);
    errno = 0;
    JASSERT(coordinatorListenerSocket.isValid())
      (coordinatorListenerSocket.port()) (JASSERT_ERRNO)
      .Text("Failed to create listen socket."
          "\nIf msg is \"Address already in use\", this may be an old coordinator."
          "\nKill other coordinators and try again in a minute or so.");
    // Now dup the sockfd to
    coordinatorListenerSocket.changeFd(PROTECTED_COORD_FD);
    dmtcp::string coordPort= jalib::XToString(coordinatorListenerSocket.port());
    setenv (ENV_VAR_NAME_PORT, coordPort.c_str(), 1);
  }

  JTRACE("Starting a new coordinator automatically.") (coordinatorPortStr);

  if (fork()==0) {
    dmtcp::string coordinator = jalib::Filesystem::FindHelperUtility("dmtcp_coordinator");
    char *modeStr = (char *)"--background";
    if (mode & COORD_BATCH) {
      modeStr = (char *)"--batch";
    }
    char * args[] = {
      (char*)coordinator.c_str(),
      (char*)"--exit-on-last",
      modeStr,
      NULL
    };
    execv(args[0], args);
    JASSERT(false)(coordinator)(JASSERT_ERRNO) .Text("exec(dmtcp_coordinator) failed");
  } else {
    _real_close (PROTECTED_COORD_FD);
  }

  errno = 0;

  if (mode & COORD_BATCH) {
    // FIXME: If running in batch Mode, we sleep here for 5 seconds to let
    // the coordinator get started up.  We need to fix this in future.
    sleep(5);
  } else {
    JASSERT(wait(&coordinatorStatus)>0)(JASSERT_ERRNO);

    JASSERT(WEXITSTATUS(coordinatorStatus) == 0)
      .Text("Failed to start coordinator, port already in use.  You may use a different port by running with \'-p 12345\'\n");
  }
}

jalib::JSocket& dmtcp::CoordinatorAPI::openRestoreSocket()
{
  JTRACE("restoreSockets begin");

  theRestorePort = RESTORE_PORT_START;

  jalib::JSocket restoreSocket (-1);
  while (!restoreSocket.isValid() && theRestorePort < RESTORE_PORT_STOP) {
    restoreSocket = jalib::JServerSocket(jalib::JSockAddr::ANY,
                                         ++theRestorePort);
    JTRACE("open listen socket attempt") (theRestorePort);
  }
  JASSERT(restoreSocket.isValid()) (RESTORE_PORT_START)
    .Text("failed to open listen socket");
  restoreSocket.changeFd(_restoreSocket.sockfd());
  JTRACE("opening listen sockets")
    (_restoreSocket.sockfd()) (restoreSocket.sockfd());
  _restoreSocket = restoreSocket;
  return _restoreSocket;
}

void dmtcp::CoordinatorAPI::sendCkptFilename()
{
  if (noCoordinator()) return;
  // Tell coordinator to record our filename in the restart script
  dmtcp::string ckptFilename = dmtcp::UniquePid::getCkptFilename();
  dmtcp::string hostname = jalib::Filesystem::GetCurrentHostname();
  JTRACE("recording filenames") (ckptFilename) (hostname);
  dmtcp::DmtcpMessage msg;
  msg.type = DMT_CKPT_FILENAME;
  msg.extraBytes = ckptFilename.length() +1 + hostname.length() +1;
  _coordinatorSocket << msg;
  _coordinatorSocket.writeAll (ckptFilename.c_str(), ckptFilename.length() +1);
  _coordinatorSocket.writeAll (hostname.c_str(),     hostname.length() +1);
}

// At restart, the HOST/PORT used by dmtcp_coordinator could be different then
// those at checkpoint time. This could cause the child processes created after
// restart to fail to connect to the coordinator.
void dmtcp::CoordinatorAPI::updateHostAndPortEnv()
{
  if (noCoordinator()) return;
  struct sockaddr addr;
  socklen_t addrLen = sizeof addr;
  JASSERT(0 == getpeername(_coordinatorSocket.sockfd(), &addr, &addrLen))
    (JASSERT_ERRNO);

  /* If the current coordinator is running on a HOST/PORT other than the
   * pre-checkpoint HOST/PORT, we need to update the environment variables
   * pointing to the coordinator HOST/PORT. This is needed if the new
   * coordinator has been moved around.
   */

  const char * origCoordAddr = getenv (ENV_VAR_NAME_HOST);
  const char * origCoordPortStr = getenv (ENV_VAR_NAME_PORT);
  if (origCoordAddr == NULL) origCoordAddr = DEFAULT_HOST;
  int origCoordPort = origCoordPortStr==NULL ? DEFAULT_PORT : jalib::StringToInt (origCoordPortStr);

  jalib::JSockAddr originalCoordinatorAddr(origCoordAddr, origCoordPort);
  if (addrLen != originalCoordinatorAddr.addrlen() ||
      memcmp(originalCoordinatorAddr.addr(), &addr, addrLen) != 0) {

    JASSERT(addr.sa_family == AF_INET) (addr.sa_family)
      .Text("Coordinator socket always uses IPV4 sockets");

    char currHost[1024];
    char currPort[16];

    int res = getnameinfo(&addr, addrLen, currHost, sizeof currHost,
                          currPort, sizeof currPort, NI_NUMERICSERV);
    JASSERT(res == 0) (currHost) (currPort) (gai_strerror(res))
      .Text("getnameinfo(... currHost, ..., currPort,...) failed");

    JTRACE("Coordinator running at a different location")
      (origCoordAddr) (origCoordPort) (currHost) (currPort);

    JASSERT(0 == setenv (ENV_VAR_NAME_HOST, currHost, 1)) (JASSERT_ERRNO);
    JASSERT(0 == setenv (ENV_VAR_NAME_PORT, currPort, 1)) (JASSERT_ERRNO);
  }
}

int dmtcp::CoordinatorAPI::sendKeyValPairToCoordinator(const void *key,
                                                       size_t key_len,
                                                       const void *val,
                                                       size_t val_len)
{
  void *extraData = JALLOC_HELPER_MALLOC(key_len + val_len);
  memcpy(extraData, key, key_len);
  memcpy((char*)extraData + key_len, val, val_len);

  DmtcpMessage msg (DMT_REGISTER_NAME_SERVICE_DATA);
  msg.keyLen = key_len;
  msg.valLen = val_len;
  msg.extraBytes = key_len + val_len;

  sendMsgToCoordinator(msg, extraData, msg.extraBytes);
  JALLOC_HELPER_FREE(extraData);
  return 1;
}

// On input, val points to a buffer in user memory and *val_len is the maximum
//   size of that buffer (the memory allocated by user).
// On output, we copy data to val, and set *val_len to the actual buffer size
//   (to the size of the data that we copied to the user buffer).
int dmtcp::CoordinatorAPI::sendQueryToCoordinator(const void *key, size_t key_len,
                                                  void *val, size_t *val_len)
{
  /* THE USER JUST GAVE US A BUFFER, val.  WHY ARE WE ALLOCATING
   * EXTRA MEMORY HERE?  ALLOCATING MEMORY IS DANGEROUS.  WE ARE A GUEST
   * IN THE USER'S PROCESS.  IF WE NEED TO, CREATE A message CONSTRUCTOR
   * AROUND THE USER'S 'key' INPUT.
   *   ALSO, SINCE THE USER GAVE US *val_len * CHARS OF MEMORY, SHOULDN'T
   * WE BE SETTING msg.extraBytes TO *val_len AND NOT key_len?
   * ANYWAY, WHY DO WE USE THE SAME msg OBJECT FOR THE "send key"
   * AND FOR THE "return val"?  IT'S NOT TO SAVE MEMORY.  :-)
   * THANKS, - Gene
   */
  void *extraData;

  DmtcpMessage msg (DMT_NAME_SERVICE_QUERY);
  msg.keyLen = key_len;
  msg.valLen = 0;
  msg.extraBytes = key_len;

  sendMsgToCoordinator(msg, key, key_len);

  msg.poison();

  recvMsgFromCoordinator(&msg, &extraData);

  JASSERT(msg.type == DMT_NAME_SERVICE_QUERY_RESPONSE &&
          msg.extraBytes > 0 && (msg.valLen + msg.keyLen) == msg.extraBytes);

  //TODO: FIXME --> enforce the JASSERT
  JASSERT(*val_len >= msg.extraBytes - key_len) (*val_len) (msg.extraBytes) (key_len);
  JASSERT(memcmp(key, extraData, key_len) == 0);
  memcpy(val, (char*)extraData + key_len, msg.extraBytes-key_len);
  *val_len = msg.valLen;
  JALLOC_HELPER_FREE(extraData);
  return 1;
}
