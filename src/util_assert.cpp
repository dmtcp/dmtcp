#include "util_assert.h"

#include <sys/syscall.h>

#include "syscallwrappers.h"

extern "C" ssize_t
dmtcp_assert_write(int fd, const void *buf, size_t count)
{
  return static_cast<ssize_t>(
    _real_syscall(SYS_write,
                  fd,
                  reinterpret_cast<long>(buf),
                  static_cast<long>(count),
                  0,
                  0,
                  0,
                  0));
}
