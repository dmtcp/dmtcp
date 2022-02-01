#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct PipesAndCmd {
  int fds[2];
  char *command;
} PipesAndCmd;

static int sn_popen_pid;

int
sn_simple_popen_r_cfn(PipesAndCmd pipesAndCmd)
{
  dup2(pipesAndCmd.fds[1], STDOUT_FILENO);
  close(pipesAndCmd.fds[1]);
  close(pipesAndCmd.fds[0]);

  execl("/bin/sh", "sh", "-c", pipesAndCmd.command, (char *)NULL);
  _exit(-1); // Should never get here
}

FILE *
sn_simple_popen_r(char *command)
{
  FILE *result;
  PipesAndCmd pipesAndCmd;

  if (pipe(pipesAndCmd.fds) < 0)
    return NULL;

  pipesAndCmd.command = command;

  if (-1 == (sn_popen_pid = vfork())) {
    close(pipesAndCmd.fds[0]);
    close(pipesAndCmd.fds[1]);
    return NULL;
  } else if (sn_popen_pid == 0) {
    sn_simple_popen_r_cfn(pipesAndCmd);
  } else {
    result = fdopen(pipesAndCmd.fds[0], "r");
    close(pipesAndCmd.fds[1]);
  }

  return result;
}


void
sn_simple_pclose_r(FILE *p)
{
  int stat = waitpid(sn_popen_pid, NULL, WNOHANG);
  if (stat == 0) {
    kill(sn_popen_pid, SIGKILL);

    while (-1 == waitpid(sn_popen_pid, NULL, 0)) {
      if (errno != EINTR)
        break;
    }
  }

  sn_popen_pid = 0;
  fclose(p);
}

int main(int argc, char *argv[])
{
  char *command = "ls | wc";

  if (argc > 1) {
    command = argv[1];
  }

  while (1) {
    FILE *fp = sn_simple_popen_r(command);
    assert(fp);

    while (!feof(fp)) {
      char buf;
      fread(&buf, 1, 1, fp);
      printf("%c", buf);
      fflush(stdout);
    }
    sn_simple_pclose_r(fp);
    sleep(1);
  }

  return 1;
}