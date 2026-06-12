#define DMTCP_TEST_NO_SHORT_ASSERT_MACROS
#include "unit_test.h"

#include "protectedfds.h"
#include "util_assert.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cstdio>
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
int hookFds[4];
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
  dmtcp::setLogLevel(dmtcp::LogLevel::Note);
  dmtcp::setLogOverrides("");
  dmtcp::closeDiagnosticConsole();
  dmtcp::initializeDiagnosticConsole(nullptr);
  dmtcp::setDiagnosticLogFile(nullptr);
  hookCallCount = 0;
  hookFd = -1;
  hookBuffer[0] = '\0';
  hookLength = 0;
  hookTriggerNestedWarning = false;
  hookInNestedWarning = false;
  for (char *buffer : hookBuffers) {
    buffer[0] = '\0';
  }
  for (int& fd : hookFds) {
    fd = -1;
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

int *
returnPtrAndCount(int *value, int *calls)
{
  ++(*calls);
  return value;
}

int
returnZeroAndCount(int *calls)
{
  ++(*calls);
  return 0;
}

int
incrementAndReturn(int *calls)
{
  ++(*calls);
  return *calls;
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
  hookFds[slot] = fd;
  hookLength = count;
  if (hookTriggerNestedWarning && !hookInNestedWarning) {
    hookInNestedWarning = true;
    WARN(false, "inner diagnostic");
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

void formatSupportsPointerHexValues()
{
  char storage[128];
  dmtcp::AssertBuffer buffer(storage, sizeof(storage));
  int value = 0;

  dmtcp::formatTo(buffer, "ptr={}", &value);

  UNIT_ASSERT_TRUE(std::strstr(buffer.c_str(), "ptr=0x") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(buffer.c_str(), "(null)") == nullptr);
}

void formatLeavesUnsupportedSpecsLiteralAndReportsUnusedArgs()
{
  char storage[128];
  dmtcp::AssertBuffer buffer(storage, sizeof(storage));

  dmtcp::formatTo(buffer, "plain={:x} padded={:08x}", 255, 10);

  UNIT_ASSERT_EQ(std::strcmp(buffer.c_str(),
                             "plain={:x} padded={:08x} "
                             "[unused-format-args=2]"), 0);
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
                          dmtcp::LogLevel::Warn,
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
                                   dmtcp::LogLevel::Warn,
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

void diagnosticTruncationIncludesMarker()
{
  char storage[64];
  dmtcp::AssertBuffer buffer(storage, sizeof(storage));

  dmtcp::formatDiagnostic(buffer,
                          dmtcp::LogLevel::Warn,
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

void warningDiagnosticsUseAuthoritativeStderrFd()
{
  resetHook();

  WARN(false, "stderr destination");

  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_EQ(hookFd, PROTECTED_STDERR_FD);
}

void warningDiagnosticsAlsoUseLogFile()
{
  resetHook();

  char path[128];
  std::snprintf(path, sizeof(path), "/tmp/dmtcp-util-assert-test-%ld.log",
                static_cast<long>(getpid()));
  unlink(path);

  UNIT_ASSERT_TRUE(dmtcp::setDiagnosticLogFile(path));
  WARN(false, "log destination");
  dmtcp::setDiagnosticLogFile(nullptr);
  unlink(path);

  UNIT_ASSERT_EQ(hookCallCount, 2);
  UNIT_ASSERT_EQ(hookFds[0], PROTECTED_STDERR_FD);
  UNIT_ASSERT_TRUE(hookFds[1] != PROTECTED_STDERR_FD);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "log destination") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[1], "log destination") != nullptr);
}

void warningReentryKeepsOuterDiagnosticStable()
{
  resetHook();
  hookTriggerNestedWarning = true;

  WARN(false, "outer diagnostic");

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

  WARN(true, "arg={}", ++calls);

  UNIT_ASSERT_EQ(calls, 0);
  UNIT_ASSERT_EQ(hookCallCount, 0);
}

void warningDoesNotEvaluateMessageArgsWhenLogLevelDisabled()
{
  resetHook();
  int calls = 0;
  dmtcp::setLogLevel(dmtcp::LogLevel::Error);

  WARN(false, "arg={}", incrementAndReturn(&calls));

  UNIT_ASSERT_EQ(calls, 0);
  UNIT_ASSERT_EQ(hookCallCount, 0);
}

void warningErrnoUsesSavedErrnoAcrossMessageArgs()
{
  resetHook();
  errno = EACCES;

  WARN_ERRNO(false, "arg={}", setErrnoAndReturn(7, ENOENT));

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
  ASSERT_LOCK_SUCCESS(0);
  ASSERT_PTHREAD_SUCCESS(0);
  ASSERT_ZERO(0);
  ASSERT_NE(-1, 0);
  ASSERT_EQ(3, 3);
  WARN_LOCK_SUCCESS(0);
  WARN_PTHREAD_SUCCESS(0);
  WARN_ZERO(0);
  WARN_NE(-1, 0);
  WARN_EQ(4, 4);
  ASSERT_NOT_NULL(ptr);
  ASSERT_NOT_NULL(ptr, "ptr context={}", "ok");
  ASSERT_NULL(nullPtr);
  ASSERT_NULL(nullPtr, "null context={}", "ok");

  UNIT_ASSERT_EQ(hookCallCount, 0);
}

void convenienceAssertMacrosEvaluateOperandsOnce()
{
  resetHook();
  int lhs = 0;
  int rhs = 1;
  int value = 2;
  int ptrCalls = 0;
  int nullCalls = 0;
  int successCalls = 0;

  ASSERT_EQ(++lhs, rhs);
  ASSERT_NULL(returnNullAndCount(&nullCalls));
  ASSERT_NULL(returnNullAndCount(&nullCalls), "null context");
  ASSERT_NOT_NULL(returnPtrAndCount(&value, &ptrCalls), "ptr context");
  ASSERT_LOCK_SUCCESS(returnZeroAndCount(&successCalls));
  ASSERT_PTHREAD_SUCCESS(returnZeroAndCount(&successCalls));
  ASSERT_ZERO(returnZeroAndCount(&successCalls));
  ASSERT_NE(-1, returnZeroAndCount(&successCalls));
  ASSERT_EQ(0, returnZeroAndCount(&successCalls));

  UNIT_ASSERT_EQ(lhs, 1);
  UNIT_ASSERT_EQ(ptrCalls, 1);
  UNIT_ASSERT_EQ(nullCalls, 2);
  UNIT_ASSERT_EQ(successCalls, 5);
  UNIT_ASSERT_EQ(hookCallCount, 0);
}

void convenienceAssertMessageMacrosEvaluateOperandsOnce()
{
  resetHook();
  int lhs = 0;
  int rhs = 1;

  ASSERT_EQ(++lhs, rhs, "lhs advanced");
  ASSERT_GE(lhs, rhs, "lhs should catch rhs");

  UNIT_ASSERT_EQ(lhs, 1);
  UNIT_ASSERT_EQ(hookCallCount, 0);
}

void convenienceWarningMacrosPassWithoutWriting()
{
  resetHook();
  int value = 2;
  int larger = 3;
  int *ptr = &value;
  int *nullPtr = nullptr;

  WARN_TRUE(value == 2);
  WARN_FALSE(value == larger);
  WARN_EQ(2, value);
  WARN_NE(larger, value);
  WARN_GT(larger, value);
  WARN_GE(value, 2);
  WARN_LT(value, larger);
  WARN_LE(value, 2);
  WARN_NOT_NULL(ptr);
  WARN_NOT_NULL(ptr, "ptr context={}", "ok");
  WARN_NULL(nullPtr);
  WARN_NULL(nullPtr, "null context={}", "ok");

  UNIT_ASSERT_EQ(hookCallCount, 0);
}

void convenienceWarningMacrosEvaluateOperandsOnce()
{
  resetHook();
  int lhs = 0;
  int rhs = 1;
  int value = 2;
  int calls = 0;
  int nullCalls = 0;

  WARN_EQ(++lhs, rhs);
  WARN_NULL(returnNullAndCount(&nullCalls));
  WARN_NULL(returnNullAndCount(&nullCalls), "null context");
  WARN_NOT_NULL(returnPtrAndCount(&value, &calls));
  WARN_NOT_NULL(returnPtrAndCount(&value, &calls), "ptr context");

  UNIT_ASSERT_EQ(lhs, 1);
  UNIT_ASSERT_EQ(calls, 2);
  UNIT_ASSERT_EQ(nullCalls, 2);
  UNIT_ASSERT_EQ(hookCallCount, 0);
}

void convenienceWarningMessageMacrosReportFailuresAndContinue()
{
  resetHook();
  int value = 3;
  int *ptr = &value;
  int *nullPtr = nullptr;

  WARN_EQ(2, value, "context={}", "warning-eq");
  WARN_GT(2, value, "context={}", "warning-gt");
  WARN_NULL(ptr, "context={}", "warning-null");
  WARN_NOT_NULL(nullPtr, "context={}", "warning-not-null");

  UNIT_ASSERT_EQ(hookCallCount, 4);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "expected 2 == value, got 2 and 3") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "context=warning-eq") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[1],
                               "expected 2 > value, got 2 and 3") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[1],
                               "context=warning-gt") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[2],
                               "expected null: ptr, got") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[2],
                               "context=warning-null") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[3],
                               "expected non-null: nullPtr, got (null)") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[3],
                               "context=warning-not-null") != nullptr);
}

void convenienceWarningMacrosReportFailuresAndContinue()
{
  resetHook();
  int value = 3;
  int *ptr = &value;

  WARN_EQ(2, value);
  WARN_NULL(ptr);

  UNIT_ASSERT_EQ(hookCallCount, 2);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "expected 2 == value, got 2 and 3") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[1],
                               "expected null: ptr") != nullptr);
}

void warningLockSuccessReportsExpressionAndReturnValue()
{
  resetHook();

  WARN_LOCK_SUCCESS(setErrnoAndReturn(EINVAL, EIO));

  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "expected 0 == setErrnoAndReturn(") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "got 0 and 22") !=
                   nullptr);
}

void warningPthreadSuccessReportsExpressionAndReturnValue()
{
  resetHook();

  WARN_PTHREAD_SUCCESS(setErrnoAndReturn(EAGAIN, EIO));

  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "expected 0 == setErrnoAndReturn(") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "got 0 and 11") !=
                   nullptr);
}

void warningZeroReturnReportsExpressionAndReturnValue()
{
  resetHook();

  WARN_ZERO(setErrnoAndReturn(EINVAL, EIO));

  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "expected 0 == setErrnoAndReturn(") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "got 0 and 22") !=
                   nullptr);
}

void warningZeroReturnMessageReportsExtraContext()
{
  resetHook();

  WARN_ZERO(setErrnoAndReturn(EINVAL, EIO),
            "fd={} path={}",
            9,
            "/dev/ptmx");

  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "expected 0 == setErrnoAndReturn(") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "got 0 and 22") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "fd=9 path=/dev/ptmx") !=
                   nullptr);
}

void warningPthreadSuccessMessageReportsExtraContext()
{
  resetHook();

  WARN_PTHREAD_SUCCESS(setErrnoAndReturn(EINVAL, EIO),
                       "tid={} signal={}",
                       123,
                       9);

  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "expected 0 == setErrnoAndReturn(") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "got 0 and 22") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "tid=123 signal=9") !=
                   nullptr);
}

void logLevelOrderingMatchesRuntimePolicy()
{
  UNIT_ASSERT_TRUE(static_cast<int>(dmtcp::LogLevel::Error) <
                   static_cast<int>(dmtcp::LogLevel::Warn));
  UNIT_ASSERT_TRUE(static_cast<int>(dmtcp::LogLevel::Warn) <
                   static_cast<int>(dmtcp::LogLevel::Note));
  UNIT_ASSERT_TRUE(static_cast<int>(dmtcp::LogLevel::Note) <
                   static_cast<int>(dmtcp::LogLevel::Trace));
}

void parseLogLevelAcceptsNamesAndNumbers()
{
  dmtcp::LogLevel level = dmtcp::LogLevel::Error;

  UNIT_ASSERT_TRUE(dmtcp::parseLogLevel("trace", &level));
  UNIT_ASSERT_EQ(static_cast<int>(level),
                 static_cast<int>(dmtcp::LogLevel::Trace));
  UNIT_ASSERT_TRUE(dmtcp::parseLogLevel("warning", &level));
  UNIT_ASSERT_EQ(static_cast<int>(level),
                 static_cast<int>(dmtcp::LogLevel::Warn));
  UNIT_ASSERT_TRUE(dmtcp::parseLogLevel("2", &level));
  UNIT_ASSERT_EQ(static_cast<int>(level),
                 static_cast<int>(dmtcp::LogLevel::Note));
  UNIT_ASSERT_TRUE(!dmtcp::parseLogLevel("verbose", &level));
}

void noteRespectsLogLevelAndPreservesErrno()
{
  resetHook();
  dmtcp::setLogLevel(dmtcp::LogLevel::Note);
  errno = EACCES;

  NOTE("note value={}", 17);

  UNIT_ASSERT_EQ(errno, EACCES);
  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "NOTE") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "note value=17") != nullptr);
}

void noteDoesNotEvaluateArgsWhenDisabled()
{
  resetHook();
  int calls = 0;
  dmtcp::setLogLevel(dmtcp::LogLevel::Warn);

  NOTE("calls={}", incrementAndReturn(&calls));

  UNIT_ASSERT_EQ(calls, 0);
  UNIT_ASSERT_EQ(hookCallCount, 0);
}

void traceDoesNotEvaluateArgsWhenDisabled()
{
  resetHook();
  int calls = 0;
  dmtcp::setLogLevel(dmtcp::LogLevel::Note);

  TRACE("calls={}", incrementAndReturn(&calls));

  UNIT_ASSERT_EQ(calls, 0);
  UNIT_ASSERT_EQ(hookCallCount, 0);
}

void traceLogsWhenEnabled()
{
  resetHook();
  int calls = 0;
  dmtcp::setLogLevel(dmtcp::LogLevel::Trace);

  TRACE("trace calls={}", incrementAndReturn(&calls));

  UNIT_ASSERT_EQ(calls, 1);
  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "TRACE") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "trace calls=1") != nullptr);
}

void traceSupportsFormattedValues()
{
  resetHook();
  int fd = 7;
  dmtcp::setLogLevel(dmtcp::LogLevel::Trace);

  TRACE("formatted trace fd={}", fd);

  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "formatted trace fd=7") !=
                   nullptr);
}

void componentOverrideEnablesTraceForMatchingComponent()
{
  resetHook();
  dmtcp::setLogLevel(dmtcp::LogLevel::Warn);
  UNIT_ASSERT_TRUE(dmtcp::setLogOverrides("pid=trace;socket=error"));

  UNIT_ASSERT_TRUE(dmtcp::logEnabled(dmtcp::LogLevel::Trace,
                                     "pid",
                                     "src/plugin/pid/pid.cpp"));
  UNIT_ASSERT_TRUE(!dmtcp::logEnabled(dmtcp::LogLevel::Trace,
                                      "socket",
                                      "src/plugin/socket/socketwrappers.cpp"));
}

void fileOverrideBeatsComponentOverride()
{
  resetHook();
  dmtcp::setLogLevel(dmtcp::LogLevel::Warn);
  UNIT_ASSERT_TRUE(dmtcp::setLogOverrides(
    "pid=error;file:src/plugin/pid/pid.cpp=trace"));

  UNIT_ASSERT_TRUE(dmtcp::logEnabled(dmtcp::LogLevel::Trace,
                                     "pid",
                                     "/build/src/plugin/pid/pid.cpp"));
  UNIT_ASSERT_TRUE(!dmtcp::logEnabled(dmtcp::LogLevel::Trace,
                                      "pid",
                                      "/build/src/plugin/pid/pidwrappers.cpp"));
}

void assertFailureExitsWithRawFailureCode()
{
  pid_t child = fork();
  UNIT_ASSERT_TRUE(child >= 0);

  if (child == 0) {
    unsetenv("DMTCP_ABORT_ON_FAILURE");
    unsetenv("DMTCP_FAIL_RC");
    ASSERT(false, "fatal");
  }

  int status = 0;
  UNIT_ASSERT_EQ(waitpid(child, &status, 0), child);
  UNIT_ASSERT_TRUE(WIFEXITED(status));
  UNIT_ASSERT_EQ(WEXITSTATUS(status), dmtcp::kAssertFailureExitCode);
}

void assertFailureHonorsFailRc()
{
  pid_t child = fork();
  UNIT_ASSERT_TRUE(child >= 0);

  if (child == 0) {
    unsetenv("DMTCP_ABORT_ON_FAILURE");
    setenv("DMTCP_FAIL_RC", "17", 1);
    ASSERT(false, "fatal");
  }

  int status = 0;
  UNIT_ASSERT_EQ(waitpid(child, &status, 0), child);
  UNIT_ASSERT_TRUE(WIFEXITED(status));
  UNIT_ASSERT_EQ(WEXITSTATUS(status), 17);
}

void assertFailureHonorsAbortOnFailure()
{
  pid_t child = fork();
  UNIT_ASSERT_TRUE(child >= 0);

  if (child == 0) {
    setenv("DMTCP_ABORT_ON_FAILURE", "1", 1);
    setenv("DMTCP_FAIL_RC", "17", 1);
    ASSERT(false, "fatal");
  }

  int status = 0;
  UNIT_ASSERT_EQ(waitpid(child, &status, 0), child);
  UNIT_ASSERT_TRUE(WIFSIGNALED(status));
  UNIT_ASSERT_EQ(WTERMSIG(status), SIGABRT);
}

} // namespace

extern const dmtcp_test::TestCase utilAssertTests[] = {
  {"fixed formatter copies literal text", formatCopiesLiteralText},
  {"fixed formatter substitutes basic values", formatSubstitutesBasicValues},
  {"fixed formatter supports enum values", formatSupportsEnumValues},
  {"fixed formatter supports std::string values",
   formatSupportsStdStringValues},
  {"fixed formatter honors escaped braces", formatHonorsEscapedBraces},
  {"fixed formatter supports pointer hex values",
   formatSupportsPointerHexValues},
  {"fixed formatter leaves unsupported specs literal",
   formatLeavesUnsupportedSpecsLiteralAndReportsUnusedArgs},
  {"fixed formatter leaves invalid specs literal",
   formatLeavesInvalidSpecLiteral},
  {"fixed formatter reports unused arguments", formatReportsUnusedArguments},
  {"fixed formatter reports missing arguments", formatReportsMissingArguments},
  {"fixed formatter truncates with terminator", formatTruncatesWithTerminator},
  {"diagnostic formatter includes location and message",
   diagnosticIncludesLocationAndMessage},
  {"diagnostic formatter includes errno", diagnosticIncludesErrno},
  {"diagnostic truncation includes marker",
   diagnosticTruncationIncludesMarker},
  {"diagnostic writer uses assert write hook", writeAllUsesAssertWriteHook},
  {"warning diagnostics use authoritative stderr fd",
   warningDiagnosticsUseAuthoritativeStderrFd},
  {"warning diagnostics also use log file",
   warningDiagnosticsAlsoUseLogFile},
  {"warning reentry keeps outer diagnostic stable",
   warningReentryKeepsOuterDiagnosticStable},
  {"warning skips message args when condition passes",
   warningDoesNotEvaluateMessageArgsWhenConditionPasses},
  {"warning skips message args when log level disabled",
   warningDoesNotEvaluateMessageArgsWhenLogLevelDisabled},
  {"warning errno uses saved errno across message args",
   warningErrnoUsesSavedErrnoAcrossMessageArgs},
  {"convenience assert macros pass without writing",
   convenienceAssertMacrosPassWithoutWriting},
  {"convenience assert macros evaluate operands once",
   convenienceAssertMacrosEvaluateOperandsOnce},
  {"convenience assert message macros evaluate operands once",
   convenienceAssertMessageMacrosEvaluateOperandsOnce},
  {"convenience warning macros pass without writing",
   convenienceWarningMacrosPassWithoutWriting},
  {"convenience warning macros evaluate operands once",
   convenienceWarningMacrosEvaluateOperandsOnce},
  {"convenience warning message macros report failures and continue",
   convenienceWarningMessageMacrosReportFailuresAndContinue},
  {"convenience warning macros report failures and continue",
   convenienceWarningMacrosReportFailuresAndContinue},
  {"warning lock success reports expression and return value",
   warningLockSuccessReportsExpressionAndReturnValue},
  {"warning pthread success reports expression and return value",
   warningPthreadSuccessReportsExpressionAndReturnValue},
  {"warning zero-return reports expression and return value",
   warningZeroReturnReportsExpressionAndReturnValue},
  {"warning zero-return message reports extra context",
   warningZeroReturnMessageReportsExtraContext},
  {"warning pthread success message reports extra context",
   warningPthreadSuccessMessageReportsExtraContext},
  {"LogLevel ordering matches runtime policy",
   logLevelOrderingMatchesRuntimePolicy},
  {"parseLogLevel accepts names and numbers",
   parseLogLevelAcceptsNamesAndNumbers},
  {"NOTE respects log level and preserves errno",
   noteRespectsLogLevelAndPreservesErrno},
  {"NOTE skips args when disabled", noteDoesNotEvaluateArgsWhenDisabled},
  {"TRACE skips args when disabled", traceDoesNotEvaluateArgsWhenDisabled},
  {"TRACE logs when enabled", traceLogsWhenEnabled},
  {"TRACE supports formatted values", traceSupportsFormattedValues},
  {"component override enables trace for matching component",
   componentOverrideEnablesTraceForMatchingComponent},
  {"file override beats component override", fileOverrideBeatsComponentOverride},
  {"assert failure exits with raw failure code",
   assertFailureExitsWithRawFailureCode},
  {"assert failure honors DMTCP_FAIL_RC", assertFailureHonorsFailRc},
  {"assert failure honors DMTCP_ABORT_ON_FAILURE",
   assertFailureHonorsAbortOnFailure},
};

extern const size_t utilAssertTestCount =
  sizeof(utilAssertTests) / sizeof(utilAssertTests[0]);
