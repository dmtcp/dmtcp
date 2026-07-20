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
#include <cstdlib>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

namespace {

const size_t MB = 1 << 20;

// RAII setenv override that restores the previous value (or unsets it if it was
// not set) on destruction, so a test does not leak/clobber process-wide env.
class ScopedEnvOverride {
 public:
  ScopedEnvOverride(const char *name, const char *value) : name_(name)
  {
    const char *old = getenv(name);
    hadOld_ = (old != nullptr);
    if (hadOld_) {
      oldValue_ = old;
    }
    setenv(name, value, 1);
  }

  ~ScopedEnvOverride()
  {
    if (hadOld_) {
      setenv(name_.c_str(), oldValue_.c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

 private:
  std::string name_;
  bool hadOld_ = false;
  std::string oldValue_;
};

// Touch (write) every page in [addr, addr+len) so the kernel makes it present.
void touchPages(char *addr, size_t len, size_t page_size)
{
  for (size_t off = 0; off < len; off += page_size) {
    addr[off] = 1;
  }
}

// Walk [start, end) and check that scanOccupiedRangeBatch() returns the
// expected (is_zero, size) for each successive leading run.
void checkRuns(uintptr_t start,
               uintptr_t end,
               const bool *expZero,
               const uintptr_t *expSize,
               int nExp)
{
  uintptr_t addr = start;
  int idx = 0;
  while (addr < end) {
    ASSERT_TRUE(idx < nExp); // no more runs than expected
    uintptr_t sizeScanned = 0;
    bool isZero = dmtcp::Util::scanOccupiedRangeBatch(addr, end, &sizeScanned);
    ASSERT_TRUE(sizeScanned > 0); // a zero-length run would loop forever
    ASSERT_EQ(isZero, expZero[idx]);
    ASSERT_EQ(sizeScanned, expSize[idx]);
    addr += sizeScanned;
    idx++;
  }
  ASSERT_EQ(idx, nExp); // exactly the expected number of runs
}

// Occupied prefix and zero suffix, both spanning the 4MB pread-batch boundary.
void mixedRunsCrossBatchBoundary()
{
  size_t page_size = sysconf(_SC_PAGESIZE);
  size_t total = 16 * MB;
  char *region = (char *)mmap(NULL, total, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ASSERT_TRUE(region != MAP_FAILED);
  touchPages(region, 6 * MB, page_size); // [0,6MB) occupied; [6MB,16MB) zero

  const bool expZero[] = { false, true };
  const uintptr_t expSize[] = { 6 * MB, 10 * MB };
  checkRuns((uintptr_t)region, (uintptr_t)region + total, expZero, expSize, 2);

  munmap(region, total);
}

// The previously-broken case: a fully zero range that never hits a boundary.
void entireRangeZero()
{
  char *region = (char *)mmap(NULL, 4 * MB, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ASSERT_TRUE(region != MAP_FAILED);

  const bool expZero[] = { true };
  const uintptr_t expSize[] = { 4 * MB };
  checkRuns((uintptr_t)region, (uintptr_t)region + 4 * MB, expZero, expSize, 1);

  munmap(region, 4 * MB);
}

void entireRangeOccupied()
{
  size_t page_size = sysconf(_SC_PAGESIZE);
  char *region = (char *)mmap(NULL, 2 * MB, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ASSERT_TRUE(region != MAP_FAILED);
  touchPages(region, 2 * MB, page_size);

  const bool expZero[] = { false };
  const uintptr_t expSize[] = { 2 * MB };
  checkRuns((uintptr_t)region, (uintptr_t)region + 2 * MB, expZero, expSize, 1);

  munmap(region, 2 * MB);
}

// Force the portable pread() fallback (DMTCP_DISABLE_PAGEMAP_SCAN=1) and
// verify it classifies runs the same as the ioctl path, including the
// all-zero fall-off-the-end branch.
void fallbackPreadPathMatchesIoctl()
{
  size_t page_size = sysconf(_SC_PAGESIZE);
  ScopedEnvOverride forceFallback("DMTCP_DISABLE_PAGEMAP_SCAN", "1");

  size_t total = 16 * MB;
  char *mixed = (char *)mmap(NULL, total, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ASSERT_TRUE(mixed != MAP_FAILED);
  touchPages(mixed, 6 * MB, page_size); // [0,6MB) occupied; [6MB,16MB) zero
  {
    const bool expZero[] = { false, true };
    const uintptr_t expSize[] = { 6 * MB, 10 * MB };
    checkRuns((uintptr_t)mixed, (uintptr_t)mixed + total, expZero, expSize, 2);
  }
  munmap(mixed, total);

  char *zeros = (char *)mmap(NULL, total, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ASSERT_TRUE(zeros != MAP_FAILED);
  {
    const bool expZero[] = { true };
    const uintptr_t expSize[] = { total };
    checkRuns((uintptr_t)zeros, (uintptr_t)zeros + total, expZero, expSize, 1);
  }
  munmap(zeros, total);
}

void emptyRangeReturnsZeroSize()
{
  char *region = (char *)mmap(NULL, MB, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ASSERT_TRUE(region != MAP_FAILED);

  uintptr_t sizeScanned = 0xdead;
  dmtcp::Util::scanOccupiedRangeBatch((uintptr_t)region, (uintptr_t)region,
                                      &sizeScanned);
  ASSERT_EQ(sizeScanned, (uintptr_t)0);

  munmap(region, MB);
}

} // namespace

extern const dmtcp_test::TestCase pagemapScanTests[] = {
  {"mixed occupied/zero runs cross pread batch boundary",
   mixedRunsCrossBatchBoundary},
  {"entire range zero", entireRangeZero},
  {"entire range occupied", entireRangeOccupied},
  {"pread fallback matches ioctl path", fallbackPreadPathMatchesIoctl},
  {"empty range returns zero size", emptyRangeReturnsZeroSize},
};

extern const size_t pagemapScanTestCount =
  sizeof(pagemapScanTests) / sizeof(pagemapScanTests[0]);
