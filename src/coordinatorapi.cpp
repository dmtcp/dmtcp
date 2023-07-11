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

#include "coordinatorapi.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <semaphore.h>  // for sem_post(&sem_launch)
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include "../jalib/jconvert.h"
#include "../jalib/jfilesystem.h"
#include "../jalib/jsocket.h"
#include "kvdb.h"
#include "dmtcp.h"
#include "processinfo.h"
#include "shareddata.h"
#include "syscallwrappers.h"
#include "threadinfo.h"
#include "util.h"

// sem_launch is used in threadlist.cpp
// sem_launch_first_time will be set just before pthread_create(checkpointhread)
LIB_PRIVATE bool sem_launch_first_time = false;
LIB_PRIVATE sem_t sem_launch;

namespace dmtcp {
namespace CoordinatorAPI {

const int coordinatorSocket = PROTECTED_COORD_FD;
int nsSock = -1;
static int childCoordinatorSocket = -1;

// Shared between getCoordHostAndPort() and setCoordPort()
static int _cachedPort = 0;
static string *_cachedHost = nullptr;


void init();
void restart();
void setCoordPort(int port);
void closeConnection();
int createNewSocketToCoordinator(CoordinatorMode mode);

DmtcpMessage sendRecvHandshake(int fd,
                               DmtcpMessage msg,
                               string progname,
                               UniquePid *compId = NULL);

void sendMsgToCoordinatorRaw(int fd,
                             DmtcpMessage msg,
                             const void *extraData = NULL,
                             size_t len = 0);

void recvMsgFromCoordinatorRaw(int fd,
                               DmtcpMessage *msg,
                               void **extraData = NULL);

void startNewCoordinator(CoordinatorMode mode);
void createNewConnToCoord(CoordinatorMode mode);

void
eventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_INIT:
      init();
      break;

    case DMTCP_EVENT_ATFORK_PREPARE:
    case DMTCP_EVENT_VFORK_PREPARE:
      CoordinatorAPI::atForkPrepare();
      break;

    case DMTCP_EVENT_ATFORK_PARENT:
    case DMTCP_EVENT_ATFORK_FAILED:
    case DMTCP_EVENT_VFORK_PARENT:
    case DMTCP_EVENT_VFORK_FAILED:
      CoordinatorAPI::atForkParent();
      break;

    case DMTCP_EVENT_ATFORK_CHILD:
      CoordinatorAPI::atForkChild();
      break;

    case DMTCP_EVENT_VFORK_CHILD:
      CoordinatorAPI::vforkChild();
      break;
    case DMTCP_EVENT_RESTART:
      restart();
      break;

  default:
    break;
  }
}

static DmtcpPluginDescriptor_t coordinatorAPIPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "coordinatorapi",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Coordinator API plugin",
  eventHook
};

DmtcpPluginDescriptor_t
pluginDescr()
{
  return coordinatorAPIPlugin;
}

void
restart()
{
  _real_close(nsSock);
  nsSock = -1;
}

void
getCoordHostAndPort(CoordinatorMode mode, string *host, int *port)
{
  if (SharedData::initialized()) {
    *host = SharedData::coordHost();
    *port = SharedData::coordPort();
    return;
  }

  if (_cachedHost == nullptr) {
    // Set host to cmd line (if --cord-host) or env var or DEFAULT_HOST
    if (*host == "") {
      if (getenv(ENV_VAR_NAME_HOST)) {
        *host = getenv(ENV_VAR_NAME_HOST);
        _cachedHost = new string(getenv(ENV_VAR_NAME_HOST));
      } else if (getenv("DMTCP_HOST")) { // deprecated
        *host = getenv("DMTCP_HOST");
        _cachedHost = new string(getenv("DMTCP_HOST"));
      } else {
        *host = DEFAULT_HOST;
        _cachedHost = new string(DEFAULT_HOST);
      }
    } else {
      // The caller's string object needs to be valid across
      // multiple calls to this function, or else, the _cachedHost
      // pointer will become a dangling pointer.
      _cachedHost = new string(*host);
    }

    // Set port to cmd line (if --coord-port) or env var
    // or 0 (if --new-coordinator from cmd line) or DEFAULT_PORT
    if (*port == UNINITIALIZED_PORT) {
      if (getenv(ENV_VAR_NAME_PORT)) {
        *port = jalib::StringToInt(getenv(ENV_VAR_NAME_PORT));
      } else if (getenv("DMTCP_PORT")) { // deprecated
        *port = jalib::StringToInt(getenv("DMTCP_PORT"));
      } else if (mode & COORD_NEW) {
        *port = 0;
      } else {
        *port = DEFAULT_PORT;
      }
    }

    _cachedPort = *port;
  } else {
    // We might have gotten a user-requested port of 0 (random port) before,
    // and now the user is passing in the actual coordinator port.
    if (*port > 0 && _cachedPort == 0) {
      _cachedPort = *port;
    }
    *host = *_cachedHost;
    *port = _cachedPort;
  }
}

void
setCoordPort(int port)
{
  _cachedPort = port;
}

static uint32_t
getCkptInterval()
{
  uint32_t ret = DMTCPMESSAGE_SAME_CKPT_INTERVAL;
  const char *interval = getenv(ENV_VAR_CKPT_INTR);

  /* DmtcpMessage constructor default:
   *   hello_local.theCheckpointInterval: DMTCPMESSAGE_SAME_CKPT_INTERVAL
   */
  if (interval != NULL) {
    ret = jalib::StringToInt(interval);
  }

  // Tell the coordinator the ckpt interval only once.  It can change later.
  _dmtcp_unsetenv(ENV_VAR_CKPT_INTR);
  return ret;
}

int
createNewSocketToCoordinator(CoordinatorMode mode)
{
  string host = "";
  int port = UNINITIALIZED_PORT;

  getCoordHostAndPort(COORD_ANY, &host, &port);
  return jalib::JClientSocket(host.c_str(), port).sockfd();
}

void init()
{
  JTRACE("Informing coordinator of new process") (UniquePid::ThisProcess());

  DmtcpMessage msg (DMT_UPDATE_PROCESS_INFO_AFTER_INIT_OR_EXEC);
  sendMsgToCoordinator(msg, jalib::Filesystem::GetProgramName());
}

void resetCoordinatorSocket(int sock)
{
  JASSERT(Util::isValidFd(sock));
  JASSERT(sock != PROTECTED_COORD_FD);
  Util::changeFd(sock, PROTECTED_COORD_FD);
  JASSERT(Util::isValidFd(coordinatorSocket));

  JTRACE("Informing coordinator of new process") (UniquePid::ThisProcess());

  DmtcpMessage msg(DMT_UPDATE_PROCESS_INFO_AFTER_FORK);
  if (dmtcp_virtual_to_real_pid) {
    msg.realPid = dmtcp_virtual_to_real_pid(getpid());
  } else {
    msg.realPid = getpid();
  }
  sendMsgToCoordinator(msg);
}

void atForkPrepare()
{
  string child_name = jalib::Filesystem::GetProgramName() + "_(forked)";

  childCoordinatorSocket =
    CoordinatorAPI::createNewConnectionBeforeFork(child_name);
}

void atForkParent()
{
  _real_close(childCoordinatorSocket);
}

void atForkChild()
{
  resetCoordinatorSocket(childCoordinatorSocket);

  _real_close(nsSock);
  nsSock = -1;
}

void vforkChild()
{
  resetCoordinatorSocket(childCoordinatorSocket);
  JASSERT(nsSock == -1) .Text("Not Implemented");
}

void
closeConnection()
{
  _real_close(coordinatorSocket);
}

char*
connectAndSendUserCommand(char c,
                          int *coordCmdStatus,
                          int *numPeers,
                          int *isRunning,
                          int *ckptInterval)
{
  char *replyData = NULL;
  int coordFd = createNewSocketToCoordinator(COORD_ANY);
  if (coordFd == -1) {
    *coordCmdStatus = CoordCmdStatus::ERROR_COORDINATOR_NOT_FOUND;
    return replyData;
  }

  // Tell the coordinator to run given user command
  DmtcpMessage msg(DMT_USER_CMD);
  msg.coordCmd = c;

  if (c == 'i') {
    const char *interval = getenv(ENV_VAR_CKPT_INTR);
    if (interval != NULL) {
      msg.theCheckpointInterval = jalib::StringToInt(interval);
    }
  }
  JASSERT(Util::writeAll(coordFd, &msg, sizeof(msg)) == sizeof(msg));

  // The coordinator will violently close our socket...
  if (c == 'q' || c == 'Q') {
    *coordCmdStatus = CoordCmdStatus::NOERROR;
    return replyData;
  }

  // Receive REPLY
  DmtcpMessage reply;
  reply.poison();
  recvMsgFromCoordinatorRaw(coordFd, &reply, (void**)&replyData);
  reply.assertValid();
  JASSERT(reply.type == DMT_USER_CMD_RESULT);

  if (coordCmdStatus != NULL) {
    *coordCmdStatus = reply.coordCmdStatus;
  }
  if (numPeers != NULL) {
    *numPeers = reply.numPeers;
  }
  if (isRunning != NULL) {
    *isRunning = reply.isRunning;
  }
  if (ckptInterval != NULL) {
    *ckptInterval = reply.theCheckpointInterval;
  }

  _real_close(coordFd);

  return replyData;
}

void
sendMsgToCoordinatorRaw(int fd,
                        DmtcpMessage msg,
                        const void *extraData,
                        size_t len)
{
  if (extraData != NULL) {
    msg.extraBytes = len;
  }
  JASSERT(Util::writeAll(fd, &msg, sizeof(msg)) == sizeof(msg));
  if (extraData != NULL) {
    JASSERT(Util::writeAll(fd, extraData, len) == (ssize_t)len);
  }
}

void
recvMsgFromCoordinatorRaw(int fd, DmtcpMessage *msg, void **extraData)
{
  msg->poison();
  if (sem_launch_first_time) {
    // Release user thread now that we've initialized the checkpoint thread.
    // This code is reached if the --no-coordinator flag is not used.
    // FIXME:  Technically, some rare type of software could still execute
    // between here and when we readall() from coord, thus creating a race.
    sem_post(&sem_launch);
    sem_launch_first_time = false;
  }

  // Read into a temporary buffer in case the process exits after reading the
  // message but before receiving the extradata.
  DmtcpMessage tmpMsg;
  if (Util::readAll(fd, &tmpMsg, sizeof(tmpMsg)) != sizeof(tmpMsg)) {
    // Perhaps the process is exit()'ing.
    return;
  }

  if (tmpMsg.extraBytes > 0) {
    JASSERT(extraData != NULL);

    // Caller must free this buffer
    void *buf = JALLOC_HELPER_MALLOC(tmpMsg.extraBytes);
    if (Util::readAll(fd, buf, tmpMsg.extraBytes) !=
        (ssize_t)tmpMsg.extraBytes) {
      JALLOC_HELPER_FREE(buf);
      return;
    }

    *extraData = buf;
  }

  // All is well, return the received message.
  *msg = tmpMsg;

  // TODO(Kapil): Distinguish between DMT_KILL_PEER that arrives during
  // checkpoint-phase (potentially due to a stuck computation that the user
  // wants to kill) vs. normal runtime.
  // TODO(Kapil): Consider generating an EXIT event for plugins.
  if (msg->isValid() && msg->type == DMT_KILL_PEER) {
    JTRACE("Received KILL message from coordinator, exiting");
    _exit(0);
  }
}

void sendMsgToCoordinator(DmtcpMessage msg, const void *extraData, size_t len)
{
  sendMsgToCoordinatorRaw(coordinatorSocket, msg, extraData, len);
}

void sendMsgToCoordinator(const DmtcpMessage &msg, const string &data)
{
  sendMsgToCoordinatorRaw(coordinatorSocket, msg,
                          data.c_str(), data.length() + 1);
}

void recvMsgFromCoordinator(DmtcpMessage *msg, void **extraData)
{
  msg->poison();
  recvMsgFromCoordinatorRaw(coordinatorSocket, msg, extraData);
}

bool waitForBarrier(const string& barrier,
                    uint32_t *numPeers)
{
  DmtcpMessage barrierMsg(DMT_BARRIER);

  JASSERT(barrier.length() < sizeof(barrierMsg.barrier)) (barrier);
  strcpy(barrierMsg.barrier, barrier.c_str());

  sendMsgToCoordinator(barrierMsg);

  JTRACE("waiting for DMT_BARRIER_RELEASED message") (barrier);

  char *extraData = NULL;
  DmtcpMessage msg;
  recvMsgFromCoordinator(&msg, (void**)&extraData);

  // Before validating message; make sure we are not exiting.
  if (!msg.isValid()) {
    return false;
  }

  // Coordinator sends a duplicate DMTCP_DO_CHECKPOINT msg if we reconnected
  // after exec. It's safe to ignore. We'll wait again for the Barrier msg.
  if (msg.type == DMT_DO_CHECKPOINT) {
    recvMsgFromCoordinator(&msg, (void**)&extraData);

    // Before validating message; make sure we are not exiting.
    if (!msg.isValid()) {
      return false;
    }
  }

  JASSERT(msg.type == DMT_BARRIER_RELEASED) (msg.type);
  JASSERT(extraData != NULL);
  JASSERT(barrier == extraData) (barrier) (extraData);

  JALLOC_FREE(extraData);

  if (numPeers != NULL) {
    *numPeers = msg.numPeers;
  }

  return true;
}

void
startNewCoordinator(CoordinatorMode mode)
{
  string host = "";
  int port = UNINITIALIZED_PORT;
  getCoordHostAndPort(mode, &host, &port);

  JASSERT(strcmp(host.c_str(), "localhost") == 0 ||
          strcmp(host.c_str(), "127.0.0.1") == 0 ||
          jalib::Filesystem::GetCurrentHostname() == host.c_str())
    (host) (jalib::Filesystem::GetCurrentHostname())
  .Text("Won't automatically start coordinator because DMTCP_HOST"
        " is set to a remote host.");

  // Create a socket and bind it to an unused port.
  errno = 0;
  jalib::JServerSocket coordinatorListenerSocket(jalib::JSockAddr::ANY,
                                                 port, 128);
  JASSERT(coordinatorListenerSocket.isValid())
    (coordinatorListenerSocket.port()) (JASSERT_ERRNO) (host) (port)
    .Text("Failed to create socket to connect to coordinator port."
          "\n  If the above message (sterror) is:"
          "\n            \"Address already in use\" or \"Bad file descriptor\","
          "\n    then this may be an old coordinator."
          "\n    Or maybe you're joining an existing coordinator, and forgot"
          "\n      to use 'dmtcp_launch --join-coordinator'."
          "\n  Either:"
          "\n    (a) use '--join-coordinator; or"
          "\n    (b) kill the old coordinator with 'pkill -9 dmtcp_coord' or"
          "\n        (while using same host and port):"
          "\n        dmtcp_command ---coord-host XX --coord-port YY --quit; or"
          "\n    (c) if the old coordinator is already gone, wait a few seconds"
          "\n        or a minute for the O/S to free up that port again.\n");
  // Now dup the sockfd to
  coordinatorListenerSocket.changeFd(PROTECTED_COORD_FD);
  setCoordPort(coordinatorListenerSocket.port());

  JTRACE("Starting a new coordinator automatically.")
    (coordinatorListenerSocket.port());

  if (fork() == 0) {
    /* NOTE:  This code assumes that dmtcp_launch (the current program)
     *  and dmtcp_coordinator are in the same directory.  Namely,
     *  GetProgramDir() gets the dir of the current program (dmtcp_launch).
     *  Hence, if dmtcp_coordinator is in a different directory, then
     *     jalib::Filesystem::GetProgramDir() + "/dmtcp_coordinator"
     *  will not exist, and the child will fail.
     */
    // We can't use Util::getPath() here since the SharedData has not been
    // initialized yet.
    string coordinator =
      jalib::Filesystem::GetProgramDir() + "/dmtcp_coordinator";

    char *modeStr = (char *)"--daemon";
    char *args[] = {
      (char *)coordinator.c_str(),
      (char *)"--quiet",

      /* If we wish to also suppress coordinator warnings, call --quiet twice */
      (char *)"--exit-on-last",
      modeStr,
      NULL
    };
    execv(args[0], args);
    JASSERT(false)(coordinator)(JASSERT_ERRNO).Text(
      "exec(dmtcp_coordinator) failed");
  } else {
    int status;
    _real_close(PROTECTED_COORD_FD);
    JASSERT(wait(&status) > 0) (JASSERT_ERRNO);
  }
}

void
createNewConnToCoord(CoordinatorMode mode)
{
  int sockfd = -1;
  if (mode & COORD_JOIN) {
    sockfd = createNewSocketToCoordinator(mode);
    JASSERT(sockfd != -1) (JASSERT_ERRNO)
      .Text("Coordinator not found, but --join was specified. Exiting.");
  } else if (mode & COORD_NEW) {
    startNewCoordinator(mode);
    sockfd = createNewSocketToCoordinator(mode);
    JASSERT(sockfd != -1) (JASSERT_ERRNO)
      .Text("Error connecting to newly started coordinator.");
  } else if (mode & COORD_ANY) {
    sockfd = createNewSocketToCoordinator(mode);
    if (sockfd == -1) {
      JTRACE("Coordinator not found, trying to start a new one.");
      startNewCoordinator(mode);
      sockfd = createNewSocketToCoordinator(mode);
      JASSERT(sockfd != -1) (JASSERT_ERRNO)
        .Text("Error connecting to newly started coordinator.");
    }
  } else {
    JASSERT(false).Text("Not Reached");
  }

  Util::changeFd(sockfd, PROTECTED_COORD_FD);
  JASSERT(Util::isValidFd(coordinatorSocket));
}

DmtcpMessage
sendRecvHandshake(int fd,
                  DmtcpMessage msg,
                  string progname,
                  UniquePid *compId)
{
  if (dmtcp_virtual_to_real_pid) {
    msg.realPid = dmtcp_virtual_to_real_pid(getpid());
  } else {
    msg.realPid = getpid();
  }

  msg.theCheckpointInterval = getCkptInterval();

  string hostname = jalib::Filesystem::GetCurrentHostname();

  size_t buflen = hostname.length() + progname.length() + 2;
  char buf[buflen];
  strcpy(buf, hostname.c_str());
  strcpy(&buf[hostname.length() + 1], progname.c_str());

  sendMsgToCoordinatorRaw(fd, msg, buf, buflen);

  recvMsgFromCoordinatorRaw(fd, &msg);
  msg.assertValid();

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
  }
  // Coordinator also prints this, but its stderr may go to /dev/null
  if (msg.type == DMT_REJECT_NOT_RESTARTING) {
    string coordinatorHost = ""; // C++ magic code; "" to be invisibly replaced
    int coordinatorPort;
    getCoordHostAndPort(COORD_ANY, &coordinatorHost, &coordinatorPort);
    JNOTE ("\n\n*** Computation not in RESTARTING or CHECKPOINTED state."
        "\n***Can't join the existing coordinator, as it is serving a"
        "\n***different computation.  Consider launching a new coordinator."
        "\n***Consider, also, checking with:  dmtcp_command --status")
        (coordinatorPort);
  }
  JASSERT(msg.type == DMT_ACCEPT)(msg.type);
  return msg;
}

void
connectToCoordOnStartup(CoordinatorMode mode,
                        string progname,
                        DmtcpUniqueProcessId *compId,
                        CoordinatorInfo *coordInfo,
                        struct in_addr  *localIP)
{
  JASSERT(compId != NULL && localIP != NULL && coordInfo != NULL);

  createNewConnToCoord(mode);
  JTRACE("sending coordinator handshake")(UniquePid::ThisProcess());
  DmtcpMessage hello_local(DMT_NEW_WORKER);
  hello_local.virtualPid = -1;

  DmtcpMessage hello_remote = sendRecvHandshake(coordinatorSocket,
                                                hello_local,
                                                progname);

  JASSERT(hello_remote.virtualPid != -1);
  JTRACE("Got virtual pid from coordinator") (hello_remote.virtualPid);

  pid_t ppid = getppid();
  Util::setVirtualPidEnvVar(hello_remote.virtualPid, getpid(), ppid, ppid);

  JASSERT(compId != NULL && localIP != NULL && coordInfo != NULL);
  *compId = hello_remote.compGroup.upid();
  coordInfo->id = hello_remote.from.upid();
  coordInfo->timeStamp = hello_remote.coordTimeStamp;
  coordInfo->addrLen = sizeof (coordInfo->addr);
  JASSERT(getpeername(coordinatorSocket,
                      (struct sockaddr*) &coordInfo->addr,
                      &coordInfo->addrLen) == 0)
    (JASSERT_ERRNO);
  memcpy(localIP, &hello_remote.ipAddr, sizeof hello_remote.ipAddr);
}

int
createNewConnectionBeforeFork(string& progname)
{
  struct sockaddr_storage addr;
  uint32_t len;
  SharedData::getCoordAddr((struct sockaddr *)&addr, &len);
  socklen_t addrlen = len;
  int sock = jalib::JClientSocket((struct sockaddr *)&addr, addrlen);
  JASSERT(sock != -1);

  DmtcpMessage hello_local(DMT_NEW_WORKER);
  DmtcpMessage hello_remote = sendRecvHandshake(sock, hello_local, progname);
  JASSERT(hello_remote.virtualPid != -1);

  if (dmtcp_virtual_to_real_pid) {
    JTRACE("Got virtual pid from coordinator") (hello_remote.virtualPid);
    pid_t pid = getpid();
    pid_t realPid = dmtcp_virtual_to_real_pid(pid);
    Util::setVirtualPidEnvVar(hello_remote.virtualPid, 0, pid, realPid);
  }
  return sock;
}

void
connectToCoordOnRestart(CoordinatorMode  mode,
                        string progname,
                        UniquePid compGroup,
                        int np,
                        CoordinatorInfo *coordInfo,
                        struct in_addr  *localIP)
{
  createNewConnToCoord(mode);
  JTRACE("sending coordinator handshake")(UniquePid::ThisProcess());
  DmtcpMessage hello_local(DMT_RESTART_WORKER);
  hello_local.virtualPid = -1;
  hello_local.numPeers = np;
  hello_local.compGroup = compGroup;

  DmtcpMessage hello_remote = sendRecvHandshake(coordinatorSocket,
                                                hello_local,
                                                progname,
                                                &compGroup);

  if (coordInfo != NULL) {
    coordInfo->id = hello_remote.from.upid();
    coordInfo->timeStamp = hello_remote.coordTimeStamp;
    coordInfo->addrLen = sizeof(coordInfo->addr);
    JASSERT(getpeername(coordinatorSocket,
                        (struct sockaddr *)&coordInfo->addr,
                        &coordInfo->addrLen) == 0)
      (JASSERT_ERRNO);
  }
  if (localIP != NULL) {
    memcpy(localIP, &hello_remote.ipAddr, sizeof hello_remote.ipAddr);
  }

  JTRACE("Coordinator handshake RECEIVED!!!!!");
}

void
sendCkptFilename()
{
  // Tell coordinator to record our filename in the restart script
  string ckptFilename = ProcessInfo::instance().getCkptFilename();
  string hostname = jalib::Filesystem::GetCurrentHostname();
  DmtcpMessage msg;
  if (dmtcp_unique_ckpt_enabled && dmtcp_unique_ckpt_enabled()) {
    msg.type = DMT_UNIQUE_CKPT_FILENAME;
  } else {
    msg.type = DMT_CKPT_FILENAME;
  }
  // Tell coordinator type of remote shell command used ssh/rsh
  string shellType = "";
  const char *remoteShellType = getenv(ENV_VAR_REMOTE_SHELL_CMD);
  if (remoteShellType != NULL) {
    shellType = remoteShellType;
  }
  JTRACE("recording filenames") (ckptFilename) (hostname) (shellType);

  size_t buflen = hostname.length() + shellType.length() +
                  ckptFilename.length() + 3;
  char buf[buflen];
  strcpy(buf, ckptFilename.c_str());
  strcpy(&buf[ckptFilename.length() + 1], shellType.c_str());
  strcpy(&buf[ckptFilename.length() + 1 + shellType.length() + 1],
         hostname.c_str());

  sendMsgToCoordinator(msg, buf, buflen);
}

kvdb::KVDBResponse
kvdbRequest(DmtcpMessage const& msg,
            string const& key,
            string const& val,
            string *oldVal)
{
  int sock = coordinatorSocket;

  if (dmtcp_is_running_state() &&
      dmtcp_is_ckpt_thread /* weak symbol */ &&
      !dmtcp_is_ckpt_thread()) {
    if (nsSock == -1) {
      nsSock = createNewSocketToCoordinator(COORD_ANY);
      JASSERT(nsSock != -1);
      nsSock = Util::changeFd(nsSock, PROTECTED_NS_FD);
      sock = nsSock;
      DmtcpMessage m(DMT_NAME_SERVICE_WORKER);
      JASSERT(Util::writeAll(sock, &m, sizeof(m)) == sizeof(m));
    }
    sock = nsSock;
  }

  JASSERT(Util::writeAll(sock, &msg, sizeof(msg)) == sizeof(msg));
  JASSERT(Util::writeAll(sock, key.data(), msg.keyLen) ==
    (ssize_t)msg.keyLen);
  JASSERT(Util::writeAll(sock, val.data(), msg.valLen) ==
    (ssize_t)msg.valLen);

  DmtcpMessage reply;
  reply.poison();
  JASSERT(Util::readAll(sock, &reply, sizeof(reply)) == sizeof(reply));
  reply.assertValid();
  JASSERT(reply.type == DMT_KVDB_RESPONSE);

  if (reply.extraBytes != 0) {
    char valBuf[reply.extraBytes];
    JASSERT(Util::readAll(sock, valBuf, reply.valLen) ==
            (ssize_t)reply.valLen);
    if (oldVal != nullptr) {
      *oldVal = valBuf;
    }
  }

  return reply.kvdbResponse;
}
} // namespace CoordinatorAPI {
} // namespace dmtcp {
