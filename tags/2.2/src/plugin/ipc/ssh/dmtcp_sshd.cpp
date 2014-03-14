#include <unistd.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "ssh.h"
#include "util_ipc.h"

static pid_t childPid = -1;
static int remotePeerSock = -1;

// Connect to dmtcp_ssh process
static void connectToRemotePeer(char *host, int port)
{
  struct sockaddr_in saddr;
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == -1) {
    perror("Error creating socket: ");
    exit(0);
  }
  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  if (inet_aton(host, &saddr.sin_addr) == -1) {
    perror("inet_aton failed");
    exit(0);
  }
  saddr.sin_port = htons(port);

  if (connect(sock, (sockaddr *)&saddr, sizeof saddr) == -1) {
    perror("Error connecting");
    exit(0);
  }
  remotePeerSock = sock;
}

static void dummySshdProcess(char *listenAddr)
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
  };
  fd = STDOUT_FILENO;
  if (sendFd(sendfd, fd, &fd, sizeof(fd), addr, len) == -1) {
    perror("sendFd failed.");
    abort();
  };
  fd = STDERR_FILENO;
  if (sendFd(sendfd, fd, &fd, sizeof(fd), addr, len) == -1) {
    perror("sendFd failed.");
    abort();
  };
  fd = pipefd[1];
  if (sendFd(sendfd, fd, &fd, sizeof(fd), addr, len) == -1) {
    perror("sendFd failed.");
    abort();
  };
  close(sendfd);

  close(pipefd[1]);

  do {
    // When the original dmtcp_sshd process quits, read would return '0'.
    ret = read(pipefd[0], buf, sizeof(buf));
  } while (ret == -1 && errno == EINTR);
  exit(0);
}

int main(int argc, char *argv[], char *envp[])
{
  int in[2], out[2], err[2];
  char *host;
  int port;

  if (argc < 2) {
    printf("***ERROR: This program shouldn't be used directly.\n");
    exit(1);
  }

  if (strcmp(argv[1], "--listenAddr") == 0) {
    dummySshdProcess(argv[2]);
    printf("ERROR: Not Implemented\n");
    assert(0);
  }

  if (strcmp(argv[1], "--host") != 0) {
    printf("Missing --host argument");
    assert(0);
  }
  host = argv[2];

  if (strcmp(argv[3], "--port") != 0) {
    printf("Missing --port argument");
    exit(0);
  }
  port = atoi(argv[4]);

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

    execvp(argv[5], &argv[5]);
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
                         remotePeerSock, 0);

  client_loop(child_stdinfd, child_stdoutfd, child_stderrfd, remotePeerSock);
  int status;
  pid_t ret = wait(&status);
  assert(ret == childPid);
  return status;
}
