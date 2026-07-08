/* Regression test for restoring a huge MAP_NORESERVE anonymous region
 * (src/mtcp/mtcp_restart.c, MAP_NORESERVE_SIZE_THRESHOLD).
 *
 * Mirrors how ThreadSanitizer reserves its shadow/meta mappings: a single
 * huge MAP_ANONYMOUS|MAP_NORESERVE region with only a handful of pages ever
 * touched.  Without MAP_NORESERVE on restore, re-mapping a region this size
 * fails with ENOMEM (see the "MAP_NORESERVE for anonymous regions" commit).
 *
 * The region is far smaller on 32-bit targets (__i386__/__arm__), whose
 * entire user address space is only a few GiB, but it stays comfortably
 * above MAP_NORESERVE_SIZE_THRESHOLD so the fix under test is still
 * exercised.
 */
// _GNU_SOURCE for MAP_NORESERVE
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

#if defined(__i386__) || defined(__arm__)
# define REGION_BYTES ((size_t)50 * 1024 * 1024)               /* 50 MiB */
#else
# define REGION_BYTES ((size_t)2 * 1024 * 1024 * 1024 * 1024) /* 2 TiB */
#endif

#define NUM_TOUCHED_PAGES 5

static unsigned char *region;
static size_t page_size;
static size_t touched_offsets[NUM_TOUCHED_PAGES];

static size_t alignDown(size_t x, size_t align)
{
  return x - (x % align);
}

static unsigned char patByte(int idx)
{
  return (unsigned char)(idx * 37 + 11);
}

static void fillTouchedPages(void)
{
  for (int i = 0; i < NUM_TOUCHED_PAGES; i++) {
    region[touched_offsets[i]] = patByte(i);
  }
}

static int verifyTouchedPages(void)
{
  for (int i = 0; i < NUM_TOUCHED_PAGES; i++) {
    if (region[touched_offsets[i]] != patByte(i)) {
      printf("FAIL: touched page %d: got %u want %u\n",
             i, region[touched_offsets[i]], patByte(i));
      return 0;
    }
  }
  return 1;
}

int main(void)
{
  page_size = sysconf(_SC_PAGESIZE);

  region = mmap(NULL, REGION_BYTES, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (region == MAP_FAILED) {
    perror("mmap");
    return 2;
  }

  touched_offsets[0] = 0;
  touched_offsets[1] = alignDown(REGION_BYTES / 4, page_size);
  touched_offsets[2] = alignDown(REGION_BYTES / 2, page_size);
  touched_offsets[3] = alignDown(3 * (REGION_BYTES / 4), page_size);
  touched_offsets[4] = REGION_BYTES - page_size;

  fillTouchedPages();

  if (!verifyTouchedPages()) {
    printf("FAIL: pre-checkpoint verify\n");
    fflush(stdout);
    return 1;
  }
  printf("READY\n");
  fflush(stdout);

  for (int iter = 0;; iter++) {
    if (!verifyTouchedPages()) {
      fflush(stdout);
      return 1;
    }
    printf("OK iter %d\n", iter);
    fflush(stdout);
    sleep(1);
  }
  return 0;
}
