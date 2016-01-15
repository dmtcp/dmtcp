#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <syscall.h>
#include <sys/types.h>
#include <vector>

#include "jassert.h"
#include "pidwrappers.h"

#define __real_pthread_mutex_lock    NEXT_FNC(pthread_mutex_lock)
#define __real_pthread_mutex_trylock  NEXT_FNC(pthread_mutex_trylock)
#define __real_pthread_mutex_unlock  NEXT_FNC(pthread_mutex_unlock)

/* Globals structs */
/* This maps mutex addresses to the virtual tid of the owner thread. */
static dmtcp::map<pthread_mutex_t*, int> mapMutexVirtTid;

extern "C" int pthread_mutex_lock(pthread_mutex_t *mutex) {
  int rc;

  rc = __real_pthread_mutex_lock(mutex);
  if (rc == 0 && dmtcp_is_running_state()) {
    mapMutexVirtTid[mutex] = dmtcp_gettid();
  }

  return rc;
}

extern "C" int pthread_mutex_trylock(pthread_mutex_t *mutex) {
  int rc;

  rc = __real_pthread_mutex_trylock(mutex);
  if (rc == 0 && dmtcp_is_running_state()) {
    mapMutexVirtTid[mutex] = dmtcp_gettid();
  }

  return rc;
}

extern "C" int pthread_mutex_unlock(pthread_mutex_t *mutex) {
  int rc;
  pid_t tid = dmtcp_gettid();

  rc =  __real_pthread_mutex_unlock(mutex);
  if (rc == EPERM && dmtcp_is_running_state() &&
      mapMutexVirtTid[mutex] == tid) {
    // We own this mutex (same virt. tid), but we failed with EPERM
    // Maybe there was a ckpt/restart and the real tid changed.
    // Let's change the mutex owner to the current real tid.
    int ntries = 3;
    do {
      mutex->__data.__owner = dmtcp_virtual_to_real_pid(tid);
      rc = __real_pthread_mutex_unlock(mutex);
      // Maybe a ckpt/restart happened just before the mutex unlock.
      // If so, we will have an EPERM again.  Do virt_to_real again
      //   to get the latest tid, and try again.
      JASSERT(ntries-- > 0);
    } while (rc == EPERM &&
             mutex->__data.__owner != dmtcp_virtual_to_real_pid(tid));

    mapMutexVirtTid[mutex] = 0;
  }

  return rc;
}
