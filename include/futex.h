#include <unistd.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/time.h>

static inline int
futex(uint32_t *uaddr,
      int futex_op,
      uint32_t val,
      const struct timespec *timeout,
      int *uaddr2,
      int val3)
{
  return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr, val3);
}


static inline int
futex_wait(uint32_t *uaddr, uint32_t old_val)
{
  return futex(uaddr, FUTEX_WAIT, old_val, NULL, NULL, 0);
}


static inline int
futex_wake(uint32_t *uaddr, uint32_t num)
{
  return futex(uaddr, FUTEX_WAKE, num, NULL, NULL, 0);
}
