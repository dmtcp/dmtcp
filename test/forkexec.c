#define _POSIX_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static const int WELL_KNOWN_FD = 10;

static void
die(const char *msg)
{
  printf("ERROR: %s \n", msg);
  _exit(-1);
}

int
main(int argc, char **argv)
{
  FILE *fp = NULL;

  if (argc == 1) { // child will have argc == 2, argv[1] == "slave"
    // coordinator

    int fd[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
      die("pipe() failed");
    }

    // char buf[] = "TEST";
    // write(fd[1], "test", sizeof buf);
    // read(fd[0], buf, sizeof buf);
    //
    // printf("%s\n",buf);


    if (fork() == 0) {  // if child
      // oops... the user forgot to close an unused socket!!!
      // close(fd[1]);
      dup2(fd[0], WELL_KNOWN_FD);
      char *t[] = { argv[0], "slave", 0 };

      // BAD USER: just trashed our environment variables:
      char *env[] = { "A=B", "C=E", "LD_PRELOAD=taco", 0, 0 };

      // On some systems, LD_LIBRARY_PATH might point to a required run-time
      // library
      if (getenv("LD_LIBRARY_PATH") != (char *)NULL) {
        static char ldpath[2048];
        sprintf(ldpath, "LD_LIBRARY_PATH=%s", getenv("LD_LIBRARY_PATH"));
        env[3] = ldpath;
      }

      execve(argv[0], t, env);
      perror("execve");
      die("exec failed");
    } else { // else parent
      close(fd[0]);
      dup2(fd[1], WELL_KNOWN_FD);

      char c;

      while (read(0, &c, 1)) {
        // if(c=='\n') c = '\n';
        // printf("sending: %c\n",c);
        while (write(WELL_KNOWN_FD, &c, 1) != 1) {
          continue;
        }
      }

      die("parent done");
    }
  }

  // client
  {
    // char* lineptr = NULL;
    // size_t n = 0;
    fp = fdopen(WELL_KNOWN_FD, "rw");
    if (fp == NULL) {
      die("fdopen failed");
    }

    char c;

    while (read(WELL_KNOWN_FD, &c, 1)) {
      printf(" %c", c); fflush(stdout);
    }

    die("child done");
  }

  return 0;
}
