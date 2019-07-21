#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <syscall.h>
#include <sys/types.h>
#include <vector>
#include <cassert>

#include "jassert.h"
#include "pidwrappers.h"
#include "dmtcp_dlsym.h" // for NEXT_FNC_DEFAULT macro

#define __real_pthread_mutex_lock       NEXT_FNC(pthread_mutex_lock)
#define __real_pthread_mutex_trylock    NEXT_FNC(pthread_mutex_trylock)
#define __real_pthread_mutex_timedlock  NEXT_FNC(pthread_mutex_timedlock)
#define __real_pthread_mutex_unlock     NEXT_FNC(pthread_mutex_unlock)
#define __real_pthread_mutex_consistent NEXT_FNC(pthread_mutex_consistent)

// For explanation why we need NEXT_FNC_DEFAULT see [1]
#define __real_pthread_cond_wait        NEXT_FNC_DEFAULT(pthread_cond_wait)
#define __real_pthread_cond_timedwait   NEXT_FNC_DEFAULT(pthread_cond_timedwait)

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
//
//
// |=------------------------=[ CONDITION VARIABLE ]=-------------------------=|
//
// From 'man 3p pthread_cond_wait'
//   pthread_cond_timedwait() and pthread_cond_wait() ... shall be called with
//   mutex locked by the calling thread or undefined behavior results.
//
//   These functions atomically release mutex and cause the calling thread to
//   block on the condition variable ...
//
//   Upon successful return, the mutex shall have been locked and shall be
//   owned by the calling thread.
//
// Putting this words from the man page into a picture, we get the following:
//
//          Th1                      Th2
//         -----                    -----
//           |                        |
//      lock mutex (M)                |             /// Th1 owns mutex M.
//           |                        |
//   wait for cond (C,M) ->           |             /// Th1 unlocks mutex M
//           |                        |             /// when waiting for cond C.
//           |                        |
//           |                 lock mutex (M)       /// Th2 owns mutex M.
//           |                        |
//           |                  fire cond (C)
//           |                unlock mutex (M)
//           |                        |
//   wait for cond (C,M) <-           |             /// Th1 owns mutex M
//           |                        |             /// after return from
//          ...                      ...            /// wait for cond C.
//
// As the pthread_cond_wait(3p)/pthread_cond_timedwait(3p) APIs don't go through
// the public pthread_mutex_* APIs to lock the mutex once the condition variable
// has been fired, we explicitly need to update the entry in our ownernership
// record for the mutex which is associated with the condition variable.


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

extern "C" int pthread_mutex_lock(pthread_mutex_t *mutex) {
  int rc;

  rc = __real_pthread_mutex_lock(mutex);
  post_lock(rc, mutex);

  return rc;
}

extern "C" int pthread_mutex_trylock(pthread_mutex_t *mutex) {
  int rc;

  rc = __real_pthread_mutex_trylock(mutex);
  post_lock(rc, mutex);

  return rc;
}

extern "C" int pthread_mutex_timedlock(pthread_mutex_t *mutex,
                                       const struct timespec * abs_timeout) {
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

extern "C" int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex) {
  int rc;

  // pthread_cond_wait will unlock the mutex, but we skip unfollowing for
  // simplicity & runtime performance and in case we checkpoint while waiting
  // in pthread_cond_wait we build on the __owner==0 check on restore

  rc = __real_pthread_cond_wait(cond, mutex);
  if (rc == 0 && dmtcp_is_running_state()) {
    follow_mutex(mutex);
  }

  return rc;
}

extern "C" int pthread_cond_timedwait(pthread_cond_t* cond,
                                      pthread_mutex_t* mutex,
                                      const struct timespec* abstime) {
  int rc;

  // pthread_cond_timedwait will unlock the mutex, but we skip unfollowing for
  // simplicity & runtime performance and in case we checkpoint while waiting
  // in pthread_cond_wait we build on the __owner==0 check on restore

  rc = __real_pthread_cond_timedwait(cond, mutex, abstime);
  // Even the call timed out we have the lock and own the mutex.
  if ((rc == 0 || rc == ETIMEDOUT) && dmtcp_is_running_state()) {
    follow_mutex(mutex);
  }

  return rc;
}

// [1] NEXT_FNC_DEFAULT for pthread_cond_wait(3p)/pthread_cond_timedwait(3p)
// ----------------------------------------------------------------------------
//
// glibc provides two different implementations for these functions, one
// for LinuxThreads and one for POSIX Threads. A dlsym lookup would return a
// function pointer to the oldest symbol defined which in this case is the
// implementation for LinuxThreads.
//
// What's behind that magic is glibc's symbol versioning.
//   nm /lib64/libpthread.so.0 | grep pthread_cond_wait
//     000000000000c140 T pthread_cond_wait@GLIBC_2.2.5
//     000000000000b5c0 T pthread_cond_wait@@GLIBC_2.3.2
// where in this case the symbol with version 2.2.5 is the LinuxThread
// implementation and the symbol with version 2.3.2 is the POSIX Thread
// implementation.
//
// A symbol that has the '@@' tag in its version information is declared as
// the default symbol for that library. Programs that link against this library
// will be linked against the default version of the symbol.
// This can be seen by executing the following snippet on the cmd line:
//   echo "
//   #include <pthread.h>
//   void foo() { pthread_cond_wait(0,0); }
//   void main() {}" |
//   gcc -O2 -o tmp.o -xc -lpthread -; nm tmp.o | grep pthread_cond_wait;
//     rm -f tmp.o
// which returns:
//   U pthread_cond_wait@@GLIBC_2.3.2
//
// It is important that even '@@' denotes the default symbol, this is only true
// for programs that link directly against the library, not for lookups done
// with dlsym(3).
// dlsym(3) makes an "unversioned" lookup for the symbol, which by default will
// match the oldest symbol version in the library (this is because glibc
// guarantees strong runtime backwards compatibility!).
//
// Since this implementation handles POSIX mutexes & condition variables we want
// to delegate to the POSIX Thread implemenation from our wrapper functions
// for pthread_cond_*wait(3p).
// As these are the default version for that symbols we use the
// NEXT_FNC_DEFAULT macro provided by dmtcp to find the next "default" symbol
// in the link map chain.
