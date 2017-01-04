#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "dmtcp.h"

// This code uses the attribute PTHREAD_MUTEX_ERRORCHECK.
// An error-checking mutex must check the owner when unlocking.
// (In contrast, a recursive mutex will check the owner when locking.)
// This version runs with two threads instead of just one thread.

pthread_mutex_t mutex;
void *mutex_loop(void *arg);

int
main()
{
  pthread_mutexattr_t attr;

  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
  pthread_mutex_init(&mutex, &attr);

  pthread_t thread1, thread2;
  pthread_create(&thread1, NULL, mutex_loop, NULL);
  pthread_create(&thread2, NULL, mutex_loop, NULL);

  // This will block forever.  mutex_loop() doesn't return.
  pthread_join(thread1, NULL);
  pthread_join(thread2, NULL);
  return 0;
}

void *
mutex_loop(void *arg /* NOTUSED */)
{
  int counter = 0;

  struct timespec hundredth_second;

  hundredth_second.tv_sec = 0;
  hundredth_second.tv_nsec = 10000000; /* 10,000,000 */

  while (1) {
    int rc;
    rc = pthread_mutex_lock(&mutex);
    if (rc != 0) {
      printf("pthread_mutex_lock (error-checking): %s\n\n",
             strerror(rc));
      exit(1);
    }

    // Wait a little to optimize chances of checkpoint occurring here.
    nanosleep(&hundredth_second, NULL);
    if (counter++ % 50 == 0) {
      printf("b"); fflush(stdout);
    }
    rc = pthread_mutex_unlock(&mutex);
    if (rc != 0) {
      printf("pthread_mutex_unlock (error-checking): %s\n\n",
             strerror(rc));
      exit(1);
    }
  }
  return NULL;
}
