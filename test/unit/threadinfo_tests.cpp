#define DMTCP_UTIL_ASSERT_NO_MACROS

#include "unit_test.h"

#include "util_assert.h"
#include "../../src/threadinfo.h"

namespace {

void *
sampleThreadStart(void *arg)
{
  return arg;
}

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

void threadInitDescriptorResetsLifecycleState()
{
  Thread thread = {};
  pid_t ptid = 1;
  pid_t ctid = 2;
  int arg = 3;

  thread.fn = sampleThreadStart;
  thread.arg = &thread;
  thread.flags = 99;
  thread.ptid = &ptid;
  thread.ctid = &ctid;
  thread.next = &thread;
  thread.prev = &thread;
  thread.state = ST_SUSPENDED;
  thread.exiting = 1;
  thread.core.wrapperLockCount = 7;
  thread.core.assertBuffer[0] = 'x';
  thread.procname[0] = 'y';

  Thread_InitDescriptor(&thread, sampleThreadStart, &arg);

  ASSERT_TRUE(thread.fn == sampleThreadStart);
  ASSERT_EQ(thread.arg, &arg);
  ASSERT_EQ(thread.flags, 0);
  ASSERT_EQ(thread.ptid, nullptr);
  ASSERT_EQ(thread.ctid, nullptr);
  ASSERT_EQ(thread.next, nullptr);
  ASSERT_EQ(thread.prev, nullptr);
  ASSERT_EQ(thread.state, ST_RUNNING);
  ASSERT_EQ(thread.exiting, 0);
  ASSERT_EQ(ThreadCoreInfo_GetWrapperLockCount(&thread.core), 0u);
  ASSERT_EQ(thread.core.assertBuffer[0], '\0');
  ASSERT_EQ(thread.procname[0], '\0');
}

void threadInitPthreadStateRecordsTidAndTlsTidPointer()
{
  Thread thread = {};
  char pthreadBlock[64] = {};

  Thread_InitPthreadState(&thread, 42, pthreadBlock, 16);

  ASSERT_EQ(thread.tid, 42);
  ASSERT_EQ(thread.flags, Thread_DefaultCloneFlags());
  ASSERT_EQ(thread.ptid, reinterpret_cast<pid_t *>(pthreadBlock + 16));
  ASSERT_EQ(thread.ctid, thread.ptid);
}

void threadTryUpdateStateUnlockedUpdatesOnlyExpectedState()
{
  Thread thread = {};
  thread.state = ST_RUNNING;

  ASSERT_EQ(Thread_TryUpdateStateUnlocked(&thread,
                                          ST_SUSPENDED,
                                          ST_SIGNALED),
            0);
  ASSERT_EQ(thread.state, ST_RUNNING);

  ASSERT_EQ(Thread_TryUpdateStateUnlocked(&thread,
                                          ST_SUSPENDED,
                                          ST_RUNNING),
            1);
  ASSERT_EQ(thread.state, ST_SUSPENDED);
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

void threadAssertBufferHelperUsesFallbackCore()
{
  ThreadCoreInfo fallback = {};
  size_t size = 0;

  ASSERT_EQ(Thread_GetAssertBufferOrFallback(NULL, &fallback, &size),
            fallback.assertBuffer);
  ASSERT_EQ(size, dmtcp::kAssertBufferSize);
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
  {"Thread descriptor helper resets lifecycle state",
   threadInitDescriptorResetsLifecycleState},
  {"Thread pthread-state helper records tid and TLS tid pointer",
   threadInitPthreadStateRecordsTidAndTlsTidPointer},
  {"Thread unlocked state helper updates only expected state",
   threadTryUpdateStateUnlockedUpdatesOnlyExpectedState},
  {"Thread assert buffer helper returns core buffer",
   threadAssertBufferHelperReturnsCoreBuffer},
  {"Thread assert buffer helper handles null thread",
   threadAssertBufferHelperHandlesNullThread},
  {"Thread assert buffer helper uses fallback core",
   threadAssertBufferHelperUsesFallbackCore},
};

extern const size_t threadInfoTestCount =
  sizeof(threadInfoTests) / sizeof(threadInfoTests[0]);
