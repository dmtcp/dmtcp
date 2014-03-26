#include <sys/syscall.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include "dmtcp.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "ipc.h"
#include "util_ipc.h"
#include "ssh.h"
#include "sshdrainer.h"

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

static bool sshPluginEnabled = false;

LIB_PRIVATE void process_fd_event(int event, int arg1, int arg2 = -1);
static void drain();
static void refill(bool isRestart);
static void sshdReceiveFds();
static void createNewDmtcpSshdProcess();

void dmtcp_SSH_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  if (!sshPluginEnabled) return;
  switch (event) {
    case DMTCP_EVENT_DRAIN:
      drain();
      break;

    case DMTCP_EVENT_THREADS_RESUME:
      refill(data->refillInfo.isRestart);
      break;

    default:
      break;
  }
}

static void drain()
{
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

static void refill(bool isRestart)
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

static void receiveFileDescr(int fd)
{
  int data;
  int ret = receiveFd(SSHD_RECEIVE_FD, &data, sizeof(data));
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

static void sshdReceiveFds()
{
  // Add receive-fd data socket.
  static struct sockaddr_un fdReceiveAddr;
  static socklen_t         fdReceiveAddrLen;

  memset(&fdReceiveAddr, 0, sizeof(fdReceiveAddr));
  jalib::JSocket sock(_real_socket(AF_UNIX, SOCK_DGRAM, 0));
  JASSERT(sock.isValid());
  sock.changeFd(SSHD_RECEIVE_FD);
  fdReceiveAddr.sun_family = AF_UNIX;
  JASSERT(_real_bind(SSHD_RECEIVE_FD,
                     (struct sockaddr*) &fdReceiveAddr,
                     sizeof(fdReceiveAddr.sun_family)) == 0) (JASSERT_ERRNO);

  fdReceiveAddrLen = sizeof(fdReceiveAddr);
  JASSERT(getsockname(SSHD_RECEIVE_FD,
                      (struct sockaddr *)&fdReceiveAddr,
                      &fdReceiveAddrLen) == 0);

  // Send this information to dmtcp_ssh process
  ssize_t ret = write(sshSockFd, &fdReceiveAddrLen, sizeof(fdReceiveAddrLen));
  JASSERT(ret == sizeof(fdReceiveAddrLen)) (sshSockFd) (ret) (JASSERT_ERRNO);
  ret = write(sshSockFd, &fdReceiveAddr, fdReceiveAddrLen);
  JASSERT(ret == (ssize_t) fdReceiveAddrLen);

  // Now receive fds
  receiveFileDescr(STDIN_FILENO);
  receiveFileDescr(STDOUT_FILENO);
  receiveFileDescr(STDERR_FILENO);
  receiveFileDescr(SSHD_PIPE_FD);
  _real_close(SSHD_RECEIVE_FD);
}

static void createNewDmtcpSshdProcess()
{
  struct sockaddr_un addr;
  socklen_t          addrLen;
  static char abstractSockName[20];
  int in[2], out[2], err[2];

  ssize_t ret = read(sshSockFd, &addrLen, sizeof(addrLen));
  JASSERT(ret == sizeof(addrLen));
  memset(&addr, 0, sizeof(addr));
  ret = read(sshSockFd, &addr, addrLen);
  JASSERT(ret == (ssize_t) addrLen);
  JASSERT(strlen(&addr.sun_path[1]) < sizeof(abstractSockName));
  strcpy(abstractSockName, &addr.sun_path[1]);

  struct sockaddr_in sshdSockAddr;
  socklen_t sshdSockAddrLen = sizeof(sshdSockAddr);
  char remoteHost[80];
  JASSERT(getpeername(sshSockFd, (struct sockaddr*)&sshdSockAddr,
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
    argv[idx++] = const_cast<char*>("ssh");
    if (noStrictHostKeyChecking) {
      argv[idx++] = const_cast<char*>("-o");
      argv[idx++] = const_cast<char*>("StrictHostKeyChecking=no");
    }
    argv[idx++] = remoteHost;
    argv[idx++] = (char*) dmtcp_sshd_path.c_str();
    argv[idx++] = const_cast<char*>("--listenAddr");
    argv[idx++] = abstractSockName;
    argv[idx++] = NULL;
    JASSERT(idx < max_args) (idx);

    process_fd_event(SYS_close, in[1]);
    process_fd_event(SYS_close, out[0]);
    process_fd_event(SYS_close, err[0]);
    dup2(in[0], STDIN_FILENO);
    dup2(out[1], STDOUT_FILENO);
    dup2(err[1], STDERR_FILENO);

    JTRACE("Launching ") (argv[0]) (argv[1]) (argv[2]) (argv[3]) (argv[4]) (argv[5]);
    _real_execvp(argv[0], argv);
    JASSERT(false);
  }

  dup2(in[1],  500 + sshStdin);
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

extern "C" void dmtcp_ssh_register_fds(int isSshd, int in, int out, int err,
                                       int sock, int noStrictChecking)
{
  if (isSshd) { // dmtcp_sshd
    process_fd_event(SYS_close, STDIN_FILENO);
    process_fd_event(SYS_close, STDOUT_FILENO);
    process_fd_event(SYS_close, STDERR_FILENO);
  } else { // dmtcp_ssh
    process_fd_event(SYS_close, in);
    process_fd_event(SYS_close, out);
    process_fd_event(SYS_close, err);
  }
  sshStdin = in;
  sshStdout = out;
  sshStderr = err;
  sshSockFd = sock;
  isSshdProcess = isSshd;
  sshPluginEnabled = true;
  noStrictHostKeyChecking = noStrictChecking;
}

static void prepareForExec(char *const argv[], char ***newArgv)
{
  size_t nargs = 0;
  bool noStrictChecking = false;
  dmtcp::string precmd, postcmd, tempcmd;
  while (argv[nargs++] != NULL);

  if (nargs < 3) {
    JNOTE("ssh with less than 3 args") (argv[0]) (argv[1]);
    *newArgv = (char**) argv;
    return;
  }

  //find command part
  size_t commandStart = 2;
  for (size_t i = 1; i < nargs; ++i) {
    if (strcmp(argv[i], "-o") == 0) {
      if (strcmp(argv[i+1], "StrictHostKeyChecking=no") == 0) {
        noStrictChecking = true;
      }
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

  vector<string> dmtcp_args;
  Util::getDmtcpArgs(dmtcp_args);

  dmtcp_launch_path = Util::getPath("dmtcp_launch");
  dmtcp_ssh_path = Util::getPath("dmtcp_ssh");
  dmtcp_sshd_path = Util::getPath("dmtcp_sshd");
  dmtcp_nocheckpoint_path = Util::getPath("dmtcp_nocheckpoint");

  prefix = dmtcp_launch_path + " --ssh-slave ";
  for(size_t i = 0; i < dmtcp_args.size(); i++){
    prefix += dmtcp_args[i] + " ";
  }
  prefix += dmtcp_sshd_path + " ";
  JTRACE("Prefix")(prefix);

  // process command
  size_t semipos, pos;
  size_t actpos = string::npos;
  tempcmd = argv[commandStart];
  for(semipos = 0; (pos = tempcmd.find(';',semipos+1)) != string::npos;
      semipos = pos, actpos = pos);

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
  if (Util::strStartsWith(postcmd, "exec")) {
    cmd += "exec " + prefix + postcmd.substr(strlen("exec"));
  } else {
    cmd += prefix + postcmd;
  }

  //now repack args
  char** new_argv = (char**) JALLOC_HELPER_MALLOC(sizeof(char*) * (nargs + 10));
  memset(new_argv, 0, sizeof(char*) * (nargs + 10));

  size_t idx = 0;
  new_argv[idx++] = (char*) dmtcp_ssh_path.c_str();
  if (noStrictChecking) {
    new_argv[idx++] = const_cast<char*>("--noStrictHostKeyChecking");
  }
  new_argv[idx++] = (char*) dmtcp_nocheckpoint_path.c_str();

  string newCommand = string(new_argv[0]) + " " + string(new_argv[1]) + " ";
  for (size_t i = 0; i < commandStart; ++i) {
    new_argv[idx++] = ( char* ) argv[i];
    if (argv[i] != NULL) {
      newCommand += argv[i];
      newCommand += ' ';
    }
  }
  new_argv[idx++] = (char*) cmd.c_str();
  newCommand += cmd + " ";

  for (size_t i = commandStart + 1; i < nargs; ++i) {
    new_argv[idx++] = (char*) argv[i];
    if (argv[i] != NULL) {
      newCommand += argv[i];
      newCommand += ' ';
    }
  }
  JNOTE("New ssh command") (newCommand);
  *newArgv = new_argv;
  return;
}

extern "C" int execve (const char *filename, char *const argv[],
                       char *const envp[])
{
  if (jalib::Filesystem::BaseName(filename) != "ssh") {
    return _real_execve(filename, argv, envp);
  }

  char **newArgv = NULL;
  prepareForExec(argv, &newArgv);
  int ret = _real_execve (newArgv[0], newArgv, envp);
  JALLOC_HELPER_FREE(newArgv);
  return ret;
}

extern "C" int execvp (const char *filename, char *const argv[])
{
  if (jalib::Filesystem::BaseName(filename) != "ssh") {
    return _real_execvp(filename, argv);
  }

  char **newArgv;
  prepareForExec(argv, &newArgv);
  int ret = _real_execvp (newArgv[0], newArgv);
  JALLOC_HELPER_FREE(newArgv);
  return ret;
}

// This function first appeared in glibc 2.11
extern "C" int execvpe (const char *filename, char *const argv[],
                         char *const envp[])
{
  if (jalib::Filesystem::BaseName(filename) != "ssh") {
    return _real_execvpe(filename, argv, envp);
  }
  char **newArgv;
  prepareForExec(argv, &newArgv);
  int ret = _real_execvpe(newArgv[0], newArgv, envp);
  JALLOC_HELPER_FREE(newArgv);
  return ret;
}
