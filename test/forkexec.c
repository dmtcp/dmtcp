#define _POSIX_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static const int WELL_KNOWN_FD = 10;

void processChild();
void processParent();

static void
die(const char *msg)
{
  printf("ERROR: %s \n", msg);
  _exit(-1);
}

int fd[2];

int
main(int argc, char **argv)
{
  FILE *fp = NULL;
  int commandIdx = 0;

  // parse arguments.
  // --child indicates child process.
  // --command indicates command to execute.
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--child") == 0) {
      processChild();
      exit(0);
    }
    if (strcmp(argv[i], "--command") == 0) {
      assert(argv[i + 1] != NULL);
      commandIdx = i + 1;
      i++;
    }
  }

  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
    die("pipe() failed");
  }

  if (fork() == 0) {  // if child
    // oops... the user forgot to close an unused socket!!!
    // close(fd[1]);
    dup2(fd[0], WELL_KNOWN_FD);
    
    char *t[] = { argv[0], "--child", 0 };
    // BAD USER: just trashed our environment variables:
    char *env[] = { "A=B", "C=E", "LD_PRELOAD=taco", 0, 0 };
    
    // On some systems, LD_LIBRARY_PATH might point to a required run-time
    // library
    if (getenv("LD_LIBRARY_PATH") != (char *)NULL) {
      static char ldpath[2048];
      sprintf(ldpath, "LD_LIBRARY_PATH=%s", getenv("LD_LIBRARY_PATH"));
      env[3] = ldpath;
    }
    
    // Try to trigger a execve failure.
    assert(execve("/some/random/binary", t, env) != 0);

    // Exec into child binary.
    if (commandIdx != 0) {
      execve(argv[commandIdx], argv + commandIdx, env);
    } else {
      execve(argv[0], t, env);
    }

    perror("execve");
    die("exec failed");
  } 

  processParent();
}

void processParent()
{
  close(fd[0]);
  dup2(fd[1], WELL_KNOWN_FD);

  char c;

  while (read(0, &c, 1)) {
    while (write(WELL_KNOWN_FD, &c, 1) != 1) {
      continue;
    }
  }

  die("parent done");
}

void processChild()
{
  FILE *fp = fdopen(WELL_KNOWN_FD, "rw");
  if (fp == NULL) {
    die("fdopen failed");
  }

  char c;

  while (read(WELL_KNOWN_FD, &c, 1)) {
    printf(" %c", c); fflush(stdout);
  }

  die("child done");
}
