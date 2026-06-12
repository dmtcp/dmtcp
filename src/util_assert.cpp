#include "util_assert.h"

#include <atomic>
#include <fcntl.h>
#include <limits.h>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

#include "protectedfds.h"
#include "syscallwrappers.h"

extern "C" __attribute__((weak)) ssize_t
dmtcp_assert_write(int fd, const void *buf, size_t count)
{
  return write(fd, buf, count);
}

namespace dmtcp {
namespace {

constexpr size_t kMaxLogOverrides = 32;
constexpr size_t kMaxLogOverrideKey = 128;

struct LogOverride {
  bool isFile;
  char key[kMaxLogOverrideKey];
  LogLevel level;
};

std::atomic<int> gLogLevel{static_cast<int>(LogLevel::Note)};
std::atomic<int> gLogOverrideCount{0};
// Log override and diagnostic fd state is configured during single-threaded
// process initialization. Runtime diagnostics read it without synchronization.
// Revisit this invariant before adding runtime log reconfiguration.
LogOverride gLogOverrides[kMaxLogOverrides];
int gDiagnosticConsoleFd = kDiagnosticFd;
int gDiagnosticLogFd = -1;

int
openProtectedFile(const char *path, int protectedFd)
{
  int fd = _real_open(path, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR);
  if (fd == -1) {
    return -1;
  }

  int newFd = _real_dup2(fd, protectedFd);
  if (fd != newFd) {
    _real_close(fd);
  }
  return newFd;
}

int
openLogFile(const char *path)
{
  return openProtectedFile(path, PROTECTED_DIAGNOSTIC_LOG_FD);
}

int
openLogFileWithFallbacks(const char *path)
{
  int fd = openLogFile(path);
  if (fd != -1) {
    return fd;
  }

  char fallback[PATH_MAX];
  for (int suffix = 2; suffix <= 5; ++suffix) {
    int len = snprintf(fallback, sizeof(fallback), "%s_%d", path, suffix);
    if (len <= 0 || static_cast<size_t>(len) >= sizeof(fallback)) {
      return -1;
    }
    fd = openLogFile(fallback);
    if (fd != -1) {
      return fd;
    }
  }
  return -1;
}

std::string_view
trim(std::string_view text)
{
  while (!text.empty() &&
         (text.front() == ' ' || text.front() == '\t' ||
          text.front() == '\n' || text.front() == '\r')) {
    text.remove_prefix(1);
  }
  while (!text.empty() &&
         (text.back() == ' ' || text.back() == '\t' ||
          text.back() == '\n' || text.back() == '\r')) {
    text.remove_suffix(1);
  }
  return text;
}

bool
startsWith(std::string_view text, std::string_view prefix)
{
  return text.size() >= prefix.size() &&
         text.substr(0, prefix.size()) == prefix;
}

bool
endsWith(std::string_view text, std::string_view suffix)
{
  return text.size() >= suffix.size() &&
         text.substr(text.size() - suffix.size()) == suffix;
}

bool
copyKey(char *destination, size_t capacity, std::string_view key)
{
  if (key.empty() || key.size() >= capacity) {
    return false;
  }
  std::memcpy(destination, key.data(), key.size());
  destination[key.size()] = '\0';
  return true;
}

bool
parseOverrideEntry(std::string_view entry, LogOverride *override)
{
  entry = trim(entry);
  if (entry.empty()) {
    return true;
  }

  const size_t equals = entry.find('=');
  if (equals == std::string_view::npos) {
    return false;
  }

  std::string_view key = trim(entry.substr(0, equals));
  std::string_view value = trim(entry.substr(equals + 1));

  LogLevel level;
  if (!parseLogLevel(value, &level)) {
    return false;
  }

  override->isFile = false;
  if (startsWith(key, "file:")) {
    override->isFile = true;
    key.remove_prefix(sizeof("file:") - 1);
  }

  if (!copyKey(override->key, sizeof(override->key), trim(key))) {
    return false;
  }
  override->level = level;
  return true;
}

bool
findFileOverride(std::string_view file, LogLevel *level)
{
  const int count = gLogOverrideCount.load(std::memory_order_relaxed);
  for (int i = count - 1; i >= 0; --i) {
    const LogOverride& override = gLogOverrides[i];
    if (override.isFile && endsWith(file, override.key)) {
      *level = override.level;
      return true;
    }
  }
  return false;
}

bool
findComponentOverride(std::string_view component, LogLevel *level)
{
  const int count = gLogOverrideCount.load(std::memory_order_relaxed);
  for (int i = count - 1; i >= 0; --i) {
    const LogOverride& override = gLogOverrides[i];
    if (!override.isFile && component == override.key) {
      *level = override.level;
      return true;
    }
  }
  return false;
}

} // namespace

void
setLogLevel(LogLevel level)
{
  gLogLevel.store(static_cast<int>(level), std::memory_order_relaxed);
}

bool
parseLogLevel(std::string_view text, LogLevel *level)
{
  if (level == nullptr) {
    return false;
  }

  text = trim(text);
  if (text == "error" || text == "0") {
    *level = LogLevel::Error;
    return true;
  }
  if (text == "warn" || text == "warning" || text == "1") {
    *level = LogLevel::Warn;
    return true;
  }
  if (text == "note" || text == "2") {
    *level = LogLevel::Note;
    return true;
  }
  if (text == "trace" || text == "3") {
    *level = LogLevel::Trace;
    return true;
  }
  return false;
}

bool
setLogOverrides(std::string_view overrides)
{
  overrides = trim(overrides);
  if (overrides.empty()) {
    gLogOverrideCount.store(0, std::memory_order_relaxed);
    return true;
  }

  LogOverride parsed[kMaxLogOverrides];
  int parsedCount = 0;
  while (!overrides.empty()) {
    const size_t separator = overrides.find(';');
    std::string_view entry = separator == std::string_view::npos
                               ? overrides
                               : overrides.substr(0, separator);
    if (!trim(entry).empty()) {
      if (parsedCount == static_cast<int>(kMaxLogOverrides) ||
          !parseOverrideEntry(entry, &parsed[parsedCount])) {
        return false;
      }
      parsedCount++;
    }

    if (separator == std::string_view::npos) {
      break;
    }
    overrides.remove_prefix(separator + 1);
  }

  for (int i = 0; i < parsedCount; ++i) {
    gLogOverrides[i] = parsed[i];
  }
  gLogOverrideCount.store(parsedCount, std::memory_order_relaxed);
  return true;
}

bool
logEnabled(LogLevel level,
           std::string_view component,
           std::string_view file)
{
  LogLevel effectiveLevel =
    static_cast<LogLevel>(gLogLevel.load(std::memory_order_relaxed));
  if (gLogOverrideCount.load(std::memory_order_relaxed) > 0) {
    LogLevel overrideLevel;
    if (!file.empty() && findFileOverride(file, &overrideLevel)) {
      effectiveLevel = overrideLevel;
    } else if (!component.empty() &&
               findComponentOverride(component, &overrideLevel)) {
      effectiveLevel = overrideLevel;
    }
  }
  return static_cast<int>(effectiveLevel) >= static_cast<int>(level);
}

void
initializeDiagnosticConsole(const char *stderrPath)
{
  if (_real_dup2(PROTECTED_STDERR_FD, PROTECTED_STDERR_FD) ==
      PROTECTED_STDERR_FD) {
    gDiagnosticConsoleFd = PROTECTED_STDERR_FD;
    return;
  }

  if (stderrPath != nullptr && stderrPath[0] != '\0') {
    int fd = openProtectedFile(stderrPath, PROTECTED_STDERR_FD);
    if (fd != -1) {
      gDiagnosticConsoleFd = fd;
      return;
    }
  }

  int fd = _real_dup2(STDERR_FILENO, PROTECTED_STDERR_FD);
  if (fd == PROTECTED_STDERR_FD) {
    gDiagnosticConsoleFd = fd;
    return;
  }

  fd = openProtectedFile("/dev/null", PROTECTED_STDERR_FD);
  gDiagnosticConsoleFd = fd == -1 ? STDERR_FILENO : fd;
}

bool
setDiagnosticLogFile(const char *path)
{
  if (gDiagnosticLogFd != -1) {
    _real_close(gDiagnosticLogFd);
    gDiagnosticLogFd = -1;
  }

  if (path == nullptr || path[0] == '\0') {
    return true;
  }

  gDiagnosticLogFd = openLogFileWithFallbacks(path);
  return gDiagnosticLogFd != -1;
}

void
closeDiagnosticConsole()
{
  if (gDiagnosticConsoleFd != -1 && gDiagnosticConsoleFd != STDERR_FILENO) {
    _real_close(gDiagnosticConsoleFd);
  }
  gDiagnosticConsoleFd = -1;
}

void
emitDiagnostic(const char *data, size_t length)
{
  if (gDiagnosticConsoleFd != -1) {
    writeAllNoAlloc(gDiagnosticConsoleFd, data, length);
  }
  if (gDiagnosticLogFd != -1 && gDiagnosticLogFd != gDiagnosticConsoleFd) {
    writeAllNoAlloc(gDiagnosticLogFd, data, length);
  }
}

} // namespace dmtcp
