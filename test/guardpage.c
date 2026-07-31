/* Checkpoint/restart test for lightweight guard pages
 * (madvise(MADV_GUARD_INSTALL), Linux 6.13+; visible in the pagemap since 6.15).
 *
 * Installs guards over two disjoint page ranges of a patterned anonymous region,
 * plus one in a read-only region, one in [heap], and one in a private
 * file-backed mapping.  On every generation change -- after a checkpoint, and
 * after a restart -- re-checks that each guarded page still faults, that its
 * immediate neighbours are byte-exact (catching an off-by-one or over-wide
 * reinstall), and that every other page is byte-exact.
 *
 * Self-skips, printing SKIP and then running as a plain worker so the harness
 * still sees a healthy process, where DMTCP cannot see guard pages: kernels
 * that cannot install them (< 6.13), and 6.13 and 6.14, which install them but
 * do not report them in /proc/self/pagemap.
 *
 * Compiles and runs both with and without DMTCP (dmtcp.h weak symbols).
 */
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/fs.h>

#include "dmtcp.h"

#ifndef MADV_GUARD_INSTALL
# define MADV_GUARD_INSTALL 102
# define MADV_GUARD_REMOVE  103
#endif

/* PAGEMAP_SCAN reached <linux/fs.h> in Linux 6.7 and the PAGE_IS_GUARD category
 * in 6.15; defined here for build environments whose headers predate them, so
 * that the skip decision below follows the running kernel, as DMTCP's does. */
#ifndef PAGEMAP_SCAN
struct page_region {
  uint64_t start;
  uint64_t end;
  uint64_t categories;
};

struct pm_scan_arg {
  uint64_t size, flags, start, end, walk_end, vec, vec_len, max_pages;
  uint64_t category_inverted, category_mask, category_anyof_mask, return_mask;
};

# define PAGEMAP_SCAN _IOWR('f', 16, struct pm_scan_arg)
#endif

#ifndef PAGE_IS_GUARD
# define PAGE_IS_GUARD (1 << 8)
#endif

#define NPAGES 32

/* Guarded page indices: one single page, one 2-page run, both with unguarded
 * neighbours on each side so an over-wide reinstall is detectable. */
#define GUARD_A     5  /* pages [5,6)  */
#define GUARD_B    20  /* pages [20,22) */
#define GUARD_B_N   2

/* Private file-backed mapping: guard on page 1 of 4. */
#define FILE_NPAGES 4
#define FILE_GUARD  1
#define SNAP_BYTES  64 /* bytes of each unguarded file page we re-compare */

static size_t page_size;
static unsigned char *region;   /* read-write region */
static unsigned char *roRegion; /* read-only region, guard on page 1 */
static unsigned char *heapGuard; /* guarded page inside [heap], or NULL */
static unsigned char *fileRegion; /* private file-backed mapping, or NULL */
static unsigned char fileSnap[FILE_NPAGES][SNAP_BYTES];

static sigjmp_buf faultJmp;
static volatile sig_atomic_t faulted;

static void faultHandler(int sig)
{
  (void)sig;
  faulted = 1;
  siglongjmp(faultJmp, 1);
}

static int isGuarded(size_t i)
{
  return i == GUARD_A || (i >= GUARD_B && i < GUARD_B + GUARD_B_N);
}

static unsigned char patByte(size_t i, size_t j)
{
  return (unsigned char)(i * 37u + (unsigned)j * 11u + 3u);
}

static void fillPage(unsigned char *base, size_t i)
{
  unsigned char *p = base + i * page_size;
  for (size_t j = 0; j < page_size; j++) {
    p[j] = patByte(i, j);
  }
}

/* Reads one byte of a page, returning 1 if it faulted (i.e. is guarded).
 * Installs the SIGSEGV/SIGBUS handler only for the duration of the probe. */
static int pageFaults(unsigned char *base, size_t i)
{
  struct sigaction sa, oldSegv, oldBus;
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = faultHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_NODEFER; /* let the handler run again after siglongjmp */
  sigaction(SIGSEGV, &sa, &oldSegv);
  sigaction(SIGBUS, &sa, &oldBus);

  faulted = 0;
  if (sigsetjmp(faultJmp, 1) == 0) {
    volatile unsigned char sink = base[i * page_size];
    (void)sink;
  }

  sigaction(SIGSEGV, &oldSegv, NULL);
  sigaction(SIGBUS, &oldBus, NULL);
  return faulted != 0;
}

static int verify(const char *phase)
{
  /* Guarded pages must still fault. */
  if (!pageFaults(region, GUARD_A)) {
    printf("FAIL: %s: guarded page %d is readable; guard lost\n",
           phase, GUARD_A);
    return 0;
  }
  for (size_t i = GUARD_B; i < GUARD_B + GUARD_B_N; i++) {
    if (!pageFaults(region, i)) {
      printf("FAIL: %s: guarded page %zu is readable; guard lost\n", phase, i);
      return 0;
    }
  }

  /* Every unguarded page must be intact.  This is what catches a guard
   * reinstalled at the wrong address or over too many pages. */
  for (size_t i = 0; i < NPAGES; i++) {
    if (isGuarded(i)) {
      continue;
    }
    if (pageFaults(region, i)) {
      printf("FAIL: %s: unguarded page %zu faults; guard is too wide\n",
             phase, i);
      return 0;
    }
    unsigned char *p = region + i * page_size;
    for (size_t j = 0; j < page_size; j++) {
      if (p[j] != patByte(i, j)) {
        printf("FAIL: %s: page %zu byte %zu: got %u want %u\n",
               phase, i, j, p[j], patByte(i, j));
        return 0;
      }
    }
  }

  /* The guard in the read-only region must also survive. */
  if (!pageFaults(roRegion, 1)) {
    printf("FAIL: %s: guard lost in read-only region\n", phase);
    return 0;
  }
  if (pageFaults(roRegion, 0)) {
    printf("FAIL: %s: read-only region page 0 faults unexpectedly\n", phase);
    return 0;
  }

  /* [heap] takes a different path in DMTCP: named anonymous areas use the
   * content scan, which physically reads every page. */
  if (heapGuard != NULL && !pageFaults(heapGuard, 0)) {
    printf("FAIL: %s: guard lost in [heap]\n", phase);
    return 0;
  }

  /* Written as one blob, so DMTCP drops and reinstalls this guard. */
  if (fileRegion != NULL) {
    if (!pageFaults(fileRegion, FILE_GUARD)) {
      printf("FAIL: %s: guard lost in private file-backed mapping\n", phase);
      return 0;
    }
    for (size_t i = 0; i < FILE_NPAGES; i++) {
      if (i == FILE_GUARD) {
        continue;
      }
      if (pageFaults(fileRegion, i)) {
        printf("FAIL: %s: file-backed page %zu faults; guard is too wide\n",
               phase, i);
        return 0;
      }
      if (memcmp(fileRegion + i * page_size, fileSnap[i], SNAP_BYTES) != 0) {
        printf("FAIL: %s: file-backed page %zu content changed\n", phase, i);
        return 0;
      }
    }
  }

  return 1;
}

/* True if ioctl(PAGEMAP_SCAN) reports 'pg' as a guard page (PAGE_IS_GUARD,
 * Linux 6.15+) -- which, not whether MADV_GUARD_INSTALL succeeded, is what
 * DMTCP depends on.  This is the same call DMTCP makes, so skipping when it
 * fails skips exactly the kernels DMTCP cannot handle. */
static int isGuardReportable(void *pg)
{
  struct page_region region;
  struct pm_scan_arg arg;
  long n;
  int fd = open("/proc/self/pagemap", O_RDONLY);

  if (fd < 0) {
    return 0;
  }
  memset(&arg, 0, sizeof arg);
  arg.size = sizeof arg;
  arg.start = (uint64_t)((uintptr_t)pg & ~(uintptr_t)(page_size - 1));
  arg.end = arg.start + page_size;
  arg.vec = (uint64_t)(uintptr_t)&region;
  arg.vec_len = 1;
  arg.category_mask = PAGE_IS_GUARD;
  arg.return_mask = PAGE_IS_GUARD;
  n = ioctl(fd, PAGEMAP_SCAN, &arg);
  close(fd);
  return n > 0; /* <0: category unknown to this kernel.  0: not a guard page. */
}

/* Returns true if [addr, addr+page_size) lies inside the [heap] mapping. */
static int isInBrkHeap(unsigned char *addr)
{
  char line[512];
  FILE *f = fopen("/proc/self/maps", "r");
  int inHeap = 0;

  if (f == NULL) {
    return 0;
  }
  while (fgets(line, sizeof line, f) != NULL) {
    unsigned long lo, hi;
    if (strstr(line, "[heap]") == NULL) {
      continue;
    }
    if (sscanf(line, "%lx-%lx", &lo, &hi) == 2 &&
        (uintptr_t)addr >= lo && (uintptr_t)addr + page_size <= hi) {
      inHeap = 1;
    }
    break;
  }
  fclose(f);
  return inHeap;
}

/* Installs a guard on a page inside [heap]; returns it, or NULL to skip.
 *
 * The page has to be one we own, since a guarded page must never be touched
 * again and poisoning an arbitrary heap page would corrupt the allocator.  A
 * small malloc() usually comes from the brk arena and so lands in [heap], but
 * that is not guaranteed, hence the /proc/self/maps check. */
static unsigned char *installHeapGuard(void)
{
  size_t want = 8 * page_size;
  unsigned char *chunk = malloc(want);
  unsigned char *pg;

  if (chunk == NULL) {
    return NULL;
  }
  memset(chunk, 0xAB, want);

  /* First page boundary at least one page into our own chunk. */
  pg = (unsigned char *)((((uintptr_t)chunk + page_size - 1)
                          & ~(uintptr_t)(page_size - 1)) + page_size);
  if (pg + page_size > chunk + want) {
    return NULL;
  }
  if (!isInBrkHeap(pg)) {
    return NULL; /* served from an mmap arena; the anon region already covers it */
  }
  if (madvise(pg, page_size, MADV_GUARD_INSTALL) != 0) {
    return NULL;
  }
  return pg;
}

/* Installs a guard on a page of a private, file-backed mapping; returns the
 * mapping, or NULL to skip.  This is the case that reaches writememoryarea()'s
 * single-blob path, where the guard cannot be carved out of the write with a
 * zero-run header and is instead written to the image as zeros.
 * (MAP_SHARED file mappings take the same path.)  Maps our own executable:
 * always present, immutable, and still there on restart. */
static unsigned char *installFileGuard(void)
{
  struct stat st;
  unsigned char *p;
  int fd = open("/proc/self/exe", O_RDONLY);

  if (fd < 0) {
    return NULL;
  }
  if (fstat(fd, &st) != 0 ||
      (size_t)st.st_size < FILE_NPAGES * page_size) {
    close(fd);
    return NULL; /* too small to map whole; reading past EOF would SIGBUS */
  }
  p = mmap(NULL, FILE_NPAGES * page_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (p == MAP_FAILED) {
    return NULL;
  }

  /* Snapshot the pages we keep re-checking, before poisoning one of them. */
  for (size_t i = 0; i < FILE_NPAGES; i++) {
    if (i != FILE_GUARD) {
      memcpy(fileSnap[i], p + i * page_size, SNAP_BYTES);
    }
  }

  if (madvise(p + FILE_GUARD * page_size, page_size,
              MADV_GUARD_INSTALL) != 0) {
    munmap(p, FILE_NPAGES * page_size);
    return NULL;
  }
  return p;
}

/* Runs forever as a healthy worker without exercising guard pages, for kernels
 * that do not support them. */
static int skipLoop(const char *why)
{
  printf("SKIP: %s\n", why);
  printf("READY\n");
  fflush(stdout);
  for (int iter = 0;; iter++) {
    printf("OK (skipped) %d\n", iter);
    fflush(stdout);
    sleep(1);
  }
}

int main(void)
{
  page_size = sysconf(_SC_PAGESIZE);

  region = mmap(NULL, NPAGES * page_size, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  roRegion = mmap(NULL, 4 * page_size, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (region == MAP_FAILED || roRegion == MAP_FAILED) {
    perror("mmap");
    return 2;
  }

  for (size_t i = 0; i < NPAGES; i++) {
    fillPage(region, i);
  }
  fillPage(roRegion, 0);

  if (madvise(region + GUARD_A * page_size, page_size,
              MADV_GUARD_INSTALL) != 0) {
    return skipLoop("MADV_GUARD_INSTALL unsupported (needs Linux >= 6.13)");
  }
  /* Installing is not enough: DMTCP locates guard pages through
   * /proc/self/pagemap, which only reports them from Linux 6.15 on. */
  if (!isGuardReportable(region + GUARD_A * page_size)) {
    return skipLoop("guard pages are not reported in /proc/self/pagemap "
                    "(Linux 6.13/6.14; needs >= 6.15)");
  }
  if (madvise(region + GUARD_B * page_size, GUARD_B_N * page_size,
              MADV_GUARD_INSTALL) != 0) {
    return skipLoop("MADV_GUARD_INSTALL unsupported for multi-page run");
  }
  if (madvise(roRegion + page_size, page_size, MADV_GUARD_INSTALL) != 0) {
    return skipLoop("MADV_GUARD_INSTALL unsupported in second region");
  }
  /* Make the second region non-writable, to cover reinstalling a guard on a
   * mapping that lacks PROT_WRITE.  Guard regions survive attribute changes. */
  if (mprotect(roRegion, 4 * page_size, PROT_READ) != 0) {
    perror("mprotect");
    return 2;
  }

  heapGuard = installHeapGuard();
  printf("heap guard: %s\n", heapGuard ? "installed" : "skipped");

  fileRegion = installFileGuard();
  printf("file-backed guard: %s\n", fileRegion ? "installed" : "skipped");

  if (!verify("pre-checkpoint")) {
    fflush(stdout);
    return 1;
  }

  int enabled = dmtcp_is_enabled();
  uint32_t lastGen = enabled ? dmtcp_get_generation() : 0;

  printf("READY\n");
  fflush(stdout);

  for (int iter = 0;; iter++) {
    /* Re-verify whenever a checkpoint or restart has happened.  The first
     * generation change proves the checkpoint neither read nor uninstalled the
     * guards; after a restart it proves they were reinstalled correctly. */
    if (enabled) {
      uint32_t gen = dmtcp_get_generation();
      if (gen != lastGen) {
        lastGen = gen;
        if (!verify("post-generation-change")) {
          fflush(stdout);
          return 1;
        }
      }
    }

    printf("OK %d\n", iter);
    fflush(stdout);
    sleep(1);
  }
}
