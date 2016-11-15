#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#define SIZE1 2
#define SIZE2 3

int
main()
{
  int fd1 = -1, fd2 = -1;
  char buff1[SIZE1] = { 0 };
  char buff2[SIZE2] = { 0 };

  fd1 = open("./test1", O_RDONLY);
  fd2 = open("./test2", O_RDONLY);


  while (read(fd1, buff1, SIZE1) > 0) {
    buff1[SIZE1 - 1] = '\0';
    printf("READ1: %s\n", buff1);
    sleep(1);
  }

  while (read(fd2, buff2, SIZE2) > 0) {
    buff1[SIZE2 - 1] = '\0';
    printf("READ2: %s\n", buff2);
    sleep(1);
  }

  close(fd1);
  close(fd2);
  return 0;
}
