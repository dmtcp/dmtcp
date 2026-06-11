#include "util_assert.h"

#include <atomic>
#include <cstring>
#include <unistd.h>

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
LogOverride gLogOverrides[kMaxLogOverrides];

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

} // namespace dmtcp
