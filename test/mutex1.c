#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dmtcp.h"

// This code uses dmtcp_mutex_trylock() and dmtcp_mutex_unlock().
// We could alternatively have used pthread_mutex_locak/unlock,
// but in this case, it will simply hang on restart, and our
// current autotest can't check for processes that live, but hang.
// This code tests on a recursive mutex, since a recursive mutex
// must check the owner when locking.

int
main()
{
  // DMTCP and native differ when this is not initialized.  Why?
  // DMTCP crashed on lock with ESRCH
  int counter = 0;

  pthread_mutex_t mutex;
  pthread_mutexattr_t attr;

  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&mutex, &attr);

  dmtcp_disable_ckpt();
  while (1) {
    int rc;
    rc = pthread_mutex_trylock(&mutex);
    if (rc != 0) {
      printf("pthread_mutex_trylock (recursive): %s\nn",
             strerror(rc));
      exit(1);
    }
    dmtcp_enable_ckpt();
    if (counter++ % 1000000 == 0) {
      printf("b"); fflush(stdout);
    }
    dmtcp_disable_ckpt();
    rc = pthread_mutex_trylock(&mutex);
    if (rc != 0) {
      printf("pthread_mutex_trylock (recursive): %s\n\n",
             strerror(rc));
      exit(1);
    }
    rc = pthread_mutex_unlock(&mutex);
    rc = pthread_mutex_unlock(&mutex);
    if (rc != 0) {
      printf("pthread_mutex_unlock (recursive): %s\n\n",
             strerror(rc));
      exit(1);
    }
  }
  return 0;
}
