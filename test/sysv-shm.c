#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SIZE 1024

void parent(int shmid)
{
  void *addr = shmat(shmid, NULL, 0);
  if (addr == (void*) -1) {
    perror("Parent: shmat");
    abort();
  }

  int *ptr = (int*) addr;
  int i;
  for (i = 1; i< 100000; i++) {
    printf("Server: %d\n", i);
    fflush(stdout);
    *ptr = i;
    while(*ptr != -i)
      sleep(1);
  }
  *ptr = 0;
  exit(0);
}

void child(int shmid)
{
  void *addr = shmat(shmid, NULL, 0);
  if (addr == (void*) -1) {
    perror("Child: shmat");
    abort();
  }

  int *ptr = (int*) addr;
  sleep(2);
  int val;
  while((val = *ptr) != 0) {
    int i = *ptr;
    if (i>0) {
      printf("Client: %d\n", i);
      fflush(stdout);
      *ptr = -i;
    } else {
      sleep(1);
    }
  }
  int a;
  while(!a);
  exit(0);
}

int main(int argc, char **argv)
{
  int shmid;

  if ((shmid = shmget(IPC_PRIVATE, SIZE, IPC_CREAT | 0666)) < 0) {
    perror("shmget");
    exit(1);
  }
  struct shmid_ds shmid_ds;
  if (shmctl(shmid, IPC_STAT, &shmid_ds) == -1) {
    perror("shmctl: shmctl failed");
    exit(1);
  }

  if (fork() == 0) {
    child(shmid);
  } else {
    parent(shmid);
  }
  return 0;
}
