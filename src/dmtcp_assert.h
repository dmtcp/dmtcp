#ifndef DMTCP_ASSERT_H
#define DMTCP_ASSERT_H

#include <cerrno>
#include <cstdlib>
#include <source_location>
#include <unistd.h>

#include "dmtcp.h"
#include "logger.h"

namespace dmtcp {

inline constexpr int kAssertFailureExitCode = 99;

/*
 * Log destination policy:
 * - Generic ASSERT/WARNING logs write to kLogFd, currently stderr.  The
 *   caller may redirect or close stderr; the log emit path
 *   treats write failures as best-effort and never falls back to allocation,
 *   logging policy, environment variables, or richer DMTCP runtime services.
 * - Fatal ASSERT exits through DMTCP_FAIL_RC after honoring
 *   DMTCP_SLEEP_ON_FAILURE and DMTCP_ABORT_ON_FAILURE.
 */

/*
 * Assertion helper guide:
 * - ASSERT(cond, "msg {}", arg) logs and exits when cond is false;
 *   WARN(cond, ...) logs and continues when cond is false.  NOTE(...) and
 *   TRACE(...) use the same formatter and are controlled by the runtime log
 *   level.  ASSERT_ERRNO/WARN_ERRNO add the errno value captured at the
 *   failing check.
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

[[noreturn]] inline void
exitAfterAssertFailure()
{
  while (std::getenv("DMTCP_SLEEP_ON_FAILURE") != nullptr) {
  }
  _exit(DMTCP_FAIL_RC);
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
      ::dmtcp::Logger::logMessage(level, dmtcpLogLocation, expr,         \
                                  dmtcpAssertSavedErrno, includeErrno,   \
                                  fmt __VA_OPT__(,) __VA_ARGS__);        \
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

#endif // DMTCP_ASSERT_H
