#include <limits.h>
#include <atomic>

#include "dmtcp.h"
#include "futex.h"
#include "jassert.h"
#include "syscallwrappers.h"


extern "C"
void DmtcpRWLockInit(DmtcpRWLock *rwlock)
{
  memset(rwlock, 0, sizeof(*rwlock));
  DmtcpMutexInit(&rwlock->xLock, DMTCP_MUTEX_NORMAL);
}


extern "C"
int DmtcpRWLockTryRdLock(DmtcpRWLock *rwlock)
{
  int result = EBUSY;

  if (DmtcpMutexTryLock(&rwlock->xLock) != 0) {
    return result;
  }

  // Detect deadlock.
  if (rwlock->writer == dmtcp_gettid()) {
    result = EDEADLK;
  }

  // See if we can acquire the lock.
  if (rwlock->writer == 0 && !rwlock->nWritersQueued) {
    uint32_t old = __atomic_add_fetch(&rwlock->nReaders, 1, __ATOMIC_SEQ_CST);
    JASSERT(old != 0); // Overflow
    result = 0;
  }

  JASSERT(DmtcpMutexUnlock(&rwlock->xLock) == 0);

  return result;
}


extern "C"
int DmtcpRWLockRdLock(DmtcpRWLock *rwlock)
{
  int result = 0;

  JASSERT(DmtcpMutexLock(&rwlock->xLock) == 0);

  while (1) {
    // Detect deadlock.
    if (rwlock->writer == dmtcp_gettid()) {
      result = EDEADLK;
    }

    // See if we can acquire the lock.
    if (rwlock->writer == 0 && !rwlock->nWritersQueued) {
      uint32_t old = __atomic_add_fetch(&rwlock->nReaders, 1, __ATOMIC_SEQ_CST);
      JASSERT(old != 0); // Overflow
      break;
    }

    // Lock was not available. Let's queue ourselves.
    ++rwlock->nReadersQueued;
    JASSERT(rwlock->nReadersQueued != 0); // Overflow

    // Record writer TID.
    uint64_t waitVal = rwlock->readersFutex;

    // Unlock exclusive lock and wait for writer to finish.
    JASSERT(DmtcpMutexUnlock(&rwlock->xLock) == 0);

    // Wait for futex.
    int s = futex_wait(&rwlock->readersFutex, waitVal);
    JASSERT (s != -1 || errno == EAGAIN) (JASSERT_ERRNO);

    // Reacquire the exclusive lock.
    JASSERT(DmtcpMutexLock(&rwlock->xLock) == 0);
    --rwlock->nReadersQueued;
  }

  JASSERT(DmtcpMutexUnlock(&rwlock->xLock) == 0);

  return result;
}

int DmtcpRWLockRdLockIgnoreQueuedWriter(DmtcpRWLock *rwlock)
{
  uint32_t old = __atomic_fetch_add(&rwlock->nReaders, 1, __ATOMIC_SEQ_CST);
  JASSERT(old > 0);
  JASSERT(old + 1 != 0); // Overflow
  return 0;
}

extern "C"
int DmtcpRWLockWrLock(DmtcpRWLock *rwlock)
{
  int result = 0;

  JASSERT(DmtcpMutexLock(&rwlock->xLock) == 0);

  while (1) {
    // Detect deadlock.
    if (rwlock->writer == dmtcp_gettid()) {
      JASSERT(DmtcpMutexUnlock(&rwlock->xLock) == 0);
      result = EDEADLK;
    }

    // See if we can acquire the lock.
    if (rwlock->writer == 0 && rwlock->nReaders == 0) {
      rwlock->writer = dmtcp_gettid();
      break;
    }

    // Lock was not available. Let's queue ourselves.
    ++rwlock->nWritersQueued;
    JASSERT(rwlock->nWritersQueued != 0); // Overflow

    // Record writer TID.
    uint64_t waitVal = rwlock->writersFutex;

    // Unlock exclusive lock and wait for writer to finish.
    JASSERT(DmtcpMutexUnlock(&rwlock->xLock) == 0);

    // Wait for futex.
    int s = futex_wait(&rwlock->writersFutex, waitVal);
    JASSERT (s != -1 || errno == EAGAIN) (JASSERT_ERRNO);

    // Reacquire the exclusive lock.
    JASSERT(DmtcpMutexLock(&rwlock->xLock) == 0);
    --rwlock->nWritersQueued;
  }

  return result;
}


extern "C"
int DmtcpRWLockUnlock(DmtcpRWLock *rwlock)
{
  if (rwlock->writer == dmtcp_gettid()) {
    // We must already have the lock.
    JASSERT(rwlock->xLock.owner == dmtcp_gettid());
    rwlock->writer = 0;
  } else {
    JASSERT(DmtcpMutexLock(&rwlock->xLock) == 0);
    JASSERT(rwlock->writer == 0) (rwlock->writer);
    uint32_t old = __atomic_fetch_sub(&rwlock->nReaders, 1, __ATOMIC_SEQ_CST);
    JASSERT(old > 0); // Overflow
  }

  // If we are the last reader or the writer, wake a waiting writer.
  if (rwlock->nReaders == 0) {
    if (rwlock->nWritersQueued > 0) {
      // If writers are queued, wakeup one of them.
      ++rwlock->writersFutex;
      JASSERT(DmtcpMutexUnlock(&rwlock->xLock) == 0);
      JASSERT(futex_wake(&rwlock->writersFutex, 1) != -1) (JASSERT_ERRNO);
      return 0;
    }

    if (rwlock->nReadersQueued > 0) {
      // Wakeup all queued Readers.
      ++rwlock->readersFutex;
      JASSERT(DmtcpMutexUnlock(&rwlock->xLock) == 0);
      JASSERT(futex_wake(&rwlock->readersFutex, INT_MAX) != -1) (JASSERT_ERRNO);
      return 0;
    }
  }

  JASSERT(DmtcpMutexUnlock(&rwlock->xLock) == 0);
  return 0;
}

