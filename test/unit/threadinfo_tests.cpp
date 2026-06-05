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

void threadCoreInfoInitResetsDiagnosticState()
{
  ThreadCoreInfo core = {};
  core.wrapperLockCount = 7;
  core.assertBuffer[0] = 'x';

  ThreadCoreInfo_Init(&core);

  ASSERT_EQ(core.wrapperLockCount, 0u);
  ASSERT_EQ(core.assertBuffer[0], '\0');
}

void threadCoreInfoBufferHelperReturnsCoreBuffer()
{
  ThreadCoreInfo core = {};
  size_t size = 0;

  ASSERT_EQ(ThreadCoreInfo_GetAssertBuffer(&core, &size),
            core.assertBuffer);
  ASSERT_EQ(size, dmtcp::kAssertBufferSize);
}

void threadCoreInfoTracksWrapperLockCount()
{
  ThreadCoreInfo core = {};
  ThreadCoreInfo_Init(&core);

  ASSERT_EQ(ThreadCoreInfo_GetWrapperLockCount(&core), 0u);
  ASSERT_EQ(ThreadCoreInfo_IncrementWrapperLockCount(&core), 1u);
  ASSERT_EQ(ThreadCoreInfo_IncrementWrapperLockCount(&core), 2u);
  ASSERT_EQ(ThreadCoreInfo_DecrementWrapperLockCount(&core), 1u);
  ASSERT_EQ(ThreadCoreInfo_DecrementWrapperLockCount(&core), 0u);
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
  {"ThreadCoreInfo init resets diagnostic state",
   threadCoreInfoInitResetsDiagnosticState},
  {"ThreadCoreInfo buffer helper returns core buffer",
   threadCoreInfoBufferHelperReturnsCoreBuffer},
  {"ThreadCoreInfo tracks wrapper lock count",
   threadCoreInfoTracksWrapperLockCount},
  {"Thread assert buffer helper returns core buffer",
   threadAssertBufferHelperReturnsCoreBuffer},
  {"Thread assert buffer helper handles null thread",
   threadAssertBufferHelperHandlesNullThread},
};

extern const size_t threadInfoTestCount =
  sizeof(threadInfoTests) / sizeof(threadInfoTests[0]);
