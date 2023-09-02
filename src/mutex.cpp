#include "dmtcp.h"
#include "futex.h"
#include "jassert.h"
#include "syscallwrappers.h"

typedef uint32_t mutex_owner_t; // See 'include/dmtcp.h' for why 'uint32_t'

static const uint32_t LOCK_FREE = 0;
static const uint32_t LOCK_ACQUIRED = 1;
static const uint32_t LOCK_ACQUIRED_WAITERS_MAY_BE_QUEUED = 2;

/*
 * Mutex has following fields:
 *   futex: The field for all futex operations
 *   type:  Lock type.
 *   owner: Used for recursive locks and to check for deadlocks.
 *   count: Record recursive lock acquisition count.
 */

extern "C" void
DmtcpMutexInit(DmtcpMutex *mutex, DmtcpMutexType type)
{
  mutex->type = type;
  mutex->futex = 0;
  mutex->owner = 0;
  mutex->count = 0;
}


 /*
 * If futex is 0 (not acquired), set to 1 (acquired with no waiters) and return.
 * Otherwise, ensure that it is >1 (acquired, possibly with waiters) and then
 * block until we acquire the lock, at which point futex will still be >1.  The
 * lock is always acquired on return.
 */
extern "C" int
DmtcpMutexLock(DmtcpMutex *mutex)
{
  if (DmtcpMutexTryLock(mutex) == 0) {
    return 0;
  }

  // If someone else is holding the lock (lock in state LOCK_ACQUIRED), then we
  // register ourselves for waiting on the lock by putting it in state
  // LOCK_ACQUIRED_WAITERS_MAY_BE_QUEUED, and then calling futex_wait(). When we
  // wake up from this, we test if the lock is in state LOCK_FREE, and if so, we
  // set it for LOCK_ACQUIRED_WAITERS_MAY_BE_QUEUED, and we return, holding this
  // lock.
  // If the lock was originally in state LOCK_FREE, then we set the lock to
  // state LOCK_ACQUIRED_WAITERS_MAY_BE_QUEUED. We succeed and return only by
  // leaving the lock in state LOCK_ACQUIRED_WAITERS_QUEUED. Else, we loop.
  do {
    int oldval =
      __sync_val_compare_and_swap(&mutex->futex,
                                  LOCK_ACQUIRED,
                                  LOCK_ACQUIRED_WAITERS_MAY_BE_QUEUED);
    if (oldval != LOCK_FREE) {
      // futex_wait() returns immediately with EAGAIN if mutex->futex is not
      // LOCK_ACQUIRED_WAITERS_MAY_BE_QUEUED
      int s = futex_wait(&mutex->futex, LOCK_ACQUIRED_WAITERS_MAY_BE_QUEUED);
      JASSERT(s != -1 || errno == EAGAIN || errno == EINTR)(JASSERT_ERRNO);
    }
  } while (__sync_val_compare_and_swap(&mutex->futex,
                                       LOCK_FREE,
                                       LOCK_ACQUIRED_WAITERS_MAY_BE_QUEUED)
           != LOCK_FREE);

  mutex->owner = (mutex->type == DMTCP_MUTEX_LLL) ? 1
                                                :(mutex_owner_t) gettid();
  mutex->count = 1;

  return 0;
}


extern "C" int
DmtcpMutexTryLock(DmtcpMutex *mutex)
{
  pid_t owner = 1;

  if (mutex->type != DMTCP_MUTEX_LLL) {
    owner = gettid();

    if ((pid_t)(mutex->owner) == owner) {
      if (mutex->type == DMTCP_MUTEX_RECURSIVE) {
        JASSERT(mutex->count + 1 != 0);
        mutex->count++;
        return 0;
      }
      return EDEADLK;
    }
  }

  if (__sync_val_compare_and_swap(&mutex->futex,
                                   LOCK_FREE,
                                   LOCK_ACQUIRED) == LOCK_FREE) {
    // We successfully acquired the lock.
    mutex->owner = (mutex_owner_t)owner;
    mutex->count = 1;
    return 0;
  }

  return EAGAIN;
}


/* Set mutex->futex to 0 (LOCK_FREE), releasing the lock. If mutex->futex was
 * LOCK_ACQUIRED_WAITERS_MAY_BE_QUEUED, also make a futex_wake call to wake one of the
 * waiters.
 */

extern "C" int
DmtcpMutexUnlock(DmtcpMutex *mutex)
{
  pid_t owner = 1;

  if (mutex->type != DMTCP_MUTEX_LLL) {
    owner = gettid();
  }

  JASSERT((pid_t)(mutex->owner) == owner);

  mutex->count--;

  if (mutex->count == 0) {
    mutex->owner = 0;

    if (__sync_bool_compare_and_swap(&mutex->futex, LOCK_ACQUIRED, LOCK_FREE)) {
      // No need to call futex_wake as we don't have any waiters.
    } else {
      JASSERT(__sync_bool_compare_and_swap(&mutex->futex,
                                           LOCK_ACQUIRED_WAITERS_MAY_BE_QUEUED,
                                           LOCK_FREE));
      JASSERT(futex_wake(&mutex->futex, 1) != -1) (JASSERT_ERRNO);
    }
  }

  return 0;
}
