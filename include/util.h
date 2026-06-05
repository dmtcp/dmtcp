/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#ifndef UTIL_H
#define UTIL_H

#include "procmapsarea.h"

#ifndef EXTERNC
# ifdef __cplusplus
#  define EXTERNC extern "C"
# else // ifdef __cplusplus
#  define EXTERNC
# endif // ifdef __cplusplus
#endif // ifndef EXTERNC

typedef char *VA;
#define UTIL_MAX_PATH_LEN 256

#define TIMESPEC_CMP(a, b, CMP)                                  \
  (((a)->tv_sec == (b)->tv_sec) ? ((a)->tv_nsec CMP(b)->tv_nsec) \
                                : ((a)->tv_sec CMP(b)->tv_sec))

#define TIMESPEC_ADD(a, b, result)                   \
  do {                                               \
    (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;    \
    (result)->tv_nsec = (a)->tv_nsec + (b)->tv_nsec; \
    if ((result)->tv_nsec >= 1000 * 1000 * 1000) {   \
      ++(result)->tv_sec;                            \
      (result)->tv_nsec -= 1000 * 1000 * 1000;       \
    }                                                \
  } while (0)

#define TIMESPEC_SUB(a, b, result)                   \
  do {                                               \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;    \
    (result)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec; \
    if ((result)->tv_nsec < 0) {                     \
      --(result)->tv_sec;                            \
      (result)->tv_nsec += 1000 * 1000 * 1000;       \
    }                                                \
  } while (0)

#define MAX(a, b)  ((a) > (b) ? (a) : (b))
#define MIN(a, b)  ((a) < (b) ? (a) : (b))

#define CEIL(a, b) ((a) % (b) ? ((a) + (b) - ((a) % (b))) : (a))

#define DEBUG_POST_RESTART    7
#define DEBUG_PLUGIN_MANAGER  6

EXTERNC void initializeJalib();

EXTERNC int dmtcp_infiniband_enabled(void) __attribute__((weak));
EXTERNC int dmtcp_alloc_enabled(void) __attribute__((weak));
EXTERNC int dmtcp_dl_enabled(void) __attribute__((weak));
EXTERNC int dmtcp_batch_queue_enabled(void) __attribute__((weak));
EXTERNC int dmtcp_modify_env_enabled(void) __attribute__((weak));
EXTERNC int dmtcp_ptrace_enabled(void) __attribute__((weak));
EXTERNC int dmtcp_unique_ckpt_enabled(void) __attribute__((weak));
EXTERNC int dmtcp_pathvirt_enabled(void) __attribute__((weak));
EXTERNC bool dmtcp_svipc_inside_shmdt(void) __attribute__((weak));

/*
 * struct MtcpRestartThreadArg
 *
 * DMTCP requires the virtualTids of the threads being created during
 *  the RESTARTING phase.  We use an MtcpRestartThreadArg structure to pass
 *  the virtualTid of the thread being created from MTCP to DMTCP.
 *
 * actual clone call: clone (fn, child_stack, flags, void *, ...)
 * new clone call   : clone (fn, child_stack, flags,
 *                           (struct MtcpRestartThreadArg *), ...)
 *
 * DMTCP automatically extracts arg from this structure and passes that
 * to the _real_clone call.
 *
 * NOTE: This structure will be moved to a more appropriate place once we have
 * finalized the code in threadlist.cpp.
 */
struct MtcpRestartThreadArg {
  void *arg;
  pid_t virtualTid;
};

#ifdef __cplusplus
# include <charconv>
# include <system_error>
# include <string_view>
# include <type_traits>
# include "dmtcpalloc.h"
namespace dmtcp
{
namespace Util
{
void lockFile(int fd);
void unlockFile(int fd);
int changeFd(int oldfd, int newfd);

bool strStartsWith(const char *str, const char *pattern);
bool strEndsWith(const char *str, const char *pattern);
inline bool strStartsWith(std::string_view str, std::string_view pattern)
{
  return str.starts_with(pattern);
}

inline bool strEndsWith(std::string_view str, std::string_view pattern)
{
  return str.ends_with(pattern);
}

template <typename Integer>
inline bool parseInteger(std::string_view text, Integer *value, int base = 10)
{
  static_assert(std::is_integral_v<Integer> &&
                !std::is_same_v<Integer, bool>);
  if (value == nullptr || text.empty()) {
    return false;
  }

  Integer parsed = 0;
  const char *begin = text.data();
  const char *end = begin + text.size();
  auto result = std::from_chars(begin, end, parsed, base);
  if (result.ec != std::errc() || result.ptr != end) {
    return false;
  }

  *value = parsed;
  return true;
}

inline bool isDecimalDigit(char ch)
{
  return ch >= '0' && ch <= '9';
}

inline bool isAsciiBlank(char ch)
{
  return ch == ' ' || ch == '\t';
}

template <typename Integer>
inline bool parseIntegerPrefix(std::string_view text,
                               Integer *value,
                               size_t *parsedLength)
{
  static_assert(std::is_integral_v<Integer> &&
                !std::is_same_v<Integer, bool>);
  if (value == nullptr || parsedLength == nullptr || text.empty()) {
    return false;
  }

  size_t digitStart = 0;
  if constexpr (std::is_signed_v<Integer>) {
    if (text[0] == '-') {
      digitStart = 1;
    }
  }

  size_t end = digitStart;
  while (end < text.size() && isDecimalDigit(text[end])) {
    ++end;
  }
  if (end == digitStart) {
    return false;
  }

  Integer parsed = 0;
  const char *begin = text.data();
  const char *parseEnd = begin + end;
  auto result = std::from_chars(begin, parseEnd, parsed);
  if (result.ec != std::errc() || result.ptr != parseEnd) {
    return false;
  }

  *value = parsed;
  *parsedLength = end;
  return true;
}

inline bool parseVirtualPidEnv(std::string_view text,
                               pid_t *virtPid,
                               pid_t *realPid,
                               pid_t *virtPpid,
                               pid_t *realPpid)
{
  pid_t values[4] = {};
  size_t offset = 0;

  for (pid_t& value : values) {
    size_t end = text.find(':', offset);
    if (end == std::string_view::npos ||
        !parseInteger(text.substr(offset, end - offset), &value)) {
      return false;
    }
    offset = end + 1;
  }
  // DMTCP_VIRTUAL_PID is updated in place; bytes after the fourth colon can
  // be stale data from an earlier value in the fixed-size environment buffer.

  if (virtPid != nullptr) {
    *virtPid = values[0];
  }
  if (realPid != nullptr) {
    *realPid = values[1];
  }
  if (virtPpid != nullptr) {
    *virtPpid = values[2];
  }
  if (realPpid != nullptr) {
    *realPpid = values[3];
  }
  return true;
}

inline bool parsePortNumber(std::string_view text, int *port)
{
  int parsedPort = 0;
  if (!parseInteger(text, &parsedPort) ||
      parsedPort < 0 ||
      parsedPort > 65535) {
    return false;
  }

  *port = parsedPort;
  return true;
}

inline bool parseNumericFlag(std::string_view text, bool *enabled)
{
  long parsedValue = 0;
  if (enabled == nullptr || !parseInteger(text, &parsedValue)) {
    return false;
  }

  *enabled = parsedValue != 0;
  return true;
}

template <typename Integer>
inline bool parseMeminfoKilobytes(std::string_view text,
                                  std::string_view key,
                                  Integer *kilobytes)
{
  static_assert(std::is_integral_v<Integer> &&
                !std::is_same_v<Integer, bool>);
  if (kilobytes == nullptr || key.empty() || !text.starts_with(key)) {
    return false;
  }

  size_t valueStart = key.size();
  if (valueStart >= text.size() || text[valueStart] != ':') {
    return false;
  }
  ++valueStart;

  while (valueStart < text.size() && isAsciiBlank(text[valueStart])) {
    ++valueStart;
  }

  Integer parsedKilobytes = 0;
  size_t parsedLength = 0;
  if (!parseIntegerPrefix(text.substr(valueStart),
                          &parsedKilobytes,
                          &parsedLength)) {
    return false;
  }

  *kilobytes = parsedKilobytes;
  return true;
}

inline bool parseDottedVersionPrefix(std::string_view text,
                                     int *major,
                                     int *minor)
{
  if (major == nullptr || minor == nullptr) {
    return false;
  }

  size_t dot = text.find('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 >= text.size()) {
    return false;
  }

  size_t minorEnd = dot + 1;
  while (minorEnd < text.size() && isDecimalDigit(text[minorEnd])) {
    ++minorEnd;
  }
  if (minorEnd == dot + 1) {
    return false;
  }

  int parsedMajor = 0;
  int parsedMinor = 0;
  if (!parseInteger(text.substr(0, dot), &parsedMajor) ||
      !parseInteger(text.substr(dot + 1, minorEnd - dot - 1),
                    &parsedMinor)) {
    return false;
  }

  *major = parsedMajor;
  *minor = parsedMinor;
  return true;
}

bool readBooleanEnv(const char *envName, bool defaultValue);

bool isNscdArea(const ProcMapsArea &area);
bool isSysVShmArea(const ProcMapsArea &area);
bool isIBShmArea(const ProcMapsArea &area);

ssize_t writeAll(int fd, const void *buf, size_t count);
ssize_t readAll(int fd, void *buf, size_t count);
ssize_t skipBytes(int fd, size_t count);
ssize_t readAll(const char *path, void *buf, size_t count);

int safeMkdir(const char *pathname, mode_t mode);
int safeSystem(const char *command);

int expandPathname(const char *inpath, char *const outpath, size_t size);
int getInterpreterType(const char *pathname, bool *isElf, bool *is32bitElf);
int elfType(const char *pathname, bool *isElf, bool *is32bitElf);

bool isStaticallyLinked(const char *filename);

void setVirtualPidEnvVar(pid_t virtPid,
                         pid_t realPid,
                         pid_t virtPpid,
                         pid_t realPpid);
void getVirtualPidFromEnvVar(pid_t *virtPid,
                             pid_t *realPid,
                             pid_t *virtPpid,
                             pid_t *realPpid);

bool isScreen(const char *filename);
void setScreenDir();
bool isSetuid(const char *filename);
void freePatchedArgv(void *ptr);
void patchArgvIfSetuid(const char *filename,
                       const char *origArgv[],
                       const char **newArgv[]);

int readLine(int fd, char *buf, int count);


void writeCoordPortToFile(int port, const char *portFile);
char *calcTmpDir(const char *tmpDir);
void initializeLogFile(const char *tmpDir, const char *prefix = "dmtcpworker");

void adjustRlimitStack();

char readDec(int fd, VA *value);
char readHex(int fd, VA *value);
char readChar(int fd);
int readProcMapsLine(int mapsfd, ProcMapsArea *area);
int memProtToOpenFlags(int prot);
bool isValidFd(int fd);
bool isPseudoTty(const char *path);
size_t pageSize();
size_t pageMask();
bool areZeroPages(void *addr, size_t numPages);

char *findExecutable(char *executable, const char *path_env, char *exec_path);
char *getPath(const char *cmd, bool is32bit = false);
char **getDmtcpArgs();
void allowGdbDebug(int currentDebugLevel);

string getTimestampStr();
void replace(char *str, const char *match, const char *replace);
string replace(const string &in, const string &match, const string &replace);
void* mmap_fixed_noreplace_private(void *addr, size_t len, int prot, int flags,
                                   int fd, off_t offset);
} // namespace Util
}
#endif // ifdef __cplusplus
#endif // ifndef UTIL_H
