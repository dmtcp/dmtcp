#define _GNU_SOURCE
#include <dlfcn.h>
#include <dmtcp.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define CHECK(x)                                   \
  do {                                             \
    if (!(x)) {                                    \
      fprintf(stderr, "state mismatch: %s\n", #x); \
      exit(2);                                     \
    }                                              \
  } while (0)

int
main(int argc, char **argv)
{
  CHECK(argc == 3);
  long page = sysconf(_SC_PAGESIZE);
  CHECK(page > 0 && page < 8 * 1024 * 1024);
  int dirty = strstr(argv[1], "dirty") != NULL;
  int writable = strstr(argv[1], "writable") != NULL;
  unsigned char *data;
  if (strncmp(argv[1], "asset", 5) == 0) {
    int anonymous = strcmp(argv[1], "asset-anon") == 0;
    int tail = strcmp(argv[1], "asset-tail") == 0;
    int fd = open(argv[2], tail ? O_RDWR : O_RDONLY);
    CHECK(fd >= 0);
    if (tail)
      CHECK(ftruncate(fd, 8 * 1024 * 1024 - 1) == 0);
    int flags = strcmp(argv[1], "asset-shared") == 0 ? MAP_SHARED : MAP_PRIVATE;
    if (anonymous)
      flags |= MAP_ANONYMOUS;
    data = mmap(NULL, 8 * 1024 * 1024,
                anonymous ? PROT_READ | PROT_WRITE : PROT_READ, flags,
                anonymous ? -1 : fd, 0);
    CHECK(data != MAP_FAILED);
    CHECK(close(fd) == 0);
    if (strcmp(argv[1], "asset-deleted") == 0)
      CHECK(unlink(argv[2]) == 0);
    if (anonymous) {
      data[0] = 42;
      data[8 * 1024 * 1024 - 1] = 17;
      CHECK(mprotect(data, 8 * 1024 * 1024, PROT_READ) == 0);
    }
  } else {
    void *handle = dlopen(argv[2], RTLD_NOW);
    CHECK(handle != NULL);
    data = dlsym(handle, "snapshot_fixture");
    CHECK(data != NULL);
    CHECK((unsigned long)data % page == 0);
  }
  if (dirty || writable) {
    CHECK((unsigned long)(data + page) % page == 0);
    CHECK(mprotect(data + page, page, PROT_READ | PROT_WRITE) == 0);
    data[page] = 99;
    if (!writable)
      CHECK(mprotect(data + page, page, PROT_READ) == 0);
  }
  for (;;) {
    CHECK(data[0] == 42);
    CHECK(data[8 * 1024 * 1024 - 1] ==
          (strcmp(argv[1], "asset-tail") == 0 ? 0 : 17));
    CHECK(data[page] == ((dirty || writable) ? 99 : 0));
    int checkpoints = 0, restarts = 0;
    dmtcp_get_local_status(&checkpoints, &restarts);
    printf("progress restarts=%d\n", restarts);
    fflush(stdout);
    usleep(100000);
  }
}
