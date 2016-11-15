#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// This example mimics a bug in some versions of MPICH2 mpd
// the parent process forgets to close one end of a socket pair

int
main(int argc, char *argv[])
{
  char ch;
  const char *me;
  int fd = open("/proc/self/maps", O_RDONLY);

  if (fd == -1) {
    perror("open failed:");
    return 1;
  }


  if (fork() > 0) {
    me = "parent";
    while (1) {
      int ret = read(fd, &ch, 1);
      if (ret == 0) {
        lseek(fd, 0, SEEK_SET);
      } else if (ret == -1) {
        exit(0);
      }

      // printf("%s: %d\n", me, count++);
      // sleep(1);
    }
  } else {
    me = "child";
    while (1) {
      int ret = read(fd, &ch, 1);
      if (ret == 0) {
        lseek(fd, 0, SEEK_SET);
      } else if (ret == -1) {
        exit(0);
      }

      // printf("%s: %d\n", me, count++);
      sleep(1);
    }
  }

  printf("%s done\n", me);
  return 0;
}
