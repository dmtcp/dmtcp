// SPDX-License-Identifier: LGPL-3.0-or-later
/* Optional read-only file references. No allocation in the checkpoint writer. */
#ifndef DMTCP_READONLY_REFS_H
#define DMTCP_READONLY_REFS_H

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"

namespace dmtcp {
namespace ReadOnlyRefs {

inline bool enabled()
{
  const char *value = getenv(ENV_VAR_EXTERNALIZE_RO);
  return value != NULL && strcmp(value, "1") == 0;
}

// Match one byte against ?, a bracket class, or a literal. As with the
// converter's original rules, backslash is literal and * may cross '/'.
inline bool matchOne(const char **pattern, unsigned char value)
{
  const char *p = *pattern;
  if (*p == '?') { *pattern = p + 1; return true; }
  if (*p == '[') {
    const char *first = p + 1;
    bool negate = *first == '!';
    if (negate) ++first;
    const char *end = strchr(first + (*first == ']'), ']');
    if (end != NULL) {
      bool found = false;
      while (first < end) {
        unsigned char low = *first++;
        if (first + 1 < end && *first == '-') {
          ++first;
          unsigned char high = *first++;
          found = found || (value >= low && value <= high);
        } else {
          found = found || value == low;
        }
      }
      *pattern = end + 1;
      return found != negate;
    }
  }
  if (*p != '\0' && (unsigned char)*p == value) {
    *pattern = p + 1;
    return true;
  }
  return false;
}

inline bool glob(const char *pattern, const char *name)
{
  const char *star = NULL;
  const char *retry = NULL;
  while (*name) {
    if (*pattern == '*') {
      star = ++pattern;
      retry = name;
    } else if (matchOne(&pattern, (unsigned char)*name)) {
      ++name;
    } else if (star != NULL) {
      pattern = star;
      name = ++retry;
    } else {
      return false;
    }
  }
  while (*pattern == '*') ++pattern;
  return *pattern == '\0';
}

inline bool matches(const char *name, const char *list)
{
  if (list == NULL) return false;
  while (*list) {
    const char *end = strchr(list, '\n');
    size_t length = end == NULL ? strlen(list) : (size_t)(end - list);
    if (length > 0 && length < PATH_MAX) {
      char pattern[PATH_MAX];
      memcpy(pattern, list, length);
      pattern[length] = '\0';
      // Literal directories include descendants, but not sibling prefixes.
      if (glob(pattern, name)) return true;
      if (strpbrk(pattern, "*?[") == NULL) {
        while (length > 1 && pattern[length - 1] == '/') --length;
        if ((length == 1 && pattern[0] == '/') ||
            (strncmp(name, pattern, length) == 0 && name[length] == '/')) {
          return true;
        }
      }
    }
    if (end == NULL) break;
    list = end + 1;
  }
  return false;
}

inline bool selected(const char *name)
{
  if (!enabled() || name[0] != '/') return false;
  const char *base = strrchr(name, '/');
  bool library = strstr(base == NULL ? name : base + 1, ".so") != NULL;
  return (library || matches(name, getenv(ENV_VAR_RO_INCLUDE))) &&
         !matches(name, getenv(ENV_VAR_RO_EXCLUDE));
}

} // namespace ReadOnlyRefs
} // namespace dmtcp
#endif
