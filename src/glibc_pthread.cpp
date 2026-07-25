#include "glibc_pthread.h"

#include <string.h>

#include "futex.h"
#include "util.h"

#ifdef USE_VIRTUAL_TID_LIBC_STRUCT_PTHREAD

#define LLL_PRIVATE 0

static void
glibc_lll_lock(int *futex)
{
  int expected = 0;
  // Match glibc's low-level lock states: 0 is unlocked, 1 is locked with
  // no waiters, and values greater than 1 mean locked with possible waiters.
  // A contending thread must publish the >1 state before sleeping so unlock
  // knows to futex_wake().
  const int lockedWithWaiters = 2;
  if (__atomic_compare_exchange_n(
      futex, &expected, 1, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
    return;
  }

  while (__atomic_exchange_n(futex, lockedWithWaiters,
                             __ATOMIC_ACQ_REL) != 0) {
    futex_wait((unsigned int *)futex, lockedWithWaiters);
  }
}

static void
glibc_lll_unlock(int *futex)
{
  int oldval = __atomic_exchange_n(futex, 0, __ATOMIC_ACQ_REL);
  // Only the contended state requires a wake; state 1 had no waiters.
  if (oldval > 1) {
    futex_wake((unsigned int *)futex, 1);
  }
}

LibcPthreadShim
LibcPthreadShim::from(pthread_t th)
{
  // This helper only discovers glibc pthread field addresses.  It must not
  // initialize DMTCP: sanitizer/libc startup can call it before DMTCP is
  // ready.  Current-thread TID virtualization is handled during
  // ThreadList::init().
  int libcMinor = dmtcp::Util::glibcVersion().minor;
  LibcPthreadShim ret = {};

  if (libcMinor >= 33) {
    struct libc2_33_pthread *lth = (struct libc2_33_pthread *)th;
    ret.tid_ = &lth->tid;
    ret.flags_ = &lth->flags;
    ret.lock_ = &lth->lock;
    ret.joinid_ = &lth->joinid;
    ret.schedparam_ = &lth->schedparam;
    ret.schedpolicy_ = &lth->schedpolicy;
    ret.stackblock_ = &lth->stackblock;
    ret.stackblock_size_ = &lth->stackblock_size;
    ret.guardsize_ = &lth->guardsize;
    ret.reported_guardsize_ = &lth->reported_guardsize;
  } else if (libcMinor >= 11) {
    struct libc2_11_pthread *lth = (struct libc2_11_pthread *)th;
    ret.tid_ = &lth->tid;
    ret.pid_ = &lth->pid;
    ret.flags_ = &lth->flags;
    ret.lock_ = &lth->lock;
    ret.joinid_ = &lth->joinid;
    ret.schedparam_ = &lth->schedparam;
    ret.schedpolicy_ = &lth->schedpolicy;
    ret.stackblock_ = &lth->stackblock;
    ret.stackblock_size_ = &lth->stackblock_size;
    ret.guardsize_ = &lth->guardsize;
    ret.reported_guardsize_ = &lth->reported_guardsize;
  } else if (libcMinor >= 10) {
    struct libc2_10_pthread *lth = (struct libc2_10_pthread *)th;
    ret.tid_ = &lth->tid;
    ret.pid_ = &lth->pid;
    ret.flags_ = &lth->flags;
    ret.lock_ = &lth->lock;
    ret.joinid_ = &lth->joinid;
    ret.schedparam_ = &lth->schedparam;
    ret.schedpolicy_ = &lth->schedpolicy;
    ret.stackblock_ = &lth->stackblock;
    ret.stackblock_size_ = &lth->stackblock_size;
    ret.guardsize_ = &lth->guardsize;
    ret.reported_guardsize_ = &lth->reported_guardsize;
  } else {
    struct libc2_x_pthread *lth = (struct libc2_x_pthread *)th;
    ret.tid_ = &lth->tid;
    ret.pid_ = &lth->pid;
    ret.flags_ = &lth->flags;
    ret.lock_ = &lth->lock;
    ret.joinid_ = &lth->joinid;
    ret.schedparam_ = &lth->schedparam;
    ret.schedpolicy_ = &lth->schedpolicy;
    ret.stackblock_ = &lth->stackblock;
    ret.stackblock_size_ = &lth->stackblock_size;
    ret.guardsize_ = &lth->guardsize;
    ret.reported_guardsize_ = &lth->reported_guardsize;
  }

  return ret;
}

void
LibcPthreadShim::lllLock() const
{
  glibc_lll_lock(lock_);
}

void
LibcPthreadShim::lllUnlock() const
{
  glibc_lll_unlock(lock_);
}

void
LibcPthreadShim::setSchedParam(int policy,
                               const struct sched_param *param) const
{
  *schedpolicy_ = policy;
  memcpy(schedparam_, param, sizeof(struct sched_param));
}

void
LibcPthreadShim::getSchedParam(int *policy, struct sched_param *param) const
{
  *policy = *schedpolicy_;
  memcpy(param, schedparam_, sizeof(struct sched_param));
}

#endif // #ifdef USE_VIRTUAL_TID_LIBC_STRUCT_PTHREAD
