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
}


extern "C"
int DmtcpRWLockRdLock(DmtcpRWLock *rwlock)
{
  // Detect deadlock.
  if (rwlock->writerTid == gettid()) {
    return EDEADLK;
  }

  DmtcpRWLockStatus oldStatus;
  DmtcpRWLockStatus newStatus;

  __atomic_load(&rwlock->status, &oldStatus, __ATOMIC_RELAXED);

  uint32_t waitVal;
  do {
    newStatus = oldStatus;
    if (oldStatus.nWriters > 0) {
      newStatus.nReadersQueued++;
    } else {
      newStatus.nReaders++;
    }
    waitVal = rwlock->readerFutex;
  } while (!__atomic_compare_exchange(&rwlock->status,  &oldStatus, &newStatus,
                                     false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED));

  if (oldStatus.nWriters > 0) {
    int ret = futex_wait(&rwlock->readerFutex, waitVal);
    JASSERT(ret == 0 || errno == EAGAIN);
  }

  return 0;
}

extern "C"
int DmtcpRWLockRdUnlock(DmtcpRWLock *rwlock)
{
  DmtcpRWLockStatus oldStatus;
  DmtcpRWLockStatus newStatus;

  __atomic_load(&rwlock->status, &oldStatus, __ATOMIC_RELAXED);
  do {
    newStatus = oldStatus;
    ASSERT_NE(0, newStatus.nReaders);
    newStatus.nReaders--;
  } while (!__atomic_compare_exchange(&rwlock->status,  &oldStatus, &newStatus,
                                     false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED));

  if (newStatus.nReaders == 0 && newStatus.nWriters > 0) {
    rwlock->writerFutex++;
    JASSERT(futex_wake(&rwlock->writerFutex, 1) != -1) (JASSERT_ERRNO);
  }

  return 0;
}

extern "C"
int DmtcpRWLockWrLock(DmtcpRWLock *rwlock)
{
  // Detect deadlock.
  if (rwlock->writerTid == gettid()) {
    return EDEADLK;
  }

  DmtcpRWLockStatus oldStatus;
  DmtcpRWLockStatus newStatus;

  __atomic_load(&rwlock->status, &oldStatus, __ATOMIC_RELAXED);

  uint32_t waitVal;
  do {
    newStatus = oldStatus;
    newStatus.nWriters++;
    waitVal = rwlock->writerFutex;
  } while (!__atomic_compare_exchange(
    &rwlock->status, &oldStatus, &newStatus, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED));

  if (newStatus.nWriters > 1 || newStatus.nReaders > 0) {
    int ret = futex_wait(&rwlock->writerFutex, waitVal);
    JASSERT(ret == 0 || errno == EAGAIN);
  }

  rwlock->writerTid = gettid();

  return 0;
}

extern "C"
int DmtcpRWLockWrUnlock(DmtcpRWLock *rwlock)
{
  DmtcpRWLockStatus oldStatus;
  DmtcpRWLockStatus newStatus;

  ASSERT_EQ(gettid(), rwlock->writerTid);
  ASSERT_EQ(0, rwlock->status.nReaders);

  rwlock->writerTid = 0;

  __atomic_load(&rwlock->status, &oldStatus, __ATOMIC_RELAXED);

  do {
    newStatus = oldStatus;
    newStatus.nWriters--;
    if (newStatus.nWriters == 0) {
      newStatus.nReaders = newStatus.nReadersQueued;
      newStatus.nReadersQueued = 0;
    }
  } while (!__atomic_compare_exchange(&rwlock->status, &oldStatus, &newStatus,
                                     false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED));

  if (newStatus.nWriters > 0) {
    rwlock->writerFutex++;
    JASSERT(futex_wake(&rwlock->writerFutex, 1) != -1) (JASSERT_ERRNO);
  } else {
    rwlock->readerFutex++;
    JASSERT(futex_wake(&rwlock->readerFutex, newStatus.nReaders) != -1) (JASSERT_ERRNO);
  }

  return 0;
}

int DmtcpRWLockUnlock(DmtcpRWLock *rwlock)
{
  if (rwlock->writerTid == gettid()) {
    return DmtcpRWLockWrUnlock(rwlock);
  }

  return DmtcpRWLockRdUnlock(rwlock);
}

int DmtcpRWLockRdLockIgnoreQueuedWriter(DmtcpRWLock *rwlock)
{
  DmtcpRWLockStatus oldStatus;
  DmtcpRWLockStatus newStatus;

  __atomic_load(&rwlock->status, &oldStatus, __ATOMIC_RELAXED);
  do {
    newStatus = oldStatus;
    newStatus.nReaders++;
  } while (!__atomic_compare_exchange(&rwlock->status,  &oldStatus, &newStatus,
                                     false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED));

  return 0;
}
