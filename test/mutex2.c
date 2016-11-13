#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "dmtcp.h"

// WE CAN ALTERNATELY USE trylock, and timedlock with a timeout of zerotime.
// They do the same thing, and we can test both methods in the same code.
// But it's better to make them separate tests, so that we always
//// fail if just one of them causes a problem.

int
main()
{
  int counter = 0;
  struct timespec zerotime;

  zerotime.tv_sec = 0;
  zerotime.tv_nsec = 0;

  pthread_mutex_t mutex;
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&mutex, &attr);

  dmtcp_disable_ckpt();
  while (1) {
    int rc;
    rc = pthread_mutex_timedlock(&mutex, &zerotime);
    if (rc != 0) {
      printf("pthread_mutex_timedlock: %s\n\n",
             strerror(rc));
      exit(1);
    }
    dmtcp_enable_ckpt();
    if (counter++ % 1000000 == 0) {
      printf("b"); fflush(stdout);
    }
    dmtcp_disable_ckpt();

    // zerotime means either succeed in locking or fail immediately.
    rc = pthread_mutex_timedlock(&mutex, &zerotime);
    if (rc != 0) {
      printf("pthread_mutex_timedlock (recursive): %s\n\n",
             strerror(rc));
      exit(1);
    }
    rc = pthread_mutex_unlock(&mutex);
    rc = pthread_mutex_unlock(&mutex);
    if (rc == -1) {
      perror("pthread_mutex_unlock");
    }
  }
  return 0;
}
