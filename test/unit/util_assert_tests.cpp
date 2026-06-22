#define DMTCP_TEST_NO_SHORT_ASSERT_MACROS
#include "unit_test.h"

#include "dmtcp_assert.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <fcntl.h>
#include <cstdio>
#include <pthread.h>
#include <source_location>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define UNIT_ASSERT_TRUE(expr) \
  ::dmtcp_test::assertTrue((expr), #expr, __FILE__, __LINE__)

#define UNIT_ASSERT_EQ(lhs, rhs) \
  ::dmtcp_test::assertEqual((lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__)

namespace {

constexpr int kThreadLogEntries = 4;
constexpr const char *kFormatTestExpr = "format_test_expr";

char logCapturePath[128];
int logCaptureSerial = 0;

struct ThreadLogArgs {
  int id;
};

void
makeTempPath(char *path, size_t capacity, const char *suffix)
{
  std::snprintf(path, capacity,
                "/tmp/dmtcp-util-assert-test-%ld-%d-%s.log",
                static_cast<long>(getpid()), ++logCaptureSerial, suffix);
}

void
resetLogCapture()
{
  dmtcp::setLogLevel(dmtcp::LogLevel::Note);
  dmtcp::setLogOverrides("");
  dmtcp::setLogFile(nullptr);
  dmtcp::closeLogConsole();
  if (logCapturePath[0] != '\0') {
    unlink(logCapturePath);
  }
  makeTempPath(logCapturePath, sizeof(logCapturePath), "console");
  unlink(logCapturePath);
  dmtcp::initializeLogConsole(logCapturePath);
}

std::string
readFile(const char *path)
{
  FILE *file = std::fopen(path, "rb");
  if (file == nullptr) {
    return "";
  }

  std::string contents;
  char buffer[256];
  size_t bytesRead = 0;
  while ((bytesRead = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    contents.append(buffer, bytesRead);
  }
  std::fclose(file);
  return contents;
}

std::string
readLogCapture()
{
  return readFile(logCapturePath);
}

size_t
countOccurrences(std::string_view text, std::string_view needle)
{
  size_t count = 0;
  size_t offset = 0;
  while ((offset = text.find(needle, offset)) != std::string_view::npos) {
    ++count;
    offset += needle.size();
  }
  return count;
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

void *
emitThreadLogs(void *data)
{
  ThreadLogArgs *args = static_cast<ThreadLogArgs *>(data);
  for (int entry = 0; entry < kThreadLogEntries; ++entry) {
    NOTE("thread log id={} entry={}", args->id, entry);
  }
  return nullptr;
}

template <typename... Args>
std::string
logMessageForTest(dmtcp::LogLevel level,
                  std::source_location location,
                  const char *expr,
                  int savedErrno,
                  bool includeErrno,
                  std::string_view fmt,
                  const Args&... args)
{
  resetLogCapture();
  dmtcp::Logger::logMessage(level, location, expr, savedErrno,
                            includeErrno, fmt, args...);
  return readLogCapture();
}

template <typename... Args>
std::string
formatPayloadForTest(std::string_view fmt, const Args&... args)
{
  std::string log = logMessageForTest(dmtcp::LogLevel::Warn,
                                      std::source_location::current(),
                                      kFormatTestExpr,
                                      0,
                                      false,
                                      fmt,
                                      args...);

  std::string marker = std::string(": ") + kFormatTestExpr + ": ";
  const char *payload = std::strstr(log.c_str(), marker.c_str());
  UNIT_ASSERT_TRUE(payload != nullptr);
  payload += marker.size();

  std::string result(payload);
  if (!result.empty() && result.back() == '\n') {
    result.pop_back();
  }
  return result;
}

void formatCopiesLiteralText()
{
  std::string text = formatPayloadForTest("plain text");

  UNIT_ASSERT_EQ(std::strcmp(text.c_str(), "plain text"), 0);
}

void formatSubstitutesBasicValues()
{
  std::string text = formatPayloadForTest("fd={} path={} ok={}",
                                          7,
                                          "/tmp/demo",
                                          true);

  UNIT_ASSERT_EQ(std::strcmp(text.c_str(), "fd=7 path=/tmp/demo ok=true"),
                 0);
}

enum FormatTestEnum {
  kFormatTestEnumValue = 42,
};

void formatSupportsEnumValues()
{
  std::string text = formatPayloadForTest("type={}", kFormatTestEnumValue);

  UNIT_ASSERT_EQ(std::strcmp(text.c_str(), "type=42"), 0);
}

void formatSupportsStdStringValues()
{
  std::string path = "/tmp/dmtcp";

  std::string text = formatPayloadForTest("path={}", path);

  UNIT_ASSERT_EQ(std::strcmp(text.c_str(), "path=/tmp/dmtcp"), 0);
}

void formatHonorsEscapedBraces()
{
  std::string text = formatPayloadForTest("{{value}}={}", 12);

  UNIT_ASSERT_EQ(std::strcmp(text.c_str(), "{value}=12"), 0);
}

void formatSupportsPointerHexValues()
{
  int value = 0;

  std::string text = formatPayloadForTest("ptr={}", &value);

  UNIT_ASSERT_TRUE(std::strstr(text.c_str(), "ptr=0x") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(text.c_str(), "(null)") == nullptr);
}

void formatLeavesUnsupportedSpecsLiteralAndReportsUnusedArgs()
{
  std::string text = formatPayloadForTest("plain={:x} padded={:08x}",
                                          255,
                                          10);

  UNIT_ASSERT_EQ(std::strcmp(text.c_str(),
                             "plain={:x} padded={:08x} "
                             "[unused-format-args=2]"), 0);
}

void formatLeavesInvalidSpecLiteral()
{
  std::string text = formatPayloadForTest("bad={:q} value={}", 9);

  UNIT_ASSERT_EQ(std::strcmp(text.c_str(), "bad={:q} value=9"), 0);
}

void formatReportsUnusedArguments()
{
  std::string text = formatPayloadForTest("value={}", 7, "dropped", 3);

  UNIT_ASSERT_EQ(std::strcmp(text.c_str(),
                             "value=7 [unused-format-args=2]"), 0);
}

void formatReportsMissingArguments()
{
  std::string text = formatPayloadForTest("first={} second={}", 7);

  UNIT_ASSERT_EQ(std::strcmp(text.c_str(),
                             "first=7 second=[missing-format-arg]"), 0);
}

void formatTruncatesWithMarker()
{
  std::string payload(5000, 'x');

  std::string log = logMessageForTest(dmtcp::LogLevel::Warn,
                                      std::source_location::current(),
                                      kFormatTestExpr,
                                      0,
                                      false,
                                      "{}",
                                      payload);

  UNIT_ASSERT_TRUE(log.size() <= dmtcp::kLogBufferSize);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), " [truncated]\n") !=
                   nullptr);
}

void logIncludesLocationAndMessage()
{
  const auto location = std::source_location::current();
  std::string log = logMessageForTest(dmtcp::LogLevel::Warn,
                                      location,
                                      "x > 0",
                                      0,
                                      false,
                                      "fd={}",
                                      9);

  char expectedLocation[32];
  std::snprintf(expectedLocation, sizeof(expectedLocation), ":%u:",
                static_cast<unsigned>(location.line()));

  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), "WARNING") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), "util_assert_tests.cpp") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), expectedLocation) != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), "x > 0") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), "fd=9") != nullptr);
}

void logIncludesErrno()
{
  const auto location = std::source_location::current();
  std::string log = logMessageForTest(dmtcp::LogLevel::Warn,
                                      location,
                                      "fd >= 0",
                                      EACCES,
                                      true,
                                      "path={}",
                                      "/tmp/demo");

  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), "WARNING") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), "fd >= 0") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), "path=/tmp/demo") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), "errno=13") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), "EACCES") != nullptr);
}

void logTruncationIncludesMarker()
{
  std::string payload(5000, 'x');
  std::string log = logMessageForTest(dmtcp::LogLevel::Warn,
                                      std::source_location::current(),
                                      "x > 0",
                                      0,
                                      false,
                                      "payload={}",
                                      payload);

  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), " [truncated]\n") != nullptr);
}

void warningLogsUseConfiguredConsole()
{
  resetLogCapture();

  WARN(false, "stderr destination");

  std::string log = readLogCapture();
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), "stderr destination") != nullptr);
}

void warningLogsAlsoUseLogFile()
{
  resetLogCapture();

  char path[128];
  makeTempPath(path, sizeof(path), "extra-log");
  unlink(path);

  UNIT_ASSERT_TRUE(dmtcp::setLogFile(path));
  WARN(false, "log destination");
  dmtcp::setLogFile(nullptr);

  std::string consoleLog = readLogCapture();
  std::string fileLog = readFile(path);
  unlink(path);

  UNIT_ASSERT_TRUE(std::strstr(consoleLog.c_str(),
                               "log destination") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(fileLog.c_str(), "log destination") !=
                   nullptr);
}

void multipleThreadsCanWriteLogs()
{
  resetLogCapture();
  dmtcp::setLogLevel(dmtcp::LogLevel::Note);

  constexpr int threadCount = 4;
  pthread_t threads[threadCount];
  ThreadLogArgs args[threadCount];

  for (int id = 0; id < threadCount; ++id) {
    args[id].id = id;
    UNIT_ASSERT_EQ(pthread_create(&threads[id], nullptr,
                                  emitThreadLogs, &args[id]),
                   0);
  }
  for (pthread_t thread : threads) {
    UNIT_ASSERT_EQ(pthread_join(thread, nullptr), 0);
  }

  std::string log = readLogCapture();
  UNIT_ASSERT_EQ(countOccurrences(log, "thread log id="),
                 static_cast<size_t>(threadCount * kThreadLogEntries));
  for (int id = 0; id < threadCount; ++id) {
    for (int entry = 0; entry < kThreadLogEntries; ++entry) {
      char needle[64];
      std::snprintf(needle, sizeof(needle),
                    "thread log id=%d entry=%d", id, entry);
      UNIT_ASSERT_TRUE(std::strstr(log.c_str(), needle) != nullptr);
    }
  }
}

void warningDoesNotEvaluateMessageArgsWhenConditionPasses()
{
  resetLogCapture();
  int calls = 0;

  WARN(true, "arg={}", ++calls);

  UNIT_ASSERT_EQ(calls, 0);
  UNIT_ASSERT_TRUE(readLogCapture().empty());
}

void warningDoesNotEvaluateMessageArgsWhenLogLevelDisabled()
{
  resetLogCapture();
  int calls = 0;
  dmtcp::setLogLevel(dmtcp::LogLevel::Error);

  WARN(false, "arg={}", incrementAndReturn(&calls));

  UNIT_ASSERT_EQ(calls, 0);
  UNIT_ASSERT_TRUE(readLogCapture().empty());
}

void warningErrnoUsesSavedErrnoAcrossMessageArgs()
{
  resetLogCapture();
  errno = EACCES;

  WARN_ERRNO(false, "arg={}", setErrnoAndReturn(7, ENOENT));

  UNIT_ASSERT_EQ(errno, EACCES);
  std::string log = readLogCapture();
  UNIT_ASSERT_EQ(countOccurrences(log, "WARNING"), static_cast<size_t>(1));
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), "arg=7") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), "errno=13") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), "EACCES") != nullptr);
}

void convenienceAssertMacrosPassWithoutWriting()
{
  resetLogCapture();
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

  UNIT_ASSERT_TRUE(readLogCapture().empty());
}

void convenienceAssertMacrosEvaluateOperandsOnce()
{
  resetLogCapture();
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
  UNIT_ASSERT_TRUE(readLogCapture().empty());
}

void convenienceAssertMessageMacrosEvaluateOperandsOnce()
{
  resetLogCapture();
  int lhs = 0;
  int rhs = 1;

  ASSERT_EQ(++lhs, rhs, "lhs advanced");
  ASSERT_GE(lhs, rhs, "lhs should catch rhs");

  UNIT_ASSERT_EQ(lhs, 1);
  UNIT_ASSERT_TRUE(readLogCapture().empty());
}

void convenienceWarningMacrosPassWithoutWriting()
{
  resetLogCapture();
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

  UNIT_ASSERT_TRUE(readLogCapture().empty());
}

void convenienceWarningMacrosEvaluateOperandsOnce()
{
  resetLogCapture();
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
  UNIT_ASSERT_TRUE(readLogCapture().empty());
}

void convenienceWarningMessageMacrosReportFailuresAndContinue()
{
  resetLogCapture();
  int value = 3;
  int *ptr = &value;
  int *nullPtr = nullptr;

  WARN_EQ(2, value, "context={}", "warning-eq");
  WARN_GT(2, value, "context={}", "warning-gt");
  WARN_NULL(ptr, "context={}", "warning-null");
  WARN_NOT_NULL(nullPtr, "context={}", "warning-not-null");

  std::string log = readLogCapture();
  UNIT_ASSERT_EQ(countOccurrences(log, "WARNING"), static_cast<size_t>(4));
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
                               "expected 2 == value, got 2 and 3") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
                               "context=warning-eq") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
                               "expected 2 > value, got 2 and 3") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
                               "context=warning-gt") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
                               "expected null: ptr, got") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
                               "context=warning-null") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
                               "expected non-null: nullPtr, got (null)") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
                               "context=warning-not-null") != nullptr);
}

void convenienceWarningMacrosReportFailuresAndContinue()
{
  resetLogCapture();
  int value = 3;
  int *ptr = &value;

  WARN_EQ(2, value);
  WARN_NULL(ptr);

  std::string log = readLogCapture();
  UNIT_ASSERT_EQ(countOccurrences(log, "WARNING"), static_cast<size_t>(2));
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
                               "expected 2 == value, got 2 and 3") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
                               "expected null: ptr") != nullptr);
}

void warningLockSuccessReportsExpressionAndReturnValue()
{
  resetLogCapture();

  WARN_LOCK_SUCCESS(setErrnoAndReturn(EINVAL, EIO));

  std::string log = readLogCapture();
  UNIT_ASSERT_EQ(countOccurrences(log, "WARNING"), static_cast<size_t>(1));
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
                               "expected 0 == setErrnoAndReturn(") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
                               "got 0 and 22") !=
                   nullptr);
}

void warningPthreadSuccessReportsExpressionAndReturnValue()
{
  resetLogCapture();

  WARN_PTHREAD_SUCCESS(setErrnoAndReturn(EAGAIN, EIO));

  std::string log = readLogCapture();
  UNIT_ASSERT_EQ(countOccurrences(log, "WARNING"), static_cast<size_t>(1));
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
                               "expected 0 == setErrnoAndReturn(") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
                               "got 0 and 11") !=
                   nullptr);
}

void warningZeroReturnReportsExpressionAndReturnValue()
{
  resetLogCapture();

  WARN_ZERO(setErrnoAndReturn(EINVAL, EIO));

  std::string log = readLogCapture();
  UNIT_ASSERT_EQ(countOccurrences(log, "WARNING"), static_cast<size_t>(1));
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
                               "expected 0 == setErrnoAndReturn(") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
                               "got 0 and 22") !=
                   nullptr);
}

void warningZeroReturnMessageReportsExtraContext()
{
  resetLogCapture();

  WARN_ZERO(setErrnoAndReturn(EINVAL, EIO),
            "fd={} path={}",
            9,
            "/dev/ptmx");

  std::string log = readLogCapture();
  UNIT_ASSERT_EQ(countOccurrences(log, "WARNING"), static_cast<size_t>(1));
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
                               "expected 0 == setErrnoAndReturn(") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
                               "got 0 and 22") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
                               "fd=9 path=/dev/ptmx") !=
                   nullptr);
}

void warningPthreadSuccessMessageReportsExtraContext()
{
  resetLogCapture();

  WARN_PTHREAD_SUCCESS(setErrnoAndReturn(EINVAL, EIO),
                       "tid={} signal={}",
                       123,
                       9);

  std::string log = readLogCapture();
  UNIT_ASSERT_EQ(countOccurrences(log, "WARNING"), static_cast<size_t>(1));
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
                               "expected 0 == setErrnoAndReturn(") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
                               "got 0 and 22") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(),
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

void errorLogLevelIsAlwaysEnabled()
{
  resetLogCapture();
  dmtcp::setLogLevel(dmtcp::LogLevel::Error);
  UNIT_ASSERT_TRUE(dmtcp::setLogOverrides("pid=error"));

  UNIT_ASSERT_TRUE(dmtcp::logEnabled(dmtcp::LogLevel::Error,
                                     "socket",
                                     "src/plugin/socket/socketwrappers.cpp"));
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
  resetLogCapture();
  dmtcp::setLogLevel(dmtcp::LogLevel::Note);
  errno = EACCES;

  const int logLine = __LINE__ + 1;
  NOTE("note value={}", 17);

  char expectedLocation[32];
  std::snprintf(expectedLocation, sizeof(expectedLocation), ":%d:", logLine);

  UNIT_ASSERT_EQ(errno, EACCES);
  std::string log = readLogCapture();
  UNIT_ASSERT_EQ(countOccurrences(log, "NOTE"), static_cast<size_t>(1));
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), "NOTE") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), "util_assert_tests.cpp") !=
                   nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), expectedLocation) != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), "note value=17") != nullptr);
}

void noteDoesNotEvaluateArgsWhenDisabled()
{
  resetLogCapture();
  int calls = 0;
  dmtcp::setLogLevel(dmtcp::LogLevel::Warn);

  NOTE("calls={}", incrementAndReturn(&calls));

  UNIT_ASSERT_EQ(calls, 0);
  UNIT_ASSERT_TRUE(readLogCapture().empty());
}

void traceDoesNotEvaluateArgsWhenDisabled()
{
  resetLogCapture();
  int calls = 0;
  dmtcp::setLogLevel(dmtcp::LogLevel::Note);

  TRACE("calls={}", incrementAndReturn(&calls));

  UNIT_ASSERT_EQ(calls, 0);
  UNIT_ASSERT_TRUE(readLogCapture().empty());
}

void traceLogsWhenEnabled()
{
  resetLogCapture();
  int calls = 0;
  dmtcp::setLogLevel(dmtcp::LogLevel::Trace);

  TRACE("trace calls={}", incrementAndReturn(&calls));

  UNIT_ASSERT_EQ(calls, 1);
  std::string log = readLogCapture();
  UNIT_ASSERT_EQ(countOccurrences(log, "TRACE"), static_cast<size_t>(1));
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), "TRACE") != nullptr);
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), "trace calls=1") != nullptr);
}

void traceSupportsFormattedValues()
{
  resetLogCapture();
  int fd = 7;
  dmtcp::setLogLevel(dmtcp::LogLevel::Trace);

  TRACE("formatted trace fd={}", fd);

  std::string log = readLogCapture();
  UNIT_ASSERT_EQ(countOccurrences(log, "TRACE"), static_cast<size_t>(1));
  UNIT_ASSERT_TRUE(std::strstr(log.c_str(), "formatted trace fd=7") !=
                   nullptr);
}

void componentOverrideEnablesTraceForMatchingComponent()
{
  resetLogCapture();
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
  resetLogCapture();
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
  {"fixed formatter truncates with marker", formatTruncatesWithMarker},
  {"log formatter includes location and message",
   logIncludesLocationAndMessage},
  {"log formatter includes errno", logIncludesErrno},
  {"log truncation includes marker",
   logTruncationIncludesMarker},
  {"warning logs use configured console",
   warningLogsUseConfiguredConsole},
  {"warning logs also use log file",
   warningLogsAlsoUseLogFile},
  {"multiple threads can write logs",
   multipleThreadsCanWriteLogs},
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
  {"Error log level is always enabled",
   errorLogLevelIsAlwaysEnabled},
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
  {"file override beats component override",
   fileOverrideBeatsComponentOverride},
  {"assert failure exits with raw failure code",
   assertFailureExitsWithRawFailureCode},
  {"assert failure honors DMTCP_FAIL_RC", assertFailureHonorsFailRc},
  {"assert failure honors DMTCP_ABORT_ON_FAILURE",
   assertFailureHonorsAbortOnFailure},
};

extern const size_t utilAssertTestCount =
  sizeof(utilAssertTests) / sizeof(utilAssertTests[0]);
