// Multi-producer/multi-consumer condition-variable target, compiled with
// -fsanitize=thread. Every other TSAN test target here (tsan_target.c,
// tsan_target_race.c) only exercises a mutex, and only ever 2 threads of
// the same role -- this exercises DMTCP checkpoint/restart against a
// condition variable and 3 threads of each role sequentially cycling
// through the same mutex, closer to a real producer/consumer workload.
//   gcc -fsanitize=thread -g -O0 -pthread tsan_target_cv_multi.c -o tsan_target_cv_multi
//   setarch -R ./tsan_target_cv_multi          # ASLR off: TSAN requires it
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#define NUM_PRODUCERS 3
#define NUM_CONSUMERS 3
#define BUFFER_SIZE 3

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int buffer[BUFFER_SIZE];
static int in, out, count;

static void *producer(void *arg)
{
  long id = (long)arg;
  for (int i = 0;; i++) {
    pthread_mutex_lock(&m);
    while (count == BUFFER_SIZE) {
      pthread_cond_wait(&cv, &m);
    }
    buffer[in] = (int)(id * 1000 + i);
    in = (in + 1) % BUFFER_SIZE;
    count++;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&m);
    if (i % 5 == 0) {
      printf("producer %ld: inserted %d\n", id, i);
      fflush(stdout);
    }
    sleep(1);
  }
  return NULL;
}

static void *consumer(void *arg)
{
  long id = (long)arg;
  for (int i = 0;; i++) {
    pthread_mutex_lock(&m);
    while (count == 0) {
      pthread_cond_wait(&cv, &m);
    }
    int item = buffer[out];
    out = (out + 1) % BUFFER_SIZE;
    count--;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&m);
    if (i % 5 == 0) {
      printf("consumer %ld: removed %d\n", id, item);
      fflush(stdout);
    }
    sleep(1);
  }
  return NULL;
}

int main(void)
{
  pthread_t producers[NUM_PRODUCERS], consumers[NUM_CONSUMERS];
  for (long i = 0; i < NUM_PRODUCERS; i++) {
    pthread_create(&producers[i], NULL, producer, (void *)i);
  }
  for (long i = 0; i < NUM_CONSUMERS; i++) {
    pthread_create(&consumers[i], NULL, consumer, (void *)i);
  }
  for (int i = 0; i < NUM_PRODUCERS; i++) {
    pthread_join(producers[i], NULL);
  }
  for (int i = 0; i < NUM_CONSUMERS; i++) {
    pthread_join(consumers[i], NULL);
  }
  return 0;
}
