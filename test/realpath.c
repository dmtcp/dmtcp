#define _XOPEN_SOURCE 500
#include <limits.h>
#include <stdlib.h>

// POSIX.1-2008 says that NULL will cause memory to be allocated.
// In the earlier POSIX.1-2001, GNU implementations would return NULL.
// GNU libc contains two symbols:  one for each flavor.
// Hence, this test can succeed only in newer GNU libc's supporting POSIX.1-2008
// A value of _XOPEN_SOURCE of 500 should support this.
// Configure autotest first to test if this program can succeed without DMTCP.
int main() {
  while (1) {
    char *path = realpath("/etc/passwd", NULL);
    if (path == NULL)
      abort();
    else
      free(path);
  }
  return 0;
}
