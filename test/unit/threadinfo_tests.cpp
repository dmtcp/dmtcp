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

void threadAssertBufferHelperReturnsCoreBuffer()
{
  Thread thread = {};
  size_t size = 0;

  ASSERT_EQ(Thread_GetAssertBuffer(&thread, &size),
            thread.core.assertBuffer);
  ASSERT_EQ(size, dmtcp::kAssertBufferSize);
}

void threadAssertBufferHelperHandlesNullThread()
{
  size_t size = 123;

  ASSERT_EQ(Thread_GetAssertBuffer(NULL, &size), nullptr);
  ASSERT_EQ(size, static_cast<size_t>(0));
}

} // namespace

extern const dmtcp_test::TestCase threadInfoTests[] = {
  {"ThreadInfo owns fixed assert buffer", threadInfoOwnsAssertBuffer},
  {"Thread assert buffer helper returns core buffer",
   threadAssertBufferHelperReturnsCoreBuffer},
  {"Thread assert buffer helper handles null thread",
   threadAssertBufferHelperHandlesNullThread},
};

extern const size_t threadInfoTestCount =
  sizeof(threadInfoTests) / sizeof(threadInfoTests[0]);
