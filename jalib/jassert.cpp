/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel                                 *
 *   jansel@csail.mit.edu                                                   *
 *                                                                          *
 *   This file is part of the JALIB module of DMTCP (DMTCP:dmtcp/jalib).    *
 *                                                                          *
 *  DMTCP:dmtcp/jalib is free software: you can redistribute it and/or      *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/types.h>
#include <unistd.h>

#include <execinfo.h>  /* For backtrace() */
#include <fstream>

#include "jalib.h"
#include "jassert.h"
#include "jconvert.h"
#include "jfilesystem.h"

#undef JASSERT_CONT_A
#undef JASSERT_CONT_B

using namespace jalib;
int jassert_quiet = 0;

namespace jalib
{
static int theLogFileFd = -1;
static int errConsoleFd = -1;

// With multiple global non-POD (Plain Old Data) objects, the order in which
// their constructors are called is hard to reason about. It is not defined by
// the language specification. However, in this case, we want precise control
// over when the initialization of tmpDir happens. This is important because
// the DMTCP libraries, and the executables that statically link with
// libjalib.a can define their own global C++ objects. (In fact, libdmtcp
// relies on this trick to inject a ckpt thread in every process).
//
// Thus wrapping global C++ objects in functions is the idiomatic way of
// implementing this. This is portable and avoids non-deterministic behavior.
// See: https://isocpp.org/wiki/faq/ctors#static-init-order, and
// https://isocpp.org/wiki/faq/ctors#static-init-order-on-first-use.
//
// The following two functions provide wrappers around static local variables
// and return a reference. Thus, we can use the function on the left-hand side
// (as a lvalue) to modifying the underlying variable.

static dmtcp::string&
tmpDir()
{
  static dmtcp::string *s = NULL;
  if (s == NULL) {
    // Technically, this is a memory leak, but s is static and so it happens
    // only once.
    s = new dmtcp::string();
  }
  return *s;
}

static dmtcp::string&
uniquePidStr()
{
  static dmtcp::string *s = NULL;
  if (s == NULL) {
    // Technically, this is a memory leak, but s is static and so it happens
    // only once.
    s = new dmtcp::string();
  }
  return *s;
}

static int
jwrite(int fd, const char *str)
{
  jalib::writeAll(fd, str, strlen(str));

  return strlen(str);
}
}

jassert_internal::JAssert&
jassert_internal::JAssert::Text(const char *msg)
{
  Print("Message: ");
  Print(msg);
  Print("\n");
  return *this;
}

jassert_internal::JAssert::JAssert(bool exitWhenDone)
  : JASSERT_CONT_A(*this)
  , JASSERT_CONT_B(*this)
  , _exitWhenDone(exitWhenDone)
{}

jassert_internal::JAssert::~JAssert()
{
  if (_exitWhenDone) {
    Print(jalib::Filesystem::GetProgramName());
    Print(" (");
    Print(getpid());
    Print("): Terminating...\n");
    jassert_safe_print(ss.str().c_str());
    ss.str("");

    // while(1) sleep(1);
#ifdef LOGGING
    jbacktrace();
#endif // ifdef LOGGING
  }

  if (!ss.str().empty()) {
    jassert_safe_print(ss.str().c_str());
  }

  if (_exitWhenDone) {
    /* Generate core-dump for debugging */
    if (getenv("DMTCP_ABORT_ON_FAILED_ASSERT")) {
      abort();
    } else {
      _exit(jalib::dmtcp_fail_rc());
    }
  }
}

const char *
jassert_internal::jassert_basename(const char *str)
{
  for (const char *c = str; c[0] != '\0' && c[1] != '\0'; ++c) {
    if (c[0] == '/') {
      str = c + 1;
    }
  }
  return str;
}

static int
_open_log_safe(const char *filename, int protectedFd)
{
  // open file
  int tfd = jalib::open(filename, O_WRONLY | O_APPEND | O_CREAT /*| O_SYNC*/,
                        S_IRUSR | S_IWUSR);

  if (tfd == -1) {
    return -1;
  }

  // change fd to 827 (jalib::logFd() -- PFD(6))
  int nfd = jalib::dup2(tfd, protectedFd);
  if (tfd != nfd) {
    jalib::close(tfd);
  }

  return nfd;
}

static int
_open_log_safe(const dmtcp::string &s, int protectedFd)
{
  return _open_log_safe(s.c_str(), protectedFd);
}

static dmtcp::string&
theLogFilePath() {
  static dmtcp::string *s = NULL;
  if (s == NULL) {
    // Technically, this is a memory leak, but s is static and so it happens
    // only once.
    s = new dmtcp::string();
  }
  return *s;
}

void
jassert_internal::jassert_init()
{
  // Check if we already have a valid stderrFd
  if (jalib::dup2(jalib::stderrFd(), jalib::stderrFd()) != jalib::stderrFd()) {
    const char *errpath = getenv("JALIB_STDERR_PATH");

    if (errpath != NULL) {
      errConsoleFd = _open_log_safe(errpath, jalib::stderrFd());
    } else {
      /* TODO:
       * If stderr is a pseudo terminal for IPC between parent/child processes,
       * this logic fails and JASSERT may write data to FD 2 (stderr).  This
       * will cause problems in programs that use FD 2 (stderr) for non stderr
       * purposes.
       */
      dmtcp::string stderrProcPath, stderrDevice;
      stderrProcPath = "/proc/self/fd/" + jalib::XToString(fileno(stderr));
      stderrDevice = jalib::Filesystem::ResolveSymlink(stderrProcPath);

      if (stderrDevice.length() > 0
          && jalib::Filesystem::FileExists(stderrDevice)) {
        errConsoleFd = jalib::dup2(fileno(stderr), jalib::stderrFd());
      } else {
        errConsoleFd = _open_log_safe("/dev/null", jalib::stderrFd());
      }
    }

    if (errConsoleFd == -1) {
      jwrite(fileno(stderr),
             "dmtcp: cannot open output channel for error logging\n");
    }
  } else {
    errConsoleFd = jalib::stderrFd();
  }
}

void
jassert_internal::close_stderr()
{
  jalib::close(errConsoleFd);

  errConsoleFd = -1;
}

static const dmtcp::string
writeJbacktraceMsg()
{
  dmtcp::ostringstream o;
  dmtcp::string thisProgram = "libdmtcp.so";

  if (jalib::Filesystem::GetProgramName() == "dmtcp_coordinator") {
    thisProgram = "dmtcp_coordinator";
  }
  if (jalib::Filesystem::GetProgramName() == "dmtcp_checkpoint") {
    thisProgram = "dmtcp_launch";
  }
  if (jalib::Filesystem::GetProgramName() == "dmtcp_restart") {
    thisProgram = "dmtcp_restart";
  }
  dmtcp::string msg = dmtcp::string("")
    + "\n   *** Stack trace is available ***\n"                         \
      "   Try using:  util/dmtcp_backtrace.py  (found in DMTCP_ROOT)\n" \
      "   Try the following command line:\n"                            \
      "     ";
  o << msg << "util/dmtcp_backtrace.py" << " "
    << thisProgram << " "
    << tmpDir() << "/backtrace."
    << uniquePidStr() << " ";

  // Weird bug:  If we don't start a new statement here,
  // then the second call to jalib::uniquePidStr() returns just 831.
  o << tmpDir() << "/proc-maps."
    << uniquePidStr()
    << "\n   (For further help, try:  util/dmtcp_backtrace.py --help)\n";
  return o.str();
}

static void
writeBacktrace()
{
  void *buffer[BT_SIZE];
  int nptrs = backtrace(buffer, BT_SIZE);
  dmtcp::string path = tmpDir() + "/backtrace." + uniquePidStr();
  int fd = jalib::open(
      path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);

  if (fd != -1) {
    backtrace_symbols_fd(buffer, nptrs, fd);
    jalib::close(fd);
  }
}

// This routine is called when JASSERT triggers.  Something failed.
// DOES (for further diagnosis):  cp /proc/self/maps $DMTCP_TMPDIR/proc-maps
// WITHOUT malloc or spawning process (could be dangerous in fragile state).
static void
writeProcMaps()
{
  char mapsBuf[50000];
  int count;
  int fd = jalib::open("/proc/self/maps", O_RDONLY, 0);

  if (fd == -1) {
    return;
  }
  count = jalib::readAll(fd, mapsBuf, sizeof(mapsBuf) - 1);
  jalib::close(fd);

  dmtcp::string path = tmpDir() + "/proc-maps." + uniquePidStr();
  fd =
    jalib::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  if (fd == -1) {
    return;
  }
  count = jalib::writeAll(fd, mapsBuf, count);
  jalib::close(fd);
}

jassert_internal::JAssert&
jassert_internal::JAssert::jbacktrace()
{
  writeBacktrace();
  writeProcMaps();

  // This prints to stdout and to jalib::logFd()
  Print(writeJbacktraceMsg());
  return *this;  // Needed as part of JASSERT macro
}

void
jassert_internal::set_log_file(const dmtcp::string &path,
                               const dmtcp::string _tmpDir,
                               const dmtcp::string &_uniquePidStr)
{
  tmpDir() = _tmpDir;
  uniquePidStr() = _uniquePidStr;

  theLogFilePath() = path;
  if (theLogFileFd != -1) {
    jalib::close(theLogFileFd);
  }
  theLogFileFd = -1;
  if (path.length() > 0) {
    theLogFileFd = _open_log_safe(path, jalib::logFd());
    if (theLogFileFd == -1) {
      theLogFileFd = _open_log_safe(path + "_2", jalib::logFd());
    }
    if (theLogFileFd == -1) {
      theLogFileFd = _open_log_safe(path + "_3", jalib::logFd());
    }
    if (theLogFileFd == -1) {
      theLogFileFd = _open_log_safe(path + "_4", jalib::logFd());
    }
    if (theLogFileFd == -1) {
      theLogFileFd = _open_log_safe(path + "_5", jalib::logFd());
    }
  }
}

void
jassert_internal::jassert_safe_print(const char *str)
{
  if (errConsoleFd != -1) {
    jwrite(errConsoleFd, str);
  }

  if (theLogFileFd != -1) {
    int rv = jwrite(theLogFileFd, str);

    if (rv < 0 && theLogFileFd == EBADF) {
      if (errConsoleFd != -1) {
        jwrite(errConsoleFd, "JASSERT: failed to write to log file.\n");
      }
      theLogFileFd = -1;
    }
  }
}
