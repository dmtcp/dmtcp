#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <syscall.h>
#include <sys/types.h>
#include <vector>

#include "jassert.h"
#include "pidwrappers.h"

#define __real_pthread_mutex_lock    NEXT_FNC(pthread_mutex_lock)
#define __real_pthread_mutex_trylock  NEXT_FNC(pthread_mutex_trylock)
#define __real_pthread_mutex_timedlock  NEXT_FNC(pthread_mutex_timedlock)

/* Globals structs */
/* This maps mutex addresses to the virtual tid of the owner thread. */
/* FIXME:
 * STL map isn't thread-safe. However, if we use mutex to protect it,
 * there is still the problem where checkpoints happen between the lock
 * and the unlock function. */
dmtcp::map<pthread_mutex_t*, pid_t>& mapMutexVirtTid();

extern "C" int pthread_mutex_lock(pthread_mutex_t *mutex) {
  int rc;

  rc = __real_pthread_mutex_lock(mutex);
  if (rc == 0 && dmtcp_is_running_state()) {
    mapMutexVirtTid()[mutex] = dmtcp_gettid();
  }

  return rc;
}

extern "C" int pthread_mutex_trylock(pthread_mutex_t *mutex) {
  int rc;

  rc = __real_pthread_mutex_trylock(mutex);
  if (rc == 0 && dmtcp_is_running_state()) {
    mapMutexVirtTid()[mutex] = dmtcp_gettid();
  }

  return rc;
}

extern "C" int pthread_mutex_timedlock(pthread_mutex_t *mutex,
                                       const struct timespec * abs_timeout) {
  int rc;

  rc = __real_pthread_mutex_timedlock(mutex, abs_timeout);
  if (rc == 0 && dmtcp_is_running_state()) {
    mapMutexVirtTid()[mutex] = dmtcp_gettid();
  }

  return rc;
}
