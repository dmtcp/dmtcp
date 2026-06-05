#ifndef DMTCP_UTIL_ASSERT_H
#define DMTCP_UTIL_ASSERT_H

#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unistd.h>

#include "assert_buffer.h"

extern "C" ssize_t dmtcp_assert_write(int fd, const void *buf, size_t count);

namespace dmtcp {

inline constexpr int kAssertFailureExitCode = 99;
inline constexpr int kDiagnosticFd = STDERR_FILENO;

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
 *   continues.  ASSERT_ERRNO/WARN_ERRNO add the errno value captured at the
 *   failing check.
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

enum class AssertSeverity {
  Warning,
  Error,
};

class AssertBuffer {
 public:
  AssertBuffer(char *storage, size_t capacity)
    : data_(storage),
      capacity_(capacity),
      size_(0),
      truncated_(false)
  {
    if (capacity_ > 0) {
      data_[0] = '\0';
    }
  }

  void reset()
  {
    size_ = 0;
    truncated_ = false;
    if (capacity_ > 0) {
      data_[0] = '\0';
    }
  }

  void append(std::string_view text)
  {
    for (char ch : text) {
      append(ch);
    }
  }

  void append(char ch)
  {
    if (capacity_ == 0) {
      truncated_ = true;
      return;
    }

    if (size_ + 1 >= capacity_) {
      data_[capacity_ - 1] = '\0';
      truncated_ = true;
      return;
    }

    data_[size_++] = ch;
    data_[size_] = '\0';
  }

  const char *c_str() const
  {
    return capacity_ == 0 ? "" : data_;
  }

  size_t size() const { return size_; }
  size_t capacity() const { return capacity_; }
  bool truncated() const { return truncated_; }

  void finishTruncated(std::string_view marker)
  {
    if (capacity_ == 0) {
      truncated_ = true;
      return;
    }

    if (marker.size() >= capacity_) {
      marker.remove_prefix(marker.size() - (capacity_ - 1));
    }

    const size_t offset = capacity_ - 1 - marker.size();
    for (size_t i = 0; i < marker.size(); ++i) {
      data_[offset + i] = marker[i];
    }
    size_ = offset + marker.size();
    data_[size_] = '\0';
    truncated_ = true;
  }

 private:
  char *data_;
  size_t capacity_;
  size_t size_;
  bool truncated_;
};

inline const char *
severityName(AssertSeverity severity)
{
  switch (severity) {
  case AssertSeverity::Warning:
    return "WARNING";
  case AssertSeverity::Error:
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

template <std::integral T>
inline void
appendInteger(AssertBuffer& buffer, T value, int base = 10)
{
  char tmp[64];
  auto result = std::to_chars(tmp, tmp + sizeof(tmp), value, base);
  if (result.ec != std::errc()) {
    buffer.append("<format-error>");
    return;
  }
  buffer.append(std::string_view(tmp, result.ptr - tmp));
}

inline void appendValue(AssertBuffer& buffer, std::string_view value)
{
  buffer.append(value);
}

inline void appendValue(AssertBuffer& buffer, const char *value)
{
  buffer.append(value == nullptr ? "(null)" : value);
}

inline void appendValue(AssertBuffer& buffer, char *value)
{
  appendValue(buffer, static_cast<const char *>(value));
}

inline void appendValue(AssertBuffer& buffer, bool value)
{
  buffer.append(value ? "true" : "false");
}

inline void appendValue(AssertBuffer& buffer, char value)
{
  buffer.append(value);
}

inline void appendValue(AssertBuffer& buffer, std::nullptr_t)
{
  buffer.append("(null)");
}

template <std::integral T>
  requires (!std::same_as<std::remove_cv_t<T>, bool> &&
            !std::same_as<std::remove_cv_t<T>, char>)
inline void
appendValue(AssertBuffer& buffer, T value)
{
  appendInteger(buffer, value);
}

template <typename T>
  requires std::is_enum_v<std::remove_cvref_t<T>>
inline void
appendValue(AssertBuffer& buffer, T value)
{
  using Underlying = std::underlying_type_t<std::remove_cvref_t<T>>;
  appendValue(buffer, static_cast<Underlying>(value));
}

template <typename T>
  requires (std::is_pointer_v<T> &&
            !std::is_convertible_v<T, const char *>)
inline void
appendValue(AssertBuffer& buffer, T value)
{
  if (value == nullptr) {
    buffer.append("(null)");
    return;
  }

  buffer.append("0x");
  appendInteger(buffer, reinterpret_cast<uintptr_t>(value), 16);
}

inline void
formatImpl(AssertBuffer& buffer, std::string_view fmt)
{
  for (size_t i = 0; i < fmt.size(); ++i) {
    if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '{') {
      buffer.append('{');
      ++i;
    } else if (fmt[i] == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
      buffer.append('}');
      ++i;
    } else if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
      buffer.append("[missing-format-arg]");
      ++i;
    } else {
      buffer.append(fmt[i]);
    }
  }
}

inline void
appendUnusedFormatArgs(AssertBuffer& buffer, size_t count)
{
  buffer.append(" [unused-format-args=");
  appendInteger(buffer, count);
  buffer.append(']');
}

template <typename First, typename... Rest>
inline void
formatImpl(AssertBuffer& buffer,
           std::string_view fmt,
           const First& first,
           const Rest&... rest)
{
  for (size_t i = 0; i < fmt.size(); ++i) {
    if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '{') {
      buffer.append('{');
      ++i;
    } else if (fmt[i] == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
      buffer.append('}');
      ++i;
    } else if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
      appendValue(buffer, first);
      formatImpl(buffer, fmt.substr(i + 2), rest...);
      return;
    } else {
      buffer.append(fmt[i]);
    }
  }
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
  appendInteger(buffer, savedErrno);
  buffer.append(" (");
  buffer.append(errnoName(savedErrno));
  buffer.append(')');
}

inline void
appendDiagnosticPrefix(AssertBuffer& buffer,
                       AssertSeverity severity,
                       const char *expr,
                       const char *file,
                       int line)
{
  buffer.reset();
  buffer.append(severityName(severity));
  buffer.append(" at ");
  buffer.append(file == nullptr ? "<unknown>" : file);
  buffer.append(':');
  appendInteger(buffer, line);
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
  constexpr std::string_view marker = " [truncated]\n";
  if (buffer.truncated()) {
    buffer.finishTruncated(marker);
    return;
  }
  buffer.append('\n');
  if (buffer.truncated()) {
    buffer.finishTruncated(marker);
    return;
  }
}

template <typename... Args>
inline void
formatDiagnostic(AssertBuffer& buffer,
                 AssertSeverity severity,
                 const char *expr,
                 const char *file,
                 int line,
                 std::string_view fmt,
                 const Args&... args)
{
  appendDiagnosticPrefix(buffer, severity, expr, file, line);
  appendDiagnosticMessage(buffer, fmt, args...);
  finishDiagnostic(buffer);
}

template <typename... Args>
inline void
formatDiagnosticWithErrno(AssertBuffer& buffer,
                          AssertSeverity severity,
                          const char *expr,
                          const char *file,
                          int line,
                          int savedErrno,
                          std::string_view fmt,
                          const Args&... args)
{
  appendDiagnosticPrefix(buffer, severity, expr, file, line);
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
logDiagnostic(AssertSeverity severity,
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
    formatDiagnosticWithErrno(buffer, severity, expr, file, line, savedErrno,
                              fmt, args...);
  } else {
    formatDiagnostic(buffer, severity, expr, file, line, fmt, args...);
  }
  writeAllNoAlloc(kDiagnosticFd, buffer.c_str(), buffer.size());
}

} // namespace dmtcp

#ifndef DMTCP_UTIL_ASSERT_NO_MACROS

#undef ASSERT
#undef ASSERT_ERRNO
#undef ASSERT_EQ
#undef ASSERT_EQ_MSG
#undef ASSERT_FALSE
#undef ASSERT_FORK_SUCCESS
#undef ASSERT_FORK_SUCCESS_MSG
#undef ASSERT_GE
#undef ASSERT_GE_MSG
#undef ASSERT_GT
#undef ASSERT_GT_MSG
#undef ASSERT_LE
#undef ASSERT_LE_MSG
#undef ASSERT_LOCK_SUCCESS
#undef ASSERT_LOCK_SUCCESS_MSG
#undef ASSERT_LT
#undef ASSERT_LT_MSG
#undef ASSERT_NE
#undef ASSERT_NE_MSG
#undef ASSERT_NOT_NULL
#undef ASSERT_NOT_NULL_MSG
#undef ASSERT_NULL
#undef ASSERT_NULL_MSG
#undef ASSERT_PTHREAD_SUCCESS
#undef ASSERT_PTHREAD_SUCCESS_MSG
#undef ASSERT_RWLOCK_SUCCESS
#undef ASSERT_RWLOCK_SUCCESS_MSG
#undef ASSERT_MUTEX_SUCCESS
#undef ASSERT_MUTEX_SUCCESS_MSG
#undef ASSERT_SYSCALL_EQ
#undef ASSERT_SYSCALL_EQ_MSG
#undef ASSERT_SYSCALL_SUCCESS
#undef ASSERT_SYSCALL_SUCCESS_MSG
#undef ASSERT_TRUE
#undef ASSERT_VALID_FD
#undef ASSERT_VALID_FD_MSG
#undef ASSERT_ZERO
#undef ASSERT_ZERO_MSG
#undef ASSERT_ZERO_RETURN
#undef ASSERT_ZERO_RETURN_MSG
#undef WARN
#undef WARN_ERRNO
#undef WARN_EQ
#undef WARN_FALSE
#undef WARN_FORK_SUCCESS
#undef WARN_GE
#undef WARN_GT
#undef WARN_LE
#undef WARN_LOCK_SUCCESS
#undef WARN_LT
#undef WARN_NE
#undef WARN_NOT_NULL
#undef WARN_NULL
#undef WARN_PTHREAD_SUCCESS
#undef WARN_SYSCALL_EQ
#undef WARN_SYSCALL_SUCCESS
#undef WARN_TRUE
#undef WARN_VALID_FD
#undef WARN_ZERO
#undef WARNING
#undef WARNING_ERRNO
#undef WARNING_EQ
#undef WARNING_EQ_MSG
#undef WARNING_FALSE
#undef WARNING_FORK_SUCCESS
#undef WARNING_FORK_SUCCESS_MSG
#undef WARNING_GE
#undef WARNING_GE_MSG
#undef WARNING_GT
#undef WARNING_GT_MSG
#undef WARNING_LE
#undef WARNING_LE_MSG
#undef WARNING_LOCK_SUCCESS
#undef WARNING_LOCK_SUCCESS_MSG
#undef WARNING_LT
#undef WARNING_LT_MSG
#undef WARNING_NE
#undef WARNING_NE_MSG
#undef WARNING_NOT_NULL
#undef WARNING_NOT_NULL_MSG
#undef WARNING_NULL
#undef WARNING_NULL_MSG
#undef WARNING_PTHREAD_SUCCESS
#undef WARNING_PTHREAD_SUCCESS_MSG
#undef WARNING_RWLOCK_SUCCESS
#undef WARNING_RWLOCK_SUCCESS_MSG
#undef WARNING_MUTEX_SUCCESS
#undef WARNING_MUTEX_SUCCESS_MSG
#undef WARNING_SYSCALL_EQ
#undef WARNING_SYSCALL_EQ_MSG
#undef WARNING_SYSCALL_SUCCESS
#undef WARNING_SYSCALL_SUCCESS_MSG
#undef WARNING_TRUE
#undef WARNING_VALID_FD
#undef WARNING_VALID_FD_MSG
#undef WARNING_ZERO
#undef WARNING_ZERO_MSG
#undef WARNING_ZERO_RETURN
#undef WARNING_ZERO_RETURN_MSG

#define WARN(condition, fmt, ...)                                         \
  do {                                                                   \
    if (!(condition)) {                                                   \
      int dmtcpAssertSavedErrno = errno;                                  \
      ::dmtcp::logDiagnostic(::dmtcp::AssertSeverity::Warning,            \
                             #condition, __FILE__, __LINE__,              \
                             dmtcpAssertSavedErrno, false, fmt            \
                             __VA_OPT__(,) __VA_ARGS__);                 \
      errno = dmtcpAssertSavedErrno;                                      \
    }                                                                    \
  } while (0)

#define WARN_ERRNO(condition, fmt, ...)                                   \
  do {                                                                   \
    if (!(condition)) {                                                   \
      int dmtcpAssertSavedErrno = errno;                                  \
      ::dmtcp::logDiagnostic(::dmtcp::AssertSeverity::Warning,            \
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
      ::dmtcp::logDiagnostic(::dmtcp::AssertSeverity::Error,              \
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
      ::dmtcp::logDiagnostic(::dmtcp::AssertSeverity::Error,              \
                             #condition, __FILE__, __LINE__,              \
                             dmtcpAssertSavedErrno, true, fmt             \
                             __VA_OPT__(,) __VA_ARGS__);                 \
      errno = dmtcpAssertSavedErrno;                                      \
      _exit(::dmtcp::kAssertFailureExitCode);                             \
    }                                                                    \
  } while (0)

#define DMTCP_ASSERT_BOOL(assertion, condition, expected, test, fmt, ...) \
  assertion((test), "expected " expected ": {}" fmt, #condition           \
            __VA_OPT__(,) __VA_ARGS__)

#define ASSERT_FALSE(condition, ...) \
  DMTCP_ASSERT_BOOL(ASSERT, condition, "false", !(condition), ""          \
                    __VA_OPT__("; " __VA_ARGS__))

#define ASSERT_TRUE(condition, ...) \
  DMTCP_ASSERT_BOOL(ASSERT, condition, "true", (condition), ""            \
                    __VA_OPT__("; " __VA_ARGS__))

#define WARN_FALSE(condition, ...) \
  DMTCP_ASSERT_BOOL(WARN, condition, "false", !(condition), ""            \
                    __VA_OPT__("; " __VA_ARGS__))

#define WARN_TRUE(condition, ...) \
  DMTCP_ASSERT_BOOL(WARN, condition, "true", (condition), ""              \
                    __VA_OPT__("; " __VA_ARGS__))

#define DMTCP_ASSERT_NULL(assertion, value, fmt, ...)                      \
  do {                                                                     \
    const auto dmtcpAssertValue = (value);                                  \
    assertion(dmtcpAssertValue == nullptr,                                 \
              "expected null: {}, got {}" fmt,                             \
              #value, dmtcpAssertValue                                     \
              __VA_OPT__(,) __VA_ARGS__);                                  \
  } while (0)

#define DMTCP_ASSERT_NOT_NULL(assertion, value, fmt, ...)                  \
  do {                                                                     \
    const auto dmtcpAssertValue = (value);                                  \
    assertion(dmtcpAssertValue != nullptr,                                 \
              "expected non-null: {}, got {}" fmt,                         \
              #value, dmtcpAssertValue                                     \
              __VA_OPT__(,) __VA_ARGS__);                                  \
  } while (0)

#define ASSERT_NULL(value, ...) \
  DMTCP_ASSERT_NULL(ASSERT, value, "" __VA_OPT__("; " __VA_ARGS__))

#define ASSERT_NOT_NULL(value, ...) \
  DMTCP_ASSERT_NOT_NULL(ASSERT, value, "" __VA_OPT__("; " __VA_ARGS__))

#define WARN_NULL(value, ...) \
  DMTCP_ASSERT_NULL(WARN, value, "" __VA_OPT__("; " __VA_ARGS__))

#define WARN_NOT_NULL(value, ...) \
  DMTCP_ASSERT_NOT_NULL(WARN, value, "" __VA_OPT__("; " __VA_ARGS__))

#define DMTCP_ASSERT_COMPARE(assertion, lhs, rhs, op, opText, fmt, ...)   \
  do {                                                                   \
    const auto& dmtcpAssertLhs = (lhs);                                   \
    const auto& dmtcpAssertRhs = (rhs);                                   \
    assertion(dmtcpAssertLhs op dmtcpAssertRhs,                           \
              "expected {} " opText " {}, got {} and {}" fmt,            \
              #lhs, #rhs, dmtcpAssertLhs, dmtcpAssertRhs                  \
              __VA_OPT__(,) __VA_ARGS__);                                 \
  } while (0)

#define ASSERT_EQ(expected, actual, ...) \
  DMTCP_ASSERT_COMPARE(ASSERT, expected, actual, ==, "==", ""             \
                       __VA_OPT__("; " __VA_ARGS__))

#define WARN_EQ(expected, actual, ...) \
  DMTCP_ASSERT_COMPARE(WARN, expected, actual, ==, "==", ""               \
                       __VA_OPT__("; " __VA_ARGS__))

#define ASSERT_NE(expected, actual, ...) \
  DMTCP_ASSERT_COMPARE(ASSERT, expected, actual, !=, "!=", ""             \
                       __VA_OPT__("; " __VA_ARGS__))

#define WARN_NE(expected, actual, ...) \
  DMTCP_ASSERT_COMPARE(WARN, expected, actual, !=, "!=", ""               \
                       __VA_OPT__("; " __VA_ARGS__))

#define ASSERT_GT(lhs, rhs, ...) \
  DMTCP_ASSERT_COMPARE(ASSERT, lhs, rhs, >, ">", ""                       \
                       __VA_OPT__("; " __VA_ARGS__))

#define WARN_GT(lhs, rhs, ...) \
  DMTCP_ASSERT_COMPARE(WARN, lhs, rhs, >, ">", ""                         \
                       __VA_OPT__("; " __VA_ARGS__))

#define ASSERT_LT(lhs, rhs, ...) \
  DMTCP_ASSERT_COMPARE(ASSERT, lhs, rhs, <, "<", ""                       \
                       __VA_OPT__("; " __VA_ARGS__))

#define WARN_LT(lhs, rhs, ...) \
  DMTCP_ASSERT_COMPARE(WARN, lhs, rhs, <, "<", ""                         \
                       __VA_OPT__("; " __VA_ARGS__))

#define ASSERT_GE(lhs, rhs, ...) \
  DMTCP_ASSERT_COMPARE(ASSERT, lhs, rhs, >=, ">=", ""                     \
                       __VA_OPT__("; " __VA_ARGS__))

#define WARN_GE(lhs, rhs, ...) \
  DMTCP_ASSERT_COMPARE(WARN, lhs, rhs, >=, ">=", ""                       \
                       __VA_OPT__("; " __VA_ARGS__))

#define ASSERT_LE(lhs, rhs, ...) \
  DMTCP_ASSERT_COMPARE(ASSERT, lhs, rhs, <=, "<=", ""                     \
                       __VA_OPT__("; " __VA_ARGS__))

#define WARN_LE(lhs, rhs, ...) \
  DMTCP_ASSERT_COMPARE(WARN, lhs, rhs, <=, "<=", ""                       \
                       __VA_OPT__("; " __VA_ARGS__))

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

// libc/syscall-style APIs return -1 on failure and set errno.  Keep these
// aliases errno-aware even though they reuse the generic comparison formatter.
#define ASSERT_SYSCALL_SUCCESS(expression, ...) \
  DMTCP_ASSERT_COMPARE(ASSERT_ERRNO, -1, expression, !=, "!=", ""          \
                       __VA_OPT__("; " __VA_ARGS__))

#define WARN_SYSCALL_SUCCESS(expression, ...) \
  DMTCP_ASSERT_COMPARE(WARN_ERRNO, -1, expression, !=, "!=", ""           \
                       __VA_OPT__("; " __VA_ARGS__))

#define ASSERT_SYSCALL_EQ(expected, expression, ...) \
  DMTCP_ASSERT_COMPARE(ASSERT_ERRNO, expected, expression, ==, "==", ""   \
                       __VA_OPT__("; " __VA_ARGS__))

#define WARN_SYSCALL_EQ(expected, expression, ...) \
  DMTCP_ASSERT_COMPARE(WARN_ERRNO, expected, expression, ==, "==", ""     \
                       __VA_OPT__("; " __VA_ARGS__))

#define ASSERT_VALID_FD(expression, ...) \
  DMTCP_ASSERT_COMPARE(ASSERT_ERRNO, expression, 0, >=, ">=", ""          \
                       __VA_OPT__("; " __VA_ARGS__))

#define WARN_VALID_FD(expression, ...) \
  DMTCP_ASSERT_COMPARE(WARN_ERRNO, expression, 0, >=, ">=", ""           \
                       __VA_OPT__("; " __VA_ARGS__))

#define ASSERT_FORK_SUCCESS(expression, ...) \
  DMTCP_ASSERT_COMPARE(ASSERT_ERRNO, expression, 0, >=, ">=", ""          \
                       __VA_OPT__("; " __VA_ARGS__))

#define WARN_FORK_SUCCESS(expression, ...) \
  DMTCP_ASSERT_COMPARE(WARN_ERRNO, expression, 0, >=, ">=", ""           \
                       __VA_OPT__("; " __VA_ARGS__))

#define ASSERT_EQ_MSG(expected, actual, ...) \
  ASSERT_EQ(expected, actual __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_NE_MSG(expected, actual, ...) \
  ASSERT_NE(expected, actual __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_GT_MSG(lhs, rhs, ...) \
  ASSERT_GT(lhs, rhs __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_GE_MSG(lhs, rhs, ...) \
  ASSERT_GE(lhs, rhs __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_LT_MSG(lhs, rhs, ...) \
  ASSERT_LT(lhs, rhs __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_LE_MSG(lhs, rhs, ...) \
  ASSERT_LE(lhs, rhs __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_NULL_MSG(value, ...) \
  ASSERT_NULL(value __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_NOT_NULL_MSG(value, ...) \
  ASSERT_NOT_NULL(value __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_LOCK_SUCCESS_MSG(expression, ...) \
  ASSERT_LOCK_SUCCESS(expression __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_MUTEX_SUCCESS(expression, ...) \
  ASSERT_LOCK_SUCCESS(expression __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_MUTEX_SUCCESS_MSG(expression, ...) \
  ASSERT_LOCK_SUCCESS(expression __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_RWLOCK_SUCCESS(expression, ...) \
  ASSERT_LOCK_SUCCESS(expression __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_RWLOCK_SUCCESS_MSG(expression, ...) \
  ASSERT_LOCK_SUCCESS(expression __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_PTHREAD_SUCCESS_MSG(expression, ...) \
  ASSERT_PTHREAD_SUCCESS(expression __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_ZERO_MSG(expression, ...) \
  ASSERT_ZERO(expression __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_ZERO_RETURN(expression, ...) \
  ASSERT_ZERO(expression __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_ZERO_RETURN_MSG(expression, ...) \
  ASSERT_ZERO(expression __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_SYSCALL_SUCCESS_MSG(expression, ...) \
  ASSERT_SYSCALL_SUCCESS(expression __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_SYSCALL_EQ_MSG(expected, expression, ...) \
  ASSERT_SYSCALL_EQ(expected, expression __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_VALID_FD_MSG(expression, ...) \
  ASSERT_VALID_FD(expression __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_FORK_SUCCESS_MSG(expression, ...) \
  ASSERT_FORK_SUCCESS(expression __VA_OPT__(,) __VA_ARGS__)

#define WARNING(condition, ...) \
  WARN(condition __VA_OPT__(,) __VA_ARGS__)
#define WARNING_ERRNO(condition, ...) \
  WARN_ERRNO(condition __VA_OPT__(,) __VA_ARGS__)
#define WARNING_EQ(expected, actual, ...) \
  WARN_EQ(expected, actual __VA_OPT__(,) __VA_ARGS__)
#define WARNING_EQ_MSG(expected, actual, ...) \
  WARN_EQ(expected, actual __VA_OPT__(,) __VA_ARGS__)
#define WARNING_NE(expected, actual, ...) \
  WARN_NE(expected, actual __VA_OPT__(,) __VA_ARGS__)
#define WARNING_NE_MSG(expected, actual, ...) \
  WARN_NE(expected, actual __VA_OPT__(,) __VA_ARGS__)
#define WARNING_SYSCALL_SUCCESS(expression, ...) \
  WARN_SYSCALL_SUCCESS(expression __VA_OPT__(,) __VA_ARGS__)
#define WARNING_SYSCALL_SUCCESS_MSG(expression, ...) \
  WARN_SYSCALL_SUCCESS(expression __VA_OPT__(,) __VA_ARGS__)
#define WARNING_VALID_FD(expression, ...) \
  WARN_VALID_FD(expression __VA_OPT__(,) __VA_ARGS__)
#define WARNING_VALID_FD_MSG(expression, ...) \
  WARN_VALID_FD(expression __VA_OPT__(,) __VA_ARGS__)

#endif // DMTCP_UTIL_ASSERT_NO_MACROS

#endif // DMTCP_UTIL_ASSERT_H
