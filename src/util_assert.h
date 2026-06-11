#ifndef DMTCP_UTIL_ASSERT_H
#define DMTCP_UTIL_ASSERT_H

#include <algorithm>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <tuple>
#include <unistd.h>

#include "dmtcp.h"

extern "C" ssize_t dmtcp_assert_write(int fd, const void *buf, size_t count);

namespace dmtcp {

inline constexpr int kAssertFailureExitCode = 99;
inline constexpr int kDiagnosticFd = STDERR_FILENO;
inline constexpr size_t kAssertBufferSize = 4096;
inline constexpr std::string_view kLogTruncatedMarker = " [truncated]\n";

/*
 * Diagnostic destination policy:
 * - Generic ASSERT/WARNING diagnostics write to kDiagnosticFd, currently
 *   stderr.  The caller may redirect or close stderr; the diagnostic emit path
 *   treats write failures as best-effort and never falls back to allocation,
 *   logging policy, environment variables, or richer DMTCP runtime services.
 * - Signal-handler diagnostics use the same fixed-buffer backend.  Keep this
 *   path free of allocation, locks, wrapped I/O, and richer DMTCP runtime
 *   services so it remains usable from fragile runtime contexts.
 * - Fatal ASSERT exits use kAssertFailureExitCode and do not consult
 *   environment-controlled jalib failure policy.
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
 *   operand text plus observed values in the diagnostic.
 * - Format strings support only "{}" replacement plus "{{" and "}}" escapes.
 *   There are no width, alignment, precision, or type specifiers.  Pointers
 *   print in hexadecimal; bools print as true/false; missing or unused
 *   arguments are reported in the diagnostic text.
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

class AssertBuffer;

template <typename... Args>
inline void formatTo(AssertBuffer& buffer,
                     std::string_view fmt,
                     const Args&... args);

template <typename... Args>
struct FormatDetail {
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

  void append(char *value)
  {
    append(static_cast<const char *>(value));
  }

  void append(bool value)
  {
    append(value ? "true" : "false");
  }

  void append(char value)
  {
    append(std::string_view(&value, 1));
  }

  void append(std::nullptr_t)
  {
    append("(null)");
  }

  template <typename... Args>
  void append(const FormatDetail<Args...>& detail)
  {
    std::apply([&](const auto&... args) {
      formatTo(*this, detail.fmt, args...);
    }, detail.args);
  }

  template <typename T>
    requires requires(const T& value, AssertBuffer& buffer) {
      value.appendTo(buffer);
    }
  void append(const T& value)
  {
    value.appendTo(*this);
  }

  template <std::integral T>
    requires (!std::same_as<std::remove_cv_t<T>, bool> &&
              !std::same_as<std::remove_cv_t<T>, char>)
  void append(T value)
  {
    appendInteger(value);
  }

  template <std::integral T>
  void appendHex(T value)
  {
    appendInteger(value, 16);
  }

  template <typename T>
    requires std::is_enum_v<std::remove_cvref_t<T>>
  void append(T value)
  {
    using Underlying = std::underlying_type_t<std::remove_cvref_t<T>>;
    append(static_cast<Underlying>(value));
  }

  template <typename T>
    requires (std::is_pointer_v<T> &&
              !std::is_convertible_v<T, const char *>)
  void append(T value)
  {
    if (value == nullptr) {
      append("(null)");
      return;
    }

    append("0x");
    appendHex(reinterpret_cast<uintptr_t>(value));
  }

  void append(const DmtcpUniqueProcessId& value)
  {
    appendHex(value._hostid);
    append("-");
    appendInteger(value._pid);
    append("-");
    appendHex(value._time);
  }

  const char *c_str() const
  {
    return availableStorage_ == 0 ? "" : data_;
  }

  size_t size() const { return size_; }
  bool truncated() const { return truncated_; }

 private:
  template <std::integral T>
  void appendInteger(T value, int base = 10)
  {
    char tmp[64];
    auto result = std::to_chars(tmp, tmp + sizeof(tmp), value, base);
    if (result.ec != std::errc()) {
      append("<format-error>");
      return;
    }
    append(std::string_view(tmp, result.ptr - tmp));
  }

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
inline FormatDetail<Args...>
formatDetail(std::string_view fmt, const Args&... args)
{
  return {fmt, std::tuple<const Args&...>(args...)};
}

inline void
formatImpl(AssertBuffer& buffer, std::string_view fmt)
{
  size_t literalStart = 0;
  for (size_t i = 0; i < fmt.size(); ++i) {
    if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '{') {
      buffer.append(fmt.substr(literalStart, i - literalStart));
      buffer.append("{");
      ++i;
      literalStart = i + 1;
    } else if (fmt[i] == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
      buffer.append(fmt.substr(literalStart, i - literalStart));
      buffer.append("}");
      ++i;
      literalStart = i + 1;
    } else if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
      buffer.append(fmt.substr(literalStart, i - literalStart));
      buffer.append("[missing-format-arg]");
      ++i;
      literalStart = i + 1;
    }
  }
  buffer.append(fmt.substr(literalStart));
}

inline void
appendUnusedFormatArgs(AssertBuffer& buffer, size_t count)
{
  buffer.append(" [unused-format-args=");
  buffer.append(count);
  buffer.append("]");
}

template <typename First, typename... Rest>
inline void
formatImpl(AssertBuffer& buffer,
           std::string_view fmt,
           const First& first,
           const Rest&... rest)
{
  size_t literalStart = 0;
  for (size_t i = 0; i < fmt.size(); ++i) {
    if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '{') {
      buffer.append(fmt.substr(literalStart, i - literalStart));
      buffer.append("{");
      ++i;
      literalStart = i + 1;
    } else if (fmt[i] == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
      buffer.append(fmt.substr(literalStart, i - literalStart));
      buffer.append("}");
      ++i;
      literalStart = i + 1;
    } else if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
      buffer.append(fmt.substr(literalStart, i - literalStart));
      buffer.append(first);
      formatImpl(buffer, fmt.substr(i + 2), rest...);
      return;
    }
  }
  buffer.append(fmt.substr(literalStart));
  appendUnusedFormatArgs(buffer, sizeof...(Rest) + 1);
}

template <typename... Args>
inline void
formatTo(AssertBuffer& buffer, std::string_view fmt, const Args&... args)
{
  formatImpl(buffer, fmt, args...);
}

inline void
appendErrno(AssertBuffer& buffer, int savedErrno)
{
  buffer.append(": errno=");
  buffer.append(savedErrno);
  buffer.append(" (");
  buffer.append(errnoName(savedErrno));
  buffer.append(")");
}

inline void
appendDiagnosticPrefix(AssertBuffer& buffer,
                       LogLevel level,
                       const char *expr,
                       const char *file,
                       int line)
{
  buffer.append(logLevelName(level));
  buffer.append(" at ");
  buffer.append(file == nullptr ? "<unknown>" : file);
  buffer.append(":");
  buffer.append(line);
  buffer.append(": ");
  buffer.append(expr == nullptr ? "<none>" : expr);
}

template <typename... Args>
inline void
appendDiagnosticMessage(AssertBuffer& buffer,
                        std::string_view fmt,
                        const Args&... args)
{
  if (!fmt.empty()) {
    buffer.append(": ");
    formatTo(buffer, fmt, args...);
  }
}

inline void
finishDiagnostic(AssertBuffer& buffer)
{
  if (!buffer.truncated()) {
    buffer.append("\n");
  }
}

inline void writeAllNoAlloc(int fd, const char *data, size_t length);

template <typename... Args>
inline void
formatDiagnostic(AssertBuffer& buffer,
                 LogLevel level,
                 const char *expr,
                 const char *file,
                 int line,
                 std::string_view fmt,
                 const Args&... args)
{
  appendDiagnosticPrefix(buffer, level, expr, file, line);
  appendDiagnosticMessage(buffer, fmt, args...);
  finishDiagnostic(buffer);
}

template <typename... Args>
inline void
formatDiagnosticWithErrno(AssertBuffer& buffer,
                          LogLevel level,
                          const char *expr,
                          const char *file,
                          int line,
                          int savedErrno,
                          std::string_view fmt,
                          const Args&... args)
{
  appendDiagnosticPrefix(buffer, level, expr, file, line);
  appendDiagnosticMessage(buffer, fmt, args...);
  appendErrno(buffer, savedErrno);
  finishDiagnostic(buffer);
}

inline void
writeAllNoAlloc(int fd, const char *data, size_t length)
{
  while (length > 0) {
    ssize_t written = dmtcp_assert_write(fd, data, length);
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

template <typename... Args>
inline void
logDiagnostic(LogLevel level,
              const char *expr,
              const char *file,
              int line,
              int savedErrno,
              bool includeErrno,
              std::string_view fmt,
              const Args&... args)
{
  char storage[kAssertBufferSize];
  AssertBuffer buffer(storage, sizeof(storage));
  if (includeErrno) {
    formatDiagnosticWithErrno(buffer, level, expr, file, line, savedErrno,
                              fmt, args...);
  } else {
    formatDiagnostic(buffer, level, expr, file, line, fmt, args...);
  }
  writeAllNoAlloc(kDiagnosticFd, buffer.c_str(), buffer.size());
}

} // namespace dmtcp

#ifndef DMTCP_UTIL_ASSERT_NO_MACROS

#ifndef DMTCP_LOG_COMPONENT_DEFAULT
#define DMTCP_LOG_COMPONENT_DEFAULT "core"
#endif

#ifndef DMTCP_LOG_COMPONENT
#define DMTCP_LOG_COMPONENT DMTCP_LOG_COMPONENT_DEFAULT
#endif

#define TRACE(fmt, ...)                                                  \
  do {                                                                   \
    if (::dmtcp::logEnabled(::dmtcp::LogLevel::Trace,                    \
                            DMTCP_LOG_COMPONENT, __FILE__)) {            \
      int dmtcpAssertSavedErrno = errno;                                  \
      ::dmtcp::logDiagnostic(::dmtcp::LogLevel::Trace,                    \
                             nullptr, __FILE__, __LINE__,                 \
                             dmtcpAssertSavedErrno, false, fmt            \
                             __VA_OPT__(,) __VA_ARGS__);                 \
      errno = dmtcpAssertSavedErrno;                                      \
    }                                                                    \
  } while (0)

#define NOTE(fmt, ...)                                                   \
  do {                                                                   \
    if (::dmtcp::logEnabled(::dmtcp::LogLevel::Note,                     \
                            DMTCP_LOG_COMPONENT, __FILE__)) {            \
      int dmtcpAssertSavedErrno = errno;                                  \
      ::dmtcp::logDiagnostic(::dmtcp::LogLevel::Note,                     \
                             nullptr, __FILE__, __LINE__,                 \
                             dmtcpAssertSavedErrno, false, fmt            \
                             __VA_OPT__(,) __VA_ARGS__);                 \
      errno = dmtcpAssertSavedErrno;                                      \
    }                                                                    \
  } while (0)

#define WARN(condition, fmt, ...)                                         \
  do {                                                                   \
    if (!(condition) &&                                                   \
        ::dmtcp::logEnabled(::dmtcp::LogLevel::Warn,                     \
                            DMTCP_LOG_COMPONENT, __FILE__)) {            \
      int dmtcpAssertSavedErrno = errno;                                  \
      ::dmtcp::logDiagnostic(::dmtcp::LogLevel::Warn,                     \
                             #condition, __FILE__, __LINE__,              \
                             dmtcpAssertSavedErrno, false, fmt            \
                             __VA_OPT__(,) __VA_ARGS__);                 \
      errno = dmtcpAssertSavedErrno;                                      \
    }                                                                    \
  } while (0)

#define WARN_ERRNO(condition, fmt, ...)                                   \
  do {                                                                   \
    if (!(condition) &&                                                   \
        ::dmtcp::logEnabled(::dmtcp::LogLevel::Warn,                     \
                            DMTCP_LOG_COMPONENT, __FILE__)) {            \
      int dmtcpAssertSavedErrno = errno;                                  \
      ::dmtcp::logDiagnostic(::dmtcp::LogLevel::Warn,                     \
                             #condition, __FILE__, __LINE__,              \
                             dmtcpAssertSavedErrno, true, fmt             \
                             __VA_OPT__(,) __VA_ARGS__);                 \
      errno = dmtcpAssertSavedErrno;                                      \
    }                                                                    \
  } while (0)

#define ASSERT(condition, fmt, ...)                                       \
  do {                                                                   \
    if (!(condition)) {                                                   \
      int dmtcpAssertSavedErrno = errno;                                  \
      ::dmtcp::logDiagnostic(::dmtcp::LogLevel::Error,                    \
                             #condition, __FILE__, __LINE__,              \
                             dmtcpAssertSavedErrno, false, fmt            \
                             __VA_OPT__(,) __VA_ARGS__);                 \
      _exit(::dmtcp::kAssertFailureExitCode);                             \
    }                                                                    \
  } while (0)

#define ASSERT_ERRNO(condition, fmt, ...)                                 \
  do {                                                                   \
    if (!(condition)) {                                                   \
      int dmtcpAssertSavedErrno = errno;                                  \
      ::dmtcp::logDiagnostic(::dmtcp::LogLevel::Error,                    \
                             #condition, __FILE__, __LINE__,              \
                             dmtcpAssertSavedErrno, true, fmt             \
                             __VA_OPT__(,) __VA_ARGS__);                 \
      errno = dmtcpAssertSavedErrno;                                      \
      _exit(::dmtcp::kAssertFailureExitCode);                             \
    }                                                                    \
  } while (0)

#define ASSERT_FALSE(condition, ...)                                      \
  ASSERT(!(condition), "expected false: {}"                              \
         __VA_OPT__("; {}"), #condition                                  \
         __VA_OPT__(, ::dmtcp::formatDetail(__VA_ARGS__)))

#define ASSERT_TRUE(condition, ...)                                       \
  ASSERT((condition), "expected true: {}"                                \
         __VA_OPT__("; {}"), #condition                                  \
         __VA_OPT__(, ::dmtcp::formatDetail(__VA_ARGS__)))

#define WARN_FALSE(condition, ...)                                        \
  WARN(!(condition), "expected false: {}"                                \
       __VA_OPT__("; {}"), #condition                                    \
       __VA_OPT__(, ::dmtcp::formatDetail(__VA_ARGS__)))

#define WARN_TRUE(condition, ...)                                         \
  WARN((condition), "expected true: {}"                                  \
       __VA_OPT__("; {}"), #condition                                    \
       __VA_OPT__(, ::dmtcp::formatDetail(__VA_ARGS__)))

#define ASSERT_NULL(value, ...)                                           \
  do {                                                                    \
    const auto dmtcpAssertValue = (value);                                 \
    ASSERT(dmtcpAssertValue == nullptr,                                    \
           "expected null: {}, got {}" __VA_OPT__("; {}"),               \
           #value, dmtcpAssertValue                                       \
           __VA_OPT__(, ::dmtcp::formatDetail(__VA_ARGS__)));             \
  } while (0)

#define ASSERT_NOT_NULL(value, ...)                                       \
  do {                                                                    \
    const auto dmtcpAssertValue = (value);                                 \
    ASSERT(dmtcpAssertValue != nullptr,                                    \
           "expected non-null: {}, got {}"                                \
           __VA_OPT__("; {}"),                                            \
           #value, dmtcpAssertValue                                       \
           __VA_OPT__(, ::dmtcp::formatDetail(__VA_ARGS__)));             \
  } while (0)

#define WARN_NULL(value, ...)                                             \
  do {                                                                    \
    const auto dmtcpAssertValue = (value);                                 \
    WARN(dmtcpAssertValue == nullptr,                                      \
         "expected null: {}, got {}" __VA_OPT__("; {}"),                 \
         #value, dmtcpAssertValue                                         \
         __VA_OPT__(, ::dmtcp::formatDetail(__VA_ARGS__)));               \
  } while (0)

#define WARN_NOT_NULL(value, ...)                                         \
  do {                                                                    \
    const auto dmtcpAssertValue = (value);                                 \
    WARN(dmtcpAssertValue != nullptr,                                      \
         "expected non-null: {}, got {}" __VA_OPT__("; {}"),             \
         #value, dmtcpAssertValue                                         \
         __VA_OPT__(, ::dmtcp::formatDetail(__VA_ARGS__)));               \
  } while (0)

#define ASSERT_EQ(expected, actual, ...)                                  \
  do {                                                                    \
    const auto& dmtcpAssertExpected = (expected);                          \
    const auto& dmtcpAssertActual = (actual);                              \
    ASSERT(dmtcpAssertExpected == dmtcpAssertActual,                       \
           "expected {} == {}, got {} and {}"                             \
           __VA_OPT__("; {}"),                                            \
           #expected, #actual, dmtcpAssertExpected, dmtcpAssertActual      \
           __VA_OPT__(, ::dmtcp::formatDetail(__VA_ARGS__)));             \
  } while (0)

#define WARN_EQ(expected, actual, ...)                                    \
  do {                                                                    \
    const auto& dmtcpAssertExpected = (expected);                          \
    const auto& dmtcpAssertActual = (actual);                              \
    WARN(dmtcpAssertExpected == dmtcpAssertActual,                         \
         "expected {} == {}, got {} and {}"                               \
         __VA_OPT__("; {}"),                                              \
         #expected, #actual, dmtcpAssertExpected, dmtcpAssertActual        \
         __VA_OPT__(, ::dmtcp::formatDetail(__VA_ARGS__)));               \
  } while (0)

#define ASSERT_NE(expected, actual, ...)                                  \
  do {                                                                    \
    const auto& dmtcpAssertExpected = (expected);                          \
    const auto& dmtcpAssertActual = (actual);                              \
    ASSERT(dmtcpAssertExpected != dmtcpAssertActual,                       \
           "expected {} != {}, got {} and {}"                             \
           __VA_OPT__("; {}"),                                            \
           #expected, #actual, dmtcpAssertExpected, dmtcpAssertActual      \
           __VA_OPT__(, ::dmtcp::formatDetail(__VA_ARGS__)));             \
  } while (0)

#define WARN_NE(expected, actual, ...)                                    \
  do {                                                                    \
    const auto& dmtcpAssertExpected = (expected);                          \
    const auto& dmtcpAssertActual = (actual);                              \
    WARN(dmtcpAssertExpected != dmtcpAssertActual,                         \
         "expected {} != {}, got {} and {}"                               \
         __VA_OPT__("; {}"),                                              \
         #expected, #actual, dmtcpAssertExpected, dmtcpAssertActual        \
         __VA_OPT__(, ::dmtcp::formatDetail(__VA_ARGS__)));               \
  } while (0)

#define ASSERT_GT(lhs, rhs, ...)                                          \
  do {                                                                    \
    const auto& dmtcpAssertLhs = (lhs);                                    \
    const auto& dmtcpAssertRhs = (rhs);                                    \
    ASSERT(dmtcpAssertLhs > dmtcpAssertRhs,                                \
           "expected {} > {}, got {} and {}"                              \
           __VA_OPT__("; {}"),                                            \
           #lhs, #rhs, dmtcpAssertLhs, dmtcpAssertRhs                     \
           __VA_OPT__(, ::dmtcp::formatDetail(__VA_ARGS__)));             \
  } while (0)

#define WARN_GT(lhs, rhs, ...)                                            \
  do {                                                                    \
    const auto& dmtcpAssertLhs = (lhs);                                    \
    const auto& dmtcpAssertRhs = (rhs);                                    \
    WARN(dmtcpAssertLhs > dmtcpAssertRhs,                                  \
         "expected {} > {}, got {} and {}"                                \
         __VA_OPT__("; {}"),                                              \
         #lhs, #rhs, dmtcpAssertLhs, dmtcpAssertRhs                       \
         __VA_OPT__(, ::dmtcp::formatDetail(__VA_ARGS__)));               \
  } while (0)

#define ASSERT_LT(lhs, rhs, ...)                                          \
  do {                                                                    \
    const auto& dmtcpAssertLhs = (lhs);                                    \
    const auto& dmtcpAssertRhs = (rhs);                                    \
    ASSERT(dmtcpAssertLhs < dmtcpAssertRhs,                                \
           "expected {} < {}, got {} and {}"                              \
           __VA_OPT__("; {}"),                                            \
           #lhs, #rhs, dmtcpAssertLhs, dmtcpAssertRhs                     \
           __VA_OPT__(, ::dmtcp::formatDetail(__VA_ARGS__)));             \
  } while (0)

#define WARN_LT(lhs, rhs, ...)                                            \
  do {                                                                    \
    const auto& dmtcpAssertLhs = (lhs);                                    \
    const auto& dmtcpAssertRhs = (rhs);                                    \
    WARN(dmtcpAssertLhs < dmtcpAssertRhs,                                  \
         "expected {} < {}, got {} and {}"                                \
         __VA_OPT__("; {}"),                                              \
         #lhs, #rhs, dmtcpAssertLhs, dmtcpAssertRhs                       \
         __VA_OPT__(, ::dmtcp::formatDetail(__VA_ARGS__)));               \
  } while (0)

#define ASSERT_GE(lhs, rhs, ...)                                          \
  do {                                                                    \
    const auto& dmtcpAssertLhs = (lhs);                                    \
    const auto& dmtcpAssertRhs = (rhs);                                    \
    ASSERT(dmtcpAssertLhs >= dmtcpAssertRhs,                               \
           "expected {} >= {}, got {} and {}"                             \
           __VA_OPT__("; {}"),                                            \
           #lhs, #rhs, dmtcpAssertLhs, dmtcpAssertRhs                     \
           __VA_OPT__(, ::dmtcp::formatDetail(__VA_ARGS__)));             \
  } while (0)

#define WARN_GE(lhs, rhs, ...)                                            \
  do {                                                                    \
    const auto& dmtcpAssertLhs = (lhs);                                    \
    const auto& dmtcpAssertRhs = (rhs);                                    \
    WARN(dmtcpAssertLhs >= dmtcpAssertRhs,                                 \
         "expected {} >= {}, got {} and {}"                               \
         __VA_OPT__("; {}"),                                              \
         #lhs, #rhs, dmtcpAssertLhs, dmtcpAssertRhs                       \
         __VA_OPT__(, ::dmtcp::formatDetail(__VA_ARGS__)));               \
  } while (0)

#define ASSERT_LE(lhs, rhs, ...)                                          \
  do {                                                                    \
    const auto& dmtcpAssertLhs = (lhs);                                    \
    const auto& dmtcpAssertRhs = (rhs);                                    \
    ASSERT(dmtcpAssertLhs <= dmtcpAssertRhs,                               \
           "expected {} <= {}, got {} and {}"                             \
           __VA_OPT__("; {}"),                                            \
           #lhs, #rhs, dmtcpAssertLhs, dmtcpAssertRhs                     \
           __VA_OPT__(, ::dmtcp::formatDetail(__VA_ARGS__)));             \
  } while (0)

#define WARN_LE(lhs, rhs, ...)                                            \
  do {                                                                    \
    const auto& dmtcpAssertLhs = (lhs);                                    \
    const auto& dmtcpAssertRhs = (rhs);                                    \
    WARN(dmtcpAssertLhs <= dmtcpAssertRhs,                                 \
         "expected {} <= {}, got {} and {}"                               \
         __VA_OPT__("; {}"),                                              \
         #lhs, #rhs, dmtcpAssertLhs, dmtcpAssertRhs                       \
         __VA_OPT__(, ::dmtcp::formatDetail(__VA_ARGS__)));               \
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
