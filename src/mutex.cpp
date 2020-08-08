#include "dmtcp.h"
#include "futex.h"
#include "jassert.h"
#include "syscallwrappers.h"


/*
 * Mutex
 */

extern "C" void
DmtcpMutexInit(DmtcpMutex *mutex, DmtcpMutexType type)
{
  mutex->type = type;
  mutex->owner = 0;
  mutex->count = 0;
}


extern "C" int
DmtcpMutexLock(DmtcpMutex *mutex)
{
  pid_t owner = 1;

  if (mutex->type != DMTCP_MUTEX_LLL) {
    owner = dmtcp_gettid();

    if (mutex->owner == owner) {
      if (mutex->type == DMTCP_MUTEX_RECURSIVE) {
        JASSERT(mutex->count + 1 != 0);
        mutex->count++;
        return 0;
      }
      return EDEADLK;
    }
  }

  while (1) {
    uint32_t waitVal =
      __sync_val_compare_and_swap((uint32_t *)&mutex->owner, 0, owner);

    if (waitVal == 0) {
      // We successfully acquired the lock.
      break;
    }

    int s = futex_wait((uint32_t *)&mutex->owner, waitVal);
    JASSERT(s != -1 || errno == EAGAIN)(JASSERT_ERRNO);
  }

  JASSERT(mutex->owner == owner);
  mutex->count = 1;

  return 0;
}


extern "C" int
DmtcpMutexTryLock(DmtcpMutex *mutex)
{
  pid_t owner = 1;

  if (mutex->type != DMTCP_MUTEX_LLL) {
    owner = dmtcp_gettid();

    if (mutex->owner == dmtcp_gettid()) {
      if (mutex->type == DMTCP_MUTEX_RECURSIVE) {
        JASSERT(mutex->count + 1 != 0);
        mutex->count++;
        return 0;
      }
      return EDEADLK;
    }
  }

  if (__sync_bool_compare_and_swap((uint32_t *)&mutex->owner, 0, owner)) {
    JASSERT(mutex->owner == owner);
    mutex->count = 1;
    return 0;
  }

  return EAGAIN;
}


extern "C" int
DmtcpMutexUnlock(DmtcpMutex *mutex)
{
  pid_t owner = 1;

  if (mutex->type != DMTCP_MUTEX_LLL) {
    owner = dmtcp_gettid();
  }

  JASSERT(mutex->owner == owner);

  mutex->count--;

  if (mutex->count == 0) {
    mutex->owner = 0;

    // TODO(Kapil): Call futex_wake only if there are futex waiters.
    JASSERT(futex_wake((uint32_t*) &mutex->owner, 1) != -1) (JASSERT_ERRNO);
  }

  return 0;
}
