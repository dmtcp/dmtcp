/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include <netdb.h>
#include <arpa/inet.h>
#include "coordinatorapi.h"
#include "dmtcp.h"
#include "syscallwrappers.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jfilesystem.h"
#include <fcntl.h>

using namespace dmtcp;

extern "C" int fred_record_replay_enabled() __attribute__ ((weak));

void dmtcp_CoordinatorAPI_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_INIT:
      CoordinatorAPI::instance().connectToCoordinatorWithHandshake();
      break;

    case DMTCP_EVENT_THREADS_SUSPEND:
      JASSERT(CoordinatorAPI::instance().isValid());
      break;

    case DMTCP_EVENT_RESTART:
      CoordinatorAPI::instance().updateCoordTimeStamp();
      if (fred_record_replay_enabled == 0 || !fred_record_replay_enabled()) {
        /* This calls setenv() which calls malloc. Since this is only executed on
           restart, that means it there is an extra malloc on replay. Commenting this
           until we have time to fix it. */
        CoordinatorAPI::instance().updateHostAndPortEnv();
      }
      break;

    case DMTCP_EVENT_RESUME:
      dmtcp::CoordinatorAPI::instance().sendCkptFilename();
      break;

    case DMTCP_EVENT_EXIT:
      JTRACE("exit() in progress, disconnecting from dmtcp coordinator");
      CoordinatorAPI::instance().closeConnection();
      break;

    default:
      break;
  }
}

dmtcp::CoordinatorAPI::CoordinatorAPI (int sockfd)
  : _coordinatorSocket(sockfd)
{
  memset(&_coordAddr, 0, sizeof(_coordAddr));
  memset(&_localIPAddr, 0, sizeof(_localIPAddr));
  _coordAddrLen = 0;
  return;
}

static dmtcp::CoordinatorAPI *coordAPIInst = NULL;
dmtcp::CoordinatorAPI& dmtcp::CoordinatorAPI::instance()
{
  //static SysVIPC *inst = new SysVIPC(); return *inst;
  if (coordAPIInst == NULL) {
    coordAPIInst = new CoordinatorAPI();
  }
  return *coordAPIInst;
}

void dmtcp::CoordinatorAPI::resetOnFork(dmtcp::CoordinatorAPI& coordAPI)
{
  JASSERT(coordAPI.coordinatorSocket().isValid());
  JASSERT(coordAPI.coordinatorSocket().sockfd() != PROTECTED_COORD_FD);
  instance() = coordAPI;
  instance()._coordinatorSocket.changeFd(PROTECTED_COORD_FD);

  JTRACE("Informing coordinator of new process") (UniquePid::ThisProcess());

  instance().sendCoordinatorHandshake(jalib::Filesystem::GetProgramName()
                                        + "_(forked)",
                                      UniquePid::ComputationId(),
                                      -1,
                                      DMT_UPDATE_PROCESS_INFO_AFTER_FORK);
  // The coordinator won't send any msg in response to DMT_UPDATE... so no need
  // to call recvCoordinatorHandshake().
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

  reply.coordCmdStatus = CoordCmdStatus::NOERROR;

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
      reply.coordCmdStatus = CoordCmdStatus::ERROR_INVALID_COMMAND;
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

void dmtcp::CoordinatorAPI::sendMsgToCoordinator(const dmtcp::DmtcpMessage &msg,
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
                                                      int *coordCmdStatus,
                                                      int *numPeers,
                                                      int *running)
{
  if (tryConnectToCoordinator() == false) {
    *coordCmdStatus = CoordCmdStatus::ERROR_COORDINATOR_NOT_FOUND;
    return;
  }

  sendUserCommand(c, coordCmdStatus, numPeers, running);
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

  JASSERT(fd.isValid()) (coordinatorAddr) (coordinatorPort) (JASSERT_ERRNO)
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
    JASSERT(getpeername(_coordinatorSocket.sockfd(),
                        (struct sockaddr*)&_coordAddr, &_coordAddrLen) == 0)
      (JASSERT_ERRNO);

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

  JASSERT(getpeername(_coordinatorSocket.sockfd(),
                      (struct sockaddr*)&_coordAddr, &_coordAddrLen) == 0)
    (JASSERT_ERRNO);

  sendCoordinatorHandshake(progName, UniquePid(), -1, DMT_HELLO_COORDINATOR,
                           true);
  recvCoordinatorHandshake();
  JASSERT(_virtualPid != -1);
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

  if (preForkHandshake || getenv(ENV_VAR_VIRTUAL_PID) == NULL) {
    hello_local.virtualPid = -1;
  } else {
    hello_local.virtualPid = (pid_t) atoi(getenv(ENV_VAR_VIRTUAL_PID));
  }

  if (dmtcp_virtual_to_real_pid) {
    hello_local.realPid = dmtcp_virtual_to_real_pid(getpid());
  } else {
    hello_local.realPid = getpid();
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
    const char *utilDirPath = getenv(ENV_VAR_UTILITY_DIR);
    dmtcp::string utilDirPrefix = "";
    if (utilDirPath != NULL) {
      utilDirPrefix = jalib::Filesystem::DirName(utilDirPath);
    }
    if (utilDirPrefix == jalib::Filesystem::ResolveSymlink(prefixPathEnv)) {
      prefixDir = prefixPathEnv;
    } else {
      prefixDir = utilDirPrefix;
    }
    if (!prefixDir.empty()) {
      hello_local.extraBytes += prefixDir.length() + 1;
    }
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

  JASSERT(hello_remote.type == DMT_HELLO_WORKER) (hello_remote.type);

  _coordinatorId = hello_remote.from.upid();
  if (UniquePid::ComputationId() == UniquePid(0,0,0) &&
      hello_remote.compGroup != UniquePid(0,0,0)) {
    UniquePid::ComputationId() = hello_remote.compGroup;
  }
  _coordTimeStamp = hello_remote.coordTimeStamp;
  memcpy(&_localIPAddr, &hello_remote.ipAddr, sizeof _localIPAddr);
  _virtualPid = hello_remote.virtualPid;
  JTRACE("Coordinator handshake RECEIVED!!!!!");
}

//tell the coordinator to run given user command
void dmtcp::CoordinatorAPI::sendUserCommand(char c, int* coordCmdStatus /*= NULL*/,
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
    *coordCmdStatus = CoordCmdStatus::NOERROR;
    return;
  }

  //receive REPLY
  reply.poison();
  _coordinatorSocket >> reply;
  reply.assertValid();
  JASSERT(reply.type == DMT_USER_CMD_RESULT);

  if (coordCmdStatus != NULL) {
    *coordCmdStatus =  reply.coordCmdStatus;
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

void dmtcp::CoordinatorAPI::updateCoordTimeStamp()
{
  DmtcpMessage msg(DMT_GET_COORD_TSTAMP);
  _coordinatorSocket << msg;

  DmtcpMessage reply;
  reply.poison();
  _coordinatorSocket >> reply;
  reply.assertValid();
  JASSERT(reply.type == DMT_COORD_TSTAMP) (reply.type);
  JASSERT(reply.coordTimeStamp != 0);

  _coordTimeStamp = reply.coordTimeStamp;
}

dmtcp::string dmtcp::CoordinatorAPI::getCoordCkptDir(void)
{
  // FIXME: Add a test for make-check.
  char buf[PATH_MAX];
  if (noCoordinator()) return "";
  DmtcpMessage msg(DMT_GET_CKPT_DIR);
  _coordinatorSocket << msg;

  msg.poison();
  _coordinatorSocket >> msg;
  msg.assertValid();
  JASSERT(msg.type == DMT_GET_CKPT_DIR_RESULT) (msg.type);

  JASSERT(msg.extraBytes > 0);
  _coordinatorSocket.readAll(buf, msg.extraBytes);
  return buf;
}

void dmtcp::CoordinatorAPI::updateCoordCkptDir(const char *dir)
{
  if (noCoordinator()) return;
  JASSERT(dir != NULL);
  DmtcpMessage msg(DMT_UPDATE_CKPT_DIR);
  msg.extraBytes = strlen(dir) + 1;
  _coordinatorSocket << msg;
  _coordinatorSocket.writeAll(dir, strlen(dir) + 1);
}

void dmtcp::CoordinatorAPI::startCoordinatorIfNeeded(CoordinatorAPI::CoordinatorMode mode,
                                                     int isRestart)
{
  const static int CS_OK = DMTCP_FAIL_RC+1;
  const static int CS_NO = DMTCP_FAIL_RC+2;
  int status = -1;

  if (mode & COORD_NONE) {
    setupVirtualCoordinator();
    return;
  }

  //fork a child process to probe the coordinator
  if (fork() == 0) {
    // If port '0' is given, assume no coordinator is running.
    char *portStr = getenv(ENV_VAR_NAME_PORT);
    if ((portStr == NULL && (mode & COORD_NEW)) ||
        (portStr != NULL && strcmp(portStr, "0") == 0))
      _real_exit(DMTCP_FAIL_RC);

    //fork so if we hit an error parent won't die
    _real_dup2(2, 1);                          //copy stderr to stdout
    _real_dup2(open("/dev/null", O_RDWR), 2);  //close stderr
    int numPeers;
    int isRunning;
    int coordCmdStatus;
    CoordinatorAPI coordinatorAPI;
    if (coordinatorAPI.tryConnectToCoordinator() == false) {
      _real_exit(DMTCP_FAIL_RC);
    }

    coordinatorAPI.sendUserCommand('s', &coordCmdStatus, &numPeers, &isRunning);
    coordinatorAPI._coordinatorSocket.close();

    if (numPeers == 0 || (isRunning ^ isRestart)) {
      if (numPeers != 0) {
        JTRACE("Coordinator present with existing computation.") (numPeers);
      }
      _real_exit(CS_OK);
    } else {
      _real_exit(CS_NO);
    }
  }
  errno = 0;
  // FIXME:  wait() could return -1 if a signal happened before child exits
  JASSERT(::wait(&status)>0)(JASSERT_ERRNO);
  JASSERT(WIFEXITED(status));

  int coordStatus = WEXITSTATUS(status);

  JASSERT(coordStatus != CS_NO)
    .Text("Existing computation not in running state, perhaps a checkpoint in "
          "progress.");

  if (mode & COORD_JOIN) {
    JASSERT(coordStatus != DMTCP_FAIL_RC)
      .Text("Coordinator not found, but --join was specified. Exiting.");
  } else if (mode & COORD_NEW) {
    JASSERT(coordStatus == DMTCP_FAIL_RC)
      .Text("Coordinator already running at given address. Exiting.");
    startNewCoordinator (mode);
  } else if (mode & COORD_ANY) {
    if (coordStatus == DMTCP_FAIL_RC) {
      startNewCoordinator(mode);
    }
  }
}

void dmtcp::CoordinatorAPI::startNewCoordinator(CoordinatorAPI::CoordinatorMode mode)
{
  int status = -1;
  //get location of coordinator
  const char *coordinatorAddr = getenv (ENV_VAR_NAME_HOST);
  if (coordinatorAddr == NULL) coordinatorAddr = DEFAULT_HOST;
  const char *coordinatorPortStr = getenv (ENV_VAR_NAME_PORT);
  int coordinatorPort;

  JASSERT(mode & COORD_NEW || mode & COORD_ANY);
  if (coordinatorPortStr != NULL) {
    coordinatorPort = jalib::StringToInt(coordinatorPortStr);
  } else {
    coordinatorPort = (mode & COORD_ANY) ? DEFAULT_PORT : 0;
  }

  dmtcp::string s = coordinatorAddr;
  if (s != "localhost" && s != "127.0.0.1" &&
     s != jalib::Filesystem::GetCurrentHostname()) {
    JASSERT(false)(s)(jalib::Filesystem::GetCurrentHostname())
      .Text("Won't automatically start coordinator because DMTCP_HOST"
            " is set to a remote host.");
    _real_exit(DMTCP_FAIL_RC);
  }

  // Create a socket and bind it to an unused port.
  jalib::JServerSocket coordinatorListenerSocket (jalib::JSockAddr::ANY,
                                                  coordinatorPort);
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

  JTRACE("Starting a new coordinator automatically.") (coordPort);

  if (fork()==0) {
    dmtcp::string coordinator = jalib::Filesystem::FindHelperUtility("dmtcp_coordinator");
    char *modeStr = (char *)"--daemon";
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

  JASSERT(wait(&status)>0)(JASSERT_ERRNO);

  JASSERT(WEXITSTATUS(status) == 0)
    .Text("Failed to start coordinator, port already in use. "
          "You may use a different port by running with \'-p 12345\'\n");
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

void dmtcp::CoordinatorAPI::getLocalIPAddr(struct in_addr *in)
{
  JASSERT(in != NULL);
  memcpy(in, &_localIPAddr, sizeof _localIPAddr);
}

// At restart, the HOST/PORT used by dmtcp_coordinator could be different then
// those at checkpoint time. This could cause the child processes created after
// restart to fail to connect to the coordinator.
void dmtcp::CoordinatorAPI::updateHostAndPortEnv()
{
  if (noCoordinator()) return;
  struct sockaddr_storage currAddr;
  socklen_t currAddrLen = sizeof currAddr;
  JASSERT(0 == getpeername(_coordinatorSocket.sockfd(),
                           (struct sockaddr*)&currAddr, &currAddrLen))
    (JASSERT_ERRNO);

  /* If the current coordinator is running on a HOST/PORT other than the
   * pre-checkpoint HOST/PORT, we need to update the environment variables
   * pointing to the coordinator HOST/PORT. This is needed if the new
   * coordinator has been moved around.
   */
  if (_coordAddrLen != currAddrLen ||
      memcmp(&_coordAddr, &currAddr, currAddrLen) != 0) {
    char ipstr[INET6_ADDRSTRLEN];
    int port;
    dmtcp::string portStr;

    memcpy(&_coordAddr, &currAddr, currAddrLen);
    _coordAddrLen = currAddrLen;

    // deal with both IPv4 and IPv6:
    if (currAddr.ss_family == AF_INET) {
      struct sockaddr_in *s = (struct sockaddr_in *)&currAddr;
      port = ntohs(s->sin_port);
      inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);
    } else { // AF_INET6
      JASSERT (currAddr.ss_family == AF_INET6);
      struct sockaddr_in6 *s = (struct sockaddr_in6 *)&currAddr;
      port = ntohs(s->sin6_port);
      inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof ipstr);

    }

    portStr = jalib::XToString(port);
    JASSERT(0 == setenv(ENV_VAR_NAME_HOST, ipstr, 1)) (JASSERT_ERRNO);
    JASSERT(0 == setenv(ENV_VAR_NAME_PORT, portStr.c_str(), 1)) (JASSERT_ERRNO);
  }
}

int dmtcp::CoordinatorAPI::sendKeyValPairToCoordinator(const char *id,
                                                       const void *key,
                                                       uint32_t key_len,
                                                       const void *val,
                                                       uint32_t val_len,
						       int sync)
{
  DmtcpMessage msg (DMT_REGISTER_NAME_SERVICE_DATA);
  if (sync) {
    msg.type = DMT_REGISTER_NAME_SERVICE_DATA_SYNC;
  }
  JWARNING(strlen(id) < sizeof(msg.nsid));
  strncpy(msg.nsid, id, 8);
  msg.keyLen = key_len;
  msg.valLen = val_len;
  msg.extraBytes = key_len + val_len;
  jalib::JSocket sock = _coordinatorSocket;
  if (dmtcp_is_running_state()) {
    sock = createNewConnectionToCoordinator(true);
    JASSERT(sock.isValid());
  }

  sock << msg;
  sock.writeAll((const char *)key, key_len);
  sock.writeAll((const char *)val, val_len);
  if (sync) {
    msg.poison();
    sock >> msg;
    JASSERT(msg.type == DMT_REGISTER_NAME_SERVICE_DATA_SYNC_RESPONSE);
  }
  if (dmtcp_is_running_state()) {
    sock.close();
  }
  return 1;
}

// On input, val points to a buffer in user memory and *val_len is the maximum
//   size of that buffer (the memory allocated by user).
// On output, we copy data to val, and set *val_len to the actual buffer size
//   (to the size of the data that we copied to the user buffer).
int dmtcp::CoordinatorAPI::sendQueryToCoordinator(const char *id,
                                                  const void *key,
                                                  uint32_t key_len,
                                                  void *val,
                                                  uint32_t *val_len)
{
  DmtcpMessage msg (DMT_NAME_SERVICE_QUERY);
  JWARNING(strlen(id) < sizeof(msg.nsid));
  strncpy(msg.nsid, id, 8);
  msg.keyLen = key_len;
  msg.valLen = 0;
  msg.extraBytes = key_len;
  jalib::JSocket sock = _coordinatorSocket;

  if (key == NULL || key_len == 0 || val == NULL || val_len == 0) {
    return 0;
  }

  if (dmtcp_is_running_state()) {
    sock = createNewConnectionToCoordinator(true);
    JASSERT(sock.isValid());
  }

  sock << msg;
  sock.writeAll((const char *)key, key_len);

  msg.poison();
  sock >> msg;
  msg.assertValid();
  JASSERT(msg.type == DMT_NAME_SERVICE_QUERY_RESPONSE &&
          msg.extraBytes == msg.valLen);

  JASSERT (*val_len >= msg.valLen);
  *val_len = msg.valLen;
  if (*val_len > 0) {
    sock.readAll((char*)val, *val_len);
  }

  if (dmtcp_is_running_state()) {
    sock.close();
  }

  return *val_len;
}
