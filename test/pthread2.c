#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>

int numWorkers = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void *threadMain (void *_n);

int main ()
{
  int count = 0;

  while (1) {
    pthread_mutex_lock(&mutex);
    if (numWorkers < 6) {
      pthread_t pthread_id;
      pthread_attr_t attr;
      pthread_attr_init(&attr);
      pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
      int *id = malloc(sizeof(int));
      *id = count++;

      int res = pthread_create(&pthread_id, &attr, &threadMain, id);
      if (res != 0) {
        fprintf (stderr, "error creating thread: %s\n", strerror (res));
        return (-1);
      } else {
        numWorkers++;
      }
    }
    pthread_mutex_unlock(&mutex);
  }
  return (0);
}

static void *threadMain (void *data)
{
  int id = *(int*) data;

  while (1) {
    printf("Worker: %d (%d) alive. numWorkers: %d\n",
           id, syscall(SYS_gettid), numWorkers);
    usleep(100*1000);
    pthread_mutex_lock(&mutex);
    if (numWorkers > 5) {
      numWorkers--;
      printf("Worker: %d (%d) exiting: numWorkers: %d\n",
             id, syscall(SYS_gettid), numWorkers);
      pthread_mutex_unlock(&mutex);
      free(data);
      pthread_exit(NULL);
    }
    pthread_mutex_unlock(&mutex);
  }
  return NULL;
}
