#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "ssh.h"
#include "util.h"
#include "../../../constants.h" // Needed for ENV_VAR_REMOTE_SHELL_CMD
#include "util_ipc.h"

using dmtcp::Util::sendFd;

static pid_t childPid = -1;
static int remotePeerSock = -1;
static int isRshProcess = 0;

// Connect to dmtcp_ssh process
static void
connectToRemotePeer(char *host, int port)
{
  struct sockaddr_in saddr;
  int sock = socket(AF_INET, SOCK_STREAM, 0);

  if (sock == -1) {
    perror("Error creating socket: ");
    exit(DMTCP_FAIL_RC);
  }
  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  if (inet_aton(host, &saddr.sin_addr) == -1) {
    perror("inet_aton failed");
    exit(DMTCP_FAIL_RC);
  }
  saddr.sin_port = htons(port);

  if (connect(sock, (sockaddr *)&saddr, sizeof saddr) == -1) {
    perror("Error connecting");
    exit(DMTCP_FAIL_RC);
  }
  remotePeerSock = sock;
}

static void
dummySshdProcess(char *listenAddr)
{
  int fd;
  struct sockaddr_un addr;
  socklen_t len;

  addr.sun_family = AF_UNIX;
  addr.sun_path[0] = '\0';
  strcpy(&addr.sun_path[1], listenAddr);
  len = sizeof(addr.sun_family) + strlen(listenAddr) + 1;
  int pipefd[2];
  char buf[16];
  ssize_t ret;

  if (pipe(pipefd) == -1) {
    perror("pipe failed.");
    abort();
  }

  int sendfd = socket(AF_UNIX, SOCK_DGRAM, 0);
  fd = STDIN_FILENO;
  if (sendFd(sendfd, fd, &fd, sizeof(fd), addr, len) == -1) {
    perror("sendFd failed.");
    abort();
  }
  fd = STDOUT_FILENO;
  if (sendFd(sendfd, fd, &fd, sizeof(fd), addr, len) == -1) {
    perror("sendFd failed.");
    abort();
  }
  fd = STDERR_FILENO;
  if (sendFd(sendfd, fd, &fd, sizeof(fd), addr, len) == -1) {
    perror("sendFd failed.");
    abort();
  }
  fd = pipefd[1];
  if (sendFd(sendfd, fd, &fd, sizeof(fd), addr, len) == -1) {
    perror("sendFd failed.");
    abort();
  }
  close(sendfd);

  close(pipefd[1]);

  do {
    // When the original dmtcp_sshd process quits, read would return '0'.
    ret = read(pipefd[0], buf, sizeof(buf));
  } while (ret == -1 && errno == EINTR);
  exit(0);
}

// shift args
#define shift argc--, argv++

int main(int argc, char *argv[], char *envp[])
{
  int in[2], out[2], err[2];
  char *host = NULL;
  int port = 0;

  if (argc < 2) {
    printf("***ERROR: This program shouldn't be used directly.\n");
    exit(DMTCP_FAIL_RC);
  }

  shift;
  while (true) {
    if (strcmp(argv[0], "--listenAddr") == 0) {
      dummySshdProcess(argv[1]);
      printf("ERROR: Not Implemented\n");
      assert(0);
      shift; shift;
    } else if (strcmp(argv[0], "--host") == 0) {
      host = argv[1];
      shift; shift;
    } else if (strcmp(argv[0], "--port") == 0) {
      port = atoi(argv[1]);
      shift; shift;
    } else if (strcmp(argv[0], "--ssh-slave") == 0) {
      isRshProcess = 0;
      shift;
    } else if (strcmp(argv[0], "--rsh-slave") == 0) {
      isRshProcess = 1;
      shift;
    } else {
      break;
    }
  }

  if (host == NULL) {
    printf("Missing --host argument");
    exit(DMTCP_FAIL_RC);
  }

  if (port == 0) {
    printf("Missing --port argument");
    exit(DMTCP_FAIL_RC);
  }

  connectToRemotePeer(host, port);

  if (pipe(in) != 0) {
    perror("Error creating pipe: ");
  }
  if (pipe(out) != 0) {
    perror("Error creating pipe: ");
  }
  if (pipe(err) != 0) {
    perror("Error creating pipe: ");
  }

  /* Checkpoint database should have information about which command rsh/ssh
   * lauched the daemon orginally on remote host. This information is required
   * at 2 places, first in restart script which will launch the user process/
   * dmtcp_sshd on remote host using the same command. Secondly launching
   * dummy daemon which will launched by the same command.
   */

  if (isRshProcess) {
    setenv(ENV_VAR_REMOTE_SHELL_CMD, "rsh", 1);
  } else {
    setenv(ENV_VAR_REMOTE_SHELL_CMD, "ssh", 1);
  }

  childPid = fork();
  if (childPid == 0) {
    close(remotePeerSock);
    close(in[1]);
    close(out[0]);
    close(err[0]);
    dup2(in[0], STDIN_FILENO);
    dup2(out[1], STDOUT_FILENO);
    dup2(err[1], STDERR_FILENO);
    close(in[0]);
    close(out[1]);
    close(err[1]);

    execvp(argv[0], &argv[0]);
    printf("%s:%d DMTCP Error detected. Failed to exec.", __FILE__, __LINE__);
    abort();
  }

  close(in[0]);
  close(out[1]);
  close(err[1]);

  int child_stdinfd = in[1];
  int child_stdoutfd = out[0];
  int child_stderrfd = err[0];

  assert(dmtcp_ssh_register_fds);
  dmtcp_ssh_register_fds(true, child_stdinfd, child_stdoutfd, child_stderrfd,
                         remotePeerSock, 0, isRshProcess);

  client_loop(child_stdinfd, child_stdoutfd, child_stderrfd, remotePeerSock);
  int status;
  pid_t ret = wait(&status);
  assert(ret == childPid);
  return status;
}
