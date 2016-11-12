#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__x86_64__) || defined(__aarch64__)
static const char *badAddress = (char *)0xffffffffff000000;
#else /* if defined(__x86_64__) || defined(__aarch64__) */
static const char *badAddress = (char *)0xff000000;
#endif /* if defined(__x86_64__) || defined(__aarch64__) */

int
main()
{
  struct stat buf;
  int rc;

  while (1) {
    char procFd[100];
    snprintf(procFd, sizeof(procFd), "/proc/%d/fd", getpid());
    procFd[sizeof(procFd) - 1] = '\0';

    rc = stat("/dev/tty", &buf);
    if (rc == -1) {
      return 1;
    }
    rc = stat("/etc/passwd", &buf);
    if (rc == -1) {
      return 2;
    }
    rc = stat(procFd, &buf);
    if (rc == -1) {
      return 3;
    }
    rc = stat("/etc/there_is_no_such_file_and_so_rc_is_error", &buf);
    if (rc != -1 || errno != ENOENT) {
      return 4;
    }
    rc = stat(badAddress, &buf);
    if (rc != -1 || errno != EFAULT) {
      return 5;
    }

    rc = lstat("/dev/tty", &buf);
    if (rc == -1) {
      return 1;
    }
    rc = lstat("/etc/passwd", &buf);
    if (rc == -1) {
      return 2;
    }
    rc = lstat(procFd, &buf);
    if (rc == -1) {
      return 3;
    }
    rc = lstat("/etc/there_is_no_such_file_and_so_rc_is_error", &buf);
    if (rc != -1 || errno != ENOENT) {
      return 4;
    }
    rc = lstat(badAddress, &buf);
    if (rc != -1 || errno != EFAULT) {
      return 5;
    }

    printf(". "); fflush(stdout);
    sleep(1);
  }
  return 1;  /* NOTREACHED */
}
