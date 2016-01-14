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
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
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
#include <semaphore.h> // for sem_post(&sem_launch)

// sem_launch is used in threadlist.cpp
// sem_launch_first_time will be set just before pthread_create(checkpointhread)
LIB_PRIVATE bool sem_launch_first_time = false;
LIB_PRIVATE sem_t sem_launch;

namespace dmtcp {

static void coordinatorAPI_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  if (CoordinatorAPI::noCoordinator()) return;
  switch (event) {
    case DMTCP_EVENT_INIT:
      CoordinatorAPI::instance().init();
      break;

    case DMTCP_EVENT_EXIT:
      JTRACE("exit() in progress, disconnecting from dmtcp coordinator");
      CoordinatorAPI::instance().closeConnection();
      break;

    default:
      break;
  }
}

static DmtcpBarrier coordinatorAPIBarriers[] = {
  {DMTCP_LOCAL_BARRIER_RESTART, CoordinatorAPI::restart, "restart"}
};

static DmtcpPluginDescriptor_t coordinatorAPIPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "coordinatorapi",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Coordinator API plugin",
  DMTCP_DECL_BARRIERS(coordinatorAPIBarriers),
  coordinatorAPI_EventHook
};

DmtcpPluginDescriptor_t dmtcp_CoordinatorAPI_PluginDescr()
{
  return coordinatorAPIPlugin;
}

void CoordinatorAPI::restart()
{
  instance()._nsSock.close();
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

static jalib::JSocket createNewSocketToCoordinator(CoordinatorMode mode)
{
  const char*host = NULL;
  int port = UNINITIALIZED_PORT;

  Util::getCoordHostAndPort(COORD_ANY, &host, &port);
  return jalib::JClientSocket(host, port);
}

//CoordinatorAPI::CoordinatorAPI (int sockfd)
  //: _coordinatorSocket(sockfd)
//{ }

static CoordinatorAPI *coordAPIInst = NULL;
CoordinatorAPI& CoordinatorAPI::instance()
{
  //static SysVIPC *inst = new SysVIPC(); return *inst;
  if (coordAPIInst == NULL) {
    coordAPIInst = new CoordinatorAPI();
    if (noCoordinator() ||
        Util::isValidFd(PROTECTED_COORD_FD)) {
      coordAPIInst->_coordinatorSocket = jalib::JSocket(PROTECTED_COORD_FD);
    }
  }
  return *coordAPIInst;
}

void CoordinatorAPI::init()
{
  JTRACE("Informing coordinator of new process") (UniquePid::ThisProcess());

  DmtcpMessage msg (DMT_UPDATE_PROCESS_INFO_AFTER_INIT_OR_EXEC);
  string progname = jalib::Filesystem::GetProgramName();
  msg.extraBytes = progname.length() + 1;

  instance()._coordinatorSocket << msg;
  instance()._coordinatorSocket.writeAll(progname.c_str(),
                                         progname.length() + 1);
  // The coordinator won't send any msg in response to DMT_UPDATE... so no need
  // to call recvCoordinatorHandshake().
}

void CoordinatorAPI::resetOnFork(CoordinatorAPI& coordAPI)
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

// FIXME:  Does "virtual coordinator" mean coordinator built into the
//         the current process (no separate process?)
void CoordinatorAPI::setupVirtualCoordinator(CoordinatorInfo *coordInfo,
                                             struct in_addr  *localIP)
{
  const char *host = NULL;
  int port;
  Util::getCoordHostAndPort(COORD_NONE, &host, &port);
  _coordinatorSocket = jalib::JServerSocket(jalib::JSockAddr::ANY, port);
  JASSERT(_coordinatorSocket.isValid()) (port) (JASSERT_ERRNO)
    .Text("Failed to create listen socket.");
  _coordinatorSocket.changeFd(PROTECTED_COORD_FD);
  Util::setCoordPort(_coordinatorSocket.port());

  pid_t ppid = getppid();
  Util::setVirtualPidEnvVar(INITIAL_VIRTUAL_PID, ppid, ppid);

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

void CoordinatorAPI::waitForCheckpointCommand()
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

    // This call to select() does nothing and returns.
    // But we want to find address of select() using dlsym/libc before
    //   allowing the user thread to continue.
    struct timeval timezero = {0,0};
    select(0, NULL, NULL, NULL, &timezero);
    if (sem_launch_first_time) {
      // Release user thread now that we've initialized the checkpoint thread.
      // This code is reached if the --no-coordinator flag is used.
      sem_post(&sem_launch);
      sem_launch_first_time = false;
    }

    FD_ZERO(&rfds);
    FD_SET(_coordinatorSocket.sockfd(), &rfds );
    int retval =
      select(_coordinatorSocket.sockfd()+1, &rfds, NULL, NULL, timeout);
    if (retval == 0) { // timeout expired, time for checkpoint
      JTRACE("Timeout expired, checkpointing now.");
      return;
    } else if (retval > 0) {
      JASSERT(FD_ISSET(_coordinatorSocket.sockfd(), &rfds));
      JTRACE("Connect request on virtual coordinator socket.");
      break;
    }
    JASSERT(errno == EINTR) (JASSERT_ERRNO); /* EINTR: a signal was caught */
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
  DmtcpMessage msg;
  DmtcpMessage reply(DMT_USER_CMD_RESULT);
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
    _real_exit(0);
  }
  return;
}

bool CoordinatorAPI::noCoordinator()
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

void CoordinatorAPI::connectAndSendUserCommand(char c,
                                               int *coordCmdStatus,
                                               int *numPeers,
                                               int *isRunning,
                                               int *ckptInterval)
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
  if (ckptInterval != NULL) {
    *ckptInterval = reply.theCheckpointInterval;
  }

  _coordinatorSocket.close();
}

string CoordinatorAPI::getCoordCkptDir(void)
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

void CoordinatorAPI::updateCoordCkptDir(const char *dir)
{
  if (noCoordinator()) return;
  JASSERT(dir != NULL);
  DmtcpMessage msg(DMT_UPDATE_CKPT_DIR);
  msg.extraBytes = strlen(dir) + 1;
  _coordinatorSocket << msg;
  _coordinatorSocket.writeAll(dir, strlen(dir) + 1);
}

void CoordinatorAPI::sendMsgToCoordinator(const DmtcpMessage &msg,
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

void CoordinatorAPI::recvMsgFromCoordinator(DmtcpMessage *msg, void **extraData)
{
  JASSERT(!noCoordinator()).Text("internal error");
  if (sem_launch_first_time) {
    // Release user thread now that we've initialized the checkpoint thread.
    // This code is reached if the --no-coordinator flag is not used.
    // FIXME:  Technically, some rare type of software could still execute
    //   between here and when we readall() from coord, thus creating a race.
    sem_post(&sem_launch);
    sem_launch_first_time = false;
  }
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

void CoordinatorAPI::waitForBarrier(const string& barrierId)
{
  CoordinatorAPI::instance().sendMsgToCoordinator(DmtcpMessage(DMT_OK));

  JTRACE("waiting for DMT_BARRIER_LIFTED message");

  char *extraData = NULL;
  DmtcpMessage msg;
  CoordinatorAPI::instance().recvMsgFromCoordinator(&msg, (void**)&extraData);

  msg.assertValid();
  if (msg.type == DMT_KILL_PEER) {
    JTRACE("Received KILL message from coordinator, exiting");
    _exit (0);
  }

  JASSERT(msg.type == DMT_BARRIER_LIFTED) (msg.type);
  JASSERT(extraData != NULL);
  JASSERT(barrierId == extraData) (barrierId) (extraData);

  JALLOC_FREE(extraData);

}

void CoordinatorAPI::startNewCoordinator(CoordinatorMode mode)
{
  const char *host;
  int port;
  Util::getCoordHostAndPort(mode, &host, &port);

  JASSERT(strcmp(host, "localhost") == 0 ||
          strcmp(host, "127.0.0.1") == 0 ||
          jalib::Filesystem::GetCurrentHostname() == host)
    (host) (jalib::Filesystem::GetCurrentHostname())
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
  Util::setCoordPort(coordinatorListenerSocket.port());

  JTRACE("Starting a new coordinator automatically.")
        (coordinatorListenerSocket.port());

  if (fork() == 0) {
    // We can't use Util::getPath() here since the SharedData has not been
    // initialized yet.
    string coordinator =
      jalib::Filesystem::GetProgramDir() + "/dmtcp_coordinator";

    char *modeStr = (char *)"--daemon";
    char * args[] = {
      (char*)coordinator.c_str(),
      (char*)"--quiet",
      /* If we wish to also suppress coordinator warnings, call --quiet twice */
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

void CoordinatorAPI::createNewConnToCoord(CoordinatorMode mode)
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

DmtcpMessage CoordinatorAPI::sendRecvHandshake(DmtcpMessage msg,
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
  msg.extraBytes = hostname.length() + 1 + progname.length() + 1;

  _coordinatorSocket << msg;
  _coordinatorSocket.writeAll(hostname.c_str(), hostname.length() + 1);
  _coordinatorSocket.writeAll(progname.c_str(), progname.length() + 1);

  msg.poison();
  _coordinatorSocket >> msg;
  msg.assertValid();
  if (msg.type == DMT_KILL_PEER) {
    JTRACE("Received KILL message from coordinator, exiting");
    _real_exit (0);
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
  }
  JASSERT(msg.type == DMT_ACCEPT)(msg.type);
  return msg;
}

void CoordinatorAPI::connectToCoordOnStartup(CoordinatorMode mode,
                                             string progname,
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

  pid_t ppid = getppid();
  Util::setVirtualPidEnvVar(hello_remote.virtualPid, ppid, ppid);

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

void CoordinatorAPI::createNewConnectionBeforeFork(string& progname)
{
  JASSERT(!noCoordinator())
    .Text("Process attempted to call fork() while in --no-coordinator mode\n"
          "  Because the coordinator is embedded in a single process,\n"
          "    DMTCP will not work with multiple processes.");
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
    pid_t pid = getpid();
    pid_t realPid = dmtcp_virtual_to_real_pid(pid);
    Util::setVirtualPidEnvVar(hello_remote.virtualPid, pid, realPid);
  }
}

void CoordinatorAPI::connectToCoordOnRestart(CoordinatorMode  mode,
                                             string progname,
                                             UniquePid compGroup,
                                             int np,
                                             CoordinatorInfo *coordInfo,
                                             const char *host,
                                             int port,
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

void CoordinatorAPI::sendCkptFilename()
{
  if (noCoordinator()) return;
  // Tell coordinator to record our filename in the restart script
  string ckptFilename = ProcessInfo::instance().getCkptFilename();
  string hostname = jalib::Filesystem::GetCurrentHostname();
  JTRACE("recording filenames") (ckptFilename) (hostname);
  DmtcpMessage msg;
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


int CoordinatorAPI::sendKeyValPairToCoordinator(const char *id,
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
int CoordinatorAPI::sendQueryToCoordinator(const char *id,
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

}
