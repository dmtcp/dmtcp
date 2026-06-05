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
#include "util_assert.h"

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

LIB_PRIVATE DmtcpPluginDescriptor_t coordinatorAPIPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "COORDINATOR_API",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Coordinator API plugin",
  eventHook
};

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
  ASSERT(Util::isValidFd(sock), "invalid coordinator socket: fd={}", sock);
  ASSERT(sock != PROTECTED_COORD_FD,
         "new coordinator socket already uses protected fd: fd={}", sock);
  Util::changeFd(sock, PROTECTED_COORD_FD);
  ASSERT(Util::isValidFd(coordinatorSocket),
         "protected coordinator socket is invalid after fd change: fd={}",
         coordinatorSocket);

  JTRACE("Informing coordinator of new process") (UniquePid::ThisProcess());

  DmtcpMessage msg(DMT_UPDATE_PROCESS_INFO_AFTER_FORK);
  if (dmtcp_pid_virtual_to_real) {
    msg.realPid = dmtcp_pid_virtual_to_real(getpid());
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
  ASSERT(nsSock == -1,
         "vfork child namespace socket support is not implemented");
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
  ASSERT_SYSCALL_EQ_MSG(static_cast<ssize_t>(sizeof(msg)),
                        Util::writeAll(coordFd, &msg, sizeof(msg)),
                        "failed to send user command to coordinator: fd={} "
                        "command={}",
                        coordFd, c);

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
  ASSERT(reply.type == DMT_USER_CMD_RESULT,
         "unexpected coordinator user-command reply type: type={}",
         reply.type);

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
  ASSERT_SYSCALL_EQ_MSG(static_cast<ssize_t>(sizeof(msg)),
                        Util::writeAll(fd, &msg, sizeof(msg)),
                        "failed to send coordinator message header: fd={} "
                        "type={}",
                        fd, msg.type);
  if (extraData != NULL) {
    ASSERT_SYSCALL_EQ_MSG(static_cast<ssize_t>(len),
                          Util::writeAll(fd, extraData, len),
                          "failed to send coordinator message payload: fd={} "
                          "type={} len={}",
                          fd, msg.type, len);
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

  if (!tmpMsg.isValid()) {
    return;
  }

  if (tmpMsg.extraBytes > 0) {
    ASSERT_NOT_NULL_MSG(extraData,
                        "coordinator message has payload but caller did not "
                        "request it: type={} bytes={}",
                        tmpMsg.type, tmpMsg.extraBytes);

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

  ASSERT(barrier.length() < sizeof(barrierMsg.barrier),
         "barrier name too long: barrier={} len={} max={}", barrier,
         barrier.length(), sizeof(barrierMsg.barrier));
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

  ASSERT(msg.type == DMT_BARRIER_RELEASED,
         "unexpected barrier reply type: type={}", msg.type);
  ASSERT_NOT_NULL_MSG(extraData, "barrier reply missing payload: barrier={}",
                      barrier);
  ASSERT(barrier == extraData,
         "barrier reply payload mismatch: expected={} actual={}", barrier,
         static_cast<char *>(extraData));

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

  string currentHost = jalib::Filesystem::GetCurrentHostname();
  ASSERT(host == "localhost" || host == "127.0.0.1" || currentHost == host,
         "Won't automatically start coordinator because DMTCP_HOST is set "
         "to a remote host: host={} current_host={}",
         host, currentHost);

  // Create a socket and bind it to an unused port.
  errno = 0;
  jalib::JServerSocket coordinatorListenerSocket(jalib::JSockAddr::ANY,
                                                 port, 128);
  ASSERT_ERRNO(coordinatorListenerSocket.isValid(),
               "Failed to create socket to connect to coordinator port; this "
               "may be an old coordinator, a missing --join-coordinator, or a "
               "port still being released by the OS: host={} port={} "
               "listener_port={}",
               host, port, coordinatorListenerSocket.port());
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
    ASSERT_ERRNO(false, "exec(dmtcp_coordinator) failed: path={}",
                 coordinator);
  } else {
    int status;
    _real_close(PROTECTED_COORD_FD);
    ASSERT_ERRNO(wait(&status) > 0,
                 "failed waiting for auto-started coordinator");
  }
}

void
createNewConnToCoord(CoordinatorMode mode)
{
  int sockfd = -1;
  if (mode & COORD_JOIN) {
    sockfd = createNewSocketToCoordinator(mode);
    ASSERT_VALID_FD_MSG(sockfd,
                        "Coordinator not found, but --join was specified");
  } else if (mode & COORD_NEW) {
    startNewCoordinator(mode);
    sockfd = createNewSocketToCoordinator(mode);
    ASSERT_VALID_FD_MSG(sockfd,
                        "Error connecting to newly started coordinator");
  } else if (mode & COORD_ANY) {
    sockfd = createNewSocketToCoordinator(mode);
    if (sockfd == -1) {
      JTRACE("Coordinator not found, trying to start a new one.");
      startNewCoordinator(mode);
      sockfd = createNewSocketToCoordinator(mode);
      ASSERT_VALID_FD_MSG(sockfd,
                          "Error connecting to newly started coordinator");
    }
  } else {
    ASSERT(false, "invalid coordinator mode: mode={}", mode);
  }

  Util::changeFd(sockfd, PROTECTED_COORD_FD);
  ASSERT(Util::isValidFd(coordinatorSocket),
         "protected coordinator socket is invalid after connection: fd={}",
         coordinatorSocket);
}

DmtcpMessage
sendRecvHandshake(int fd,
                  DmtcpMessage msg,
                  string progname,
                  UniquePid *compId)
{
  if (dmtcp_pid_virtual_to_real) {
    msg.realPid = dmtcp_pid_virtual_to_real(getpid());
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
    ASSERT(false,
           "Connection rejected by the coordinator: current computation is "
           "not in RUNNING state; checkpoint/restart may be in progress");
  } else if (msg.type == DMT_REJECT_WRONG_COMP) {
    ASSERT_NOT_NULL_MSG(compId,
                        "coordinator rejected wrong computation without "
                        "expected compId");
    ASSERT(false,
           "Connection rejected by the coordinator: different computation "
           "group: hostid={} pid={} generation={}",
           compId->hostid(), compId->pid(), compId->computationGeneration());
  } else if (msg.type == DMT_REJECT_RESTART_PEER_MISMATCH) {
    ASSERT(false,
           "Connection rejected by the coordinator: restart peer count does "
           "not match this computation");
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
  ASSERT(msg.type == DMT_ACCEPT,
         "unexpected coordinator handshake reply type: type={}", msg.type);
  return msg;
}

void
connectToCoordOnStartup(CoordinatorMode mode,
                        string progname,
                        DmtcpUniqueProcessId *compId,
                        CoordinatorInfo *coordInfo,
                        struct in_addr  *localIP)
{
  ASSERT_NOT_NULL_MSG(compId,
                      "connectToCoordOnStartup requires non-null compId "
                      "output pointer");
  ASSERT_NOT_NULL_MSG(localIP,
                      "connectToCoordOnStartup requires non-null localIP "
                      "output pointer");
  ASSERT_NOT_NULL_MSG(coordInfo,
                      "connectToCoordOnStartup requires non-null coordInfo "
                      "output pointer");

  createNewConnToCoord(mode);
  JTRACE("sending coordinator handshake")(UniquePid::ThisProcess());
  DmtcpMessage hello_local(DMT_NEW_WORKER);
  hello_local.virtualPid = -1;

  DmtcpMessage hello_remote = sendRecvHandshake(coordinatorSocket,
                                                hello_local,
                                                progname);

  ASSERT(hello_remote.virtualPid != -1,
         "coordinator did not assign a virtual pid during startup handshake");
  JTRACE("Got virtual pid from coordinator") (hello_remote.virtualPid);

  pid_t ppid = getppid();
  Util::setVirtualPidEnvVar(hello_remote.virtualPid, getpid(), ppid, ppid);

  ASSERT_NOT_NULL_MSG(compId,
                      "connectToCoordOnStartup compId output pointer became "
                      "null");
  ASSERT_NOT_NULL_MSG(localIP,
                      "connectToCoordOnStartup localIP output pointer became "
                      "null");
  ASSERT_NOT_NULL_MSG(coordInfo,
                      "connectToCoordOnStartup coordInfo output pointer "
                      "became null");
  *compId = hello_remote.compGroup.upid();
  coordInfo->id = hello_remote.from.upid();
  coordInfo->timeStamp = hello_remote.coordTimeStamp;
  coordInfo->addrLen = sizeof (coordInfo->addr);
  ASSERT_SYSCALL_SUCCESS_MSG(getpeername(coordinatorSocket,
                                         (struct sockaddr*) &coordInfo->addr,
                                         &coordInfo->addrLen),
                             "failed to get coordinator peer address: fd={}",
                             coordinatorSocket);
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
  ASSERT_VALID_FD_MSG(sock,
                      "failed to create coordinator connection before fork");

  DmtcpMessage hello_local(DMT_NEW_WORKER);
  DmtcpMessage hello_remote = sendRecvHandshake(sock, hello_local, progname);
  ASSERT(hello_remote.virtualPid != -1,
         "coordinator did not assign a virtual pid before fork");

  if (dmtcp_pid_virtual_to_real) {
    JTRACE("Got virtual pid from coordinator") (hello_remote.virtualPid);
    pid_t pid = getpid();
    pid_t realPid = dmtcp_pid_virtual_to_real(pid);
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
    ASSERT_SYSCALL_SUCCESS_MSG(
      getpeername(coordinatorSocket,
                  (struct sockaddr *)&coordInfo->addr,
                  &coordInfo->addrLen),
      "failed to get coordinator peer address after restart: fd={}",
      coordinatorSocket);
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
      ASSERT_VALID_FD_MSG(nsSock,
                          "failed to create namespace coordinator socket");
      nsSock = Util::changeFd(nsSock, PROTECTED_NS_FD);
      sock = nsSock;
      DmtcpMessage m(DMT_NAME_SERVICE_WORKER);
      ASSERT_SYSCALL_EQ_MSG(static_cast<ssize_t>(sizeof(m)),
                            Util::writeAll(sock, &m, sizeof(m)),
                            "failed to register namespace service worker: "
                            "fd={}",
                            sock);
    }
    sock = nsSock;
  }

  ASSERT_SYSCALL_EQ_MSG(static_cast<ssize_t>(sizeof(msg)),
                        Util::writeAll(sock, &msg, sizeof(msg)),
                        "failed to send KVDB message header: fd={} type={}",
                        sock, msg.type);
  ASSERT_SYSCALL_EQ_MSG(static_cast<ssize_t>(msg.keyLen),
                        Util::writeAll(sock, key.data(), msg.keyLen),
                        "failed to send KVDB key: fd={} key_len={}", sock,
                        msg.keyLen);
  ASSERT_SYSCALL_EQ_MSG(static_cast<ssize_t>(msg.valLen),
                        Util::writeAll(sock, val.data(), msg.valLen),
                        "failed to send KVDB value: fd={} val_len={}", sock,
                        msg.valLen);

  DmtcpMessage reply;
  reply.poison();
  ASSERT_SYSCALL_EQ_MSG(static_cast<ssize_t>(sizeof(reply)),
                        Util::readAll(sock, &reply, sizeof(reply)),
                        "failed to read KVDB reply: fd={}", sock);
  reply.assertValid();
  ASSERT(reply.type == DMT_KVDB_RESPONSE,
         "unexpected KVDB reply type: type={}", reply.type);

  if (reply.extraBytes != 0) {
    char valBuf[reply.extraBytes];
    ASSERT_SYSCALL_EQ_MSG(static_cast<ssize_t>(reply.valLen),
                          Util::readAll(sock, valBuf, reply.valLen),
                          "failed to read KVDB reply payload: fd={} len={}",
                          sock, reply.valLen);
    if (oldVal != nullptr) {
      *oldVal = valBuf;
    }
  }

  return reply.kvdbResponse;
}
} // namespace CoordinatorAPI {
} // namespace dmtcp {
