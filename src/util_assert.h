#ifndef DMTCP_UTIL_ASSERT_H
#define DMTCP_UTIL_ASSERT_H

#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <string_view>
#include <sys/syscall.h>
#include <system_error>
#include <type_traits>
#include <unistd.h>

#include "assert_buffer.h"

extern "C" char *dmtcp_get_thread_assert_buffer(size_t *size)
  __attribute__((weak));
extern "C" ssize_t dmtcp_assert_write(int fd, const void *buf, size_t count)
  __attribute__((weak));

namespace dmtcp {

inline constexpr int kAssertFailureExitCode = 99;

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

struct FormatSpec {
  int base = 10;
  size_t width = 0;
  char pad = ' ';
  bool alternate = false;
};

inline bool
parseFormatSpec(std::string_view text, FormatSpec *spec)
{
  *spec = FormatSpec();
  if (text.empty()) {
    return true;
  }
  if (text[0] != ':') {
    return false;
  }

  size_t i = 1;
  if (i < text.size() && text[i] == '#') {
    spec->alternate = true;
    ++i;
  }
  if (i < text.size() && text[i] == '0') {
    spec->pad = '0';
    ++i;
  }
  while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
    spec->width = spec->width * 10 + static_cast<size_t>(text[i] - '0');
    ++i;
  }
  if (i == text.size()) {
    return !spec->alternate;
  }
  if (text[i] == 'd') {
    ++i;
    return i == text.size() && !spec->alternate;
  }
  if (text[i] == 'x') {
    spec->base = 16;
    ++i;
  } else {
    return false;
  }
  return i == text.size();
}

template <std::unsigned_integral T>
inline void
appendUnsignedInteger(AssertBuffer& buffer, T value, const FormatSpec& spec)
{
  char tmp[64];
  auto result = std::to_chars(tmp, tmp + sizeof(tmp), value, spec.base);
  if (result.ec != std::errc()) {
    buffer.append("<format-error>");
    return;
  }

  std::string_view digits(tmp, result.ptr - tmp);
  const bool prefixHex = spec.alternate && spec.base == 16;
  const size_t prefixLen = prefixHex ? 2 : 0;
  const size_t totalLen = prefixLen + digits.size();
  const size_t padding = spec.width > totalLen ? spec.width - totalLen : 0;

  if (spec.pad == ' ') {
    for (size_t i = 0; i < padding; ++i) {
      buffer.append(' ');
    }
  }
  if (prefixHex) {
    buffer.append("0x");
  }
  if (spec.pad == '0') {
    for (size_t i = 0; i < padding; ++i) {
      buffer.append('0');
    }
  }
  buffer.append(digits);
}

template <std::integral T>
  requires (!std::same_as<std::remove_cv_t<T>, bool> &&
            !std::same_as<std::remove_cv_t<T>, char>)
inline void
appendInteger(AssertBuffer& buffer, T value, const FormatSpec& spec)
{
  if constexpr (std::is_signed_v<std::remove_cv_t<T>>) {
    if (value < 0 && spec.base == 10) {
      buffer.append('-');
      using Unsigned = std::make_unsigned_t<std::remove_cv_t<T>>;
      Unsigned magnitude = static_cast<Unsigned>(-(value + 1)) + 1;
      appendUnsignedInteger(buffer, magnitude, spec);
      return;
    }
  }

  using Unsigned = std::make_unsigned_t<std::remove_cv_t<T>>;
  appendUnsignedInteger(buffer, static_cast<Unsigned>(value), spec);
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
  requires (!std::integral<std::remove_cvref_t<T>> ||
            std::same_as<std::remove_cvref_t<T>, bool> ||
            std::same_as<std::remove_cvref_t<T>, char>) &&
            (!std::is_enum_v<std::remove_cvref_t<T>>)
inline void
appendFormattedValue(AssertBuffer& buffer, const T& value,
                     const FormatSpec&)
{
  appendValue(buffer, value);
}

template <typename T>
  requires std::is_enum_v<std::remove_cvref_t<T>>
inline void
appendFormattedValue(AssertBuffer& buffer, T value, const FormatSpec& spec)
{
  using Underlying = std::underlying_type_t<std::remove_cvref_t<T>>;
  appendFormattedValue(buffer, static_cast<Underlying>(value), spec);
}

template <std::integral T>
  requires (!std::same_as<std::remove_cv_t<T>, bool> &&
            !std::same_as<std::remove_cv_t<T>, char>)
inline void
appendFormattedValue(AssertBuffer& buffer, T value, const FormatSpec& spec)
{
  appendInteger(buffer, value, spec);
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
    } else if (fmt[i] == '{') {
      size_t close = fmt.find('}', i + 1);
      if (close != std::string_view::npos) {
        FormatSpec spec;
        std::string_view placeholder = fmt.substr(i + 1, close - i - 1);
        if (parseFormatSpec(placeholder, &spec)) {
          buffer.append("[missing-format-arg]");
          i = close;
          continue;
        }
      }
      buffer.append(fmt[i]);
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
    } else if (fmt[i] == '{') {
      size_t close = fmt.find('}', i + 1);
      if (close != std::string_view::npos) {
        FormatSpec spec;
        std::string_view placeholder = fmt.substr(i + 1, close - i - 1);
        if (parseFormatSpec(placeholder, &spec)) {
          appendFormattedValue(buffer, first, spec);
          formatImpl(buffer, fmt.substr(close + 1), rest...);
          return;
        }
      }
      buffer.append(fmt[i]);
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
    ssize_t written =
      dmtcp_assert_write != nullptr
        ? dmtcp_assert_write(fd, data, length)
        : syscall(SYS_write, fd, data, length);
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

inline void
signalAppendLiteral(char **cursor, size_t *remaining, const char *text)
{
  if (text == nullptr) {
    text = "(null)";
  }

  while (*text != '\0' && *remaining > 1) {
    **cursor = *text;
    ++(*cursor);
    --(*remaining);
    ++text;
  }
}

inline void
signalAppendInteger(char **cursor, size_t *remaining, long value)
{
  char digits[32];
  size_t count = 0;
  unsigned long magnitude;

  if (value < 0) {
    signalAppendLiteral(cursor, remaining, "-");
    magnitude = static_cast<unsigned long>(-(value + 1)) + 1;
  } else {
    magnitude = static_cast<unsigned long>(value);
  }

  do {
    digits[count++] = static_cast<char>('0' + (magnitude % 10));
    magnitude /= 10;
  } while (magnitude != 0 && count < sizeof(digits));

  while (count > 0 && *remaining > 1) {
    --count;
    **cursor = digits[count];
    ++(*cursor);
    --(*remaining);
  }
}

inline void
signalWriteAll(const char *data, size_t length)
{
  while (length > 0) {
    ssize_t written = syscall(SYS_write, STDERR_FILENO, data, length);
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

inline void
signalDiagnostic(AssertSeverity severity,
                 const char *expr,
                 const char *file,
                 int line,
                 const char *message,
                 int savedErrno,
                 bool includeErrno,
                 long result,
                 bool includeResult)
{
  char storage[512];
  char *cursor = storage;
  size_t remaining = sizeof(storage);

  signalAppendLiteral(&cursor, &remaining, severityName(severity));
  signalAppendLiteral(&cursor, &remaining, " signal-context at ");
  signalAppendLiteral(&cursor, &remaining, file);
  signalAppendLiteral(&cursor, &remaining, ":");
  signalAppendInteger(&cursor, &remaining, line);
  signalAppendLiteral(&cursor, &remaining, ": ");
  signalAppendLiteral(&cursor, &remaining, expr);
  signalAppendLiteral(&cursor, &remaining, ": ");
  signalAppendLiteral(&cursor, &remaining, message);
  if (includeResult) {
    signalAppendLiteral(&cursor, &remaining, ": result=");
    signalAppendInteger(&cursor, &remaining, result);
  }
  if (includeErrno) {
    signalAppendLiteral(&cursor, &remaining, ": errno=");
    signalAppendInteger(&cursor, &remaining, savedErrno);
  }
  signalAppendLiteral(&cursor, &remaining, "\n");

  if (remaining == 0) {
    storage[sizeof(storage) - 1] = '\n';
    signalWriteAll(storage, sizeof(storage));
    return;
  }

  *cursor = '\0';
  signalWriteAll(storage, static_cast<size_t>(cursor - storage));
}

inline void
signalWarningFailure(const char *expr,
                     const char *file,
                     int line,
                     const char *message,
                     int savedErrno,
                     bool includeErrno)
{
  signalDiagnostic(AssertSeverity::Warning, expr, file, line, message,
                   savedErrno, includeErrno, 0, false);
}

inline void
signalWarningFailureResult(const char *expr,
                           const char *file,
                           int line,
                           const char *message,
                           int savedErrno,
                           bool includeErrno,
                           long result,
                           bool includeResult)
{
  signalDiagnostic(AssertSeverity::Warning, expr, file, line, message,
                   savedErrno, includeErrno, result, includeResult);
}

[[noreturn]] inline void
signalAssertFailure(const char *expr,
                    const char *file,
                    int line,
                    const char *message,
                    int savedErrno,
                    bool includeErrno,
                    long result,
                    bool includeResult)
{
  signalDiagnostic(AssertSeverity::Error, expr, file, line, message,
                   savedErrno, includeErrno, result, includeResult);
  _exit(kAssertFailureExitCode);
}

inline AssertBuffer
currentAssertBuffer(bool reentrant)
{
  if (reentrant) {
    static thread_local char reentrantStorage[kAssertBufferSize];
    return AssertBuffer(reentrantStorage, sizeof(reentrantStorage));
  }

  size_t size = 0;
  if (dmtcp_get_thread_assert_buffer != nullptr) {
    char *storage = dmtcp_get_thread_assert_buffer(&size);
    if (storage != nullptr && size > 0) {
      return AssertBuffer(storage, size);
    }
  }

  static thread_local char storage[kAssertBufferSize];
  return AssertBuffer(storage, sizeof(storage));
}

inline unsigned&
diagnosticDepth()
{
  static thread_local unsigned depth = 0;
  return depth;
}

class DiagnosticScope {
 public:
  DiagnosticScope()
    : reentrant_(diagnosticDepth() != 0)
  {
    ++diagnosticDepth();
  }

  ~DiagnosticScope()
  {
    --diagnosticDepth();
  }

  bool reentrant() const { return reentrant_; }

 private:
  bool reentrant_;
};

template <typename... Args>
inline void
warningFailure(const char *expr,
               const char *file,
               int line,
               std::string_view fmt,
               const Args&... args)
{
  DiagnosticScope scope;
  AssertBuffer buffer = currentAssertBuffer(scope.reentrant());
  formatDiagnostic(buffer, AssertSeverity::Warning, expr, file, line, fmt,
                   args...);
  writeAllNoAlloc(STDERR_FILENO, buffer.c_str(), buffer.size());
}

template <typename... Args>
inline void
warningFailureErrno(const char *expr,
                    const char *file,
                    int line,
                    int savedErrno,
                    std::string_view fmt,
                    const Args&... args)
{
  DiagnosticScope scope;
  AssertBuffer buffer = currentAssertBuffer(scope.reentrant());
  formatDiagnosticWithErrno(buffer, AssertSeverity::Warning, expr, file, line,
                            savedErrno, fmt, args...);
  writeAllNoAlloc(STDERR_FILENO, buffer.c_str(), buffer.size());
}

template <typename... Args>
[[noreturn]] inline void
assertFailure(const char *expr,
              const char *file,
              int line,
              std::string_view fmt,
              const Args&... args)
{
  DiagnosticScope scope;
  AssertBuffer buffer = currentAssertBuffer(scope.reentrant());
  formatDiagnostic(buffer, AssertSeverity::Error, expr, file, line, fmt,
                   args...);
  writeAllNoAlloc(STDERR_FILENO, buffer.c_str(), buffer.size());
  _exit(kAssertFailureExitCode);
}

template <typename... Args>
[[noreturn]] inline void
assertFailureErrno(const char *expr,
                   const char *file,
                   int line,
                   int savedErrno,
                   std::string_view fmt,
                   const Args&... args)
{
  DiagnosticScope scope;
  AssertBuffer buffer = currentAssertBuffer(scope.reentrant());
  formatDiagnosticWithErrno(buffer, AssertSeverity::Error, expr, file, line,
                            savedErrno, fmt, args...);
  writeAllNoAlloc(STDERR_FILENO, buffer.c_str(), buffer.size());
  errno = savedErrno;
  _exit(kAssertFailureExitCode);
}

} // namespace dmtcp

#ifndef DMTCP_UTIL_ASSERT_NO_MACROS

#ifdef ASSERT
# undef ASSERT
#endif
#ifdef ASSERT_ERRNO
# undef ASSERT_ERRNO
#endif
#ifdef WARNING
# undef WARNING
#endif
#ifdef WARNING_ERRNO
# undef WARNING_ERRNO
#endif
#ifdef WARNING_EQ
# undef WARNING_EQ
#endif
#ifdef WARNING_EQ_MSG
# undef WARNING_EQ_MSG
#endif
#ifdef WARNING_NE
# undef WARNING_NE
#endif
#ifdef WARNING_NE_MSG
# undef WARNING_NE_MSG
#endif
#ifdef WARNING_NULL
# undef WARNING_NULL
#endif
#ifdef WARNING_NOT_NULL
# undef WARNING_NOT_NULL
#endif
#ifdef WARNING_FALSE
# undef WARNING_FALSE
#endif
#ifdef WARNING_TRUE
# undef WARNING_TRUE
#endif
#ifdef ASSERT_EQ
# undef ASSERT_EQ
#endif
#ifdef ASSERT_EQ_MSG
# undef ASSERT_EQ_MSG
#endif
#ifdef ASSERT_NE
# undef ASSERT_NE
#endif
#ifdef ASSERT_NE_MSG
# undef ASSERT_NE_MSG
#endif
#ifdef ASSERT_NULL
# undef ASSERT_NULL
#endif
#ifdef ASSERT_NOT_NULL
# undef ASSERT_NOT_NULL
#endif
#ifdef ASSERT_FALSE
# undef ASSERT_FALSE
#endif
#ifdef ASSERT_TRUE
# undef ASSERT_TRUE
#endif
#ifdef ASSERT_MUTEX_SUCCESS
# undef ASSERT_MUTEX_SUCCESS
#endif
#ifdef ASSERT_MUTEX_SUCCESS_MSG
# undef ASSERT_MUTEX_SUCCESS_MSG
#endif
#ifdef ASSERT_RWLOCK_SUCCESS
# undef ASSERT_RWLOCK_SUCCESS
#endif
#ifdef ASSERT_RWLOCK_SUCCESS_MSG
# undef ASSERT_RWLOCK_SUCCESS_MSG
#endif
#ifdef ASSERT_PTHREAD_SUCCESS
# undef ASSERT_PTHREAD_SUCCESS
#endif
#ifdef ASSERT_PTHREAD_SUCCESS_MSG
# undef ASSERT_PTHREAD_SUCCESS_MSG
#endif
#ifdef ASSERT_ZERO_RETURN
# undef ASSERT_ZERO_RETURN
#endif
#ifdef ASSERT_ZERO_RETURN_MSG
# undef ASSERT_ZERO_RETURN_MSG
#endif
#ifdef ASSERT_SYSCALL_SUCCESS
# undef ASSERT_SYSCALL_SUCCESS
#endif
#ifdef ASSERT_SYSCALL_SUCCESS_MSG
# undef ASSERT_SYSCALL_SUCCESS_MSG
#endif
#ifdef ASSERT_SYSCALL_EQ
# undef ASSERT_SYSCALL_EQ
#endif
#ifdef ASSERT_SYSCALL_EQ_MSG
# undef ASSERT_SYSCALL_EQ_MSG
#endif
#ifdef ASSERT_VALID_FD
# undef ASSERT_VALID_FD
#endif
#ifdef ASSERT_VALID_FD_MSG
# undef ASSERT_VALID_FD_MSG
#endif
#ifdef ASSERT_FORK_SUCCESS
# undef ASSERT_FORK_SUCCESS
#endif
#ifdef ASSERT_FORK_SUCCESS_MSG
# undef ASSERT_FORK_SUCCESS_MSG
#endif
#ifdef WARNING_MUTEX_SUCCESS
# undef WARNING_MUTEX_SUCCESS
#endif
#ifdef WARNING_MUTEX_SUCCESS_MSG
# undef WARNING_MUTEX_SUCCESS_MSG
#endif
#ifdef WARNING_RWLOCK_SUCCESS
# undef WARNING_RWLOCK_SUCCESS
#endif
#ifdef WARNING_RWLOCK_SUCCESS_MSG
# undef WARNING_RWLOCK_SUCCESS_MSG
#endif
#ifdef WARNING_PTHREAD_SUCCESS
# undef WARNING_PTHREAD_SUCCESS
#endif
#ifdef WARNING_PTHREAD_SUCCESS_MSG
# undef WARNING_PTHREAD_SUCCESS_MSG
#endif
#ifdef WARNING_ZERO_RETURN
# undef WARNING_ZERO_RETURN
#endif
#ifdef WARNING_ZERO_RETURN_MSG
# undef WARNING_ZERO_RETURN_MSG
#endif
#ifdef WARNING_SYSCALL_SUCCESS
# undef WARNING_SYSCALL_SUCCESS
#endif
#ifdef WARNING_SYSCALL_SUCCESS_MSG
# undef WARNING_SYSCALL_SUCCESS_MSG
#endif
#ifdef WARNING_SYSCALL_EQ
# undef WARNING_SYSCALL_EQ
#endif
#ifdef WARNING_SYSCALL_EQ_MSG
# undef WARNING_SYSCALL_EQ_MSG
#endif
#ifdef WARNING_VALID_FD
# undef WARNING_VALID_FD
#endif
#ifdef WARNING_VALID_FD_MSG
# undef WARNING_VALID_FD_MSG
#endif
#ifdef WARNING_FORK_SUCCESS
# undef WARNING_FORK_SUCCESS
#endif
#ifdef WARNING_FORK_SUCCESS_MSG
# undef WARNING_FORK_SUCCESS_MSG
#endif
#ifdef SIGNAL_WARNING
# undef SIGNAL_WARNING
#endif
#ifdef SIGNAL_WARNING_ERRNO
# undef SIGNAL_WARNING_ERRNO
#endif
#ifdef SIGNAL_ASSERT
# undef SIGNAL_ASSERT
#endif
#ifdef SIGNAL_ASSERT_ERRNO
# undef SIGNAL_ASSERT_ERRNO
#endif
#ifdef SIGNAL_WARNING_SYSCALL_SUCCESS
# undef SIGNAL_WARNING_SYSCALL_SUCCESS
#endif
#ifdef SIGNAL_ASSERT_ZERO_RETURN
# undef SIGNAL_ASSERT_ZERO_RETURN
#endif
#ifdef SIGNAL_ASSERT_RWLOCK_SUCCESS
# undef SIGNAL_ASSERT_RWLOCK_SUCCESS
#endif
#ifdef SIGNAL_ASSERT_SYSCALL_SUCCESS
# undef SIGNAL_ASSERT_SYSCALL_SUCCESS
#endif
#ifdef ASSERT_GT
# undef ASSERT_GT
#endif
#ifdef ASSERT_GT_MSG
# undef ASSERT_GT_MSG
#endif
#ifdef ASSERT_GE
# undef ASSERT_GE
#endif
#ifdef ASSERT_GE_MSG
# undef ASSERT_GE_MSG
#endif
#ifdef ASSERT_LT
# undef ASSERT_LT
#endif
#ifdef ASSERT_LT_MSG
# undef ASSERT_LT_MSG
#endif
#ifdef ASSERT_LE
# undef ASSERT_LE
#endif
#ifdef ASSERT_LE_MSG
# undef ASSERT_LE_MSG
#endif
#ifdef WARNING_GT
# undef WARNING_GT
#endif
#ifdef WARNING_GT_MSG
# undef WARNING_GT_MSG
#endif
#ifdef WARNING_GE
# undef WARNING_GE
#endif
#ifdef WARNING_GE_MSG
# undef WARNING_GE_MSG
#endif
#ifdef WARNING_LT
# undef WARNING_LT
#endif
#ifdef WARNING_LT_MSG
# undef WARNING_LT_MSG
#endif
#ifdef WARNING_LE
# undef WARNING_LE
#endif
#ifdef WARNING_LE_MSG
# undef WARNING_LE_MSG
#endif

#define WARNING(condition, fmt, ...)                                      \
  do {                                                                   \
    if (!(condition)) {                                                   \
      int dmtcpAssertSavedErrno = errno;                                  \
      ::dmtcp::warningFailure(#condition, __FILE__, __LINE__, fmt         \
                              __VA_OPT__(,) __VA_ARGS__);                \
      errno = dmtcpAssertSavedErrno;                                      \
    }                                                                    \
  } while (0)

#define WARNING_ERRNO(condition, fmt, ...)                                \
  do {                                                                   \
    if (!(condition)) {                                                   \
      int dmtcpAssertSavedErrno = errno;                                  \
      ::dmtcp::warningFailureErrno(#condition, __FILE__, __LINE__,        \
                                   dmtcpAssertSavedErrno, fmt             \
                                   __VA_OPT__(,) __VA_ARGS__);            \
      errno = dmtcpAssertSavedErrno;                                      \
    }                                                                    \
  } while (0)

#define ASSERT(condition, fmt, ...)                                       \
  do {                                                                   \
    if (!(condition)) {                                                   \
      int dmtcpAssertSavedErrno = errno;                                  \
      ::dmtcp::assertFailure(#condition, __FILE__, __LINE__, fmt          \
                             __VA_OPT__(,) __VA_ARGS__);                 \
      errno = dmtcpAssertSavedErrno;                                      \
    }                                                                    \
  } while (0)

#define ASSERT_ERRNO(condition, fmt, ...)                                 \
  do {                                                                   \
    if (!(condition)) {                                                   \
      int dmtcpAssertSavedErrno = errno;                                  \
      ::dmtcp::assertFailureErrno(#condition, __FILE__, __LINE__,         \
                                  dmtcpAssertSavedErrno, fmt              \
                                  __VA_OPT__(,) __VA_ARGS__);             \
    }                                                                    \
  } while (0)

#define ASSERT_FALSE(condition) \
  ASSERT(!(condition), "expected false: {}", #condition)

#define ASSERT_TRUE(condition) \
  ASSERT((condition), "expected true: {}", #condition)

#define WARNING_FALSE(condition) \
  WARNING(!(condition), "expected false: {}", #condition)

#define WARNING_TRUE(condition) \
  WARNING((condition), "expected true: {}", #condition)

#define SIGNAL_WARNING(condition, message)                                \
  do {                                                                   \
    if (!(condition)) {                                                   \
      int dmtcpAssertSavedErrno = errno;                                  \
      ::dmtcp::signalWarningFailure(#condition, __FILE__, __LINE__,       \
                                    message, dmtcpAssertSavedErrno,       \
                                    false);                               \
      errno = dmtcpAssertSavedErrno;                                      \
    }                                                                    \
  } while (0)

#define SIGNAL_WARNING_ERRNO(condition, message)                          \
  do {                                                                   \
    if (!(condition)) {                                                   \
      int dmtcpAssertSavedErrno = errno;                                  \
      ::dmtcp::signalWarningFailure(#condition, __FILE__, __LINE__,       \
                                    message, dmtcpAssertSavedErrno,       \
                                    true);                                \
      errno = dmtcpAssertSavedErrno;                                      \
    }                                                                    \
  } while (0)

#define SIGNAL_ASSERT(condition, message)                                 \
  do {                                                                   \
    if (!(condition)) {                                                   \
      int dmtcpAssertSavedErrno = errno;                                  \
      ::dmtcp::signalAssertFailure(#condition, __FILE__, __LINE__,        \
                                   message, dmtcpAssertSavedErrno,        \
                                   false, 0, false);                      \
    }                                                                    \
  } while (0)

#define SIGNAL_ASSERT_ERRNO(condition, message)                           \
  do {                                                                   \
    if (!(condition)) {                                                   \
      int dmtcpAssertSavedErrno = errno;                                  \
      ::dmtcp::signalAssertFailure(#condition, __FILE__, __LINE__,        \
                                   message, dmtcpAssertSavedErrno,        \
                                   true, 0, false);                       \
    }                                                                    \
  } while (0)

#define SIGNAL_WARNING_SYSCALL_SUCCESS(expression)                        \
  do {                                                                   \
    const auto dmtcpAssertResult = (expression);                          \
    if (dmtcpAssertResult == -1) {                                        \
      int dmtcpAssertSavedErrno = errno;                                  \
      ::dmtcp::signalWarningFailureResult(                                \
        #expression, __FILE__, __LINE__,                                  \
        "expected a return value other than -1",                          \
        dmtcpAssertSavedErrno, true,                                      \
        static_cast<long>(dmtcpAssertResult), true);                      \
      errno = dmtcpAssertSavedErrno;                                      \
    }                                                                    \
  } while (0)

#define SIGNAL_ASSERT_ZERO_RETURN(expression)                             \
  do {                                                                   \
    const auto dmtcpAssertResult = (expression);                          \
    if (dmtcpAssertResult != 0) {                                         \
      int dmtcpAssertSavedErrno = errno;                                  \
      ::dmtcp::signalAssertFailure(#expression, __FILE__, __LINE__,       \
                                   "expected '0' but returned nonzero",   \
                                   dmtcpAssertSavedErrno, false,          \
                                   static_cast<long>(dmtcpAssertResult),  \
                                   true);                                 \
    }                                                                    \
  } while (0)

#define SIGNAL_ASSERT_RWLOCK_SUCCESS(expression) \
  SIGNAL_ASSERT_ZERO_RETURN(expression)

#define SIGNAL_ASSERT_SYSCALL_SUCCESS(expression)                         \
  do {                                                                   \
    const auto dmtcpAssertResult = (expression);                          \
    if (dmtcpAssertResult == -1) {                                        \
      int dmtcpAssertSavedErrno = errno;                                  \
      ::dmtcp::signalAssertFailure(                                       \
        #expression, __FILE__, __LINE__,                                  \
        "expected a return value other than -1",                          \
        dmtcpAssertSavedErrno, true,                                      \
        static_cast<long>(dmtcpAssertResult), true);                      \
    }                                                                    \
  } while (0)

// For C APIs that return 0 on success and a nonzero status code on failure.
// Do not use these for syscall-style APIs that return -1 and set errno, or
// for APIs where nonzero is a successful payload.
#define DMTCP_ASSERT_ZERO_RETURN(expressionText, expression)              \
  do {                                                                   \
    const auto dmtcpAssertResult = (expression);                          \
    ASSERT(dmtcpAssertResult == 0,                                        \
           "{} failed: expected '0' but returned '{}' ({})",              \
           expressionText, dmtcpAssertResult,                             \
           ::dmtcp::errnoName(static_cast<int>(dmtcpAssertResult)));      \
  } while (0)

#define DMTCP_WARNING_ZERO_RETURN(expressionText, expression)             \
  do {                                                                   \
    const auto dmtcpAssertResult = (expression);                          \
    WARNING(dmtcpAssertResult == 0,                                      \
            "{} failed: expected '0' but returned '{}' ({})",             \
            expressionText, dmtcpAssertResult,                            \
            ::dmtcp::errnoName(static_cast<int>(dmtcpAssertResult)));     \
  } while (0)

#define DMTCP_ASSERT_ZERO_RETURN_MSG(expressionText, expression, fmt, ...) \
  do {                                                                    \
    const auto dmtcpAssertResult = (expression);                           \
    ASSERT(dmtcpAssertResult == 0,                                         \
           "{} failed: expected '0' but returned '{}' ({}); " fmt,         \
           expressionText, dmtcpAssertResult,                              \
           ::dmtcp::errnoName(static_cast<int>(dmtcpAssertResult))         \
           __VA_OPT__(,) __VA_ARGS__);                                     \
  } while (0)

#define DMTCP_WARNING_ZERO_RETURN_MSG(expressionText, expression, fmt, ...) \
  do {                                                                     \
    const auto dmtcpAssertResult = (expression);                            \
    WARNING(dmtcpAssertResult == 0,                                         \
            "{} failed: expected '0' but returned '{}' ({}); " fmt,         \
            expressionText, dmtcpAssertResult,                              \
            ::dmtcp::errnoName(static_cast<int>(dmtcpAssertResult))         \
            __VA_OPT__(,) __VA_ARGS__);                                     \
  } while (0)

// For libc/syscall-style APIs that return -1 on failure and set errno. Do not
// use these for pthread-style APIs that return a status code directly.
#define DMTCP_ASSERT_SYSCALL_SUCCESS(expressionText, expression)       \
  do {                                                                \
    const auto dmtcpAssertResult = (expression);                       \
    ASSERT_ERRNO(dmtcpAssertResult != -1,                              \
                 "{} failed: expected a return value other than -1, "  \
                 "returned {}",                                       \
                 expressionText, dmtcpAssertResult);                   \
  } while (0)

#define DMTCP_WARNING_SYSCALL_SUCCESS(expressionText, expression)      \
  do {                                                                \
    const auto dmtcpAssertResult = (expression);                       \
    WARNING_ERRNO(dmtcpAssertResult != -1,                             \
                  "{} failed: expected a return value other than -1, " \
                  "returned {}",                                      \
                  expressionText, dmtcpAssertResult);                  \
  } while (0)

#define DMTCP_ASSERT_SYSCALL_SUCCESS_MSG(expressionText, expression, fmt, ...) \
  do {                                                                        \
    const auto dmtcpAssertResult = (expression);                               \
    ASSERT_ERRNO(dmtcpAssertResult != -1,                                      \
                 "{} failed: expected a return value other than -1, "          \
                 "returned {}; " fmt,                                         \
                 expressionText, dmtcpAssertResult                             \
                 __VA_OPT__(,) __VA_ARGS__);                                   \
  } while (0)

#define DMTCP_WARNING_SYSCALL_SUCCESS_MSG(expressionText, expression, fmt, ...) \
  do {                                                                         \
    const auto dmtcpAssertResult = (expression);                                \
    WARNING_ERRNO(dmtcpAssertResult != -1,                                      \
                  "{} failed: expected a return value other than -1, "          \
                  "returned {}; " fmt,                                         \
                  expressionText, dmtcpAssertResult                             \
                  __VA_OPT__(,) __VA_ARGS__);                                   \
  } while (0)

#define DMTCP_ASSERT_SYSCALL_EQ(expected, expressionText, expression)                \
  do {                                                                            \
    const auto dmtcpAssertExpected = (expected);                                   \
    const auto dmtcpAssertResult = (expression);                                   \
    ASSERT_ERRNO(dmtcpAssertResult == dmtcpAssertExpected,                         \
                 "{} failed: expected a return value of '{}', returned '{}'",      \
                 expressionText, dmtcpAssertExpected, dmtcpAssertResult);          \
  } while (0)

#define DMTCP_WARNING_SYSCALL_EQ(expected, expressionText, expression)               \
  do {                                                                             \
    const auto dmtcpAssertExpected = (expected);                                    \
    const auto dmtcpAssertResult = (expression);                                    \
    WARNING_ERRNO(dmtcpAssertResult == dmtcpAssertExpected,                         \
                  "{} failed: expected a return value of '{}', returned '{}'",      \
                  expressionText, dmtcpAssertExpected, dmtcpAssertResult);          \
  } while (0)

#define DMTCP_ASSERT_SYSCALL_EQ_MSG(expected, expressionText, expression, fmt, ...)                 \
  do {                                                                                          \
    const auto dmtcpAssertExpected = (expected);                                                 \
    const auto dmtcpAssertResult = (expression);                                                 \
    ASSERT_ERRNO(dmtcpAssertResult == dmtcpAssertExpected,                                       \
                 "{} failed: expected a return value of '{}', returned '{}'; " fmt,              \
                 expressionText, dmtcpAssertExpected, dmtcpAssertResult                          \
                 __VA_OPT__(,) __VA_ARGS__);                                                     \
  } while (0)

#define DMTCP_WARNING_SYSCALL_EQ_MSG(expected, expressionText, expression, fmt, ...)               \
  do {                                                                                           \
    const auto dmtcpAssertExpected = (expected);                                                  \
    const auto dmtcpAssertResult = (expression);                                                  \
    WARNING_ERRNO(dmtcpAssertResult == dmtcpAssertExpected,                                       \
                  "{} failed: expected a return value of '{}', returned '{}'; " fmt,              \
                  expressionText, dmtcpAssertExpected, dmtcpAssertResult                          \
                  __VA_OPT__(,) __VA_ARGS__);                                                     \
  } while (0)

#define DMTCP_ASSERT_VALID_FD(expressionText, expression)                    \
  do {                                                                      \
    const auto dmtcpAssertResult = (expression);                             \
    ASSERT_ERRNO(dmtcpAssertResult >= 0,                                     \
                 "{} failed: expected a valid fd >= 0, returned '{}'",       \
                 expressionText, dmtcpAssertResult);                         \
  } while (0)

#define DMTCP_WARNING_VALID_FD(expressionText, expression)                   \
  do {                                                                      \
    const auto dmtcpAssertResult = (expression);                             \
    WARNING_ERRNO(dmtcpAssertResult >= 0,                                    \
                  "{} failed: expected a valid fd >= 0, returned '{}'",      \
                  expressionText, dmtcpAssertResult);                        \
  } while (0)

#define DMTCP_ASSERT_VALID_FD_MSG(expressionText, expression, fmt, ...)      \
  do {                                                                      \
    const auto dmtcpAssertResult = (expression);                             \
    ASSERT_ERRNO(dmtcpAssertResult >= 0,                                     \
                 "{} failed: expected a valid fd >= 0, returned '{}'; " fmt, \
                 expressionText, dmtcpAssertResult                           \
                 __VA_OPT__(,) __VA_ARGS__);                                 \
  } while (0)

#define DMTCP_WARNING_VALID_FD_MSG(expressionText, expression, fmt, ...)     \
  do {                                                                      \
    const auto dmtcpAssertResult = (expression);                             \
    WARNING_ERRNO(dmtcpAssertResult >= 0,                                    \
                  "{} failed: expected a valid fd >= 0, returned '{}'; " fmt,\
                  expressionText, dmtcpAssertResult                          \
                  __VA_OPT__(,) __VA_ARGS__);                                \
  } while (0)

#define DMTCP_ASSERT_FORK_SUCCESS(expressionText, expression)                \
  do {                                                                      \
    const auto dmtcpAssertResult = (expression);                             \
    ASSERT_ERRNO(dmtcpAssertResult >= 0,                                     \
                 "{} failed: expected a fork result >= 0, returned '{}'",    \
                 expressionText, dmtcpAssertResult);                         \
  } while (0)

#define DMTCP_WARNING_FORK_SUCCESS(expressionText, expression)               \
  do {                                                                      \
    const auto dmtcpAssertResult = (expression);                             \
    WARNING_ERRNO(dmtcpAssertResult >= 0,                                    \
                  "{} failed: expected a fork result >= 0, returned '{}'",   \
                  expressionText, dmtcpAssertResult);                        \
  } while (0)

#define DMTCP_ASSERT_FORK_SUCCESS_MSG(expressionText, expression, fmt, ...)  \
  do {                                                                      \
    const auto dmtcpAssertResult = (expression);                             \
    ASSERT_ERRNO(dmtcpAssertResult >= 0,                                     \
                 "{} failed: expected a fork result >= 0, returned '{}'; "   \
                 fmt,                                                        \
                 expressionText, dmtcpAssertResult                           \
                 __VA_OPT__(,) __VA_ARGS__);                                 \
  } while (0)

#define DMTCP_WARNING_FORK_SUCCESS_MSG(expressionText, expression, fmt, ...) \
  do {                                                                      \
    const auto dmtcpAssertResult = (expression);                             \
    WARNING_ERRNO(dmtcpAssertResult >= 0,                                    \
                  "{} failed: expected a fork result >= 0, returned '{}'; "  \
                  fmt,                                                       \
                  expressionText, dmtcpAssertResult                          \
                  __VA_OPT__(,) __VA_ARGS__);                                \
  } while (0)

#define ASSERT_MUTEX_SUCCESS(expression) \
  DMTCP_ASSERT_ZERO_RETURN(#expression, expression)

#define ASSERT_MUTEX_SUCCESS_MSG(expression, fmt, ...) \
  DMTCP_ASSERT_ZERO_RETURN_MSG(#expression, expression, fmt \
                               __VA_OPT__(,) __VA_ARGS__)

#define ASSERT_RWLOCK_SUCCESS(expression) \
  DMTCP_ASSERT_ZERO_RETURN(#expression, expression)

#define ASSERT_RWLOCK_SUCCESS_MSG(expression, fmt, ...) \
  DMTCP_ASSERT_ZERO_RETURN_MSG(#expression, expression, fmt \
                               __VA_OPT__(,) __VA_ARGS__)

#define ASSERT_PTHREAD_SUCCESS(expression) \
  DMTCP_ASSERT_ZERO_RETURN(#expression, expression)

#define ASSERT_PTHREAD_SUCCESS_MSG(expression, fmt, ...) \
  DMTCP_ASSERT_ZERO_RETURN_MSG(#expression, expression, fmt \
                               __VA_OPT__(,) __VA_ARGS__)

#define ASSERT_ZERO_RETURN(expression) \
  DMTCP_ASSERT_ZERO_RETURN(#expression, expression)

#define ASSERT_ZERO_RETURN_MSG(expression, fmt, ...) \
  DMTCP_ASSERT_ZERO_RETURN_MSG(#expression, expression, fmt \
                               __VA_OPT__(,) __VA_ARGS__)

#define ASSERT_SYSCALL_SUCCESS(expression) \
  DMTCP_ASSERT_SYSCALL_SUCCESS(#expression, expression)

#define ASSERT_SYSCALL_SUCCESS_MSG(expression, fmt, ...) \
  DMTCP_ASSERT_SYSCALL_SUCCESS_MSG(#expression, expression, fmt \
                                   __VA_OPT__(,) __VA_ARGS__)

#define ASSERT_SYSCALL_EQ(expected, expression) \
  DMTCP_ASSERT_SYSCALL_EQ(expected, #expression, expression)

#define ASSERT_SYSCALL_EQ_MSG(expected, expression, fmt, ...) \
  DMTCP_ASSERT_SYSCALL_EQ_MSG(expected, #expression, expression, fmt \
                              __VA_OPT__(,) __VA_ARGS__)

#define ASSERT_VALID_FD(expression) \
  DMTCP_ASSERT_VALID_FD(#expression, expression)

#define ASSERT_VALID_FD_MSG(expression, fmt, ...) \
  DMTCP_ASSERT_VALID_FD_MSG(#expression, expression, fmt \
                            __VA_OPT__(,) __VA_ARGS__)

#define ASSERT_FORK_SUCCESS(expression) \
  DMTCP_ASSERT_FORK_SUCCESS(#expression, expression)

#define ASSERT_FORK_SUCCESS_MSG(expression, fmt, ...) \
  DMTCP_ASSERT_FORK_SUCCESS_MSG(#expression, expression, fmt \
                                __VA_OPT__(,) __VA_ARGS__)

#define WARNING_MUTEX_SUCCESS(expression) \
  DMTCP_WARNING_ZERO_RETURN(#expression, expression)

#define WARNING_MUTEX_SUCCESS_MSG(expression, fmt, ...) \
  DMTCP_WARNING_ZERO_RETURN_MSG(#expression, expression, fmt \
                                __VA_OPT__(,) __VA_ARGS__)

#define WARNING_RWLOCK_SUCCESS(expression) \
  DMTCP_WARNING_ZERO_RETURN(#expression, expression)

#define WARNING_RWLOCK_SUCCESS_MSG(expression, fmt, ...) \
  DMTCP_WARNING_ZERO_RETURN_MSG(#expression, expression, fmt \
                                __VA_OPT__(,) __VA_ARGS__)

#define WARNING_PTHREAD_SUCCESS(expression) \
  DMTCP_WARNING_ZERO_RETURN(#expression, expression)

#define WARNING_PTHREAD_SUCCESS_MSG(expression, fmt, ...) \
  DMTCP_WARNING_ZERO_RETURN_MSG(#expression, expression, fmt \
                                __VA_OPT__(,) __VA_ARGS__)

#define WARNING_ZERO_RETURN(expression) \
  DMTCP_WARNING_ZERO_RETURN(#expression, expression)

#define WARNING_ZERO_RETURN_MSG(expression, fmt, ...) \
  DMTCP_WARNING_ZERO_RETURN_MSG(#expression, expression, fmt \
                                __VA_OPT__(,) __VA_ARGS__)

#define WARNING_SYSCALL_SUCCESS(expression) \
  DMTCP_WARNING_SYSCALL_SUCCESS(#expression, expression)

#define WARNING_SYSCALL_SUCCESS_MSG(expression, fmt, ...) \
  DMTCP_WARNING_SYSCALL_SUCCESS_MSG(#expression, expression, fmt \
                                    __VA_OPT__(,) __VA_ARGS__)

#define WARNING_SYSCALL_EQ(expected, expression) \
  DMTCP_WARNING_SYSCALL_EQ(expected, #expression, expression)

#define WARNING_SYSCALL_EQ_MSG(expected, expression, fmt, ...) \
  DMTCP_WARNING_SYSCALL_EQ_MSG(expected, #expression, expression, fmt \
                               __VA_OPT__(,) __VA_ARGS__)

#define WARNING_VALID_FD(expression) \
  DMTCP_WARNING_VALID_FD(#expression, expression)

#define WARNING_VALID_FD_MSG(expression, fmt, ...) \
  DMTCP_WARNING_VALID_FD_MSG(#expression, expression, fmt \
                             __VA_OPT__(,) __VA_ARGS__)

#define WARNING_FORK_SUCCESS(expression) \
  DMTCP_WARNING_FORK_SUCCESS(#expression, expression)

#define WARNING_FORK_SUCCESS_MSG(expression, fmt, ...) \
  DMTCP_WARNING_FORK_SUCCESS_MSG(#expression, expression, fmt \
                                 __VA_OPT__(,) __VA_ARGS__)

#define ASSERT_NULL(value) \
  ASSERT((value) == nullptr, "expected null: {}", #value)

#define ASSERT_NOT_NULL(value) \
  ASSERT((value) != nullptr, "expected non-null: {}", #value)

#define WARNING_NULL(value) \
  WARNING((value) == nullptr, "expected null: {}", #value)

#define WARNING_NOT_NULL(value) \
  WARNING((value) != nullptr, "expected non-null: {}", #value)

#define DMTCP_ASSERT_COMPARE(assertion, lhs, rhs, op, opText)             \
  do {                                                                   \
    const auto& dmtcpAssertLhs = (lhs);                                   \
    const auto& dmtcpAssertRhs = (rhs);                                   \
    assertion(dmtcpAssertLhs op dmtcpAssertRhs,                           \
              "expected {} " opText " {}, got {} and {}",                \
              #lhs, #rhs, dmtcpAssertLhs, dmtcpAssertRhs);                \
  } while (0)

#define DMTCP_ASSERT_COMPARE_MSG(assertion, lhs, rhs, op, opText, fmt, ...) \
  do {                                                                   \
    const auto& dmtcpAssertLhs = (lhs);                                   \
    const auto& dmtcpAssertRhs = (rhs);                                   \
    assertion(dmtcpAssertLhs op dmtcpAssertRhs,                           \
              "expected {} " opText " {}, got {} and {}; " fmt,          \
              #lhs, #rhs, dmtcpAssertLhs, dmtcpAssertRhs                  \
              __VA_OPT__(,) __VA_ARGS__);                                 \
  } while (0)

#define ASSERT_EQ(expected, actual) \
  DMTCP_ASSERT_COMPARE(ASSERT, expected, actual, ==, "==")

#define ASSERT_EQ_MSG(expected, actual, fmt, ...) \
  DMTCP_ASSERT_COMPARE_MSG(ASSERT, expected, actual, ==, "==", fmt \
                           __VA_OPT__(,) __VA_ARGS__)

#define WARNING_EQ(expected, actual) \
  DMTCP_ASSERT_COMPARE(WARNING, expected, actual, ==, "==")

#define WARNING_EQ_MSG(expected, actual, fmt, ...) \
  DMTCP_ASSERT_COMPARE_MSG(WARNING, expected, actual, ==, "==", fmt \
                           __VA_OPT__(,) __VA_ARGS__)

#define ASSERT_NE(expected, actual) \
  DMTCP_ASSERT_COMPARE(ASSERT, expected, actual, !=, "!=")

#define ASSERT_NE_MSG(expected, actual, fmt, ...) \
  DMTCP_ASSERT_COMPARE_MSG(ASSERT, expected, actual, !=, "!=", fmt \
                           __VA_OPT__(,) __VA_ARGS__)

#define WARNING_NE(expected, actual) \
  DMTCP_ASSERT_COMPARE(WARNING, expected, actual, !=, "!=")

#define WARNING_NE_MSG(expected, actual, fmt, ...) \
  DMTCP_ASSERT_COMPARE_MSG(WARNING, expected, actual, !=, "!=", fmt \
                           __VA_OPT__(,) __VA_ARGS__)

#define ASSERT_GT(lhs, rhs) \
  DMTCP_ASSERT_COMPARE(ASSERT, lhs, rhs, >, ">")

#define ASSERT_GT_MSG(lhs, rhs, fmt, ...) \
  DMTCP_ASSERT_COMPARE_MSG(ASSERT, lhs, rhs, >, ">", fmt \
                           __VA_OPT__(,) __VA_ARGS__)

#define WARNING_GT(lhs, rhs) \
  DMTCP_ASSERT_COMPARE(WARNING, lhs, rhs, >, ">")

#define WARNING_GT_MSG(lhs, rhs, fmt, ...) \
  DMTCP_ASSERT_COMPARE_MSG(WARNING, lhs, rhs, >, ">", fmt \
                           __VA_OPT__(,) __VA_ARGS__)

#define ASSERT_LT(lhs, rhs) \
  DMTCP_ASSERT_COMPARE(ASSERT, lhs, rhs, <, "<")

#define ASSERT_LT_MSG(lhs, rhs, fmt, ...) \
  DMTCP_ASSERT_COMPARE_MSG(ASSERT, lhs, rhs, <, "<", fmt \
                           __VA_OPT__(,) __VA_ARGS__)

#define WARNING_LT(lhs, rhs) \
  DMTCP_ASSERT_COMPARE(WARNING, lhs, rhs, <, "<")

#define WARNING_LT_MSG(lhs, rhs, fmt, ...) \
  DMTCP_ASSERT_COMPARE_MSG(WARNING, lhs, rhs, <, "<", fmt \
                           __VA_OPT__(,) __VA_ARGS__)

#define ASSERT_GE(lhs, rhs) \
  DMTCP_ASSERT_COMPARE(ASSERT, lhs, rhs, >=, ">=")

#define ASSERT_GE_MSG(lhs, rhs, fmt, ...) \
  DMTCP_ASSERT_COMPARE_MSG(ASSERT, lhs, rhs, >=, ">=", fmt \
                           __VA_OPT__(,) __VA_ARGS__)

#define WARNING_GE(lhs, rhs) \
  DMTCP_ASSERT_COMPARE(WARNING, lhs, rhs, >=, ">=")

#define WARNING_GE_MSG(lhs, rhs, fmt, ...) \
  DMTCP_ASSERT_COMPARE_MSG(WARNING, lhs, rhs, >=, ">=", fmt \
                           __VA_OPT__(,) __VA_ARGS__)

#define ASSERT_LE(lhs, rhs) \
  DMTCP_ASSERT_COMPARE(ASSERT, lhs, rhs, <=, "<=")

#define ASSERT_LE_MSG(lhs, rhs, fmt, ...) \
  DMTCP_ASSERT_COMPARE_MSG(ASSERT, lhs, rhs, <=, "<=", fmt \
                           __VA_OPT__(,) __VA_ARGS__)

#define WARNING_LE(lhs, rhs) \
  DMTCP_ASSERT_COMPARE(WARNING, lhs, rhs, <=, "<=")

#define WARNING_LE_MSG(lhs, rhs, fmt, ...) \
  DMTCP_ASSERT_COMPARE_MSG(WARNING, lhs, rhs, <=, "<=", fmt \
                           __VA_OPT__(,) __VA_ARGS__)

#endif // DMTCP_UTIL_ASSERT_NO_MACROS

#endif // DMTCP_UTIL_ASSERT_H
