/* Keep growing the main malloc heap across checkpoint/restart.
 *
 * Built with -no-pie and re-executed with address randomization disabled, so
 * the program break sits just above the executable, below where
 * mtcp_restart's (randomized) break lands. On restart the kernel's break then
 * lies above the saved one and cannot be moved back down, while glibc's cached
 * break still holds the saved value. The 64 KiB blocks are below glibc's mmap
 * threshold, so they come from the main heap and soon make glibc call sbrk().
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/personality.h>
#include <unistd.h>

#define BLOCK (64 * 1024)
#define MAX_BYTES (256L * 1024 * 1024)

int
main(int argc, char *argv[])
{
  int persona = personality(0xffffffff);
  if (persona != -1 && !(persona & ADDR_NO_RANDOMIZE)) {
    personality(persona | ADDR_NO_RANDOMIZE);
    execv("/proc/self/exe", argv);
    perror("execv");  // fall through and run randomized
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
