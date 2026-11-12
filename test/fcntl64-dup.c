#define _GNU_SOURCE
#include <dlfcn.h>
#include <dmtcp.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x)  \
  do {            \
    if (!(x)) {   \
      perror(#x); \
      exit(2);    \
    }             \
  } while (0)

typedef int (*FcntlFunction)(int, int, ...);

int
main(int argc, char **argv)
{
  CHECK(argc == 2);
  int large = strcmp(argv[1], "large") == 0;
  FcntlFunction call_fcntl = fcntl;
  if (large) {
    // Keep ordinary-mode coverage buildable on libc without fcntl64.
    // RTLD_DEFAULT resolves DMTCP's wrapper when it is preloaded.
    call_fcntl = (FcntlFunction)dlsym(RTLD_DEFAULT, "fcntl64");
    if (call_fcntl == NULL) {
      fprintf(stderr, "fcntl64 is unavailable; skipping large mode\n");
      return 77;
    }
  }
  int fds[2], copies[2];
  CHECK(pipe(fds) == 0);
  for (int i = 0; i < 2; ++i) {
    copies[i] = call_fcntl(fds[i], F_DUPFD, 100);
    CHECK(copies[i] >= 100);
    CHECK(close(fds[i]) == 0);
  }
  // Keep the separate descriptor-flag preservation issue out of this test.
  int extra = call_fcntl(copies[0], F_DUPFD_CLOEXEC, 110);
  CHECK(extra >= 110);
  CHECK(call_fcntl(extra, F_GETFD) & FD_CLOEXEC);
  CHECK(call_fcntl(extra, F_SETFD, 0) == 0);
  CHECK(close(copies[0]) == 0);
  copies[0] = extra;
  char path[] = "/tmp/dmtcp-fcntl64-XXXXXX";
  int file = mkstemp(path);
  CHECK(file >= 0);
  CHECK(unlink(path) == 0);
  struct flock64 lock = { 0 };
  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  lock.l_len = 1;
  CHECK(call_fcntl(file, F_GETLK64, &lock) == 0);
  CHECK(lock.l_type == F_UNLCK);
  lock.l_type = F_WRLCK;
  CHECK(call_fcntl(file, F_SETLK64, &lock) == 0);
  lock.l_type = F_UNLCK;
  CHECK(call_fcntl(file, F_SETLK64, &lock) == 0);
  CHECK(close(file) == 0);
  errno = 0;
  CHECK(call_fcntl(-1, F_GETFL) == -1 && errno == EBADF);
  errno = 0;
  CHECK(call_fcntl(-1, F_GETOWN) == -1 && errno == EBADF);
  unsigned long count = 0;
  for (;;) {
    int checkpoints_before = 0, restarts_before = 0;
    dmtcp_get_local_status(&checkpoints_before, &restarts_before);
    int owner = (count % 2) ? -getpgrp() : getpid();
    CHECK(call_fcntl(copies[0], F_SETOWN, owner) == 0);
    errno = EDOM;
    CHECK(call_fcntl(copies[0], F_GETOWN) == owner);
    CHECK(errno == EDOM);
    char value = 'x', received = 0;
    CHECK(write(copies[1], &value, 1) == 1);
    CHECK(read(copies[0], &received, 1) == 1 && received == value);
    int flags = call_fcntl(copies[0], F_GETFL);
    CHECK(flags >= 0);
    CHECK(call_fcntl(copies[0], F_SETFL, flags) == 0);
    int checkpoints = 0, restarts = 0;
    dmtcp_get_local_status(&checkpoints, &restarts);
    if (restarts_before != restarts) {
      continue;
    }
    printf("PROBE fd64=%d restarts=%d count=%lu\n", large, restarts, ++count);
    fflush(stdout);
    usleep(100000);
  }
}
