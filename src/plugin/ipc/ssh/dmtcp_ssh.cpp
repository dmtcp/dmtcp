#include <unistd.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <linux/limits.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "ssh.h"

static int listenSock = -1;
static int noStrictHostKeyChecking = 0;

extern "C" void dmtcp_get_local_ip_addr(struct in_addr *addr) __attribute((weak));

//static bool strEndsWith(const char *str, const char *pattern)
//{
//  assert(str != NULL && pattern != NULL);
//  int len1 = strlen(str);
//  int len2 = strlen(pattern);
//  if (len1 >= len2) {
//    size_t idx = len1 - len2;
//    return strncmp(str+idx, pattern, len2) == 0;
//  }
//  return false;
//}

static int getport(int fd)
{
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof(addr);
  if (getsockname(fd, (struct sockaddr *)&addr, &addrlen) == -1) {
    return -1;
  }
  return (int)ntohs(addr.sin_port);
}

static void createStdioFds(int *in, int *out, int *err)
{
  struct stat buf;
  if (fstat(STDIN_FILENO,  &buf)  == -1) {
    int fd = open("/dev/null", O_RDWR);
    if (fd != STDIN_FILENO) {
      dup2(fd, STDIN_FILENO);
      close(fd);
    }
  }
  if (fstat(STDOUT_FILENO,  &buf)  == -1) {
    int fd = open("/dev/null", O_RDWR);
    if (fd != STDOUT_FILENO) {
      dup2(fd, STDOUT_FILENO);
      close(fd);
    }
  }
  if (fstat(STDERR_FILENO,  &buf)  == -1) {
    int fd = open("/dev/null", O_RDWR);
    if (fd != STDERR_FILENO) {
      dup2(fd, STDERR_FILENO);
      close(fd);
    }
  }

  // Close all open file descriptors
  int maxfd = sysconf(_SC_OPEN_MAX);
  for (int i = 3; i < maxfd; i++) {
    close(i);
  }

  if (pipe(in) != 0) {
    perror("Error creating pipe: ");
  }
  if (pipe(out) != 0) {
    perror("Error creating pipe: ");
  }
  if (pipe(err) != 0) {
    perror("Error creating pipe: ");
  }
}

static int openListenSocket()
{
  struct sockaddr_in saddr;
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == -1) {
    perror("Error creating socket: ");
  }
  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_addr.s_addr = INADDR_ANY;
  saddr.sin_port = 0;
  if (bind(sock, (struct sockaddr*) &saddr, sizeof saddr) == -1) {
    perror("Error binding socket");
  }

  if (listen(sock, 1) == -1) {
    perror("Error binding socket");
  }
  return sock;
}

static void signal_handler(int sig)
{
  if (sig == SIGCHLD) {
    int status;
    wait(&status);
    exit(status);
  }
}

static int waitForConnection(int listenSock)
{
  int fd = accept(listenSock, NULL, NULL);
  if (fd == -1) {
    perror("accept failed:");
    abort();
    exit(0);
  }
  close(listenSock);
  return fd;
}

int main(int argc, char *argv[], char *envp[])
{
  int in[2], out[2], err[2];
  int status;
  int ssh_stdinfd, ssh_stdoutfd, ssh_stderrfd;

  if (argc < 2) {
    printf("***ERROR: This program shouldn't be used directly.\n");
    exit(1);
  }

  if (strcmp(argv[1], "--noStrictHostKeyChecking") == 0) {
    noStrictHostKeyChecking = 1;
    argv++;
  }

  createStdioFds(in, out, err);
  listenSock = openListenSocket();
  signal(SIGCHLD, signal_handler);

  pid_t sshChildPid = fork();
  if (sshChildPid == 0) {
    char buf[PATH_MAX + 80];
    char hostname[80];
    int port = getport(listenSock);
    close(listenSock);

    close(in[1]);
    close(out[0]);
    close(err[0]);
    dup2(in[0], STDIN_FILENO);
    dup2(out[1], STDOUT_FILENO);
    dup2(err[1], STDERR_FILENO);

    unsetenv("LD_PRELOAD");

    // Replace dmtcp_sshd replace with "dmtcp_sshd --host <host> --port <port>"
    struct in_addr saddr;
    if (dmtcp_get_local_ip_addr == NULL) {
      printf("ERROR: Unable to find dmtcp_get_local_ip_addr.\n");
      abort();
    }
    dmtcp_get_local_ip_addr(&saddr);
    char *hostip = inet_ntoa(saddr);
    strcpy(hostname, hostip);

    size_t i = 0;
    while (argv[i] != NULL) {
      // "dmtcp_sshd" may be embedded deep inside the command line.
      char *ptr = strstr(argv[i], SSHD_BINARY);
      if (ptr != NULL) {
        ptr += strlen(SSHD_BINARY);
        if (*ptr != '\0') {
          *ptr = '\0';
          ptr++;
        }
        sprintf(buf, "%s --host %s --port %d %s",
                argv[i], hostip, port, ptr);
        argv[i] = buf;
      }
      i++;
    }
    execvp(argv[1], &argv[1]);
    printf("%s:%d DMTCP Error detected. Failed to exec.", __FILE__, __LINE__);
    abort();
  }

  int childSock = waitForConnection(listenSock);

  close(in[0]);
  close(out[1]);
  close(err[1]);

  ssh_stdinfd = in[1];
  ssh_stdoutfd = out[0];
  ssh_stderrfd = err[0];

  assert(dmtcp_ssh_register_fds != NULL);
  dmtcp_ssh_register_fds(false, ssh_stdinfd, ssh_stdoutfd, ssh_stderrfd,
                         childSock, noStrictHostKeyChecking);

  client_loop(ssh_stdinfd, ssh_stdoutfd, ssh_stderrfd, childSock);
  wait(&status);
  return status;
}
