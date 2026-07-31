// Tests for Util::guardPagesSupported() / Util::scanNextGuardRange().
//
// Every test returns early when the kernel cannot report guard regions
// (pre-6.15), since unit_test.h has no notion of a skipped test.

#include "util.h"

#undef ASSERT_EQ
#undef ASSERT_NE
#undef ASSERT_LT
#undef ASSERT_NULL
#undef ASSERT_NOT_NULL
#undef ASSERT_TRUE

#include "unit_test.h"

#include <cstddef>
#include <cstdint>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MADV_GUARD_INSTALL
# define MADV_GUARD_INSTALL 102
# define MADV_GUARD_REMOVE  103
#endif

namespace {

const size_t MB = 1 << 20;

// A private anonymous region, unmapped on destruction.
class Region {
 public:
  explicit Region(size_t bytes) : size_(bytes)
  {
    addr_ = (char *)mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  }

  ~Region()
  {
    if (addr_ != MAP_FAILED) {
      munmap(addr_, size_);
    }
  }

  bool valid() const { return addr_ != MAP_FAILED; }
  char *addr() const { return addr_; }
  uintptr_t base() const { return (uintptr_t)addr_; }
  uintptr_t end() const { return (uintptr_t)addr_ + size_; }

 private:
  char *addr_ = (char *)MAP_FAILED;
  size_t size_;
};

size_t pageSize()
{
  return sysconf(_SC_PAGESIZE);
}

// Install a guard over pages [firstPage, firstPage+nPages) of 'r'.
bool installGuard(const Region &r, size_t firstPage, size_t nPages)
{
  size_t ps = pageSize();
  return madvise(r.addr() + firstPage * ps, nPages * ps,
                 MADV_GUARD_INSTALL) == 0;
}

void touchPages(char *addr, size_t len, size_t page_size)
{
  for (size_t off = 0; off < len; off += page_size) {
    addr[off] = 1;
  }
}

// Assert that the first guard run in [start, end) is exactly pages
// [firstPage, firstPage+nPages) of 'r'.
void expectGuardRun(const Region &r, uintptr_t start, uintptr_t end,
                    size_t firstPage, size_t nPages)
{
  size_t ps = pageSize();
  uintptr_t gStart = 0;
  uintptr_t gEnd = 0;
  ASSERT_TRUE(dmtcp::Util::scanNextGuardRange(start, end, &gStart, &gEnd));
  ASSERT_EQ(gStart, r.base() + firstPage * ps);
  ASSERT_EQ(gEnd, r.base() + (firstPage + nPages) * ps);
}

void expectNoGuardRun(uintptr_t start, uintptr_t end)
{
  uintptr_t gStart = 0xdead;
  uintptr_t gEnd = 0xbeef;
  ASSERT_TRUE(!dmtcp::Util::scanNextGuardRange(start, end, &gStart, &gEnd));
  // Outputs must be left untouched on a miss.
  ASSERT_EQ(gStart, (uintptr_t)0xdead);
  ASSERT_EQ(gEnd, (uintptr_t)0xbeef);
}

// ---------------------------------------------------------------- tests

void guardRunAtStartOfRegion()
{
  if (!dmtcp::Util::guardPagesSupported()) { return; }
  Region r(4 * MB);
  ASSERT_TRUE(r.valid());
  ASSERT_TRUE(installGuard(r, 0, 1));

  expectGuardRun(r, r.base(), r.end(), 0, 1);
}

void guardRunInMiddleOfRegion()
{
  if (!dmtcp::Util::guardPagesSupported()) { return; }
  size_t ps = pageSize();
  Region r(4 * MB);
  ASSERT_TRUE(r.valid());
  ASSERT_TRUE(installGuard(r, 5, 2));

  expectGuardRun(r, r.base(), r.end(), 5, 2);
  // Nothing after the run.
  expectNoGuardRun(r.base() + 7 * ps, r.end());
}

void guardRunAtEndOfRegionIsClamped()
{
  if (!dmtcp::Util::guardPagesSupported()) { return; }
  size_t ps = pageSize();
  size_t npages = (4 * MB) / ps;
  Region r(4 * MB);
  ASSERT_TRUE(r.valid());
  ASSERT_TRUE(installGuard(r, npages - 1, 1));

  expectGuardRun(r, r.base(), r.end(), npages - 1, 1);
}

// Successive scans from the previous guardEnd must enumerate every run in order.
void multipleGuardRunsAreEnumeratedInOrder()
{
  if (!dmtcp::Util::guardPagesSupported()) { return; }
  size_t ps = pageSize();
  Region r(4 * MB);
  ASSERT_TRUE(r.valid());
  touchPages(r.addr(), 4 * MB, ps); // interleave with present pages
  ASSERT_TRUE(installGuard(r, 3, 1));
  ASSERT_TRUE(installGuard(r, 10, 3));
  ASSERT_TRUE(installGuard(r, 20, 1));

  uintptr_t gStart = 0;
  uintptr_t gEnd = 0;
  ASSERT_TRUE(dmtcp::Util::scanNextGuardRange(r.base(), r.end(),
                                              &gStart, &gEnd));
  ASSERT_EQ(gStart, r.base() + 3 * ps);
  ASSERT_EQ(gEnd, r.base() + 4 * ps);

  ASSERT_TRUE(dmtcp::Util::scanNextGuardRange(gEnd, r.end(), &gStart, &gEnd));
  ASSERT_EQ(gStart, r.base() + 10 * ps);
  ASSERT_EQ(gEnd, r.base() + 13 * ps);

  ASSERT_TRUE(dmtcp::Util::scanNextGuardRange(gEnd, r.end(), &gStart, &gEnd));
  ASSERT_EQ(gStart, r.base() + 20 * ps);
  ASSERT_EQ(gEnd, r.base() + 21 * ps);

  expectNoGuardRun(gEnd, r.end());
}

void unguardedRegionReportsNoRun()
{
  if (!dmtcp::Util::guardPagesSupported()) { return; }
  size_t ps = pageSize();
  Region r(2 * MB);
  ASSERT_TRUE(r.valid());
  touchPages(r.addr(), MB, ps); // half present, half absent, no guards

  expectNoGuardRun(r.base(), r.end());
}

void emptyRangeReportsNoRun()
{
  if (!dmtcp::Util::guardPagesSupported()) { return; }
  Region r(MB);
  ASSERT_TRUE(r.valid());
  ASSERT_TRUE(installGuard(r, 0, 1));

  expectNoGuardRun(r.base(), r.base());
}

// A guard PTE sets the same "swapped" bit as a paged-out page, so a paged-out
// page must not be reported as a guard page.
void swappedPageIsNotReportedAsGuard()
{
  if (!dmtcp::Util::guardPagesSupported()) { return; }
  size_t ps = pageSize();
  Region r(2 * MB);
  ASSERT_TRUE(r.valid());
  touchPages(r.addr(), 2 * MB, ps);

#ifdef MADV_PAGEOUT
  if (madvise(r.addr(), 2 * MB, MADV_PAGEOUT) != 0) {
    return; // no swap configured, or unsupported: nothing to assert
  }
#else // ifdef MADV_PAGEOUT
  return;
#endif // ifdef MADV_PAGEOUT

  expectNoGuardRun(r.base(), r.end());
}

// GuardPages::reinstall() re-guards a range without restoring PROT_WRITE, which
// relies on a guard surviving a protection change.
void guardRunSurvivesMprotect()
{
  if (!dmtcp::Util::guardPagesSupported()) { return; }
  Region r(2 * MB);
  ASSERT_TRUE(r.valid());
  ASSERT_TRUE(installGuard(r, 4, 1));

  ASSERT_EQ(mprotect(r.addr(), 2 * MB, PROT_READ), 0);
  expectGuardRun(r, r.base(), r.end(), 4, 1);

  ASSERT_EQ(mprotect(r.addr(), 2 * MB, PROT_READ | PROT_WRITE), 0);
}

// The capability predicate never installs a guard page, so this is a real check
// that a kernel which reports guard regions also accepts them.
void detectionImpliesInstallability()
{
  if (!dmtcp::Util::guardPagesSupported()) { return; }
  Region r(MB);
  ASSERT_TRUE(r.valid());
  ASSERT_TRUE(installGuard(r, 0, 1));
}

// The predicate is deliberately uncached and is called afresh every checkpoint,
// so repeated calls must agree and must not disturb scanning.
void capabilityPredicateIsRepeatable()
{
  const bool first = dmtcp::Util::guardPagesSupported();

  ASSERT_EQ(dmtcp::Util::guardPagesSupported(), first);
  ASSERT_EQ(dmtcp::Util::guardPagesSupported(), first);

  if (!first) { return; }

  Region r(MB);
  ASSERT_TRUE(r.valid());
  ASSERT_TRUE(installGuard(r, 2, 1));
  expectGuardRun(r, r.base(), r.end(), 2, 1);
}

} // namespace

extern const dmtcp_test::TestCase guardRegionTests[] = {
  {"guard run at start of region", guardRunAtStartOfRegion},
  {"guard run in middle of region", guardRunInMiddleOfRegion},
  {"guard run at end of region is clamped", guardRunAtEndOfRegionIsClamped},
  {"multiple guard runs enumerated in order",
   multipleGuardRunsAreEnumeratedInOrder},
  {"unguarded region reports no run", unguardedRegionReportsNoRun},
  {"empty range reports no run", emptyRangeReportsNoRun},
  {"swapped page is not reported as guard", swappedPageIsNotReportedAsGuard},
  {"guard run survives mprotect", guardRunSurvivesMprotect},
  {"detection implies installability", detectionImpliesInstallability},
  {"capability predicate is repeatable", capabilityPredicateIsRepeatable},
};

extern const size_t guardRegionTestCount =
  sizeof(guardRegionTests) / sizeof(guardRegionTests[0]);
