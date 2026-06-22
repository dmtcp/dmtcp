/* Memory-integrity regression test for the pagemap residency zero-page
 * optimization in writeckpt.cpp (Util::scanOccupiedRangeBatch()).
 *
 * Maps a large anonymous region and writes a deterministic NON-zero pattern to
 * 1/4 of the pages, leaving the rest as untouched zero holes.  It must survive
 * checkpoint/restart with:
 *   - touched (present, non-zero) pages restored byte-exact, and
 *   - untouched zero holes restored as zero.
 * On any mismatch it prints FAIL and exits, so the harness (which checks that
 * the worker stays connected) reports a failure.
 *
 * The holes are deliberately NOT read before the first checkpoint, so they stay
 * absent and the checkpoint exercises the residency "absent => zero" skip path.
 * Holes are only verified once a checkpoint has occurred (dmtcp_get_generation
 * changes); by then the checkpoint image has already captured them as absent.
 *
 * Compiles and runs both with and without DMTCP (dmtcp.h weak symbols).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

#include "dmtcp.h"

#define REGION_BYTES (32 * 1024 * 1024)

static unsigned char *region;
static size_t page_size;
static size_t npages;

static inline unsigned char patByte(size_t i, size_t j)
{
  return (unsigned char)(i * 131u + (unsigned)j * 7u + 1u);
}

static int isTouched(size_t i) { return (i % 4) == 0; }

static void fillPage(size_t i)
{
  unsigned char *p = region + i * page_size;
  for (size_t j = 0; j < page_size; j++) {
    p[j] = patByte(i, j);
  }
}

static int verifyPattern(void)
{
  for (size_t i = 0; i < npages; i++) {
    if (!isTouched(i)) {
      continue;
    }
    unsigned char *p = region + i * page_size;
    for (size_t j = 0; j < page_size; j++) {
      if (p[j] != patByte(i, j)) {
        printf("FAIL: pattern page %zu byte %zu: got %u want %u\n",
               i, j, p[j], patByte(i, j));
        return 0;
      }
    }
  }
  return 1;
}

static int verifyHolesZero(void)
{
  for (size_t i = 0; i < npages; i++) {
    if (isTouched(i)) {
      continue;
    }
    unsigned char *p = region + i * page_size;
    for (size_t j = 0; j < page_size; j++) {
      if (p[j] != 0) {
        printf("FAIL: hole page %zu byte %zu: got %u want 0\n", i, j, p[j]);
        return 0;
      }
    }
  }
  return 1;
}

int main(void)
{
  page_size = sysconf(_SC_PAGESIZE);
  npages = REGION_BYTES / page_size;
  region = mmap(NULL, REGION_BYTES, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (region == MAP_FAILED) {
    perror("mmap");
    return 2;
  }

  for (size_t i = 0; i < npages; i++) {
    if (isTouched(i)) {
      fillPage(i);
    }
  }

  int enabled = dmtcp_is_enabled();
  uint32_t gen0 = enabled ? dmtcp_get_generation() : 0;

  /* Do not read the holes yet, so the first checkpoint sees them as absent. */
  if (!verifyPattern()) {
    printf("FAIL: pre-checkpoint pattern verify\n");
    fflush(stdout);
    return 1;
  }
  printf("READY\n");
  fflush(stdout);

  for (int iter = 0;; iter++) {
    if (!verifyPattern()) {
      fflush(stdout);
      return 1;
    }
    /* Safe to read holes once a checkpoint has happened (or with no DMTCP). */
    if (!enabled || dmtcp_get_generation() != gen0) {
      if (!verifyHolesZero()) {
        fflush(stdout);
        return 1;
      }
    }
    printf("OK iter %d\n", iter);
    fflush(stdout);
    sleep(1);
  }
  return 0;
}
