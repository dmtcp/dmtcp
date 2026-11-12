#define _GNU_SOURCE
#include <dmtcp.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#define CHECK(x)  \
  do {            \
    if (!(x)) {   \
      perror(#x); \
      exit(2);    \
    }             \
  } while (0)

int
main(int argc, char **argv)
{
  CHECK(argc == 2 || argc == 3);
  volatile unsigned char stack[256 * 1024];
  for (size_t i = 0; i < sizeof(stack); ++i)
    stack[i] = (unsigned char)(i % 251);
  size_t bytes = 16 * 1024 * 1024;
  unsigned char *sparse = NULL, *tail = NULL;
  if (strcmp(argv[1], "sparse") == 0 || strcmp(argv[1], "tail") == 0) {
    sparse = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(sparse != MAP_FAILED);
    sparse[0] = 42;
    sparse[bytes / 2] = 67;
    sparse[bytes - 1] = 17;
    if (strcmp(argv[1], "sparse") == 0)
      CHECK(mprotect(sparse, bytes, PROT_READ) == 0);
  }
  if (strcmp(argv[1], "tail") == 0) {
    int fd;
    if (argc == 3) {
      fd = open(argv[2], O_CREAT | O_EXCL | O_RDWR, 0600);
    } else {
      const char *directory = getenv("DMTCP_TMPDIR");
      CHECK(directory != NULL);
      char path[PATH_MAX];
      int length =
        snprintf(path, sizeof(path), "%s/fast-tail-XXXXXX", directory);
      CHECK(length > 0 && (size_t)length < sizeof(path));
      fd = mkstemp(path);
    }
    CHECK(fd >= 0);
    CHECK(ftruncate(fd, 8193) == 0);
    unsigned char value = 43;
    CHECK(pwrite(fd, &value, 1, 0) == 1);
    // The harness retains this file until the complete round trip ends.
    // Its 8193-byte payload misaligns later area payloads in the image.
    tail = mmap(NULL, 12288, PROT_READ, MAP_PRIVATE, fd, 0);
    CHECK(tail != MAP_FAILED);
    CHECK(close(fd) == 0);
  }
  unsigned long count = 0;
  for (;;) {
    int checkpoints_before = 0, restarts_before = 0;
    dmtcp_get_local_status(&checkpoints_before, &restarts_before);
    for (size_t i = 0; i < sizeof(stack); ++i)
      CHECK(stack[i] == (unsigned char)(i % 251));
    if (sparse) {
      CHECK(sparse[0] == 42 && sparse[bytes / 2] == 67 &&
            sparse[bytes - 1] == 17);
      CHECK(sparse[4096] == 0 && sparse[bytes / 2 - 1] == 0);
    }
    if (tail)
      CHECK(tail[0] == 43 && tail[8192] == 0 && tail[12287] == 0);
    int checkpoints = 0, restarts = 0;
    dmtcp_get_local_status(&checkpoints, &restarts);
    // Do not label pre-checkpoint work as post-restart validation.
    if (restarts_before != restarts)
      continue;
    printf("PROBE mode=%s restarts=%d count=%lu\n", argv[1], restarts, ++count);
    fflush(stdout);
    usleep(100000);
  }
}
