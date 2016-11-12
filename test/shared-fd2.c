#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// This example is related to a DMTCP bug found when MVAPICH is run
// directly under ibrun/mpirun_rsh (for example, at Stampede).
// The two child processes share a fd, and the leader may not realize
// that the fd is shared, and so it may fail to send the fd over
// the UNIX domain socket to the follower.
// In the case of ibrun, the parent process is on a remote host, and it
// is _not_ under the control of DMTCP.  It uses ssh to spawn a process
// on the local host (e.g., CHILD1).  Presumably, CHILD2 is created through
// fork and exec, and this is how the fd ends up being shared.

enum proc { PARENT, CHILD1, CHILD2 };

int
main(int argc, char *argv[])
{
  enum proc thisProc;
  const char *me;
  int fd;

  if (argc == 1) {
    thisProc = PARENT;
  } else if (argc == 4 && atoi(argv[1]) == CHILD1 && !strcmp(argv[2], "-fd")) {
    thisProc = CHILD1;
    me = "child1";
    fd = atoi(argv[3]);
  } else if (argc == 4 && atoi(argv[1]) == CHILD2 && !strcmp(argv[2], "-fd")) {
    thisProc = CHILD2;
    me = "child2";
    fd = atoi(argv[3]);
  } else {
    fprintf(stderr, "ERROR(%s):  incorrect arguments.\n", argv[0]);
    exit(1);
  }

  if (thisProc == PARENT) {
    int sockets[2];
    char child[10];
    char fd_str[10];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0) {
      perror("socketpair");
      return 1;
    }

    if (fork() == 0) { // exec to CHILD1
      char *childArgv[] = { argv[0], child, "-fd", fd_str, NULL };
      sprintf(child, "%d", CHILD1);
      sprintf(fd_str, "%d", sockets[0]);
      execvp(argv[0], childArgv);
    }

    if (fork() == 0) { // exec to CHILD2
      char *childArgv[] = { argv[0], child, "-fd", fd_str, NULL };
      sprintf(child, "%d", CHILD2);
      sprintf(fd_str, "%d", sockets[1]);
      execvp(argv[0], childArgv);
    }

    return 0; // Parent exits
  }

  // Now the children do the work.
  int count = 1;
  char buf[32];

  if (thisProc == CHILD1) {
    while (1) {
      sprintf(buf, "%s(%d)", me, count++);
      if (write(fd, buf, sizeof(buf)) != sizeof(buf)) {
        break;
      }
      sleep(2);
    }
  } else if (thisProc == CHILD2) {
    while (1) {
      if (read(fd, buf, sizeof(buf)) != sizeof(buf)) {
        break;
      }
      printf("%s read: %s\n", me, buf);
    }
  }

  /* NOTREACHED */
  fprintf(stderr, "%s: internal error.\n", argv[0]);
  return 1;
}
