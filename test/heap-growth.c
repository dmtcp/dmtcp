/* Keep growing the main malloc heap across checkpoint/restart.
 *
 * Built with -no-pie and re-executed with address randomization disabled, so
 * the program break sits just above the executable, below where
 * mtcp_restart's (randomized) break lands. On restart the kernel's break then
 * lies above the saved one and cannot be moved back down, while glibc's cached
 * break still holds the saved value. The 64 KiB blocks are below glibc's mmap
 * threshold, so they come from the main heap and soon make glibc call sbrk().
 *
 * If address randomization cannot be disabled, prints SKIP and keeps growing
 * the heap with randomization on: still a valid checkpoint/restart worker, but
 * the bug's preconditions are then not guaranteed.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/personality.h>
#include <unistd.h>

#define BLOCK (64 * 1024)
#define MAX_BYTES (256L * 1024 * 1024)

static void
skip(const char *why)
{
  printf("SKIP: %s: %s\n", why, strerror(errno));
  fflush(stdout);
}

int
main(int argc, char *argv[])
{
  int persona = personality(0xffffffff);
  if (persona == -1) {
    skip("cannot query personality");
  } else if (!(persona & ADDR_NO_RANDOMIZE)) {
    if (personality(persona | ADDR_NO_RANDOMIZE) == -1) {
      skip("cannot disable address randomization");
    } else {
      execv("/proc/self/exe", argv);
      skip("cannot re-exec without address randomization");
    }
  }

  long total = 0;
  while (1) {
    if (total < MAX_BYTES) {
      char *p = malloc(BLOCK);  // kept on purpose: the heap only grows
      if (p == NULL) {
        perror("malloc");
        return 1;
      }
      memset(p, total & 0xff, BLOCK);
      total += BLOCK;
    }
    usleep(10000);
  }
  return 0;
}
