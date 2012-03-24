#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

//This example mimics a bug in some versions of MPICH2 mpd
//the parent process forgets to close one end of a socket pair

int main(int argc, char* argv[])
{
  int count = 1;
  char ch;
  const char* me;
  FILE *fp = fopen("/proc/self/maps", "r");
  if (fp == NULL) {
    perror("fopen failed:");
    return;
  }
  int fd = fileno(fp);


  if(fork()>0){
    me = "parent";
    while (1) {
      if (feof(fp)) {
        rewind(fp);
      }
      lockf(fd, F_LOCK, 0);
      ch = fgetc(fp);
      printf("%s: %d\n", me, count++);
      sleep(1);
    }
  }else{
    me = "child";
    while (1) {
      if (feof(fp)) {
        rewind(fp);
      }
      lockf(fd, F_LOCK, 0);
      ch = fgetc(fp);
      printf("%s: %d\n", me, count++);
      sleep(1);
    }
  }

  printf("%s done\n",me);
  return 0;
}
