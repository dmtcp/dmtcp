#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>
#include <vector>
#include <cassert>

#include "jassert.h"
#include "pidwrappers.h"

#define __real_pthread_mutex_lock      NEXT_FNC(pthread_mutex_lock)
#define __real_pthread_mutex_trylock   NEXT_FNC(pthread_mutex_trylock)
#define __real_pthread_mutex_timedlock NEXT_FNC(pthread_mutex_timedlock)

#define __real_pthread_mutex_lock       NEXT_FNC(pthread_mutex_lock)
#define __real_pthread_mutex_trylock    NEXT_FNC(pthread_mutex_trylock)
#define __real_pthread_mutex_timedlock  NEXT_FNC(pthread_mutex_timedlock)
#define __real_pthread_mutex_unlock     NEXT_FNC(pthread_mutex_unlock)
#define __real_pthread_mutex_consistent NEXT_FNC(pthread_mutex_consistent)

// |=-------------------------------=[ GOAL ]=--------------------------------=|
//
// Record mutex ownership to support checkpointing and restoring of locked
// mutex objects.
// This is specific to glibc's NPTL, which encodes mutex ownership through the
// real TID. Since the real TID changes on restore, this owner field must be
// patched in 'struct pthread_mutex_t' after restore.
//
//
// |=-----------------------------=[ ALGORITHM ]=-----------------------------=|
//
// In the case where a thread acquires a mutex, we record the mutex handle
// with the corresponding virtual TID of the locking thread.
// Once a mutex gets unlocked we remove the reference to the mutex.
//
// On restore, we go over all the recorded entries and look up the new TID that
// corresponds to the recorded virtual TID for the mutex handle. We use this
// new TID to patch the owner in the mutex object (following the mutex handle).
//
// Robust mutex objects need some special treatment. Therefore, see the
// ROBUST MUTEX background and the annotations in the code below.
//
//
// |=---------------------------=[ ROBUST MUTEX ]=----------------------------=|
//
// pthread_mutex_*lock()
//   If the mutex is a robust mutex
//   (pthread_mutex_setrobust(PTHREAD_MUTEX_ROBUST)), attempts to lock the mutex
//   can return EOWNERDEAD. This is the case if the owner dies without unlocking
//   the mutex.
//   The thread receiving the return value EOWNERDEAD has the lock. However the
//   mutex is marked as inconsistent (PTHREAD_MUTEX_INCONSISTENT) and the thread
//   is not the owner. The thread needs to call pthread_mutex_consistent() to
//   make itself the owner of the mutex.
//
//   It is the application's responsibility to recover the mutex after receiving
//   EOWNERDEAD. If the application can't recover the mutex, it needs to call
//   pthread_mutex_unlock(), which marks the mutex as notrecoverable
//   (PTHREAD_MUTEX_NOTRECOVERABLE).


/* Globals structs */
/* This maps mutex addresses to the virtual tid of the owner thread.
 * STL map isn't thread-safe. We use an adaptive mutex to guard modification of
 * this map, since the time of holding the lock should be very short.
 */
dmtcp::map<pthread_mutex_t*, pid_t>& mapMutexVirtTid();

namespace {
  // Mutex to protect access to mapMutexVirtTid
  pthread_mutex_t mutex_mapMutexVirtTid =
    PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP;

  struct ScopedMutex_mapMutexVirtTid {
    ScopedMutex_mapMutexVirtTid() {
      assert(__real_pthread_mutex_lock(&mutex_mapMutexVirtTid) == 0);
    }
    ~ScopedMutex_mapMutexVirtTid() {
      assert(__real_pthread_mutex_unlock(&mutex_mapMutexVirtTid) == 0);
    }
  };

  inline void follow_mutex(pthread_mutex_t* mutex) {
    ScopedMutex_mapMutexVirtTid lock;
    mapMutexVirtTid()[mutex] = dmtcp_gettid();
  }

  inline void unfollow_mutex(pthread_mutex_t* mutex) {
    // We only remove the entry if we are the last recorded owner. This is
    // necessary to solve the potential modification race condition after
    // returning from pthread_mutex_lock() and pthread_mutex_unlock()
    // library calls.
    //
    //          Th1                            Th2
    //         -----                          -----
    //           |                              |
    //         lock M                           |
    //           |                              |
    //          ...                             |
    //           |                            lock M  <- blocking
    //        unlock M                          |
    //           |                              |
    //      return from                    return from
    //  pthread_mutex_unlock            pthread_mutex_lock
    //     delegate call                  delegate call
    //           \                              /
    //     in the wrapper functions both threads race to get
    //         mutex_mapMutexVirtTid and modify the map
    //
    // Case 1: Th1 gets mutex_mapMutexVirtTid first
    //   Th1 removes the entry from the map and Th2 adds the entry with the
    //   correct ownership.
    //
    // Case 2: Th2 gets mutex_mapMutexVirtTid first
    //   Th2 updates the entry with itself as the new owner (which is the
    //   correct state after returning from pthread_mutex_lock), but then Th1
    //   comes and removes the entry resulting in a faulty state.

    ScopedMutex_mapMutexVirtTid lock;
    dmtcp::map<pthread_mutex_t*, pid_t>::iterator it =
      mapMutexVirtTid().find(mutex);
    if (it != mapMutexVirtTid().end() && it->second == dmtcp_gettid()) {
        mapMutexVirtTid().erase(it);
    }
  }

  inline void post_lock(int ret, pthread_mutex_t* mutex) {
    if ((ret == 0 || ret == EOWNERDEAD)
        && dmtcp_is_running_state()) {
      if (ret == 0) {
        follow_mutex(mutex);
      } else if (ret == EOWNERDEAD) {
        // Mutex owner died or the mutex was marked inconsistent by the owner.
        // We acquire the lock but we are not the owner of the mutex now.
        // If the application can recover the Mutex state it needs to call
        // pthread_mutex_consistent(). In that case we follow the mutex again.
        // For details, see above in ROBUST MUTEX explanation
        unfollow_mutex(mutex);
      }
    }
  }
}


extern "C" int
pthread_mutex_lock(pthread_mutex_t *mutex)
{
  int rc;

  rc = __real_pthread_mutex_lock(mutex);
  post_lock(rc, mutex);

  return rc;
}

extern "C" int
pthread_mutex_trylock(pthread_mutex_t *mutex)
{
  int rc;

  rc = __real_pthread_mutex_trylock(mutex);
  post_lock(rc, mutex);

  return rc;
}

extern "C" int
pthread_mutex_timedlock(pthread_mutex_t *mutex,
                        const struct timespec *abs_timeout)
{
  int rc;

  rc = __real_pthread_mutex_timedlock(mutex, abs_timeout);
  post_lock(rc, mutex);

  return rc;
}

extern "C" int pthread_mutex_consistent(pthread_mutex_t *mutex) {
  int rc;

  rc = __real_pthread_mutex_consistent(mutex);
  if (rc == 0 && dmtcp_is_running_state()) {
    follow_mutex(mutex);
  }

  return rc;
}

extern "C" int pthread_mutex_unlock(pthread_mutex_t *mutex) {
  int rc;

  rc = __real_pthread_mutex_unlock(mutex);
  if (rc == 0
     // FIXME:
     // Potential issue in future: We use glibc internals and assume the mutex
     // is not owned anymore when the __owner is set to '0'.
     //
     // There is a race condition here. When we return from unlock
     // and some other thread directly locks the mutex, then __owner
     // might be !=0. However it is okay here to not remove the entry,
     // since the other thread records itself as the owner.
     && mutex->__data.__owner == 0) {
    unfollow_mutex(mutex);
  }

  return rc;
}
