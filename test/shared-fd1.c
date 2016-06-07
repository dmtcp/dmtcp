#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// This example mimics a DMTCP bug in some versions of MPICH2 mpd.
// The parent process forgets to close one end of a socket pair.

int
main(int argc, char *argv[])
{
  int count = 1;
  int sockets[2];

  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0) {
    perror("socketpair");
    return 1;
  }

  const char *me;
  int fd;
  char buf[32];

  if (fork() > 0) {
    // parent
    // close(sockets[1]); <= forgot to do this
    me = "parent";
    fd = sockets[0];

    while (1) {
      sprintf(buf, "%s(%d)", me, count++);
      if (write(fd, buf, sizeof(buf)) != sizeof(buf)) {
        break;
      }
      sleep(2);
    }
  } else {
    // child
    close(sockets[0]);
    me = "child";
    fd = sockets[1];

    while (1) {
      if (read(fd, buf, sizeof(buf)) != sizeof(buf)) {
        break;
      }
      printf("%s read: %s\n", me, buf);
    }
  }

  printf("%s done\n", me);
  return 0;
}
