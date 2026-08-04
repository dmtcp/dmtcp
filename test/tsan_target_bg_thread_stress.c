// Continuously spawns and joins short-lived threads (each doing a
// malloc/free plus a create-lock-unlock-destroy mutex cycle), to force
// TSAN's allocator and ThreadRegistry locks to be exercised constantly
// under DMTCP checkpointing. Regression guard for double_fork()'s
// raw-syscall _exit() (ckptserializer.cpp).
//
// Restart still crashes for this test (see the disabled tsan-bg-thread-
// stress TestSpec), but only reliably under DMTCP_LOG_LEVEL=trace: 10
// independent checkpoint/restart cycles with tracing off never crashed;
// 7/10 crashed with it on. Root cause: TRACE() can fail on restart for
// TSAN-compiled target code. TRACE() reaches an unwrapped write() (see
// logger.cpp; DMTCP has no _real_write, unlike its other _real_*
// syscall bypasses), and that write() is itself TSAN-intercepted.
// Forcing logEnabled() to always return false (DMTCP_LOG_LEVEL=trace
// still set, but no TRACE() ever reaches write()) made the crash
// disappear, confirmed directly on both this target and
// tsan-forked-checkpoint. mtcp_restart.c's own unrelated debug prints
// keep firing throughout with no ill effect: those run before the TSAN
// image is restored, never reaching a TSAN-intercepted write(). Same
// family of hazard as the "TODO: The TSan background thread" commit (a
// different specific trigger: TSAN's background thread frozen while
// holding a lock, rather than TRACE()'s own TSAN-intercepted call).
//   gcc -fsanitize=thread -g -O0 -pthread tsan_target_bg_thread_stress.c -o tsan_target_bg_thread_stress
//   setarch -R ./tsan_target_bg_thread_stress          # ASLR off: TSAN requires it
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define NUM_WORKERS 8
#define ALLOC_SIZE (64 * 1024)

void *short_lived_task(void *arg) {
  void *ptr = malloc(ALLOC_SIZE);

  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_lock(&mutex);
  pthread_mutex_unlock(&mutex);
  pthread_mutex_destroy(&mutex);

  free(ptr);
  return NULL;
}

void *worker_loop(void *arg) {
  while (1) {
    pthread_t t[5];
    for (int i = 0; i < 5; i++) {
      pthread_create(&t[i], NULL, short_lived_task, NULL);
    }
    for (int i = 0; i < 5; i++) {
      pthread_join(t[i], NULL);
    }
  }
  return NULL;
}

int main(void) {
  printf("[*] Starting TSAN background thread stress test...\n");
  printf("[*] Ready for DMTCP checkpointing.\n");
  fflush(stdout);

  pthread_t workers[NUM_WORKERS];
  for (int i = 0; i < NUM_WORKERS; i++) {
    pthread_create(&workers[i], NULL, worker_loop, NULL);
  }

  for (int i = 0; i < NUM_WORKERS; i++) {
    pthread_join(workers[i], NULL);
  }

  return 0;
}
