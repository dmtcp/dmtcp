#include "ssh.h"
#include <arpa/inet.h>
#include <limits.h> // for HOST_NAME_MAX
#include <netinet/in.h>
#include <string_view>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include "jassert.h"
#include "jfilesystem.h"
#include "dmtcp.h"
#include "shareddata.h"
#include "sshdrainer.h"
#include "syscallwrappers.h"
#include "util.h"
#include "util_assert.h"
#include "util_ipc.h"

using namespace dmtcp;

#define SSHD_PIPE_FD -1
// DPP, below, stands for "DMTCP Path Prefix"
#define ENV_ORIG_DPP "DMTCP_ORIGINAL_PATH_PREFIX"
#define ENV_NEW_DPP  "DMTCP_NEW_PATH_PREFIX"

static string cmd;
static string prefix;
static string dmtcp_launch_path;
static string dmtcp_ssh_path;
static string dmtcp_sshd_path;
static string dmtcp_nocheckpoint_path;

static SSHDrainer *theDrainer = NULL;
static int sshStdin = -1;
static int sshStdout = -1;
static int sshStderr = -1;
static int sshSockFd = -1;
static bool isSshdProcess = false;
static int noStrictHostKeyChecking = 0;
static int isRshProcess = 0;
static int isSshProcess = 0;

static bool sshPluginEnabled = false;

void dmtcp_ssh_drain();
void dmtcp_ssh_resume();
void dmtcp_ssh_restart();
static void refill(bool isRestart);
static void sshdReceiveFds();
static void createNewDmtcpSshdProcess();
static void updateCoordHost();
static void prepareForExec(DmtcpEventData_t *data);

void dmtcp_SocketConnList_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data);

static void
process_close_fd_event(int fd)
{
  DmtcpEventData_t data;
  data.closeFd.fd = fd;
  dmtcp_SocketConnList_EventHook(DMTCP_EVENT_CLOSE_FD, &data);
}


void
dmtcp_SSH_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_PRE_EXEC:
    prepareForExec(data);
    break;

  case DMTCP_EVENT_PRECHECKPOINT:
    dmtcp_ssh_drain();
    break;

  case DMTCP_EVENT_RESUME:
    dmtcp_ssh_resume();
    break;

  case DMTCP_EVENT_RESTART:
    dmtcp_ssh_restart();
    break;

  default:  // other events are not registered
    break;
  }
}


LIB_PRIVATE DmtcpPluginDescriptor_t sshPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  DMTCP_PACKAGE_VERSION,
  "SSH",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "SSH plugin",
  dmtcp_SSH_EventHook
};

void
dmtcp_ssh_drain()
{
  if (!sshPluginEnabled) {
    return;
  }

  ASSERT(theDrainer == NULL, "SSH drainer already exists");
  theDrainer = new SSHDrainer();
  if (isSshdProcess) { // dmtcp_ssh process
    theDrainer->beginDrainOf(STDIN_FILENO, sshStdin);
    theDrainer->beginDrainOf(STDOUT_FILENO);
    theDrainer->beginDrainOf(STDERR_FILENO);
  } else {
    theDrainer->beginDrainOf(sshStdin);
    theDrainer->beginDrainOf(sshStdout, STDOUT_FILENO);
    theDrainer->beginDrainOf(sshStderr, STDERR_FILENO);
  }
  theDrainer->monitorSockets(SSH_DRAINER_CHECK_FREQ);
}

void
dmtcp_ssh_resume()
{
  if (!sshPluginEnabled) {
    return;
  }

  refill(false);
}

void
dmtcp_ssh_restart()
{
  if (!sshPluginEnabled) {
    return;
  }

  refill(true);
}

static void
refill(bool isRestart)
{
  if (isRestart) {
    if (isSshdProcess) { // dmtcp_sshd
      sshdReceiveFds();
    } else { // dmtcp_ssh
      createNewDmtcpSshdProcess();
    }
  }

  theDrainer->refill();

  // Free up the object
  delete theDrainer;
  theDrainer = NULL;
}

static void
receiveFileDescr(int fd)
{
  int data;
  int ret = Util::receiveFd(SSHD_RECEIVE_FD, &data, sizeof(data));

  if (fd == SSHD_PIPE_FD) {
    return;
  }
  ASSERT(data == fd, "received unexpected SSH fd: data={} expected={}", data,
         fd);
  if (fd != ret) {
    _real_close(fd);
    _real_dup2(ret, fd);
    _real_close(ret);
  }
}

static void
sshdReceiveFds()
{
  // Add receive-fd data socket.
  static struct sockaddr_un fdReceiveAddr;
  static socklen_t fdReceiveAddrLen;

  memset(&fdReceiveAddr, 0, sizeof(fdReceiveAddr));
  jalib::JSocket sock(_real_socket(AF_UNIX, SOCK_DGRAM, 0));
  ASSERT_ERRNO(sock.isValid(), "failed to create ssh receive socket");
  sock.changeFd(SSHD_RECEIVE_FD);
  fdReceiveAddr.sun_family = AF_UNIX;
  ASSERT_SYSCALL_SUCCESS_MSG(
    _real_bind(SSHD_RECEIVE_FD,
               (struct sockaddr *)&fdReceiveAddr,
               sizeof(fdReceiveAddr.sun_family)),
    "failed to bind ssh receive socket: fd={}",
    SSHD_RECEIVE_FD);

  fdReceiveAddrLen = sizeof(fdReceiveAddr);
  ASSERT_SYSCALL_SUCCESS_MSG(getsockname(SSHD_RECEIVE_FD,
                                         (struct sockaddr *)&fdReceiveAddr,
                                         &fdReceiveAddrLen),
                             "getsockname failed for ssh receive socket: fd={}",
                             SSHD_RECEIVE_FD);

  // Send this information to dmtcp_ssh process
  ASSERT_SYSCALL_EQ_MSG(static_cast<ssize_t>(sizeof(fdReceiveAddrLen)),
                        write(sshSockFd,
                              &fdReceiveAddrLen,
                              sizeof(fdReceiveAddrLen)),
                        "failed to send ssh receive address length: fd={}",
                        sshSockFd);
  ASSERT_SYSCALL_EQ_MSG(static_cast<ssize_t>(fdReceiveAddrLen),
                        write(sshSockFd, &fdReceiveAddr, fdReceiveAddrLen),
                        "failed to send ssh receive address: fd={}",
                        sshSockFd);

  // Now receive fds
  receiveFileDescr(STDIN_FILENO);
  receiveFileDescr(STDOUT_FILENO);
  receiveFileDescr(STDERR_FILENO);
  receiveFileDescr(SSHD_PIPE_FD);
  _real_close(SSHD_RECEIVE_FD);
}

static void
createNewDmtcpSshdProcess()
{
  struct sockaddr_un addr;
  socklen_t addrLen;
  static char abstractSockName[20];
  int in[2], out[2], err[2];

  ASSERT_SYSCALL_EQ_MSG(static_cast<ssize_t>(sizeof(addrLen)),
                        read(sshSockFd, &addrLen, sizeof(addrLen)),
                        "failed to read ssh address length: fd={}",
                        sshSockFd);
  memset(&addr, 0, sizeof(addr));
  ASSERT_SYSCALL_EQ_MSG(static_cast<ssize_t>(addrLen),
                        read(sshSockFd, &addr, addrLen),
                        "failed to read ssh address: fd={}", sshSockFd);
  ASSERT(strlen(&addr.sun_path[1]) < sizeof(abstractSockName),
         "ssh abstract socket name is too long: length={} max={}",
         strlen(&addr.sun_path[1]), sizeof(abstractSockName));
  strcpy(abstractSockName, &addr.sun_path[1]);

  struct sockaddr_in sshdSockAddr;
  socklen_t sshdSockAddrLen = sizeof(sshdSockAddr);
  char remoteHost[80];
  ASSERT_SYSCALL_SUCCESS_MSG(getpeername(sshSockFd,
                                         (struct sockaddr *)&sshdSockAddr,
                                         &sshdSockAddrLen),
                             "getpeername failed for ssh socket: fd={}",
                             sshSockFd);
  char *ip = inet_ntoa(sshdSockAddr.sin_addr);
  strcpy(remoteHost, ip);

  if (dmtcp_nocheckpoint_path.length() == 0) {
    dmtcp_nocheckpoint_path = Util::getPath("dmtcp_nocheckpoint");
    dmtcp_sshd_path = Util::getPath("dmtcp_sshd");
  }


  ASSERT_SYSCALL_SUCCESS_MSG(pipe(in), "creating ssh stdin pipe");
  ASSERT_SYSCALL_SUCCESS_MSG(pipe(out), "creating ssh stdout pipe");
  ASSERT_SYSCALL_SUCCESS_MSG(pipe(err), "creating ssh stderr pipe");

  pid_t sshChildPid = fork();
  ASSERT_FORK_SUCCESS_MSG(sshChildPid, "failed to fork ssh child");
  if (sshChildPid == 0) {
    const int max_args = 16;
    char *argv[16];
    int idx = 0;

    argv[idx++] = (char*) dmtcp_nocheckpoint_path.c_str();

    const char* shellType = NULL;

    if (isRshProcess) {
      shellType = "rsh";
    } else {
      shellType = "ssh";
    }

    argv[idx++] = const_cast<char*>(shellType);

    if (noStrictHostKeyChecking) {
      argv[idx++] = const_cast<char *>("-o");
      argv[idx++] = const_cast<char *>("StrictHostKeyChecking=no");
    }
    argv[idx++] = remoteHost;
    argv[idx++] = (char *)dmtcp_sshd_path.c_str();
    argv[idx++] = const_cast<char *>("--listenAddr");
    argv[idx++] = abstractSockName;
    argv[idx++] = NULL;
    ASSERT(idx < max_args, "too many ssh child arguments: idx={} max={}", idx,
           max_args);

    // TODO: Hack until we improve the plugin design to remove these calls.
    process_close_fd_event(in[1]);
    process_close_fd_event(out[0]);
    process_close_fd_event(err[0]);
    dup2(in[0], STDIN_FILENO);
    dup2(out[1], STDOUT_FILENO);
    dup2(err[1], STDERR_FILENO);

    JTRACE("Launching ")
      (argv[0]) (argv[1]) (argv[2]) (argv[3]) (argv[4]) (argv[5]);
    execvp(argv[0], argv);
    ASSERT_ERRNO(false, "execvp failed for ssh child: path={}", argv[0]);
  }

  dup2(in[1], 500 + sshStdin);
  dup2(out[0], 500 + sshStdout);
  dup2(err[0], 500 + sshStderr);

  close(in[0]);
  close(in[1]);
  close(out[0]);
  close(out[1]);
  close(err[0]);
  close(err[1]);

  dup2(500 + sshStdin, sshStdin);
  dup2(500 + sshStdout, sshStdout);
  dup2(500 + sshStderr, sshStderr);
  close(500 + sshStdin);
  close(500 + sshStdout);
  close(500 + sshStderr);

  process_close_fd_event(sshStdin);
  process_close_fd_event(sshStdout);
  process_close_fd_event(sshStderr);
}

extern "C" void
dmtcp_ssh_register_fds(int isSshd,
                       int in,
                       int out,
                       int err,
                       int sock,
                       int noStrictChecking,
                       int rshProcess)
{
  if (isSshd) { // dmtcp_sshd
    process_close_fd_event(STDIN_FILENO);
    process_close_fd_event(STDOUT_FILENO);
    process_close_fd_event(STDERR_FILENO);
  } else { // dmtcp_ssh
    process_close_fd_event(in);
    process_close_fd_event(out);
    process_close_fd_event(err);
    isRshProcess = rshProcess;
  }
  sshStdin = in;
  sshStdout = out;
  sshStderr = err;
  sshSockFd = sock;
  isSshdProcess = isSshd;
  sshPluginEnabled = true;
  noStrictHostKeyChecking = noStrictChecking;
}

static void
prepareForExec(DmtcpEventData_t *data)
{
  const char **argv = data->preExec.argv;
  size_t maxArgs = data->preExec.maxArgs;

  isSshProcess = (jalib::Filesystem::BaseName(data->preExec.filename) == "ssh");
  isRshProcess = (jalib::Filesystem::BaseName(data->preExec.filename) == "rsh");

  if (!isSshProcess && !isRshProcess) {
    return;
  }

  updateCoordHost();

  size_t nargs = 0;
  bool noStrictChecking = false;
  string precmd, postcmd, tempcmd;

  while (argv[nargs++] != NULL) {}

  if (nargs < 3) {
    if (!isRshProcess) {
    JNOTE("ssh with less than 3 args") (argv[0]) (argv[1]);
    return;
    } else if (nargs < 2) {
        JNOTE("rsh with less than 2 args") (argv[0]);
        return;
      }
  }

  // find command part
  size_t commandStart = 2;
  for (size_t i = 1; i < nargs; ++i) {
    string s = argv[i];
    if (strcmp(argv[i], "-o") == 0) {
      if (strcmp(argv[i + 1], "StrictHostKeyChecking=no") == 0) {
        noStrictChecking = true;
      }
      i++;
      continue;
    }

    // The following flags have additional parameters and aren't fully
    // supported. We simply forward them to the ssh command.
    if (s == "-b" || s == "-c" || s == "-E" || s == "-e" || s == "-F" ||
        s == "-I" || s == "-i" || s == "-l" || s == "-O" || s == "-o" ||
        s == "-p" || s == "-Q" || s == "-S") {
      i++;
      continue;
    }

    // These options have a higher probability of failure due to binding
    // addresses, etc.
    if (s == "-b" || s == "-D" || s == "-L" || s == "-m" || s == "-R" ||
        s == "-W" || s == "-w") {
      JNOTE("The '" + s + "' ssh option isn't fully supported!");
      i++;
      continue;
    }

    if (argv[i][0] != '-') {
      commandStart = i + 1;
      break;
    }
  }
  ASSERT(commandStart < nargs && argv[commandStart][0] != '-',
         "failed to parse ssh command line: command_start={} nargs={}",
         commandStart, nargs);

  char **dmtcp_args = Util::getDmtcpArgs();

  dmtcp_launch_path = Util::getPath("dmtcp_launch");
  dmtcp_ssh_path = Util::getPath("dmtcp_ssh");
  dmtcp_sshd_path = Util::getPath("dmtcp_sshd");
  dmtcp_nocheckpoint_path = Util::getPath("dmtcp_nocheckpoint");

  prefix = dmtcp_launch_path + " ";
  for (size_t i = 0; dmtcp_args[i] != NULL; i++) {
    prefix.append(dmtcp_args[i]).append(" ");
  }
  prefix += dmtcp_sshd_path + " ";

  if (isRshProcess) {
    prefix += " --rsh-slave ";
  } else {
    prefix += " --ssh-slave ";
  }

  if(dmtcp_pathvirt_enabled && dmtcp_pathvirt_enabled()) {
    if(getenv(ENV_NEW_DPP)) {
      prefix += "/usr/bin/env ";
      prefix += ENV_NEW_DPP;
      prefix += "=";
      prefix += getenv(ENV_NEW_DPP);
      prefix += " ";
    }
    if(getenv(ENV_ORIG_DPP)) {
      prefix += "/usr/bin/env ";
      prefix += ENV_ORIG_DPP;
      prefix += "=";
      prefix += getenv(ENV_ORIG_DPP);
      prefix += " ";
    }
  }

  JTRACE("Prefix")(prefix);

  // process command
  size_t semipos, pos;
  size_t actpos = string::npos;
  tempcmd = argv[commandStart];
  for (semipos = 0; (pos = tempcmd.find(';', semipos + 1)) != string::npos;
       semipos = pos, actpos = pos) {}

  if (actpos > 0 && actpos != string::npos) {
    precmd = tempcmd.substr(0, actpos + 1);
    postcmd = tempcmd.substr(actpos + 1);
    postcmd = postcmd.substr(postcmd.find_first_not_of(" "));
  } else {
    precmd = "";
    postcmd = tempcmd;
  }

  cmd = precmd;

  // convert "exec cmd" to "exec <dmtcp-prefix> cmd"
  if (Util::strStartsWith(postcmd.c_str(), "exec")) {
    cmd += "exec " + prefix + postcmd.substr(strlen("exec"));
  } else {
    cmd += prefix + postcmd;
  }

  // now repack args
  size_t numNewArgs = nargs + 11;
  ASSERT(numNewArgs < maxArgs,
         "not enough argv space for ssh rewrite: needed={} max={}", numNewArgs,
         maxArgs);

  char **new_argv =
    (char **)JALLOC_HELPER_MALLOC(sizeof(char *) * (numNewArgs));
  memset(new_argv, 0, sizeof(char *) * (nargs + 11));

  size_t idx = 0;
  new_argv[idx++] = (char *)dmtcp_ssh_path.c_str();
  if (noStrictChecking) {
    new_argv[idx++] = const_cast<char *>("--noStrictHostKeyChecking");
  }
  if (isRshProcess) {
    new_argv[idx++] = const_cast<char*>("--rsh-slave");
  } else {
    new_argv[idx++] = const_cast<char*>("--ssh-slave");
  }

  new_argv[idx++] = (char*) dmtcp_nocheckpoint_path.c_str();

  string newCommand = string(new_argv[0]) + " " +
                      string(new_argv[1]) + " " + string(new_argv[2]) + " ";
  for (size_t i = 0; i < commandStart; ++i) {
    new_argv[idx++] = (char *)argv[i];
    if (argv[i] != NULL) {
      newCommand += argv[i];
      newCommand += ' ';
    }
  }
  new_argv[idx++] = (char *)cmd.c_str();
  newCommand += cmd + " ";

  for (size_t i = commandStart + 1; i < nargs; ++i) {
    new_argv[idx++] = (char *)argv[i];
    if (argv[i] != NULL) {
      newCommand += argv[i];
      newCommand += ' ';
    }
  }

  ASSERT(idx < maxArgs, "ssh rewritten argv exceeds max: idx={} max={}", idx,
         maxArgs);

  for (size_t i = 0; i <= idx; i++) {
    data->preExec.argv[i] = new_argv[i];
  }
  argv[idx] = NULL;

  strncpy(data->preExec.filename, data->preExec.argv[0], PATH_MAX - 1);

  JALLOC_FREE(new_argv);

  if (isRshProcess) {
    JNOTE("New rsh command") (newCommand);
  } else {
    JNOTE("New ssh command") (newCommand);
  }
}

// This code is copied from dmtcp_coordinator.cpp:calLocalAddr()
static void
updateCoordHost()
{
  if (SharedData::coordHost() != "127.0.0.1") {
    return;
  }

  struct in_addr localhostIPAddr;
  char hostname[HOST_NAME_MAX];

  ASSERT_SYSCALL_SUCCESS_MSG(
    gethostname(hostname, sizeof hostname),
    "reading hostname while updating coordinator host");

  struct addrinfo *result = NULL;
  struct addrinfo *res;
  int error;
  struct addrinfo hints;

  memset(&localhostIPAddr, 0, sizeof localhostIPAddr);
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC; // accept AF_INET and AF_INET6
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  hints.ai_protocol = 0;
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;

  // FROM: Wikipedia:CNAME_record:
  //  When a DNS resolver encounters a CNAME record while looking for a regular
  //  resource record, it will restart the query using the canonical name
  //  instead of the original name. (If the resolver is specifically told to
  //  look for CNAME records, the canonical name (right-hand side) is returned,
  //  rather than restarting the query.)
  hints.ai_flags |= AI_CANONNAME;
  error = getaddrinfo(hostname, NULL, &hints, &result);
  hints.ai_flags ^= AI_CANONNAME;
  if (error == 0 && result) {
    // if hostname was not fully qualified with domainname, replace it with
    // canonname.  Otherwise, keep current alias returned from gethostname().
    if ( Util::strStartsWith(result->ai_canonname, hostname) &&
         result->ai_canonname[strlen(hostname)] == '.' &&
         strlen(result->ai_canonname) < sizeof(hostname) ) {
      strncpy(hostname, result->ai_canonname, sizeof hostname);
    }
    freeaddrinfo(result);
  }
  // OPTIONAL:  If we still don't have a domainname, we could resolve with DNS
  //   (similar to 'man 1 host'), but we ont't know if Internet is present.

  /* resolve the hostname into a list of addresses */
  error = getaddrinfo(hostname, NULL, &hints, &result);
  if (error == 0) {
    /* loop over all returned results and do inverse lookup */
    bool success = false;
    bool at_least_one_match = false;
    char name[NI_MAXHOST] = "";
    for (res = result; res != NULL; res = res->ai_next) {
      struct sockaddr_in *s = (struct sockaddr_in *)res->ai_addr;

      error = getnameinfo(res->ai_addr,
                          res->ai_addrlen,
                          name,
                          NI_MAXHOST,
                          NULL,
                          0,
                          0);
      if (error != 0) {
        JTRACE("getnameinfo() failed.") (gai_strerror(error));
        continue;
      } else {
        ASSERT(sizeof localhostIPAddr == sizeof s->sin_addr,
               "unexpected localhost IP addr size: local={} sockaddr={}",
               sizeof localhostIPAddr, sizeof s->sin_addr);
        if (std::string_view(name) == std::string_view(hostname)) {
          success = true;
          memcpy(&localhostIPAddr, &s->sin_addr, sizeof s->sin_addr);
          break; // Stop here.  We found a matching hostname.
        }
        if (!at_least_one_match) { // Prefer the first match over later ones.
          at_least_one_match = true;
          memcpy(&localhostIPAddr, &s->sin_addr, sizeof s->sin_addr);
        }
      }
    }
    if (result) {
      freeaddrinfo(result);
    }
    if (at_least_one_match) {
      success = true;  // Call it a success even if hostname != name
      if (std::string_view(name) != std::string_view(hostname)) {
        JTRACE("Canonical hostname different from original hostname")
              (name)(hostname);
      }
    }

    WARNING(success,
            "Failed to find coordinator IP address. DMTCP may fail: host={}",
            hostname);
  } else {
    if (error == EAI_SYSTEM) {
      perror("getaddrinfo");
    } else {
      JTRACE("Error in getaddrinfo") (gai_strerror(error));
    }
    inet_aton("127.0.0.1", &localhostIPAddr);
  }

  SharedData::setCoordHost(&localhostIPAddr);
}
