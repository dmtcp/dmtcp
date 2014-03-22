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

// CAN REMOVE BOOL enableCheckpointing ARG OF DmtcpWorker WHEN WE'RE DONE.
// DmtcpWorker CAN INHERIT THIS CLASS, CoordinatorAPI

#include <netdb.h>
#include <arpa/inet.h>
#include "coordinatorapi.h"
#include "dmtcp.h"
#include "util.h"
#include "syscallwrappers.h"
#include "util.h"
#include "shareddata.h"
#include "processinfo.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jfilesystem.h"
#include <fcntl.h>

using namespace dmtcp;

void dmtcp_CoordinatorAPI_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  if (CoordinatorAPI::noCoordinator()) return;
  switch (event) {
    case DMTCP_EVENT_INIT:
      CoordinatorAPI::instance().init();
      break;

    case DMTCP_EVENT_THREADS_SUSPEND:
      JASSERT(CoordinatorAPI::instance().isValid());
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

static void getHostAndPort(CoordinatorAPI::CoordinatorMode mode,
                           dmtcp::string *hostname,
                           int *port)
{
  const char *addr = getenv (ENV_VAR_NAME_HOST);
  if (addr == NULL) addr = DEFAULT_HOST;
  *hostname = addr;

  const char *portStr = getenv (ENV_VAR_NAME_PORT);

  JASSERT(mode & CoordinatorAPI::COORD_NEW || mode & CoordinatorAPI::COORD_ANY);
  if (portStr != NULL) {
    *port = jalib::StringToInt(portStr);
  } else if (mode & CoordinatorAPI::COORD_NEW) {
    *port = 0;
  } else {
    *port = DEFAULT_PORT;
  }
}


static uint32_t getCkptInterval()
{
  uint32_t ret = DMTCPMESSAGE_SAME_CKPT_INTERVAL;
  const char* interval = getenv (ENV_VAR_CKPT_INTR);
  /* DmtcpMessage constructor default:
   *   hello_local.theCheckpointInterval: DMTCPMESSAGE_SAME_CKPT_INTERVAL
   */
  if (interval != NULL) {
    ret = jalib::StringToInt (interval);
  }
  // Tell the coordinator the ckpt interval only once.  It can change later.
  _dmtcp_unsetenv (ENV_VAR_CKPT_INTR);
  return ret;
}

static dmtcp::string getPrefixDir()
{
  dmtcp::string prefixDir = "";
  const char *prefixPathEnv = getenv(ENV_VAR_PREFIX_PATH);
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
  }
  return prefixDir;
}

static jalib::JSocket
createNewSocketToCoordinator( CoordinatorAPI::CoordinatorMode mode)
{
  dmtcp::string addr;
  int port;

  getHostAndPort(mode, &addr, &port);
  jalib::JSocket sock = jalib::JClientSocket(addr.c_str(), port);
  return sock;
}

//dmtcp::CoordinatorAPI::CoordinatorAPI (int sockfd)
  //: _coordinatorSocket(sockfd)
//{ }

static dmtcp::CoordinatorAPI *coordAPIInst = NULL;
dmtcp::CoordinatorAPI& dmtcp::CoordinatorAPI::instance()
{
  //static SysVIPC *inst = new SysVIPC(); return *inst;
  if (coordAPIInst == NULL) {
    coordAPIInst = new CoordinatorAPI();
    if (noCoordinator()) {
      coordAPIInst->_coordinatorSocket = jalib::JSocket(PROTECTED_COORD_FD);
    }
  }
  return *coordAPIInst;
}

void dmtcp::CoordinatorAPI::init()
{
  JTRACE("Informing coordinator of new process") (UniquePid::ThisProcess());

  DmtcpMessage msg (DMT_UPDATE_PROCESS_INFO_AFTER_EXEC);
  string progname = jalib::Filesystem::GetProgramName();
  msg.extraBytes = progname.length() + 1;

  JASSERT(Util::isValidFd(PROTECTED_COORD_FD));
  instance()._coordinatorSocket = jalib::JSocket(PROTECTED_COORD_FD);
  instance()._coordinatorSocket << msg;
  instance()._coordinatorSocket.writeAll(progname.c_str(),
                                         progname.length() + 1);
  // The coordinator won't send any msg in response to DMT_UPDATE... so no need
  // to call recvCoordinatorHandshake().
}

void dmtcp::CoordinatorAPI::resetOnFork(dmtcp::CoordinatorAPI& coordAPI)
{
  JASSERT(coordAPI._coordinatorSocket.isValid());
  JASSERT(coordAPI._coordinatorSocket.sockfd() != PROTECTED_COORD_FD);
  instance() = coordAPI;
  instance()._coordinatorSocket.changeFd(PROTECTED_COORD_FD);

  JTRACE("Informing coordinator of new process") (UniquePid::ThisProcess());

  DmtcpMessage msg (DMT_UPDATE_PROCESS_INFO_AFTER_FORK);
  if (dmtcp_virtual_to_real_pid) {
    msg.realPid = dmtcp_virtual_to_real_pid(getpid());
  } else {
    msg.realPid = getpid();
  }
  instance()._coordinatorSocket << msg;
  // The coordinator won't send any msg in response to DMT_UPDATE... so no need
  // to call recvCoordinatorHandshake().
  instance()._nsSock.close();
}

void dmtcp::CoordinatorAPI::setupVirtualCoordinator(CoordinatorInfo *coordInfo,
                                                    struct in_addr  *localIP)
{
  const char *portStr = getenv(ENV_VAR_NAME_PORT);
  int port = (portStr == NULL) ? DEFAULT_PORT : jalib::StringToInt(portStr);
  _coordinatorSocket = jalib::JServerSocket(jalib::JSockAddr::ANY, port);
  JASSERT(_coordinatorSocket.isValid()) (port) (JASSERT_ERRNO)
    .Text("Failed to create listen socket.");
  _coordinatorSocket.changeFd(PROTECTED_COORD_FD);
  string coordPort= jalib::XToString(_coordinatorSocket.port());
  setenv (ENV_VAR_NAME_PORT, coordPort.c_str(), 1);

  dmtcp::Util::setVirtualPidEnvVar(INITIAL_VIRTUAL_PID, getppid());

  UniquePid coordId = UniquePid(INITIAL_VIRTUAL_PID,
                                UniquePid::ThisProcess().hostid(),
                                UniquePid::ThisProcess().time());

  coordInfo->id = coordId.upid();
  coordInfo->timeStamp = coordId.time();
  coordInfo->addrLen = 0;
  if (getenv(ENV_VAR_CKPT_INTR) != NULL) {
    coordInfo->interval = (uint32_t) strtol(getenv(ENV_VAR_CKPT_INTR), NULL, 0);
  } else {
    coordInfo->interval = 0;
  }
  memset(&coordInfo->addr, 0, sizeof(coordInfo->addr));
  memset(localIP, 0, sizeof(*localIP));
}

void dmtcp::CoordinatorAPI::waitForCheckpointCommand()
{
  uint32_t ckptInterval = SharedData::getCkptInterval();
  struct timeval tmptime={0,0};
  long remaining = ckptInterval;
  do {
    fd_set rfds;
    struct timeval *timeout = NULL;
    struct timeval start;
    if (ckptInterval > 0) {
      timeout = &tmptime;
      timeout->tv_sec = remaining;
      JASSERT(gettimeofday(&start, NULL) == 0) (JASSERT_ERRNO);
    }
    FD_ZERO(&rfds);
    FD_SET(PROTECTED_COORD_FD, &rfds );
    int retval = select(PROTECTED_COORD_FD+1, &rfds, NULL, NULL, timeout);
    if (retval == 0) { // timeout expired, time for checkpoint
      JTRACE("Timeout expired, checkpointing now.");
      return;
    } else if (retval > 0) {
      JASSERT(FD_ISSET(PROTECTED_COORD_FD, &rfds));
      JTRACE("Connect request on virtual coordinator socket.");
      break;
    }
    JASSERT(errno == EINTR) (JASSERT_ERRNO);
    if (ckptInterval > 0) {
      struct timeval end;
      JASSERT(gettimeofday(&end, NULL) == 0) (JASSERT_ERRNO);
      remaining -= end.tv_sec - start.tv_sec;
      // If the remaining time is negative, we can checkpoint now
      if (remaining < 0) {
        return;
      }
    }
  } while (remaining > 0);

  jalib::JSocket cmdSock(-1);
  dmtcp::DmtcpMessage msg;
  dmtcp::DmtcpMessage reply(DMT_USER_CMD_RESULT);
  do {
    cmdSock.close();
    cmdSock = _coordinatorSocket.accept();
    msg.poison();
    JTRACE("Reading from incoming connection...");
    cmdSock >> msg;
  } while (!cmdSock.isValid());

  JASSERT(msg.type == DMT_USER_CMD) (msg.type)
    .Text("Unexpected connection.");

  reply.coordCmdStatus = CoordCmdStatus::NOERROR;

  bool exitWhenDone = false;
  switch (msg.coordCmd) {
//    case 'b': case 'B':  // prefix blocking command, prior to checkpoint command
//      JTRACE("blocking checkpoint beginning...");
//      blockUntilDone = true;
//      break;
    case 's': case 'S':
      JTRACE("Received status command");
      reply.numPeers = 1;
      reply.isRunning = 1;
      break;
    case 'c': case 'C':
      JTRACE("checkpointing...");
      break;
    case 'k': case 'K':
    case 'q': case 'Q':
      JTRACE("Received KILL command from user, exiting");
      exitWhenDone = true;
      break;
    default:
      JTRACE("unhandled user command") (msg.coordCmd);
      reply.coordCmdStatus = CoordCmdStatus::ERROR_INVALID_COMMAND;
  }
  cmdSock << reply;
  cmdSock.close();
  if (exitWhenDone) {
    _exit(0);
  }
  return;
}

bool dmtcp::CoordinatorAPI::noCoordinator()
{
  static int virtualCoordinator = -1;
  if (virtualCoordinator == -1) {
    int optVal = -1;
    socklen_t optLen = sizeof(optVal);
    int ret = _real_getsockopt(PROTECTED_COORD_FD, SOL_SOCKET,
                               SO_ACCEPTCONN, &optVal, &optLen);
    if (ret == 0 && optVal == 1) {
      virtualCoordinator = 1;
    } else {
      virtualCoordinator = 0;
    }
  }
  return virtualCoordinator;
}

void dmtcp::CoordinatorAPI::connectAndSendUserCommand(char c,
                                                      int *coordCmdStatus,
                                                      int *numPeers,
                                                      int *isRunning)
{
  _coordinatorSocket = createNewSocketToCoordinator(COORD_ANY);
  if (!_coordinatorSocket.isValid()) {
    *coordCmdStatus = CoordCmdStatus::ERROR_COORDINATOR_NOT_FOUND;
    return;
  }

  //tell the coordinator to run given user command
  DmtcpMessage msg, reply;

  //send
  msg.type = DMT_USER_CMD;
  msg.coordCmd = c;

  if (c == 'i') {
    const char* interval = getenv (ENV_VAR_CKPT_INTR);
    if (interval != NULL){
      msg.theCheckpointInterval = jalib::StringToInt (interval);
    }
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

  _coordinatorSocket.close();
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

void dmtcp::CoordinatorAPI::sendMsgToCoordinator(const dmtcp::DmtcpMessage &msg,
                                                 const void *extraData,
                                                 size_t len)
{
  if (noCoordinator()) return;
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
  JASSERT(!noCoordinator());
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

void dmtcp::CoordinatorAPI::startNewCoordinator(CoordinatorAPI::CoordinatorMode
                                                mode)
{
  dmtcp::string addr;
  int port;
  getHostAndPort(mode, &addr, &port);

  JASSERT(addr == "localhost" ||
          addr == "127.0.0.1" ||
          addr == jalib::Filesystem::GetCurrentHostname())
    (addr) (jalib::Filesystem::GetCurrentHostname())
    .Text("Won't automatically start coordinator because DMTCP_HOST"
          " is set to a remote host.");
  // Create a socket and bind it to an unused port.
  errno = 0;
  jalib::JServerSocket coordinatorListenerSocket(jalib::JSockAddr::ANY,
                                                 port, 128);
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

  if (fork() == 0) {
    dmtcp::string coordinator = jalib::Filesystem::FindHelperUtility("dmtcp_coordinator");
    char *modeStr = (char *)"--daemon";
    char * args[] = {
      (char*)coordinator.c_str(),
      (char*)"--quiet",
      (char*)"--exit-on-last",
      modeStr,
      NULL
    };
    execv(args[0], args);
    JASSERT(false)(coordinator)(JASSERT_ERRNO) .Text("exec(dmtcp_coordinator) failed");
  } else {
    _real_close (PROTECTED_COORD_FD);
  }

  int status;
  JASSERT(wait(&status) > 0) (JASSERT_ERRNO);
}

void CoordinatorAPI::createNewConnToCoord(CoordinatorAPI::CoordinatorMode mode)
{
  if (mode & COORD_JOIN) {
    _coordinatorSocket = createNewSocketToCoordinator(mode);
    JASSERT(_coordinatorSocket.isValid()) (JASSERT_ERRNO)
      .Text("Coordinator not found, but --join was specified. Exiting.");
  } else if (mode & COORD_NEW) {
    startNewCoordinator(mode);
    _coordinatorSocket = createNewSocketToCoordinator(mode);
    JASSERT(_coordinatorSocket.isValid()) (JASSERT_ERRNO)
      .Text("Error connecting to newly started coordinator.");
  } else if (mode & COORD_ANY) {
    _coordinatorSocket = createNewSocketToCoordinator(mode);
    if (!_coordinatorSocket.isValid()) {
      JTRACE("Coordinator not found, trying to start a new one.");
      startNewCoordinator(mode);
      _coordinatorSocket = createNewSocketToCoordinator(mode);
      JASSERT(_coordinatorSocket.isValid()) (JASSERT_ERRNO)
        .Text("Error connecting to newly started coordinator.");
    }
  } else {
    JASSERT(false) .Text("Not Reached");
  }
  _coordinatorSocket.changeFd(PROTECTED_COORD_FD);
}

DmtcpMessage dmtcp::CoordinatorAPI::sendRecvHandshake(DmtcpMessage msg,
                                                      string progname,
                                                      UniquePid *compId)
{
  if (dmtcp_virtual_to_real_pid) {
    msg.realPid = dmtcp_virtual_to_real_pid(getpid());
  } else {
    msg.realPid = getpid();
  }

  msg.theCheckpointInterval = getCkptInterval();
  dmtcp::string hostname = jalib::Filesystem::GetCurrentHostname();
  dmtcp::string prefixDir = getPrefixDir();
  msg.extraBytes = hostname.length() + 1 + progname.length() + 1;
  if (!prefixDir.empty()) {
    msg.extraBytes += prefixDir.length() + 1;
  }

  _coordinatorSocket << msg;
  _coordinatorSocket.writeAll(hostname.c_str(), hostname.length() + 1);
  _coordinatorSocket.writeAll(progname.c_str(), progname.length() + 1);
  if (!prefixDir.empty()) {
    _coordinatorSocket.writeAll(prefixDir.c_str(), prefixDir.length() + 1);
    msg.extraBytes += prefixDir.length() + 1;
  }

  msg.poison();
  _coordinatorSocket >> msg;
  msg.assertValid();
  if (msg.type == DMT_KILL_PEER) {
    JTRACE("Received KILL message from coordinator, exiting");
    _exit (0);
  }
  if (msg.type == DMT_REJECT_NOT_RUNNING) {
    JASSERT(false)
      .Text("Connection rejected by the coordinator.\n"
            "Reason: Current computation not in RUNNING state.\n"
            "         Is a checkpoint/restart in progress?");
  } else if (msg.type == DMT_REJECT_WRONG_COMP) {
    JASSERT(compId != NULL);
    JASSERT(false) (*compId)
      .Text("Connection rejected by the coordinator.\n"
            " Reason: This process has a different computation group.");
  } else if (msg.type == DMT_REJECT_WRONG_PREFIX) {
    JASSERT(false) (prefixDir)
      .Text("Connection rejected by the coordinator.\n"
            "Reason: Prefix supplied with --prefix doesn't match the prefix\n"
            "        of other processes in the computation.\n"
            "        (DMTCP installed at different paths?)");
  }
  JASSERT(msg.type == DMT_ACCEPT);
  return msg;
}

void dmtcp::CoordinatorAPI::connectToCoordOnStartup(CoordinatorMode  mode,
                                                    string           progname,
                                                    DmtcpUniqueProcessId *compId,
                                                    CoordinatorInfo *coordInfo,
                                                    struct in_addr  *localIP)
{
  JASSERT(compId != NULL && localIP != NULL && coordInfo != NULL);

  if (mode & COORD_NONE) {
    setupVirtualCoordinator(coordInfo, localIP);
    *compId = coordInfo->id;
    return;
  }

  createNewConnToCoord(mode);
  JTRACE("sending coordinator handshake")(UniquePid::ThisProcess());
  DmtcpMessage hello_local(DMT_NEW_WORKER);
  hello_local.virtualPid = -1;

  DmtcpMessage hello_remote = sendRecvHandshake(hello_local, progname);

  JASSERT(hello_remote.virtualPid != -1);
  JTRACE("Got virtual pid from coordinator") (hello_remote.virtualPid);
  dmtcp::Util::setVirtualPidEnvVar(hello_remote.virtualPid, getppid());

  JASSERT(compId != NULL && localIP != NULL && coordInfo != NULL);
  *compId = hello_remote.compGroup.upid();
  coordInfo->id = hello_remote.from.upid();
  coordInfo->timeStamp = hello_remote.coordTimeStamp;
  coordInfo->addrLen = sizeof (coordInfo->addr);
  JASSERT(getpeername(_coordinatorSocket.sockfd(),
                      (struct sockaddr*) &coordInfo->addr,
                      &coordInfo->addrLen) == 0)
    (JASSERT_ERRNO);
  memcpy(localIP, &hello_remote.ipAddr, sizeof hello_remote.ipAddr);
}

void dmtcp::CoordinatorAPI::createNewConnectionBeforeFork(string& progname)
{
  JASSERT(!noCoordinator());
  struct sockaddr_storage addr;
  uint32_t len;
  SharedData::getCoordAddr((struct sockaddr *)&addr, &len);
  socklen_t addrlen = len;
  _coordinatorSocket = jalib::JClientSocket((struct sockaddr *)&addr, addrlen);
  JASSERT(_coordinatorSocket.isValid());

  DmtcpMessage hello_local(DMT_NEW_WORKER);
  DmtcpMessage hello_remote = sendRecvHandshake(hello_local, progname);
  JASSERT(hello_remote.virtualPid != -1);

  if (dmtcp_virtual_to_real_pid) {
    JTRACE("Got virtual pid from coordinator") (hello_remote.virtualPid);
    dmtcp::Util::setVirtualPidEnvVar(hello_remote.virtualPid, getpid());
  }
}

void dmtcp::CoordinatorAPI::connectToCoordOnRestart(CoordinatorMode  mode,
                                                    dmtcp::string progname,
                                                    UniquePid compGroup,
                                                    int np,
                                                    CoordinatorInfo *coordInfo,
                                                    struct in_addr  *localIP)
{
  if (mode & COORD_NONE) {
    setupVirtualCoordinator(coordInfo, localIP);
    return;
  }

  createNewConnToCoord(mode);
  JTRACE("sending coordinator handshake")(UniquePid::ThisProcess());
  DmtcpMessage hello_local(DMT_RESTART_WORKER);
  hello_local.virtualPid = -1;
  hello_local.numPeers = np;
  hello_local.compGroup = compGroup;

  DmtcpMessage hello_remote = sendRecvHandshake(hello_local, progname,
                                                &compGroup);

  if (coordInfo != NULL) {
    coordInfo->id = hello_remote.from.upid();
    coordInfo->timeStamp = hello_remote.coordTimeStamp;
    coordInfo->addrLen = sizeof (coordInfo->addr);
    JASSERT(getpeername(_coordinatorSocket.sockfd(),
                        (struct sockaddr*) &coordInfo->addr,
                        &coordInfo->addrLen) == 0)
      (JASSERT_ERRNO);
  }
  if (localIP != NULL) {
    memcpy(localIP, &hello_remote.ipAddr, sizeof hello_remote.ipAddr);
  }

  JTRACE("Coordinator handshake RECEIVED!!!!!");
}

void dmtcp::CoordinatorAPI::sendCkptFilename()
{
  if (noCoordinator()) return;
  // Tell coordinator to record our filename in the restart script
  dmtcp::string ckptFilename = dmtcp::ProcessInfo::instance().getCkptFilename();
  dmtcp::string hostname = jalib::Filesystem::GetCurrentHostname();
  JTRACE("recording filenames") (ckptFilename) (hostname);
  dmtcp::DmtcpMessage msg;
  if (dmtcp_unique_ckpt_enabled && dmtcp_unique_ckpt_enabled()) {
    msg.type = DMT_UNIQUE_CKPT_FILENAME;
  } else {
    msg.type = DMT_CKPT_FILENAME;
  }
  msg.extraBytes = ckptFilename.length() + 1 + hostname.length() + 1;
  _coordinatorSocket << msg;
  _coordinatorSocket.writeAll(ckptFilename.c_str(), ckptFilename.length() + 1);
  _coordinatorSocket.writeAll(hostname.c_str(), hostname.length() + 1);
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
    if (_nsSock.sockfd() == -1) {
      _nsSock = createNewSocketToCoordinator(COORD_ANY);
      JASSERT(_nsSock.isValid());
      _nsSock.changeFd(PROTECTED_NS_FD);
      DmtcpMessage m(DMT_NAME_SERVICE_WORKER);
      _nsSock << m;
    }
    sock = _nsSock;
    JASSERT(sock.isValid());
  }

  sock << msg;
  sock.writeAll((const char *)key, key_len);
  sock.writeAll((const char *)val, val_len);
  if (sync) {
    msg.poison();
    sock >> msg;
    JASSERT(msg.type == DMT_REGISTER_NAME_SERVICE_DATA_SYNC_RESPONSE)(msg.type);

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
    if (!_nsSock.isValid()) {
      _nsSock = createNewSocketToCoordinator(COORD_ANY);
      JASSERT(_nsSock.isValid());
      _nsSock.changeFd(PROTECTED_NS_FD);
      DmtcpMessage m(DMT_NAME_SERVICE_WORKER);
      _nsSock << m;
    }
    sock = _nsSock;
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

  return *val_len;
}
