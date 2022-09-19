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

#define ATOMIC_SHARED volatile __attribute((aligned))

int *sharedMemory;
void reader();
void writer();

int
main()
{
  sharedMemory = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
  if (sharedMemory == MAP_FAILED) {
    perror("mmap");
    exit(1);
  }

  *sharedMemory = -1;

  if (fork()) {
    writer();
  } else {
    reader();
  }

  return 0;
}

void
reader()
{
  ATOMIC_SHARED int val;
  int i = 0;

  while (1) {
    val = *sharedMemory;
    if (val != i) {
      i = *sharedMemory;
      printf("reader %d \n", i);
      fflush(stdout);
    }
    sleep(1);
  }
}

void
writer()
{
  for (int i = 0;; i++) {
    *sharedMemory = i;
    printf("writer: %d \n", i);
    sleep(2);
  }
}
