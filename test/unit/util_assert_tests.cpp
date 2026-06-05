#include "unit_test.h"

#include "util_assert.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#define UNIT_ASSERT_TRUE(expr) \
  ::dmtcp_test::assertTrue((expr), #expr, __FILE__, __LINE__)

#define UNIT_ASSERT_EQ(lhs, rhs) \
  ::dmtcp_test::assertEqual((lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__)

namespace {

int hookCallCount = 0;
int hookFd = -1;
char hookBuffer[128];
char hookBuffers[4][256];
size_t hookLength = 0;
bool hookTriggerNestedWarning = false;
bool hookInNestedWarning = false;

void
copyHookBuffer(int slot, const void *buf, size_t count)
{
  size_t copyLength = count;
  if (copyLength >= sizeof(hookBuffers[slot])) {
    copyLength = sizeof(hookBuffers[slot]) - 1;
  }
  std::memcpy(hookBuffers[slot], buf, copyLength);
  hookBuffers[slot][copyLength] = '\0';
}

void
resetHook()
{
  hookCallCount = 0;
  hookFd = -1;
  hookBuffer[0] = '\0';
  hookLength = 0;
  hookTriggerNestedWarning = false;
  hookInNestedWarning = false;
  for (char *buffer : hookBuffers) {
    buffer[0] = '\0';
  }
}

int
setErrnoAndReturn(int value, int newErrno)
{
  errno = newErrno;
  return value;
}

int *
returnNullAndCount(int *calls)
{
  ++(*calls);
  return nullptr;
}

int
returnZeroAndCount(int *calls)
{
  ++(*calls);
  return 0;
}

} // namespace

extern "C" ssize_t
dmtcp_assert_write(int fd, const void *buf, size_t count)
{
  int slot = hookCallCount;
  if (slot >= static_cast<int>(sizeof(hookBuffers) / sizeof(hookBuffers[0]))) {
    slot = static_cast<int>(sizeof(hookBuffers) / sizeof(hookBuffers[0])) - 1;
  }
  hookCallCount++;
  hookFd = fd;
  hookLength = count;
  if (hookTriggerNestedWarning && !hookInNestedWarning) {
    hookInNestedWarning = true;
    WARNING(false, "inner diagnostic");
    hookInNestedWarning = false;
  }

  copyHookBuffer(slot, buf, count);
  size_t copyLength = count;
  if (copyLength >= sizeof(hookBuffer)) {
    copyLength = sizeof(hookBuffer) - 1;
  }
  std::memcpy(hookBuffer, buf, copyLength);
  hookBuffer[copyLength] = '\0';
  return static_cast<ssize_t>(count);
}

namespace {

void formatCopiesLiteralText()
{
  char storage[64];
  dmtcp::AssertBuffer buffer(storage, sizeof(storage));

  dmtcp::formatTo(buffer, "plain text");

  UNIT_ASSERT_EQ(std::strcmp(buffer.c_str(), "plain text"), 0);
  UNIT_ASSERT_TRUE(!buffer.truncated());
}

void formatSubstitutesBasicValues()
{
  char storage[128];
  dmtcp::AssertBuffer buffer(storage, sizeof(storage));

  dmtcp::formatTo(buffer, "fd={} path={} ok={}", 7, "/tmp/demo", true);

  UNIT_ASSERT_EQ(std::strcmp(buffer.c_str(), "fd=7 path=/tmp/demo ok=true"),
                 0);
  UNIT_ASSERT_TRUE(!buffer.truncated());
}

enum FormatTestEnum {
  kFormatTestEnumValue = 42,
};

void formatSupportsEnumValues()
{
  char storage[64];
  dmtcp::AssertBuffer buffer(storage, sizeof(storage));

  dmtcp::formatTo(buffer, "type={}", kFormatTestEnumValue);

  UNIT_ASSERT_EQ(std::strcmp(buffer.c_str(), "type=42"), 0);
}

void formatSupportsStdStringValues()
{
  char storage[128];
  dmtcp::AssertBuffer buffer(storage, sizeof(storage));
  std::string path = "/tmp/dmtcp";

  dmtcp::formatTo(buffer, "path={}", path);

  UNIT_ASSERT_EQ(std::strcmp(buffer.c_str(), "path=/tmp/dmtcp"), 0);
  UNIT_ASSERT_TRUE(!buffer.truncated());
}

void formatHonorsEscapedBraces()
{
  char storage[64];
  dmtcp::AssertBuffer buffer(storage, sizeof(storage));

  dmtcp::formatTo(buffer, "{{value}}={}", 12);

  UNIT_ASSERT_EQ(std::strcmp(buffer.c_str(), "{value}=12"), 0);
}

void formatSupportsHexIntegerSpecs()
{
  char storage[128];
  dmtcp::AssertBuffer buffer(storage, sizeof(storage));

  dmtcp::formatTo(buffer, "plain={:x} prefix={:#x} padded={:08x}",
                  255, 16, 10);

  UNIT_ASSERT_EQ(std::strcmp(buffer.c_str(),
                             "plain=ff prefix=0x10 padded=0000000a"), 0);
}

void formatSupportsDecimalIntegerSpecs()
{
  char storage[128];
  dmtcp::AssertBuffer buffer(storage, sizeof(storage));

  dmtcp::formatTo(buffer, "plain={:d} width={:4} padded={:04}",
                  12, 7, 5);

  UNIT_ASSERT_EQ(std::strcmp(buffer.c_str(),
                             "plain=12 width=   7 padded=0005"), 0);
}

void formatLeavesInvalidSpecLiteral()
{
  char storage[128];
  dmtcp::AssertBuffer buffer(storage, sizeof(storage));

  dmtcp::formatTo(buffer, "bad={:q} value={}", 9);

  UNIT_ASSERT_EQ(std::strcmp(buffer.c_str(), "bad={:q} value=9"), 0);
}

void formatReportsUnusedArguments()
{
  char storage[128];
  dmtcp::AssertBuffer buffer(storage, sizeof(storage));

  dmtcp::formatTo(buffer, "value={}", 7, "dropped", 3);

  UNIT_ASSERT_EQ(std::strcmp(buffer.c_str(),
                             "value=7 [unused-format-args=2]"), 0);
}

void formatReportsMissingArguments()
{
  char storage[128];
  dmtcp::AssertBuffer buffer(storage, sizeof(storage));

  dmtcp::formatTo(buffer, "first={} second={}", 7);

  UNIT_ASSERT_EQ(std::strcmp(buffer.c_str(),
                             "first=7 second=[missing-format-arg]"), 0);
}

void formatTruncatesWithTerminator()
{
  char storage[8];
  dmtcp::AssertBuffer buffer(storage, sizeof(storage));

  dmtcp::formatTo(buffer, "abcdefghi");

  UNIT_ASSERT_EQ(std::strcmp(buffer.c_str(), "abcdefg"), 0);
  UNIT_ASSERT_TRUE(buffer.truncated());
}

void diagnosticIncludesLocationAndMessage()
{
  char storage[256];
  dmtcp::AssertBuffer buffer(storage, sizeof(storage));

  dmtcp::formatDiagnostic(buffer,
                          dmtcp::AssertSeverity::Warning,
                          "x > 0",
                          "file.cpp",
                          42,
                          "fd={}",
                          9);

  UNIT_ASSERT_TRUE(std::strstr(buffer.c_str(), "WARNING") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(buffer.c_str(), "file.cpp:42") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(buffer.c_str(), "x > 0") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(buffer.c_str(), "fd=9") != nullptr);
}

void diagnosticIncludesErrno()
{
  char storage[256];
  dmtcp::AssertBuffer buffer(storage, sizeof(storage));

  dmtcp::formatDiagnosticWithErrno(buffer,
                                   dmtcp::AssertSeverity::Warning,
                                   "fd >= 0",
                                   "file.cpp",
                                   42,
                                   EACCES,
                                   "path={}",
                                   "/tmp/demo");

  UNIT_ASSERT_TRUE(std::strstr(buffer.c_str(), "WARNING") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(buffer.c_str(), "fd >= 0") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(buffer.c_str(), "path=/tmp/demo") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(buffer.c_str(), "errno=13") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(buffer.c_str(), "EACCES") != nullptr);
}

void currentAssertBufferUsesFallbackWithoutThreadProvider()
{
  dmtcp::AssertBuffer buffer = dmtcp::currentAssertBuffer(false);

  ASSERT_EQ(buffer.capacity(), dmtcp::kAssertBufferSize);
  dmtcp::formatTo(buffer, "fallback {}", 1);
  ASSERT_EQ(std::strcmp(buffer.c_str(), "fallback 1"), 0);
}

void reentrantAssertBufferDoesNotOverwriteOuterFallback()
{
  dmtcp::AssertBuffer outer = dmtcp::currentAssertBuffer(false);
  dmtcp::AssertBuffer inner = dmtcp::currentAssertBuffer(true);

  dmtcp::formatTo(outer, "outer");
  dmtcp::formatTo(inner, "inner");

  ASSERT_EQ(std::strcmp(outer.c_str(), "outer"), 0);
  ASSERT_EQ(std::strcmp(inner.c_str(), "inner"), 0);
}

void diagnosticTruncationIncludesMarker()
{
  char storage[64];
  dmtcp::AssertBuffer buffer(storage, sizeof(storage));

  dmtcp::formatDiagnostic(buffer,
                          dmtcp::AssertSeverity::Warning,
                          "x > 0",
                          "file.cpp",
                          42,
                          "payload={}",
                          "abcdefghijklmnopqrstuvwxyz0123456789"
                          "abcdefghijklmnopqrstuvwxyz0123456789");

  UNIT_ASSERT_TRUE(buffer.truncated());
  UNIT_ASSERT_TRUE(std::strstr(buffer.c_str(), " [truncated]\n") != nullptr);
}

void writeAllUsesAssertWriteHook()
{
  resetHook();

  dmtcp::writeAllNoAlloc(77, "hooked", 6);

  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_EQ(hookFd, 77);
  UNIT_ASSERT_EQ(hookLength, static_cast<size_t>(6));
  UNIT_ASSERT_EQ(std::strcmp(hookBuffer, "hooked"), 0);
}

void warningReentryKeepsOuterDiagnosticStable()
{
  resetHook();
  hookTriggerNestedWarning = true;

  WARNING(false, "outer diagnostic");

  UNIT_ASSERT_EQ(hookCallCount, 2);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "outer diagnostic") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[1],
                               "inner diagnostic") != nullptr);
}

void warningDoesNotEvaluateMessageArgsWhenConditionPasses()
{
  resetHook();
  int calls = 0;

  WARNING(true, "arg={}", ++calls);

  UNIT_ASSERT_EQ(calls, 0);
  UNIT_ASSERT_EQ(hookCallCount, 0);
}

void warningErrnoUsesSavedErrnoAcrossMessageArgs()
{
  resetHook();
  errno = EACCES;

  WARNING_ERRNO(false, "arg={}", setErrnoAndReturn(7, ENOENT));

  UNIT_ASSERT_EQ(errno, EACCES);
  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "arg=7") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "errno=13") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "EACCES") != nullptr);
}

void convenienceAssertMacrosPassWithoutWriting()
{
  resetHook();
  int value = 2;
  int larger = 3;
  int *ptr = &value;
  int *nullPtr = nullptr;

  ASSERT_TRUE(value == 2);
  ASSERT_FALSE(value == larger);
  ASSERT_EQ(2, value);
  ASSERT_NE(larger, value);
  ASSERT_GT(larger, value);
  ASSERT_GE(value, 2);
  ASSERT_LT(value, larger);
  ASSERT_LE(value, 2);
  ASSERT_MUTEX_SUCCESS(0);
  ASSERT_RWLOCK_SUCCESS(0);
  ASSERT_PTHREAD_SUCCESS(0);
  WARNING_MUTEX_SUCCESS(0);
  WARNING_RWLOCK_SUCCESS(0);
  WARNING_PTHREAD_SUCCESS(0);
  ASSERT_NOT_NULL(ptr);
  ASSERT_NULL(nullPtr);

  UNIT_ASSERT_EQ(hookCallCount, 0);
}

void convenienceAssertMacrosEvaluateOperandsOnce()
{
  resetHook();
  int lhs = 0;
  int rhs = 1;
  int nullCalls = 0;
  int successCalls = 0;

  ASSERT_EQ(++lhs, rhs);
  ASSERT_NULL(returnNullAndCount(&nullCalls));
  ASSERT_MUTEX_SUCCESS(returnZeroAndCount(&successCalls));
  ASSERT_PTHREAD_SUCCESS(returnZeroAndCount(&successCalls));

  UNIT_ASSERT_EQ(lhs, 1);
  UNIT_ASSERT_EQ(nullCalls, 1);
  UNIT_ASSERT_EQ(successCalls, 2);
  UNIT_ASSERT_EQ(hookCallCount, 0);
}

void warningMutexSuccessReportsExpressionAndReturnValue()
{
  resetHook();

  WARNING_MUTEX_SUCCESS(setErrnoAndReturn(EINVAL, EIO));

  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "setErrnoAndReturn(EINVAL, EIO) failed") !=
                   nullptr);
  const std::string expected =
    "expected 0, returned " + std::to_string(EINVAL) + " (EINVAL)";
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               expected.c_str()) !=
                   nullptr);
}

void warningPthreadSuccessReportsExpressionAndReturnValue()
{
  resetHook();

  WARNING_PTHREAD_SUCCESS(setErrnoAndReturn(EAGAIN, EIO));

  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "setErrnoAndReturn(EAGAIN, EIO) failed") !=
                   nullptr);
  const std::string expected =
    "expected 0, returned " + std::to_string(EAGAIN) + " (EAGAIN)";
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               expected.c_str()) !=
                   nullptr);
}

void warningPthreadSuccessMessageReportsExtraContext()
{
  resetHook();

  WARNING_PTHREAD_SUCCESS_MSG(setErrnoAndReturn(EINVAL, EIO),
                              "tid={} signal={}",
                              123,
                              9);

  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "setErrnoAndReturn(EINVAL, EIO) failed") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "expected 0, returned 22 (EINVAL)") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "tid=123 signal=9") !=
                   nullptr);
}

void assertFailureExitsWithRawFailureCode()
{
  pid_t child = fork();
  UNIT_ASSERT_TRUE(child >= 0);

  if (child == 0) {
    unsetenv("DMTCP_ABORT_ON_FAILURE");
    unsetenv("DMTCP_FAIL_RC");
    dmtcp::assertFailure("false", "file.cpp", 7, "fatal");
  }

  int status = 0;
  UNIT_ASSERT_EQ(waitpid(child, &status, 0), child);
  UNIT_ASSERT_TRUE(WIFEXITED(status));
  UNIT_ASSERT_EQ(WEXITSTATUS(status), dmtcp::kAssertFailureExitCode);
}

void assertFailureUsesRawExitPath()
{
  pid_t child = fork();
  UNIT_ASSERT_TRUE(child >= 0);

  if (child == 0) {
    setenv("DMTCP_ABORT_ON_FAILURE", "1", 1);
    setenv("DMTCP_FAIL_RC", "17", 1);
    dmtcp::assertFailure("false", "file.cpp", 7, "fatal");
  }

  int status = 0;
  UNIT_ASSERT_EQ(waitpid(child, &status, 0), child);
  UNIT_ASSERT_TRUE(WIFEXITED(status));
  UNIT_ASSERT_EQ(WEXITSTATUS(status), dmtcp::kAssertFailureExitCode);
}

} // namespace

extern const dmtcp_test::TestCase utilAssertTests[] = {
  {"fixed formatter copies literal text", formatCopiesLiteralText},
  {"fixed formatter substitutes basic values", formatSubstitutesBasicValues},
  {"fixed formatter supports enum values", formatSupportsEnumValues},
  {"fixed formatter supports std::string values",
   formatSupportsStdStringValues},
  {"fixed formatter honors escaped braces", formatHonorsEscapedBraces},
  {"fixed formatter supports hex integer specs",
   formatSupportsHexIntegerSpecs},
  {"fixed formatter supports decimal integer specs",
   formatSupportsDecimalIntegerSpecs},
  {"fixed formatter leaves invalid specs literal",
   formatLeavesInvalidSpecLiteral},
  {"fixed formatter reports unused arguments", formatReportsUnusedArguments},
  {"fixed formatter reports missing arguments", formatReportsMissingArguments},
  {"fixed formatter truncates with terminator", formatTruncatesWithTerminator},
  {"diagnostic formatter includes location and message",
   diagnosticIncludesLocationAndMessage},
  {"diagnostic formatter includes errno", diagnosticIncludesErrno},
  {"assert buffer falls back without thread provider",
   currentAssertBufferUsesFallbackWithoutThreadProvider},
  {"reentrant assert buffer does not overwrite outer fallback",
   reentrantAssertBufferDoesNotOverwriteOuterFallback},
  {"diagnostic truncation includes marker",
   diagnosticTruncationIncludesMarker},
  {"diagnostic writer uses assert write hook", writeAllUsesAssertWriteHook},
  {"warning reentry keeps outer diagnostic stable",
   warningReentryKeepsOuterDiagnosticStable},
  {"warning skips message args when condition passes",
   warningDoesNotEvaluateMessageArgsWhenConditionPasses},
  {"warning errno uses saved errno across message args",
   warningErrnoUsesSavedErrnoAcrossMessageArgs},
  {"convenience assert macros pass without writing",
   convenienceAssertMacrosPassWithoutWriting},
  {"convenience assert macros evaluate operands once",
   convenienceAssertMacrosEvaluateOperandsOnce},
  {"warning mutex success reports expression and return value",
   warningMutexSuccessReportsExpressionAndReturnValue},
  {"warning pthread success reports expression and return value",
   warningPthreadSuccessReportsExpressionAndReturnValue},
  {"warning pthread success message reports extra context",
   warningPthreadSuccessMessageReportsExtraContext},
  {"assert failure exits with raw failure code",
   assertFailureExitsWithRawFailureCode},
  {"assert failure uses raw exit path", assertFailureUsesRawExitPath},
};

extern const size_t utilAssertTestCount =
  sizeof(utilAssertTests) / sizeof(utilAssertTests[0]);
