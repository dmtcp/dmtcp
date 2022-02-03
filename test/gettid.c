#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>

// glibc-2.30 added support for gettid()
#if !__GLIBC_PREREQ(2, 30)
pid_t
gettid(void)
{
  return syscall(SYS_gettid);
}
#endif // if !__GLIBC_PREREQ(2, 30)

int main() {
  while(1) {
    if (gettid() != getpid()) {
      printf("\nERROR: 'gettid() != getpid()' in primary user thread.\n");
      printf("gettid(): %d, getpid(): %d\n\n", gettid(), getpid());
      return 1;
    }
    sleep(1);
    printf(".");
    fflush(stdout);
  }
  return 0;
}
