#define _GNU_SOURCE // For mremap
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>
#include <assert.h>

#include "lower_half_api.h"
#include "mmap_internal.h"

/* Deallocate any mapping for the region starting at ADDR and extending LEN
   bytes.  Returns 0 if successful, -1 for errors (and sets errno).  */
// Copied from glibc-2.27 source, with modifications to allow
// tracking of munmapped regions
extern int __real___munmap(void *, size_t );

static inline int
alignedWithLastPage(const void *haystackStart,
               const void *haystackEnd,
               const void *needleStart,
               const void *needleEnd)
{
  return needleStart >= haystackStart && needleEnd == haystackEnd;
}

static int
getOverlappingRegion(void *addr, size_t len)
{
  for (int i = 0; i < numRegions; i++) {
    void *rstart = mmaps[i].addr;
    void *rend = (char*)mmaps[i].addr + mmaps[i].len;
    void *ustart = addr;
    void *uend = (char*)addr + len;
    if (alignedWithLastPage(rstart, rend, ustart, uend)) {
      return i;
    }
  }
  return -1;
}

static void
removeMappings(void *addr, size_t len)
{
  int idx = getMmapIdx(addr);
  if (idx != -1) {
    if (mmaps[idx].len == len) {
      // Caller unmapped the entire region
      mmaps[idx].unmapped = 1;
    } else if (mmaps[idx].len > len) {
      // Move the start addr ahead by len bytes
      void *oldAddr = mmaps[idx].addr;
      void *newAddr = (char*)addr + len;
      if (mmaps[idx].guard) {
        // We need to move the guard page to the new start-of-region.
        void *guardPage = (char*)oldAddr - PAGE_SIZE;
        mremap(guardPage, PAGE_SIZE, PAGE_SIZE, MREMAP_MAYMOVE | MREMAP_FIXED,
               (char*)newAddr - PAGE_SIZE);
      }
      mmaps[idx].addr = (char*)addr + len;
      mmaps[idx].len -= len;
    } else {
      // case: len > mmaps[idx].len
      // This cannot happen.
      assert(0);
    }
  } else {
    idx = getOverlappingRegion(addr, len);
    if (idx != -1) {
      void *oldAddr = mmaps[idx].addr + mmaps[idx].len;
      mmaps[idx].len -= len;
      void *newAddr = mmaps[idx].addr + mmaps[idx].len;
      if (mmaps[idx].guard) {
        // We need to move the guard page to the new end-of-region.
        void *guardPage = oldAddr;
        mremap(guardPage, PAGE_SIZE, PAGE_SIZE, MREMAP_MAYMOVE | MREMAP_FIXED,
               (char*)newAddr);
      }
    }
  }
  // TODO: Handle the case where the removed region is in the middle of
  // of an mmaped region.
}

int
__wrap___munmap (void *addr, size_t len)
{
  int rc = __real___munmap(addr, len);
  if (rc == 0) {
    removeMappings(addr, len);
  }
  return rc;
}

// stub_warning (munmap)
// weak_alias (__munmap, munmap)
// extern __typeof (__munmap) munmap __attribute__ ((weak, alias ("__munmap")));
// extern __typeof (__munmap) __munmap __attribute__ ((visibility ("hidden")));
