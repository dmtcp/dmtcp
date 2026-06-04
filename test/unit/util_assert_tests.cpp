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

void warningDiagnosticsUseAuthoritativeStderrFd()
{
  resetHook();

  WARN(false, "stderr destination");

  UNIT_ASSERT_EQ(dmtcp::kDiagnosticFd, STDERR_FILENO);
  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_EQ(hookFd, dmtcp::kDiagnosticFd);
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
  ASSERT_SYSCALL_SUCCESS(0);
  ASSERT_SYSCALL_EQ(3, 3);
  WARN_LOCK_SUCCESS(0);
  WARN_PTHREAD_SUCCESS(0);
  WARN_ZERO(0);
  WARN_SYSCALL_SUCCESS(0);
  WARN_SYSCALL_EQ(4, 4);
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
  ASSERT_SYSCALL_SUCCESS(returnZeroAndCount(&successCalls));
  ASSERT_SYSCALL_EQ(0, returnZeroAndCount(&successCalls));

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

void warningSyscallSuccessReportsExpressionReturnValueAndErrno()
{
  resetHook();
  errno = 0;

  WARN_SYSCALL_SUCCESS(setErrnoAndReturn(-1, EACCES));

  UNIT_ASSERT_EQ(errno, EACCES);
  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "expected -1 != setErrnoAndReturn(") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "got -1 and -1") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "errno=13") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "EACCES") != nullptr);
}

void warningSyscallSuccessMessageReportsExtraContext()
{
  resetHook();
  errno = 0;

  WARN_SYSCALL_SUCCESS(setErrnoAndReturn(-1, EACCES),
                              "fd={} path={}",
                              9,
                              "/tmp/missing");

  UNIT_ASSERT_EQ(errno, EACCES);
  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "expected -1 != setErrnoAndReturn(") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "got -1 and -1") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "fd=9 path=/tmp/missing") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "errno=13") != nullptr);
}

void warningSyscallEqReportsExpressionReturnValueAndErrno()
{
  resetHook();
  errno = 0;

  WARN_SYSCALL_EQ(7, setErrnoAndReturn(-1, EACCES));

  UNIT_ASSERT_EQ(errno, EACCES);
  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "expected 7 == setErrnoAndReturn(") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "got 7 and -1") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "errno=13") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "EACCES") != nullptr);
}

void warningSyscallEqMessageReportsExtraContext()
{
  resetHook();
  errno = 0;

  WARN_SYSCALL_EQ(9,
                         setErrnoAndReturn(4, EIO),
                         "fd={} path={}",
                         5,
                         "/tmp/short");

  UNIT_ASSERT_EQ(errno, EIO);
  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "expected 9 == setErrnoAndReturn(") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "got 9 and 4") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "fd=5 path=/tmp/short") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "errno=5") != nullptr);
}

void warningValidFdReportsExpressionAndReturnValue()
{
  resetHook();

  errno = 0;
  WARN_VALID_FD(setErrnoAndReturn(-1, EBADF));

  UNIT_ASSERT_EQ(errno, EBADF);
  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "expected setErrnoAndReturn(") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               ">= 0") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "got -1 and 0") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "errno=9") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "EBADF") != nullptr);
}

void warningValidFdMessageReportsExtraContext()
{
  resetHook();

  errno = 0;
  WARN_VALID_FD(setErrnoAndReturn(-1, EBADF),
                       "path={}",
                       "/tmp/ckpt.dmtcp");

  UNIT_ASSERT_EQ(errno, EBADF);
  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "expected setErrnoAndReturn(") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               ">= 0") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "got -1 and 0") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "path=/tmp/ckpt.dmtcp") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "errno=9") != nullptr);
}

void validFdMacrosEvaluateExpressionOnce()
{
  int calls = 0;

  ASSERT_VALID_FD(returnZeroAndCount(&calls));
  WARN_VALID_FD(returnZeroAndCount(&calls));

  UNIT_ASSERT_EQ(calls, 2);
}

void warningForkSuccessReportsExpressionReturnValueAndErrno()
{
  resetHook();

  errno = 0;
  WARN_FORK_SUCCESS(setErrnoAndReturn(-1, EAGAIN));

  UNIT_ASSERT_EQ(errno, EAGAIN);
  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "expected setErrnoAndReturn(") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               ">= 0") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "got -1 and 0") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "errno=11") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "EAGAIN") != nullptr);
}

void warningForkSuccessMessageReportsExtraContext()
{
  resetHook();

  errno = 0;
  WARN_FORK_SUCCESS(setErrnoAndReturn(-1, EAGAIN),
                           "phase={}",
                           "checkpoint");

  UNIT_ASSERT_EQ(errno, EAGAIN);
  UNIT_ASSERT_EQ(hookCallCount, 1);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "expected setErrnoAndReturn(") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               ">= 0") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0],
                               "got -1 and 0") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "phase=checkpoint") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(hookBuffers[0], "errno=11") != nullptr);
}

void forkSuccessMacrosEvaluateExpressionOnce()
{
  int calls = 0;

  ASSERT_FORK_SUCCESS(returnZeroAndCount(&calls));
  WARN_FORK_SUCCESS(returnZeroAndCount(&calls));

  UNIT_ASSERT_EQ(calls, 2);
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

void assertFailureUsesRawExitPath()
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
  {"warning syscall-success reports expression return value and errno",
   warningSyscallSuccessReportsExpressionReturnValueAndErrno},
  {"warning syscall-success message reports extra context",
   warningSyscallSuccessMessageReportsExtraContext},
  {"warning syscall-eq reports expression return value and errno",
   warningSyscallEqReportsExpressionReturnValueAndErrno},
  {"warning syscall-eq message reports extra context",
   warningSyscallEqMessageReportsExtraContext},
  {"warning valid-fd reports expression and return value",
   warningValidFdReportsExpressionAndReturnValue},
  {"warning valid-fd message reports extra context",
   warningValidFdMessageReportsExtraContext},
  {"valid-fd macros evaluate expression once",
   validFdMacrosEvaluateExpressionOnce},
  {"warning fork-success reports expression return value and errno",
   warningForkSuccessReportsExpressionReturnValueAndErrno},
  {"warning fork-success message reports extra context",
   warningForkSuccessMessageReportsExtraContext},
  {"fork-success macros evaluate expression once",
   forkSuccessMacrosEvaluateExpressionOnce},
  {"warning pthread success message reports extra context",
   warningPthreadSuccessMessageReportsExtraContext},
  {"assert failure exits with raw failure code",
   assertFailureExitsWithRawFailureCode},
  {"assert failure uses raw exit path", assertFailureUsesRawExitPath},
};

extern const size_t utilAssertTestCount =
  sizeof(utilAssertTests) / sizeof(utilAssertTests[0]);
