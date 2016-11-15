// semop() requires sys/ipc.h, which requires _XOPEN_SOURCE
#define _XOPEN_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

#define SIZE 1024

union semun {
  int val;                 /* Value for SETVAL */
  struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
  unsigned short *array;   /* Array for GETALL, SETALL */
  struct seminfo *__buf;   /* Buffer for IPC_INFO (Linux-specific) */
};

void
sem_up(int semid)
{
  struct sembuf sops;

  sops.sem_num = 0;
  sops.sem_op = 1;
  sops.sem_flg = SEM_UNDO;

  if (semop(semid, &sops, 1) == -1) {
    perror("semop failed");
    exit(1);
  }
}

void
sem_down(int semid)
{
  struct sembuf sops;

  sops.sem_num = 0;
  sops.sem_op = -1;
  sops.sem_flg = SEM_UNDO;

  if (semop(semid, &sops, 1) == -1) {
    perror("semop failed");
    exit(1);
  }
}

void
parent(int shmid, int parent_semid, int child_semid)
{
  void *addr = shmat(shmid, NULL, 0);

  if (addr == (void *)-1) {
    perror("Parent: shmat");
    exit(1);
  }

  int i = 1;
  int *ptr = (int *)addr;
  for (i = 1; i < 32000; i++) {
    printf("Server: %d\n", i);
    fflush(stdout);
    *ptr = i;
    sem_up(child_semid);
    sem_down(parent_semid);
    assert(*ptr == -i);
    sleep(1);
  }
  exit(0);
}

void
child(int shmid, int parent_semid, int child_semid)
{
  void *addr = shmat(shmid, NULL, 0);

  if (addr == (void *)-1) {
    perror("Child: shmat");
    exit(1);
  }

  int i;
  int *ptr = (int *)addr;
  while (1) {
    sem_down(child_semid);
    i = *ptr;
    assert(i > 0);
    printf("Client: %d\n", i);
    fflush(stdout);
    *ptr = -i;
    sem_up(parent_semid);
  }
  exit(0);
}

int
main(int argc, char **argv)
{
  int shmid;
  int parent_semid;
  int child_semid;
  union semun arg;

  arg.val = 0;

  parent_semid = semget((key_t)9977, 1, IPC_CREAT | 0666);
  if (parent_semid == -1) {
    perror("semget failed");
    exit(1);
  }
  if (semctl(parent_semid, 0, SETVAL, arg) == -1) {
    perror("semctl failed");
    exit(1);
  }

  child_semid = semget((key_t)9978, 1, IPC_CREAT | 0666);
  if (child_semid == -1) {
    perror("semget failed");
    exit(1);
  }
  if (semctl(child_semid, 0, SETVAL, arg) == -1) {
    perror("semctl failed");
    exit(1);
  }

  if ((shmid = shmget((key_t)9979, SIZE, IPC_CREAT | 0666)) < 0) {
    perror("shmget");
    exit(1);
  }

  struct shmid_ds shmid_ds;
  if (shmctl(shmid, IPC_STAT, &shmid_ds) == -1) {
    perror("shmctl: shmctl failed");
    exit(1);
  }
  void *addr = shmat(shmid, NULL, 0);
  if (addr == (void *)-1) {
    perror("main: shmat");
    exit(1);
  }
  memset(addr, 0, SIZE);

  if (fork() == 0) {
    child(shmid, parent_semid, child_semid);
  } else {
    parent(shmid, parent_semid, child_semid);
  }
  return 0;
}
