#include "ssh.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include "jassert.h"
#include "jfilesystem.h"
#include "dmtcp.h"
#include "ipc.h"
#include "shareddata.h"
#include "sshdrainer.h"
#include "util.h"
#include "util_ipc.h"

using namespace dmtcp;

#define SSHD_PIPE_FD -1

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

static bool sshPluginEnabled = false;

extern "C" void process_fd_event(int event, int arg1, int arg2 = -1);
void dmtcp_ssh_drain();
void dmtcp_ssh_resume();
void dmtcp_ssh_restart();
static void refill(bool isRestart);
static void sshdReceiveFds();
static void createNewDmtcpSshdProcess();

void
dmtcp_SSH_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_PRESUSPEND:
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


DmtcpPluginDescriptor_t sshPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  DMTCP_PACKAGE_VERSION,
  "ssh",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "SSH plugin",
  dmtcp_SSH_EventHook
};

void
ipc_initialize_plugin_ssh()
{
  dmtcp_register_plugin(sshPlugin);
}

void
dmtcp_ssh_drain()
{
  if (!sshPluginEnabled) {
    return;
  }

  JASSERT(theDrainer == NULL);
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
  theDrainer->monitorSockets(DRAINER_CHECK_FREQ);
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
  JASSERT(data == fd) (data) (fd);
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
  JASSERT(sock.isValid());
  sock.changeFd(SSHD_RECEIVE_FD);
  fdReceiveAddr.sun_family = AF_UNIX;
  JASSERT(_real_bind(SSHD_RECEIVE_FD,
                     (struct sockaddr *)&fdReceiveAddr,
                     sizeof(fdReceiveAddr.sun_family)) == 0) (JASSERT_ERRNO);

  fdReceiveAddrLen = sizeof(fdReceiveAddr);
  JASSERT(getsockname(SSHD_RECEIVE_FD,
                      (struct sockaddr *)&fdReceiveAddr,
                      &fdReceiveAddrLen) == 0);

  // Send this information to dmtcp_ssh process
  ssize_t ret = write(sshSockFd, &fdReceiveAddrLen, sizeof(fdReceiveAddrLen));
  JASSERT(ret == sizeof(fdReceiveAddrLen)) (sshSockFd) (ret) (JASSERT_ERRNO);
  ret = write(sshSockFd, &fdReceiveAddr, fdReceiveAddrLen);
  JASSERT(ret == (ssize_t)fdReceiveAddrLen);

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

  ssize_t ret = read(sshSockFd, &addrLen, sizeof(addrLen));

  JASSERT(ret == sizeof(addrLen));
  memset(&addr, 0, sizeof(addr));
  ret = read(sshSockFd, &addr, addrLen);
  JASSERT(ret == (ssize_t)addrLen);
  JASSERT(strlen(&addr.sun_path[1]) < sizeof(abstractSockName));
  strcpy(abstractSockName, &addr.sun_path[1]);

  struct sockaddr_in sshdSockAddr;
  socklen_t sshdSockAddrLen = sizeof(sshdSockAddr);
  char remoteHost[80];
  JASSERT(getpeername(sshSockFd, (struct sockaddr *)&sshdSockAddr,
                      &sshdSockAddrLen) == 0);
  char *ip = inet_ntoa(sshdSockAddr.sin_addr);
  strcpy(remoteHost, ip);

  if (dmtcp_nocheckpoint_path.length() == 0) {
    dmtcp_nocheckpoint_path = Util::getPath("dmtcp_nocheckpoint");
    dmtcp_sshd_path = Util::getPath("dmtcp_sshd");
  }


  JASSERT(pipe(in) == 0) (JASSERT_ERRNO);
  JASSERT(pipe(out) == 0) (JASSERT_ERRNO);
  JASSERT(pipe(err) == 0) (JASSERT_ERRNO);

  pid_t sshChildPid = fork();
  JASSERT(sshChildPid != -1);
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
    JASSERT(idx < max_args) (idx);

    process_fd_event(SYS_close, in[1]);
    process_fd_event(SYS_close, out[0]);
    process_fd_event(SYS_close, err[0]);
    dup2(in[0], STDIN_FILENO);
    dup2(out[1], STDOUT_FILENO);
    dup2(err[1], STDERR_FILENO);

    JTRACE("Launching ")
      (argv[0]) (argv[1]) (argv[2]) (argv[3]) (argv[4]) (argv[5]);
    _real_execvp(argv[0], argv);
    JASSERT(false);
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

  process_fd_event(SYS_close, sshStdin);
  process_fd_event(SYS_close, sshStdout);
  process_fd_event(SYS_close, sshStderr);
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
    process_fd_event(SYS_close, STDIN_FILENO);
    process_fd_event(SYS_close, STDOUT_FILENO);
    process_fd_event(SYS_close, STDERR_FILENO);
  } else { // dmtcp_ssh
    process_fd_event(SYS_close, in);
    process_fd_event(SYS_close, out);
    process_fd_event(SYS_close, err);
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
prepareForExec(char *const argv[], char ***newArgv)
{
  size_t nargs = 0;
  bool noStrictChecking = false;
  string precmd, postcmd, tempcmd;

  while (argv[nargs++] != NULL) {}

  if (nargs < 3) {
    if (!isRshProcess) {
    JNOTE("ssh with less than 3 args") (argv[0]) (argv[1]);
    *newArgv = (char **)argv;
    return;
    } else if (nargs < 2) {
        JNOTE("rsh with less than 2 args") (argv[0]);
        *newArgv = (char**) argv;
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
  JASSERT(commandStart < nargs && argv[commandStart][0] != '-')
    (commandStart) (nargs) (argv[commandStart])
  .Text("failed to parse ssh command line");

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
  char **new_argv =
    (char **)JALLOC_HELPER_MALLOC(sizeof(char *) * (nargs + 11));
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
  if (isRshProcess) {
    JNOTE("New rsh command") (newCommand);
  } else {
    JNOTE("New ssh command") (newCommand);
  }
  *newArgv = new_argv;
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

  JASSERT(gethostname(hostname, sizeof hostname) == 0) (JASSERT_ERRNO);

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
        JASSERT(sizeof localhostIPAddr == sizeof s->sin_addr);
        if ( strncmp( name, hostname, sizeof hostname ) == 0 ) {
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
      if ( strncmp( name, hostname, sizeof hostname ) != 0 ) {
        JTRACE("Canonical hostname different from original hostname")
              (name)(hostname);
      }
    }

    JWARNING(success) (hostname)
      .Text("Failed to find coordinator IP address.  DMTCP may fail.");
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

/*
 * Side-effect: Modifies the global isRshProcess variable
 */
static bool isRshOrSshProcess(const char *filename)
{
  bool isSshProcess = (jalib::Filesystem::BaseName(filename) == "ssh");
  isRshProcess = (jalib::Filesystem::BaseName(filename) == "rsh");

  return (isSshProcess || isRshProcess);
}

extern "C" int execve (const char *filename, char *const argv[],
                       char *const envp[])
{
  if (!isRshOrSshProcess(filename)) {
    return _real_execve(filename, argv, envp);
  }

  updateCoordHost();

  char **newArgv = NULL;
  prepareForExec(argv, &newArgv);
  int ret = _real_execve(newArgv[0], newArgv, envp);
  JALLOC_HELPER_FREE(newArgv);
  return ret;
}

extern "C" int
execvp(const char *filename, char *const argv[])
{
  if (!isRshOrSshProcess(filename)) {
    return _real_execvp(filename, argv);
  }

  updateCoordHost();

  char **newArgv = NULL;
  prepareForExec(argv, &newArgv);
  int ret = _real_execvp(newArgv[0], newArgv);
  JALLOC_HELPER_FREE(newArgv);
  return ret;
}

// This function first appeared in glibc 2.11
extern "C" int
execvpe(const char *filename, char *const argv[], char *const envp[])
{
  if (!isRshOrSshProcess(filename)) {
    return _real_execvpe(filename, argv, envp);
  }

  updateCoordHost();

  char **newArgv = NULL;
  prepareForExec(argv, &newArgv);
  int ret = _real_execvpe(newArgv[0], newArgv, envp);
  JALLOC_HELPER_FREE(newArgv);
  return ret;
}
