// Unit tests of jalib's allocator (jalib/jalloc.cpp).  A separate executable
// from dmtcp_unit_tests, which replaces JAllocDispatcher with malloc/free (see
// virtualidtable_tests.cpp): this one links the real jalloc.o.

#include "../../jalib/jalloc.h"

#include "unit_test.h"

#include <cstddef>
#include <span>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

// Above the largest jalloc pool, so every allocation is a raw (mmap) one,
// recorded in allocArenas[MAX_ARENAS] (with JALLOC_DEBUG_STATS, the default).
const size_t RAW_SIZE = 1 << 20;

// Runs fn() in a child process and checks that it exits with status 0: before
// the fix, the allocator read past the end of allocArenas[] and the process
// died with SIGSEGV, which must fail this test, not the whole unit test run.
void assertChildSucceeds(void (*fn)())
{
  pid_t pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    fn();
    _exit(0);
  }
  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  // 0 unless the child was killed by a signal (11 = SIGSEGV)
  ASSERT_EQ(WIFSIGNALED(status) ? WTERMSIG(status) : 0, 0);
  ASSERT_EQ(WEXITSTATUS(status), 0);
}

void manyRawAllocationsAndFreesChild()
{
  // Far more than MAX_ARENAS (1024) over the life of the process, as in the
  // coordinator, which makes a raw allocation and free for every big kvdb
  // value of every worker at every checkpoint.
  for (int i = 0; i < 20 * MAX_ARENAS; i++) {
    void *p = jalib::JAllocDispatcher::malloc(RAW_SIZE);
    if (p == NULL) {
      _exit(2);
    }
    jalib::JAllocDispatcher::free(p);
  }
}

void moreLiveRawAllocationsThanArenasChild()
{
  // More than MAX_ARENAS raw allocations alive at the same time: the table
  // fills up, and freeing the ones it does not record must still be safe.
  const int n = MAX_ARENAS + 100;
  static void *live[MAX_ARENAS + 100];
  for (int i = 0; i < n; i++) {
    live[i] = jalib::JAllocDispatcher::malloc(RAW_SIZE);
    if (live[i] == NULL) {
      _exit(2);
    }
  }
  for (int i = 0; i < n; i++) {
    jalib::JAllocDispatcher::free(live[i]);
  }
  // And the table is usable again afterwards.
  for (int i = 0; i < 4 * MAX_ARENAS; i++) {
    jalib::JAllocDispatcher::free(jalib::JAllocDispatcher::malloc(RAW_SIZE));
  }
}

void manyRawAllocationsAndFrees()
{
  assertChildSucceeds(manyRawAllocationsAndFreesChild);
}

void moreLiveRawAllocationsThanArenas()
{
  assertChildSucceeds(moreLiveRawAllocationsThanArenasChild);
}

} // namespace

const dmtcp_test::TestCase jallocTests[] = {
  {"more raw allocations than MAX_ARENAS over a process lifetime",
   manyRawAllocationsAndFrees},
  {"more live raw allocations than MAX_ARENAS", moreLiveRawAllocationsThanArenas},
};

int
main()
{
  return dmtcp_test::runTests(std::span<const dmtcp_test::TestCase>(jallocTests));
}
