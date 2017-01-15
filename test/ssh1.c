#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
  char *hostname = "localhost";

  char buf[4096];
  strcpy(buf, argv[0]);
  dirname(buf);
  strcat(buf, "/dmtcp1");
  char *dmtcp1Path = realpath(buf, NULL);

  int in[2], out[2], err[2];

  if (argc > 1) {
    hostname = argv[1];
  }

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

    char *argv[] = {
      "/usr/bin/ssh",
      "-o",
      "StrictHostKeyChecking=no",
      hostname,
      dmtcp1Path,
      NULL
    };
    execv(argv[0], argv);
    perror("execv failed");
  } else {
    close(in[0]);
    close(out[1]);
    close(err[1]);
    char buf[4096];
    while (1) {
      ssize_t rt = read(out[0], buf, 4096);
      ssize_t wrt;
      if (rt > 0) {
        wrt = write(STDOUT_FILENO, buf, rt);
        if (wrt == -1 && errno != EINTR) {
          perror("write failed.");
          exit(0);
        }
      }
    }
  }
  wait(NULL);
  return 0;
}
