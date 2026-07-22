// Race-free pthread target compiled with -fsanitize=thread.
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static long counter;

static void *
worker(void *arg)
{
  long id = (long) arg;

  for (int i = 0;; i++) {
    pthread_mutex_lock(&mutex);
    long snap = ++counter;
    pthread_mutex_unlock(&mutex);

    if (i % 5 == 0) {
      printf("worker %ld: counter=%ld\n", id, snap);
      fflush(stdout);
      sleep(1);
    }
  }

  return NULL;
}

int
main(void)
{
  pthread_t threads[2];

  for (long i = 0; i < 2; i++) {
    pthread_create(&threads[i], NULL, worker, (void *) i);
  }
  for (int i = 0; i < 2; i++) {
    pthread_join(threads[i], NULL);
  }

  return 0;
}
