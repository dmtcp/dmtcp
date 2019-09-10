#include <limits.h>

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
    ++rwlock->nReaders;
    JASSERT(rwlock->nReaders != 0); // Overflow
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
      ++rwlock->nReaders;
      JASSERT(rwlock->nReaders != 0); // Overflow
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


extern "C"
int DmtcpRWLockWrLock(DmtcpRWLock *rwlock)
{
  int result = 0;

  JASSERT(DmtcpMutexLock(&rwlock->xLock) == 0);

  while (1) {
    // Detect deadlock.
    if (rwlock->writer == dmtcp_gettid()) {
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

  JASSERT(DmtcpMutexUnlock(&rwlock->xLock) == 0);

  return result;
}


extern "C"
int DmtcpRWLockUnlock(DmtcpRWLock *rwlock)
{
  JASSERT(DmtcpMutexLock(&rwlock->xLock) == 0);

  if (rwlock->writer == dmtcp_gettid()) {
    rwlock->writer = 0;
  } else {
    JASSERT(rwlock->writer == 0) (rwlock->writer);
    --rwlock->nReaders;
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

