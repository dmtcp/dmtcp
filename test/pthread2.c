// _GNU_SOURCE for syscall
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#define THREAD_CNT 5

int numWorkers = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void *threadMain(void *_n);

static int maxWorkers = THREAD_CNT;

int
main(int argc, char *argv[])
{
  int count = 0;

  if (argc > 1) {
    int c = atoi(argv[1]);
    if (c > 1) {
      maxWorkers = c;
    }
  }

  while (1) {
    pthread_mutex_lock(&mutex);
    if (numWorkers < maxWorkers + 1) {
      pthread_t pthread_id;
      pthread_attr_t attr;
      pthread_attr_init(&attr);
      pthread_attr_setstacksize(&attr, 1024 * 1024);
      pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
      int *id = malloc(sizeof(int));
      *id = count++;

      int res = pthread_create(&pthread_id, &attr, &threadMain, id);
      if (res != 0) {
        fprintf(stderr, "error creating thread: %s\n", strerror(res));
        return -1;
      } else {
        numWorkers++;
      }
    }
    pthread_mutex_unlock(&mutex);
  }
  return 0;
}

static void *
threadMain(void *data)
{
  int id = *(int *)data;

  if (id % 1000 == 0) {
    printf("Worker: %d (%ld) alive. numWorkers: %d\n",
           id, (long)syscall(SYS_gettid), numWorkers);
  }

  while (1) {
    // usleep(100*1000);
    pthread_mutex_lock(&mutex);
    if (numWorkers > maxWorkers) {
      numWorkers--;
      if (id % 1000 == 0) {
        printf("Worker: %d (%ld) exiting: numWorkers: %d\n",
               id, (long)syscall(SYS_gettid), numWorkers);
      }
      pthread_mutex_unlock(&mutex);
      free(data);
      pthread_exit(NULL);
    }
    pthread_mutex_unlock(&mutex);
  }
  return NULL;
}
