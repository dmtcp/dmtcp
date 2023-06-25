#include "glibc_pthread.h"

#include <string.h>
#include <atomic>

#include "futex.h"

#ifdef USE_VIRTUAL_TID_LIBC_STRUCT_PTHREAD

#define LLL_PRIVATE 0

static inline int
glibcMajorVersion()
{
  static int major = 0;
  if (major == 0) {
    major = (int)strtol(gnu_get_libc_version(), NULL, 10);
  }
  return major;
}

static inline int
glibcMinorVersion()
{
  static long minor = 0;
  if (minor == 0) {
    char *ptr;
    strtol(gnu_get_libc_version(), &ptr, 10);
    minor = (int)strtol(ptr + 1, NULL, 10);
  }
  return minor;
}

void
glibc_lll_lock(int *futex)
{
  static int free = 0;
  static int waiters = 2;
  if (__atomic_compare_exchange_n(
      futex, &free, 1, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
    return;
  }

  while (__atomic_exchange_n(futex, waiters, __ATOMIC_ACQ_REL) != 0) {
    futex_wait((unsigned int *)futex, waiters);
  }
}

void
glibc_lll_unlock(int *futex)
{
  static int free = 0;
  int oldval = __atomic_exchange_n(futex, free, __ATOMIC_ACQ_REL);
  if (oldval > 1) {
    futex_wake((unsigned int *)futex, 1);
  }
}

struct libc_pthread_addr dmtcp_pthread_get_addrs(pthread_t th)
{
  static int libcMinor = glibcMinorVersion();

  struct libc_pthread_addr ret;

  if (libcMinor >= 33) {
    struct libc2_33_pthread *lth = (struct libc2_33_pthread *)th;
    ret.tid = &lth->tid;
    ret.cancelhandling = &lth->cancelhandling;
    ret.flags = &lth->flags;
    ret.lock = &lth->lock;
    ret.joinid = &lth->joinid;
    ret.schedparam = &lth->schedparam;
    ret.schedpolicy = &lth->schedpolicy;
    ret.stackblock = &lth->stackblock;
    ret.stackblock_size = &lth->stackblock_size;
    ret.guardsize = &lth->guardsize;
    ret.reported_guardsize = &lth->reported_guardsize;
  } else if (libcMinor >= 11) {
    struct libc2_11_pthread *lth = (struct libc2_11_pthread *)th;
    ret.tid = &lth->tid;
    ret.cancelhandling = &lth->cancelhandling;
    ret.flags = &lth->flags;
    ret.lock = &lth->lock;
    ret.joinid = &lth->joinid;
    ret.schedparam = &lth->schedparam;
    ret.schedpolicy = &lth->schedpolicy;
    ret.stackblock = &lth->stackblock;
    ret.stackblock_size = &lth->stackblock_size;
    ret.guardsize = &lth->guardsize;
    ret.reported_guardsize = &lth->reported_guardsize;
  } else if (libcMinor >= 10) {
    struct libc2_10_pthread *lth = (struct libc2_10_pthread *)th;
    ret.tid = &lth->tid;
    ret.cancelhandling = &lth->cancelhandling;
    ret.flags = &lth->flags;
    ret.lock = &lth->lock;
    ret.joinid = &lth->joinid;
    ret.schedparam = &lth->schedparam;
    ret.schedpolicy = &lth->schedpolicy;
    ret.stackblock = &lth->stackblock;
    ret.stackblock_size = &lth->stackblock_size;
    ret.guardsize = &lth->guardsize;
    ret.reported_guardsize = &lth->reported_guardsize;
  } else {
    struct libc2_x_pthread *lth = (struct libc2_x_pthread *)th;
    ret.tid = &lth->tid;
    ret.cancelhandling = &lth->cancelhandling;
    ret.flags = &lth->flags;
    ret.lock = &lth->lock;
    ret.joinid = &lth->joinid;
    ret.schedparam = &lth->schedparam;
    ret.schedpolicy = &lth->schedpolicy;
    ret.stackblock = &lth->stackblock;
    ret.stackblock_size = &lth->stackblock_size;
    ret.guardsize = &lth->guardsize;
    ret.reported_guardsize = &lth->reported_guardsize;
  }

  return ret;
}

void
dmtcp_pthread_lll_lock(pthread_t th)
{
  libc_pthread_addr th_addr = dmtcp_pthread_get_addrs(th);
  glibc_lll_lock(th_addr.lock);
}

void
dmtcp_pthread_lll_unlock(pthread_t th)
{
  libc_pthread_addr th_addr = dmtcp_pthread_get_addrs(th);
  glibc_lll_unlock(th_addr.lock);
}

pid_t *
dmtcp_pthread_get_tid_addr(pthread_t th)
{
  libc_pthread_addr th_addr = dmtcp_pthread_get_addrs(th);
  return th_addr.tid;
}

pid_t
dmtcp_pthread_get_tid(pthread_t th)
{
  libc_pthread_addr th_addr = dmtcp_pthread_get_addrs(th);
  return *th_addr.tid;
}

void
dmtcp_pthread_set_tid(pthread_t th, pid_t tid)
{
  libc_pthread_addr th_addr = dmtcp_pthread_get_addrs(th);
  *th_addr.tid = tid;
}

int
dmtcp_pthread_get_flags(pthread_t th)
{
  libc_pthread_addr th_addr = dmtcp_pthread_get_addrs(th);
  return *th_addr.tid;
}

void
dmtcp_pthread_set_flags(pthread_t th, int flags)
{
  libc_pthread_addr th_addr = dmtcp_pthread_get_addrs(th);
  *th_addr.flags = flags;
}

void
dmtcp_pthread_set_schedparam(pthread_t th,
                             int policy,
                             const struct sched_param *param)
{
  libc_pthread_addr th_addr = dmtcp_pthread_get_addrs(th);

  *th_addr.schedpolicy = policy;
  memcpy(th_addr.schedparam, param, sizeof(struct sched_param));
}

void
dmtcp_pthread_get_schedparam(pthread_t th,
                             int *policy,
                             struct sched_param *param)
{
  libc_pthread_addr th_addr = dmtcp_pthread_get_addrs(th);

  *policy = *th_addr.schedpolicy;
  memcpy(param, th_addr.schedparam, sizeof(struct sched_param));
}

#endif // #ifdef USE_VIRTUAL_TID_LIBC_STRUCT_PTHREAD
