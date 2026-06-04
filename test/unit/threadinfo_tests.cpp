#define DMTCP_UTIL_ASSERT_NO_MACROS

#include "unit_test.h"

#include "util_assert.h"
#include "../../src/threadinfo.h"

namespace {

void threadInfoOwnsAssertBuffer()
{
  Thread thread = {};

  ASSERT_EQ(sizeof(thread.core.assertBuffer), dmtcp::kAssertBufferSize);
  ASSERT_EQ(thread.core.wrapperLockCount, 0u);
}

} // namespace

extern const dmtcp_test::TestCase threadInfoTests[] = {
  {"ThreadInfo owns fixed assert buffer", threadInfoOwnsAssertBuffer},
};

extern const size_t threadInfoTestCount =
  sizeof(threadInfoTests) / sizeof(threadInfoTests[0]);
