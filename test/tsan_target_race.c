// Verifies TSAN's race *detection* (not just process survival) survives
// a DMTCP checkpoint/restart cycle.
//   gcc -fsanitize=thread -g -O0 -pthread tsan_target_race.c -o tsan_target_race
//   setarch -R ./tsan_target_race          # ASLR off: TSAN requires it
//
// Iterations < RACE_FREE_ITERS: race-free (mutex-protected), to survive
// the pre-restart checkpoint. Iterations >= RACE_FREE_ITERS: deliberately
// unsynchronized. Since the restored counter picks up where it left off,
// phase 2 is only ever reached after restart.
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#define RACE_FREE_ITERS 3

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static long counter;

static void *worker(void *arg)
{
  long id = (long)arg;
  for (int i = 0;; i++) {
    if (i < RACE_FREE_ITERS) {
      pthread_mutex_lock(&m);
      long snap = ++counter;
      pthread_mutex_unlock(&m);
      printf("worker %ld: race-free counter=%ld\n", id, snap);
    } else {
      // Deliberate, unguarded race: both threads read-modify-write
      // 'counter' concurrently with no synchronization whatsoever.
      counter++;
      printf("worker %ld: racy counter=%ld\n", id, counter);
    }
    fflush(stdout);
    sleep(1);
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
