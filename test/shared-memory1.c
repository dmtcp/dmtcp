// _DEFAULT_SOURCE for mkstemp  (WHY?)
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

// For open()
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>


void reader(int fd);
void writer(int fd);

int
main()
{
  char filename[] = "dmtcp-shared-memory.XXXXXX";
  int fd;

  fd = mkstemp(filename);
  printf("creating temporary file in local directory: %s\n", filename);

  // printf("creating file: %s\n", "/tmp/dmtcp-shared-memory.dat");
  unlink(filename);

  // if (-1 == unlink(filename)) {
  // perror("unlink");
  // exit(1);
  // }
  // fd = open("/tmp/dmtcp-shared-memory.dat", O_RDWR | O_CREAT,
  // S_IREAD|S_IWRITE);
  // if (fd == -1) perror("open");

  /* Extend file to needed size */
  int initValue = -1;
  while (write(fd, &initValue, sizeof(initValue)) != sizeof(initValue)) {
    continue;
  }

  if (fork()) {
    writer(fd);
  } else {
    reader(fd);
  }
  return 0;
}

void
reader(int fd)
{
  int *sharedMemory;
  volatile int val;
  int i;

  sharedMemory = mmap(0, sizeof(int), PROT_READ, MAP_SHARED, fd, 0);
  if (sharedMemory == MAP_FAILED) {
    perror("mmap");
    exit(1);
  }
  for (i = -1;;) {
    val = *sharedMemory;
    if (val > i) {
      i = *sharedMemory;
      printf("%d ", i);
      fflush(stdout);
    }
    sleep(1);
  }
}

void
writer(int fd)
{
  int *sharedMemory;
  int i;

  sharedMemory =
    mmap(0, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (sharedMemory == MAP_FAILED) {
    perror("mmap");
    exit(1);
  }
  for (i = 0;; i++) {
    *sharedMemory = i;
    sleep(2);
  }
}
