// shmget() needs sysv/ipc.h, which needs )XOPEN_SOURCE
#define _XOPEN_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

#define SIZE 1024
#define NUM_SHMIDS 3

void
parent(int shmid[])
{
  int *addr[NUM_SHMIDS];

  for (int shmidx = 0; shmidx < NUM_SHMIDS; shmidx++) {
    addr[shmidx] = (int*) shmat(shmid[shmidx], NULL, 0);
    if (addr == (void *)-1) {
      perror("Parent: shmat");
      abort();
    }
  }

  for (int i = 1; i < 100000; i++) {
    printf("Server: %d\n", i);
    fflush(stdout);
    for (int shmidx = 0; shmidx < NUM_SHMIDS; shmidx++) {
      addr[shmidx][0] = i + shmidx;
    }
    for (int shmidx = 0; shmidx < NUM_SHMIDS; shmidx++) {
      while (addr[shmidx][0] != -(i + shmidx)) {
        sleep(1);
      }
    }
  }
  exit(0);
}

void
child(int shmid[])
{
  int *addr[NUM_SHMIDS];
  for (int shmidx = 0; shmidx < NUM_SHMIDS; shmidx++) {
    addr[shmidx] = (int*) shmat(shmid[shmidx], NULL, 0);
    if (addr == (void *)-1) {
      perror("Child: shmat");
      abort();
    }
  }

  for (int i = 1; i < 100000; i++) {
    printf("Client: %d\n", i);
    fflush(stdout);

    for (int shmidx = 0; shmidx < NUM_SHMIDS; shmidx++) {
      while (addr[shmidx][0] != i + shmidx) {
        sleep(1);
      }
      addr[shmidx][0] = -addr[shmidx][0];
    }
  }

  exit(0);
}

int
main(int argc, char **argv)
{
  int shmid[NUM_SHMIDS];

  // Create two private shm segments using IPC_PRIVATE;
  shmid[0] = shmget(IPC_PRIVATE, SIZE, 0666);
  if (shmid[0] < 0) {
    perror("shmget");
    exit(1);
  }

  shmid[1] = shmget(IPC_PRIVATE, SIZE, 0666);
  if (shmid[1] < 0) {
    perror("shmget");
    exit(1);
  }

  assert(shmid[0] != shmid[1]);

  srand(getpid());
  if ((shmid[2] = shmget((key_t)rand(), SIZE, IPC_CREAT | 0666)) < 0) {
    perror("shmget");
    exit(1);
  }
  printf("pid: %d, Shm1: %d, Shm2: %d, Shm3: %d\n", getpid(), shmid[0], shmid[1], shmid[2]);

  struct shmid_ds shmid_ds;
  if (shmctl(shmid[2], IPC_STAT, &shmid_ds) == -1) {
    perror("shmctl: shmctl failed");
    exit(1);
  }

  void *addr = shmat(shmid[2], NULL, 0);
  if (addr == (void *)-1) {
    perror("main: shmat");
    abort();
  }
  memset(addr, 0, SIZE);

  if (fork() == 0) {
    child(shmid);
  } else {
    parent(shmid);
  }
  return 0;
}
