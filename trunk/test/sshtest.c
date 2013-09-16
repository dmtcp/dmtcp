#include<stdio.h>
#include<unistd.h>
#include<errno.h>
#include<sys/types.h>
#include<sys/wait.h>

int main(int argc, char *argv[])
{
  int in[2], out[2], err[2];

  if (pipe(in) == -1) {
    perror("pipe(in) failed");
  }
  if (pipe(out) == -1) {
    perror("pipe(out) failed");
  }
  if (pipe(err) == -1) {
    perror("pipe(err) failed");
  }

  pid_t child = fork();
  if (child == -1) {
    perror("fork failed");
  }

  if (child == 0) {
    close(in[1]);
    close(out[0]);
    close(err[0]);
    if (dup2(in[0], STDIN_FILENO) == -1) {
      perror("dup failed");
    }
    if (dup2(out[1], STDOUT_FILENO) == -1) {
      perror("dup failed");
    }
    if (dup2(err[1], STDERR_FILENO) == -1) {
      perror("dup failed");
    }

    close(in[0]);
    close(out[1]);
    close(err[1]);

    char *argv[] = {"/usr/bin/ssh", "localhost", "sleep", "100", NULL};
    execv(argv[0], argv);
    perror("execv failed");
  } else {
    close(in[0]);
    close(out[1]);
    close(err[1]);
    char buf[4096];
    ssize_t wrt;
    ssize_t rt = read(out[0], buf, 4096);
    if (rt > 0) {
      wrt = write(STDOUT_FILENO, buf, rt);
      if (wrt == -1 && errno != EINTR) {
        perror("write failed.");
      }
    }
    rt = read(err[0], buf, 4096);
    if (rt > 0) {
      wrt = write(STDERR_FILENO, buf, rt);
      if (wrt == -1 && errno != EINTR) {
        perror("write failed.");
      }
    }
  }
  wait(NULL);
  return 0;
}
