// Long-running, race-free, checkpointable target compiled with -fsanitize=thread,
// used to develop/verify DMTCP support for TSAN'd binaries.
//   gcc -fsanitize=thread -g -O0 -pthread tsan_target.c -o tsan_target
//   setarch -R ./tsan_target          # ASLR off: TSAN requires it
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static long counter;

static void *worker(void *arg)
{
  long id = (long)arg;
  for (int i = 0;; i++) {
    pthread_mutex_lock(&m);
    long snap = ++counter;  // snapshot under the lock; printing the shared
    pthread_mutex_unlock(&m);  // variable unlocked would be a real race
    if (i % 5 == 0) {
      printf("worker %ld: counter=%ld\n", id, snap);
      fflush(stdout);
      sleep(1);
    }
  }
  return NULL;
}

int main(void)
{
  pthread_t t[2];
  for (long i = 0; i < 2; i++) {
    pthread_create(&t[i], NULL, worker, (void *)i);
  }
  for (int i = 0; i < 2; i++) {
    pthread_join(t[i], NULL);
  }
  return 0;
}
