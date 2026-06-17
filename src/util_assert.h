#ifndef DMTCP_UTIL_ASSERT_H
#define DMTCP_UTIL_ASSERT_H

#include <algorithm>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <source_location>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <tuple>
#include <unistd.h>

#include "dmtcp.h"

namespace dmtcp {

inline constexpr int kAssertFailureExitCode = 99;
inline constexpr int kLogFd = STDERR_FILENO;
inline constexpr size_t kLogBufferSize = 4096;
inline constexpr std::string_view kLogTruncatedMarker = " [truncated]\n";

/*
 * Log destination policy:
 * - Generic ASSERT/WARNING logs write to kLogFd, currently stderr.  The
 *   caller may redirect or close stderr; the log emit path
 *   treats write failures as best-effort and never falls back to allocation,
 *   logging policy, environment variables, or richer DMTCP runtime services.
 * - Signal-handler logs use the same fixed-buffer backend.  Keep this path
 *   free of allocation, locks, wrapped I/O, and richer DMTCP runtime
 *   services so it remains usable from fragile runtime contexts.
 * - Fatal ASSERT exits through DMTCP_FAIL_RC after honoring
 *   DMTCP_SLEEP_ON_FAILURE and DMTCP_ABORT_ON_FAILURE.
 */

/*
 * Assertion helper guide:
 * - ASSERT(cond, "msg {}", arg) logs and exits; WARN(cond, ...) logs and
 *   continues.  NOTE(...) and TRACE(...) use the same formatter and are
 *   controlled by the runtime log level.  ASSERT_ERRNO/WARN_ERRNO add the
 *   errno value captured at the failing check.
 * - Named helpers such as ASSERT_EQ(expected, actual),
 *   ASSERT_NOT_NULL(ptr), ASSERT_ZERO(expr), ASSERT_LOCK_SUCCESS(expr), and
 *   ASSERT_PTHREAD_SUCCESS(expr) evaluate their operands once and include the
 *   operand text plus observed values in the log.
 * - Format strings support only "{}" replacement plus "{{" and "}}" escapes.
 *   There are no width, alignment, precision, or type specifiers.  Pointers
 *   print in hexadecimal; bools print as true/false; missing or unused
 *   arguments are reported in the log text.
 * - Message arguments are evaluated only when the check fails.
 */

enum class LogLevel : int {
  Error = 0,
  Warn = 1,
  Note = 2,
  Trace = 3,
};

void setLogLevel(LogLevel level);
bool parseLogLevel(std::string_view text, LogLevel *level);
bool setLogOverrides(std::string_view overrides);
bool logEnabled(LogLevel level,
                std::string_view component,
                std::string_view file);
void initializeLogConsole(const char *stderrPath);
bool setLogFile(const char *path);
void closeLogConsole();

/*
 * Helper macros such as ASSERT_EQ build a standard failure message and then
 * optionally append user-provided detail.  This wrapper lets that optional
 * detail travel through the formatter as one argument while preserving lazy
 * evaluation: detail arguments are evaluated only inside the failing log path.
 */
template <typename... Args>
struct DeferredLogDetail {
  std::string_view fmt;
  std::tuple<const Args&...> args;
};

class AssertBuffer {
 public:
  AssertBuffer(char *storage, size_t capacity)
    : data_(storage),
      availableStorage_(capacity > kLogTruncatedMarker.size() + 1
                          ? capacity - kLogTruncatedMarker.size() - 1
                          : 0),
      size_(0),
      truncated_(false)
  {
    if (capacity > 0) {
      data_[0] = '\0';
    }
  }

  void append(std::string_view text)
  {
    if (truncated_ || text.empty()) {
      return;
    }
    if (availableStorage_ == 0) {
      truncated_ = true;
      return;
    }

    const size_t available = size_ < availableStorage_
                               ? availableStorage_ - size_
                               : 0;
    const size_t toCopy = std::min(available, text.size());
    std::memcpy(data_ + size_, text.data(), toCopy);
    size_ += toCopy;
    if (toCopy < text.size()) {
      std::memcpy(data_ + size_,
                  kLogTruncatedMarker.data(),
                  kLogTruncatedMarker.size());
      size_ += kLogTruncatedMarker.size();
      truncated_ = true;
    }
    data_[size_] = '\0';
  }

  void append(const char *value)
  {
    append(value == nullptr ? std::string_view("(null)")
                            : std::string_view(value));
  }

  const char *c_str() const
  {
    return availableStorage_ == 0 ? "" : data_;
  }

  size_t size() const { return size_; }
  bool truncated() const { return truncated_; }

 private:
  char *data_;
  size_t availableStorage_;
  size_t size_;
  bool truncated_;
};

inline const char *
logLevelName(LogLevel level)
{
  switch (level) {
  case LogLevel::Trace:
    return "TRACE";
  case LogLevel::Note:
    return "NOTE";
  case LogLevel::Warn:
    return "WARNING";
  case LogLevel::Error:
    return "ASSERT";
  }
  return "ASSERT";
}

inline const char *
errnoName(int savedErrno)
{
#ifdef EACCES
  if (savedErrno == EACCES) { return "EACCES"; }
#endif
#ifdef EAGAIN
  if (savedErrno == EAGAIN) { return "EAGAIN"; }
#endif
#ifdef EBADF
  if (savedErrno == EBADF) { return "EBADF"; }
#endif
#ifdef EEXIST
  if (savedErrno == EEXIST) { return "EEXIST"; }
#endif
#ifdef EINTR
  if (savedErrno == EINTR) { return "EINTR"; }
#endif
#ifdef EINVAL
  if (savedErrno == EINVAL) { return "EINVAL"; }
#endif
#ifdef EIO
  if (savedErrno == EIO) { return "EIO"; }
#endif
#ifdef EMFILE
  if (savedErrno == EMFILE) { return "EMFILE"; }
#endif
#ifdef ENOENT
  if (savedErrno == ENOENT) { return "ENOENT"; }
#endif
#ifdef ENOMEM
  if (savedErrno == ENOMEM) { return "ENOMEM"; }
#endif
#ifdef ENOSYS
  if (savedErrno == ENOSYS) { return "ENOSYS"; }
#endif
#ifdef EPERM
  if (savedErrno == EPERM) { return "EPERM"; }
#endif
#ifdef EPIPE
  if (savedErrno == EPIPE) { return "EPIPE"; }
#endif
#ifdef ERANGE
  if (savedErrno == ERANGE) { return "ERANGE"; }
#endif
#ifdef ETIMEDOUT
  if (savedErrno == ETIMEDOUT) { return "ETIMEDOUT"; }
#endif
  return "UNKNOWN";
}

template <typename... Args>
inline DeferredLogDetail<Args...>
logDetail(std::string_view fmt, const Args&... args)
{
  return {fmt, std::tuple<const Args&...>(args...)};
}

inline void writeAllNoAlloc(int fd, const char *data, size_t length);
void emitLogMessage(const char *data, size_t length);

[[noreturn]] inline void
exitAfterAssertFailure()
{
  while (std::getenv("DMTCP_SLEEP_ON_FAILURE") != nullptr) {
  }
  _exit(DMTCP_FAIL_RC);
}

inline void
writeAllNoAlloc(int fd, const char *data, size_t length)
{
  while (length > 0) {
    ssize_t written = write(fd, data, length);
    if (written == -1 && errno == EINTR) {
      continue;
    }
    if (written <= 0) {
      return;
    }
    data += written;
    length -= written;
  }
}

class LogMessage {
 public:
  LogMessage(LogLevel level,
             std::source_location location,
             const char *expr,
             int savedErrno,
             bool includeErrno)
    : buffer_(storage_, sizeof(storage_)),
      level_(level),
      location_(location),
      expr_(expr),
      savedErrno_(savedErrno),
      includeErrno_(includeErrno)
  {
  }

  template <typename... Args>
  void format(std::string_view fmt, const Args&... args)
  {
    appendPrefix();
    appendUserMessage(fmt, args...);
    if (includeErrno_) {
      appendErrno();
    }
    finish();
  }

  const char *c_str() const { return buffer_.c_str(); }
  size_t size() const { return buffer_.size(); }
  bool truncated() const { return buffer_.truncated(); }

 private:
  void appendPrefix()
  {
    buffer_.append(logLevelName(level_));
    buffer_.append(" at ");
    buffer_.append(location_.file_name() == nullptr ? "<unknown>"
                                                    : location_.file_name());
    buffer_.append(":");
    appendValue(location_.line());
    buffer_.append(": ");
    buffer_.append(expr_ == nullptr ? "<none>" : expr_);
  }

  template <typename... Args>
  void appendUserMessage(std::string_view fmt, const Args&... args)
  {
    if (!fmt.empty()) {
      buffer_.append(": ");
      formatText(fmt, args...);
    }
  }

  void appendErrno()
  {
    buffer_.append(": errno=");
    appendValue(savedErrno_);
    buffer_.append(" (");
    buffer_.append(errnoName(savedErrno_));
    buffer_.append(")");
  }

  void formatText(std::string_view fmt)
  {
    size_t literalStart = 0;
    for (size_t i = 0; i < fmt.size(); ++i) {
      if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '{') {
        buffer_.append(fmt.substr(literalStart, i - literalStart));
        buffer_.append("{");
        ++i;
        literalStart = i + 1;
      } else if (fmt[i] == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
        buffer_.append(fmt.substr(literalStart, i - literalStart));
        buffer_.append("}");
        ++i;
        literalStart = i + 1;
      } else if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
        buffer_.append(fmt.substr(literalStart, i - literalStart));
        buffer_.append("[missing-format-arg]");
        ++i;
        literalStart = i + 1;
      }
    }
    buffer_.append(fmt.substr(literalStart));
  }

  template <typename First, typename... Rest>
  void formatText(std::string_view fmt,
                  const First& first,
                  const Rest&... rest)
  {
    size_t literalStart = 0;
    for (size_t i = 0; i < fmt.size(); ++i) {
      if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '{') {
        buffer_.append(fmt.substr(literalStart, i - literalStart));
        buffer_.append("{");
        ++i;
        literalStart = i + 1;
      } else if (fmt[i] == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
        buffer_.append(fmt.substr(literalStart, i - literalStart));
        buffer_.append("}");
        ++i;
        literalStart = i + 1;
      } else if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
        buffer_.append(fmt.substr(literalStart, i - literalStart));
        appendValue(first);
        formatText(fmt.substr(i + 2), rest...);
        return;
      }
    }
    buffer_.append(fmt.substr(literalStart));
    appendUnusedFormatArgs(sizeof...(Rest) + 1);
  }

  void appendValue(std::string_view value)
  {
    buffer_.append(value);
  }

  void appendValue(const char *value)
  {
    buffer_.append(value == nullptr ? std::string_view("(null)")
                                    : std::string_view(value));
  }

  void appendValue(char *value)
  {
    appendValue(static_cast<const char *>(value));
  }

  void appendValue(bool value)
  {
    buffer_.append(value ? "true" : "false");
  }

  void appendValue(char value)
  {
    buffer_.append(std::string_view(&value, 1));
  }

  void appendValue(std::nullptr_t)
  {
    buffer_.append("(null)");
  }

  template <typename... Args>
  void appendValue(const DeferredLogDetail<Args...>& detail)
  {
    std::apply([&](const auto&... args) {
      formatText(detail.fmt, args...);
    }, detail.args);
  }

  template <std::integral T>
    requires (!std::same_as<std::remove_cv_t<T>, bool> &&
              !std::same_as<std::remove_cv_t<T>, char>)
  void appendValue(T value)
  {
    appendInteger(value);
  }

  template <typename T>
    requires std::is_enum_v<std::remove_cvref_t<T>>
  void appendValue(T value)
  {
    using Underlying = std::underlying_type_t<std::remove_cvref_t<T>>;
    appendValue(static_cast<Underlying>(value));
  }

  template <typename T>
    requires (std::is_pointer_v<T> &&
              !std::is_convertible_v<T, const char *>)
  void appendValue(T value)
  {
    if (value == nullptr) {
      buffer_.append("(null)");
      return;
    }

    buffer_.append("0x");
    appendHex(reinterpret_cast<uintptr_t>(value));
  }

  void appendValue(const DmtcpUniqueProcessId& value)
  {
    appendHex(value._hostid);
    buffer_.append("-");
    appendInteger(value._pid);
    buffer_.append("-");
    appendHex(value._time);
  }

  template <std::integral T>
  void appendHex(T value)
  {
    appendInteger(value, 16);
  }

  template <std::integral T>
  void appendInteger(T value, int base = 10)
  {
    char tmp[64];
    auto result = std::to_chars(tmp, tmp + sizeof(tmp), value, base);
    if (result.ec != std::errc()) {
      buffer_.append("<format-error>");
      return;
    }
    buffer_.append(std::string_view(tmp, result.ptr - tmp));
  }

  void appendUnusedFormatArgs(size_t count)
  {
    buffer_.append(" [unused-format-args=");
    appendValue(count);
    buffer_.append("]");
  }

  void finish()
  {
    if (!buffer_.truncated()) {
      buffer_.append("\n");
    }
  }

  char storage_[kLogBufferSize];
  AssertBuffer buffer_;
  LogLevel level_;
  std::source_location location_;
  const char *expr_;
  int savedErrno_;
  bool includeErrno_;
};

template <typename... Args>
inline void
logMessage(LogLevel level,
           std::source_location location,
           const char *expr,
           int savedErrno,
           bool includeErrno,
           std::string_view fmt,
           const Args&... args)
{
  LogMessage message(level, location, expr, savedErrno, includeErrno);
  message.format(fmt, args...);
  emitLogMessage(message.c_str(), message.size());
}

} // namespace dmtcp

#ifndef DMTCP_UTIL_ASSERT_NO_MACROS

#ifndef DMTCP_LOG_COMPONENT_DEFAULT
#define DMTCP_LOG_COMPONENT_DEFAULT "core"
#endif

#ifndef DMTCP_LOG_COMPONENT
#define DMTCP_LOG_COMPONENT DMTCP_LOG_COMPONENT_DEFAULT
#endif

#define DMTCP_LOG_WRITE(level, expr, includeErrno, fmt, ...)              \
  do {                                                                   \
    const int dmtcpAssertSavedErrno = errno;                              \
    const auto dmtcpLogLocation = std::source_location::current();        \
    if (::dmtcp::logEnabled(level, DMTCP_LOG_COMPONENT,                   \
                            dmtcpLogLocation.file_name())) {             \
      ::dmtcp::logMessage(level, dmtcpLogLocation, expr,                  \
                          dmtcpAssertSavedErrno, includeErrno, fmt       \
                          __VA_OPT__(,) __VA_ARGS__);                    \
    }                                                                    \
    errno = dmtcpAssertSavedErrno;                                        \
  } while (0)

#define TRACE(fmt, ...)                                                  \
  DMTCP_LOG_WRITE(::dmtcp::LogLevel::Trace, nullptr, false, fmt          \
                  __VA_OPT__(,) __VA_ARGS__)

#define NOTE(fmt, ...)                                                   \
  DMTCP_LOG_WRITE(::dmtcp::LogLevel::Note, nullptr, false, fmt           \
                  __VA_OPT__(,) __VA_ARGS__)

#define WARN(condition, fmt, ...)                                         \
  do {                                                                   \
    if (!(condition)) {                                                   \
      DMTCP_LOG_WRITE(::dmtcp::LogLevel::Warn, #condition, false, fmt    \
                      __VA_OPT__(,) __VA_ARGS__);                        \
    }                                                                    \
  } while (0)

#define WARN_ERRNO(condition, fmt, ...)                                   \
  do {                                                                   \
    if (!(condition)) {                                                   \
      DMTCP_LOG_WRITE(::dmtcp::LogLevel::Warn, #condition, true, fmt     \
                      __VA_OPT__(,) __VA_ARGS__);                        \
    }                                                                    \
  } while (0)

#define ASSERT(condition, fmt, ...)                                       \
  do {                                                                   \
    if (!(condition)) {                                                   \
      DMTCP_LOG_WRITE(::dmtcp::LogLevel::Error, #condition, false, fmt   \
                      __VA_OPT__(,) __VA_ARGS__);                        \
      ::dmtcp::exitAfterAssertFailure();                                  \
    }                                                                    \
  } while (0)

#define ASSERT_ERRNO(condition, fmt, ...)                                 \
  do {                                                                   \
    if (!(condition)) {                                                   \
      DMTCP_LOG_WRITE(::dmtcp::LogLevel::Error, #condition, true, fmt    \
                      __VA_OPT__(,) __VA_ARGS__);                        \
      ::dmtcp::exitAfterAssertFailure();                                  \
    }                                                                    \
  } while (0)

#define ASSERT_FALSE(condition, ...)                                      \
  ASSERT(!(condition), "expected false: {}"                              \
         __VA_OPT__("; {}"), #condition                                  \
         __VA_OPT__(, ::dmtcp::logDetail(__VA_ARGS__)))

#define ASSERT_TRUE(condition, ...)                                       \
  ASSERT((condition), "expected true: {}"                                \
         __VA_OPT__("; {}"), #condition                                  \
         __VA_OPT__(, ::dmtcp::logDetail(__VA_ARGS__)))

#define WARN_FALSE(condition, ...)                                        \
  WARN(!(condition), "expected false: {}"                                \
       __VA_OPT__("; {}"), #condition                                    \
       __VA_OPT__(, ::dmtcp::logDetail(__VA_ARGS__)))

#define WARN_TRUE(condition, ...)                                         \
  WARN((condition), "expected true: {}"                                  \
       __VA_OPT__("; {}"), #condition                                    \
       __VA_OPT__(, ::dmtcp::logDetail(__VA_ARGS__)))

#define ASSERT_NULL(value, ...)                                           \
  do {                                                                    \
    const auto dmtcpAssertValue = (value);                                 \
    ASSERT(dmtcpAssertValue == nullptr,                                    \
           "expected null: {}, got {}" __VA_OPT__("; {}"),               \
           #value, dmtcpAssertValue                                       \
           __VA_OPT__(, ::dmtcp::logDetail(__VA_ARGS__)));             \
  } while (0)

#define ASSERT_NOT_NULL(value, ...)                                       \
  do {                                                                    \
    const auto dmtcpAssertValue = (value);                                 \
    ASSERT(dmtcpAssertValue != nullptr,                                    \
           "expected non-null: {}, got {}"                                \
           __VA_OPT__("; {}"),                                            \
           #value, dmtcpAssertValue                                       \
           __VA_OPT__(, ::dmtcp::logDetail(__VA_ARGS__)));             \
  } while (0)

#define WARN_NULL(value, ...)                                             \
  do {                                                                    \
    const auto dmtcpAssertValue = (value);                                 \
    WARN(dmtcpAssertValue == nullptr,                                      \
         "expected null: {}, got {}" __VA_OPT__("; {}"),                 \
         #value, dmtcpAssertValue                                         \
         __VA_OPT__(, ::dmtcp::logDetail(__VA_ARGS__)));               \
  } while (0)

#define WARN_NOT_NULL(value, ...)                                         \
  do {                                                                    \
    const auto dmtcpAssertValue = (value);                                 \
    WARN(dmtcpAssertValue != nullptr,                                      \
         "expected non-null: {}, got {}" __VA_OPT__("; {}"),             \
         #value, dmtcpAssertValue                                         \
         __VA_OPT__(, ::dmtcp::logDetail(__VA_ARGS__)));               \
  } while (0)

#define ASSERT_EQ(expected, actual, ...)                                  \
  do {                                                                    \
    const auto& dmtcpAssertExpected = (expected);                          \
    const auto& dmtcpAssertActual = (actual);                              \
    ASSERT(dmtcpAssertExpected == dmtcpAssertActual,                       \
           "expected {} == {}, got {} and {}"                             \
           __VA_OPT__("; {}"),                                            \
           #expected, #actual, dmtcpAssertExpected, dmtcpAssertActual      \
           __VA_OPT__(, ::dmtcp::logDetail(__VA_ARGS__)));             \
  } while (0)

#define WARN_EQ(expected, actual, ...)                                    \
  do {                                                                    \
    const auto& dmtcpAssertExpected = (expected);                          \
    const auto& dmtcpAssertActual = (actual);                              \
    WARN(dmtcpAssertExpected == dmtcpAssertActual,                         \
         "expected {} == {}, got {} and {}"                               \
         __VA_OPT__("; {}"),                                              \
         #expected, #actual, dmtcpAssertExpected, dmtcpAssertActual        \
         __VA_OPT__(, ::dmtcp::logDetail(__VA_ARGS__)));               \
  } while (0)

#define ASSERT_NE(expected, actual, ...)                                  \
  do {                                                                    \
    const auto& dmtcpAssertExpected = (expected);                          \
    const auto& dmtcpAssertActual = (actual);                              \
    ASSERT(dmtcpAssertExpected != dmtcpAssertActual,                       \
           "expected {} != {}, got {} and {}"                             \
           __VA_OPT__("; {}"),                                            \
           #expected, #actual, dmtcpAssertExpected, dmtcpAssertActual      \
           __VA_OPT__(, ::dmtcp::logDetail(__VA_ARGS__)));             \
  } while (0)

#define WARN_NE(expected, actual, ...)                                    \
  do {                                                                    \
    const auto& dmtcpAssertExpected = (expected);                          \
    const auto& dmtcpAssertActual = (actual);                              \
    WARN(dmtcpAssertExpected != dmtcpAssertActual,                         \
         "expected {} != {}, got {} and {}"                               \
         __VA_OPT__("; {}"),                                              \
         #expected, #actual, dmtcpAssertExpected, dmtcpAssertActual        \
         __VA_OPT__(, ::dmtcp::logDetail(__VA_ARGS__)));               \
  } while (0)

#define ASSERT_GT(lhs, rhs, ...)                                          \
  do {                                                                    \
    const auto& dmtcpAssertLhs = (lhs);                                    \
    const auto& dmtcpAssertRhs = (rhs);                                    \
    ASSERT(dmtcpAssertLhs > dmtcpAssertRhs,                                \
           "expected {} > {}, got {} and {}"                              \
           __VA_OPT__("; {}"),                                            \
           #lhs, #rhs, dmtcpAssertLhs, dmtcpAssertRhs                     \
           __VA_OPT__(, ::dmtcp::logDetail(__VA_ARGS__)));             \
  } while (0)

#define WARN_GT(lhs, rhs, ...)                                            \
  do {                                                                    \
    const auto& dmtcpAssertLhs = (lhs);                                    \
    const auto& dmtcpAssertRhs = (rhs);                                    \
    WARN(dmtcpAssertLhs > dmtcpAssertRhs,                                  \
         "expected {} > {}, got {} and {}"                                \
         __VA_OPT__("; {}"),                                              \
         #lhs, #rhs, dmtcpAssertLhs, dmtcpAssertRhs                       \
         __VA_OPT__(, ::dmtcp::logDetail(__VA_ARGS__)));               \
  } while (0)

#define ASSERT_LT(lhs, rhs, ...)                                          \
  do {                                                                    \
    const auto& dmtcpAssertLhs = (lhs);                                    \
    const auto& dmtcpAssertRhs = (rhs);                                    \
    ASSERT(dmtcpAssertLhs < dmtcpAssertRhs,                                \
           "expected {} < {}, got {} and {}"                              \
           __VA_OPT__("; {}"),                                            \
           #lhs, #rhs, dmtcpAssertLhs, dmtcpAssertRhs                     \
           __VA_OPT__(, ::dmtcp::logDetail(__VA_ARGS__)));             \
  } while (0)

#define WARN_LT(lhs, rhs, ...)                                            \
  do {                                                                    \
    const auto& dmtcpAssertLhs = (lhs);                                    \
    const auto& dmtcpAssertRhs = (rhs);                                    \
    WARN(dmtcpAssertLhs < dmtcpAssertRhs,                                  \
         "expected {} < {}, got {} and {}"                                \
         __VA_OPT__("; {}"),                                              \
         #lhs, #rhs, dmtcpAssertLhs, dmtcpAssertRhs                       \
         __VA_OPT__(, ::dmtcp::logDetail(__VA_ARGS__)));               \
  } while (0)

#define ASSERT_GE(lhs, rhs, ...)                                          \
  do {                                                                    \
    const auto& dmtcpAssertLhs = (lhs);                                    \
    const auto& dmtcpAssertRhs = (rhs);                                    \
    ASSERT(dmtcpAssertLhs >= dmtcpAssertRhs,                               \
           "expected {} >= {}, got {} and {}"                             \
           __VA_OPT__("; {}"),                                            \
           #lhs, #rhs, dmtcpAssertLhs, dmtcpAssertRhs                     \
           __VA_OPT__(, ::dmtcp::logDetail(__VA_ARGS__)));             \
  } while (0)

#define WARN_GE(lhs, rhs, ...)                                            \
  do {                                                                    \
    const auto& dmtcpAssertLhs = (lhs);                                    \
    const auto& dmtcpAssertRhs = (rhs);                                    \
    WARN(dmtcpAssertLhs >= dmtcpAssertRhs,                                 \
         "expected {} >= {}, got {} and {}"                               \
         __VA_OPT__("; {}"),                                              \
         #lhs, #rhs, dmtcpAssertLhs, dmtcpAssertRhs                       \
         __VA_OPT__(, ::dmtcp::logDetail(__VA_ARGS__)));               \
  } while (0)

#define ASSERT_LE(lhs, rhs, ...)                                          \
  do {                                                                    \
    const auto& dmtcpAssertLhs = (lhs);                                    \
    const auto& dmtcpAssertRhs = (rhs);                                    \
    ASSERT(dmtcpAssertLhs <= dmtcpAssertRhs,                               \
           "expected {} <= {}, got {} and {}"                             \
           __VA_OPT__("; {}"),                                            \
           #lhs, #rhs, dmtcpAssertLhs, dmtcpAssertRhs                     \
           __VA_OPT__(, ::dmtcp::logDetail(__VA_ARGS__)));             \
  } while (0)

#define WARN_LE(lhs, rhs, ...)                                            \
  do {                                                                    \
    const auto& dmtcpAssertLhs = (lhs);                                    \
    const auto& dmtcpAssertRhs = (rhs);                                    \
    WARN(dmtcpAssertLhs <= dmtcpAssertRhs,                                 \
         "expected {} <= {}, got {} and {}"                               \
         __VA_OPT__("; {}"),                                              \
         #lhs, #rhs, dmtcpAssertLhs, dmtcpAssertRhs                       \
         __VA_OPT__(, ::dmtcp::logDetail(__VA_ARGS__)));               \
  } while (0)

#define ASSERT_LOCK_SUCCESS(expression, ...) \
  ASSERT_EQ(0, expression __VA_OPT__(,) __VA_ARGS__)

#define ASSERT_PTHREAD_SUCCESS(expression, ...) \
  ASSERT_EQ(0, expression __VA_OPT__(,) __VA_ARGS__)

#define ASSERT_ZERO(expression, ...) \
  ASSERT_EQ(0, expression __VA_OPT__(,) __VA_ARGS__)

#define WARN_LOCK_SUCCESS(expression, ...) \
  WARN_EQ(0, expression __VA_OPT__(,) __VA_ARGS__)

#define WARN_PTHREAD_SUCCESS(expression, ...) \
  WARN_EQ(0, expression __VA_OPT__(,) __VA_ARGS__)

#define WARN_ZERO(expression, ...) \
  WARN_EQ(0, expression __VA_OPT__(,) __VA_ARGS__)

#endif // DMTCP_UTIL_ASSERT_NO_MACROS

#endif // DMTCP_UTIL_ASSERT_H
