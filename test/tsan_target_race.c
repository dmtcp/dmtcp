// Race-detection-fidelity target: verifies that TSAN's data-race detection
// itself (not just process survival) still works correctly after a DMTCP
// checkpoint/restart cycle.
//   gcc -fsanitize=thread -g -O0 -pthread tsan_target_race.c -o tsan_target_race
//   setarch -R ./tsan_target_race          # ASLR off: TSAN requires it
//
// Phase 1 (race-free, iterations 0..RACE_FREE_ITERS-1): both threads
// increment 'counter' under a mutex, like tsan_target.c. Long enough (with
// the 1s sleep below) to comfortably outlast the checkpoint that the test
// harness takes immediately after launch.
//
// Phase 2 (racy, iterations >= RACE_FREE_ITERS): both threads increment
// 'counter' with NO synchronization -- a deliberate, TSAN-detectable data
// race. Since DMTCP restores the iteration counter exactly as it was at
// checkpoint time, phase 2 is only ever reached after checkpoint+restart.
// So a detected race here proves TSAN's detection state survived restart.
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
