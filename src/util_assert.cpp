#include "util_assert.h"

#include <atomic>
#include <fcntl.h>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

#include "protectedfds.h"
#include "syscallwrappers.h"

namespace dmtcp {
namespace {

constexpr size_t kMaxLogOverrides = 32;
constexpr size_t kMaxLogOverrideKey = 128;

struct LogOverride {
  bool isFile;
  char key[kMaxLogOverrideKey];
  LogLevel level;
};

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

class LoggerState {
 public:
  void setLevel(LogLevel level)
  {
    logLevel_.store(static_cast<int>(level), std::memory_order_relaxed);
  }

  bool setOverrides(std::string_view overrides)
  {
    overrides = trim(overrides);
    if (overrides.empty()) {
      overrideCount_.store(0, std::memory_order_relaxed);
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
      overrides_[i] = parsed[i];
    }
    overrideCount_.store(parsedCount, std::memory_order_relaxed);
    return true;
  }

  bool enabled(LogLevel level,
               std::string_view component,
               std::string_view file) const
  {
    if (level == LogLevel::Error) {
      return true;
    }

    LogLevel effectiveLevel =
      static_cast<LogLevel>(logLevel_.load(std::memory_order_relaxed));
    if (overrideCount_.load(std::memory_order_relaxed) > 0) {
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

  void initializeConsole(const char *stderrPath)
  {
    if (_real_dup2(PROTECTED_STDERR_FD, PROTECTED_STDERR_FD) ==
        PROTECTED_STDERR_FD) {
      consoleFd_ = PROTECTED_STDERR_FD;
      return;
    }

    if (stderrPath != nullptr && stderrPath[0] != '\0') {
      int fd = openProtectedFile(stderrPath, PROTECTED_STDERR_FD);
      if (fd != -1) {
        consoleFd_ = fd;
        return;
      }
    }

    int fd = _real_dup2(STDERR_FILENO, PROTECTED_STDERR_FD);
    if (fd == PROTECTED_STDERR_FD) {
      consoleFd_ = fd;
      return;
    }

    fd = openProtectedFile("/dev/null", PROTECTED_STDERR_FD);
    consoleFd_ = fd == -1 ? STDERR_FILENO : fd;
  }

  bool setFile(const char *path)
  {
    if (fileFd_ != -1) {
      _real_close(fileFd_);
      fileFd_ = -1;
    }

    if (path == nullptr || path[0] == '\0') {
      return true;
    }

    fileFd_ = openProtectedFile(path, PROTECTED_LOG_FD);
    return fileFd_ != -1;
  }

  void closeConsole()
  {
    if (consoleFd_ != -1 && consoleFd_ != STDERR_FILENO) {
      _real_close(consoleFd_);
    }
    consoleFd_ = -1;
  }

  void emit(const char *data, size_t length) const
  {
    if (consoleFd_ != -1) {
      writeAllNoAlloc(consoleFd_, data, length);
    }
    if (fileFd_ != -1 && fileFd_ != consoleFd_) {
      writeAllNoAlloc(fileFd_, data, length);
    }
  }

 private:
  static bool parseOverrideEntry(std::string_view entry,
                                 LogOverride *override)
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
    if (key.starts_with("file:")) {
      override->isFile = true;
      key.remove_prefix(sizeof("file:") - 1);
    }

    key = trim(key);
    if (key.empty() || key.size() >= sizeof(override->key)) {
      return false;
    }
    std::memcpy(override->key, key.data(), key.size());
    override->key[key.size()] = '\0';
    override->level = level;
    return true;
  }

  static int openProtectedFile(const char *path, int protectedFd)
  {
    int fd = _real_open(path,
                        O_WRONLY | O_APPEND | O_CREAT,
                        S_IRUSR | S_IWUSR);
    if (fd == -1) {
      return -1;
    }

    int newFd = _real_dup2(fd, protectedFd);
    if (fd != newFd) {
      _real_close(fd);
    }
    return newFd;
  }

  bool findFileOverride(std::string_view file, LogLevel *level) const
  {
    const int count = overrideCount_.load(std::memory_order_relaxed);
    for (int i = count - 1; i >= 0; --i) {
      const LogOverride& override = overrides_[i];
      if (override.isFile && file.ends_with(override.key)) {
        *level = override.level;
        return true;
      }
    }
    return false;
  }

  bool findComponentOverride(std::string_view component,
                             LogLevel *level) const
  {
    const int count = overrideCount_.load(std::memory_order_relaxed);
    for (int i = count - 1; i >= 0; --i) {
      const LogOverride& override = overrides_[i];
      if (!override.isFile && component == override.key) {
        *level = override.level;
        return true;
      }
    }
    return false;
  }

  std::atomic<int> logLevel_{static_cast<int>(LogLevel::Note)};
  std::atomic<int> overrideCount_{0};
  // Log override and log fd state is configured during single-threaded
  // process initialization. Runtime logging reads it without synchronization.
  // Revisit this invariant before adding runtime log reconfiguration.
  LogOverride overrides_[kMaxLogOverrides];
  int consoleFd_{kLogFd};
  int fileFd_{-1};
};

LoggerState gLogger;

} // namespace

void
setLogLevel(LogLevel level)
{
  gLogger.setLevel(level);
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
  return gLogger.setOverrides(overrides);
}

bool
logEnabled(LogLevel level,
           std::string_view component,
           std::string_view file)
{
  return gLogger.enabled(level, component, file);
}

void
initializeLogConsole(const char *stderrPath)
{
  gLogger.initializeConsole(stderrPath);
}

bool
setLogFile(const char *path)
{
  return gLogger.setFile(path);
}

void
closeLogConsole()
{
  gLogger.closeConsole();
}

void
emitLogMessage(const char *data, size_t length)
{
  gLogger.emit(data, length);
}

} // namespace dmtcp
