#include "util_assert.h"

#include <unistd.h>

extern "C" ssize_t
dmtcp_assert_write(int fd, const void *buf, size_t count)
{
  return write(fd, buf, count);
}
