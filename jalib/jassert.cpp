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
#include <limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <cxxabi.h>  /* For backtrace() */
#include <execinfo.h>  /* For backtrace() */
#include <fstream>
#include <iomanip>

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
static char logFilePath[PATH_MAX] = {0};

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

jassert_internal::JAssert::JAssert(JAssertType type)
  : JASSERT_CONT_A(*this), JASSERT_CONT_B(*this), _type(type)
{
  if (type == JAssertType::Raw) {
    return;
  }

  ss << JAssertTypeToColor(type) << "[" << jalib::getTimestampStr() << ", "
     << getpid() << ", " << jalib::gettid() << ", " << JAssertTypeToStr(type)
     << "]";
}

jassert_internal::JAssert::~JAssert()
{
  if (_type != JAssertType::Error) {
    writeToConsole(ss.str().c_str());
    writeToLog(ss.str().c_str());
    return;
  }

  Print("    ");
  Print(jalib::Filesystem::GetProgramName());
  Print(": Terminating...\n");

  PrintBacktrace();

  Print("\n");
  Print(clearEscapeStr);
  Print("\n");

  writeToConsole(ss.str().c_str());

  // Do not write proc maps to console.
  // TODO(kaarya): add PrintProcFds();
  PrintProcMaps();
  writeToLog(ss.str().c_str());

  while (getenv("DMTCP_SLEEP_ON_FAILURE"));

  /* Generate core-dump for debugging */
  if (getenv("DMTCP_ABORT_ON_FAILURE")) {
    abort();
  } else {
    _exit(jalib::dmtcp_fail_rc());
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

void
jassert_internal::JAssert::PrintBacktrace()
{
  void *buffer[BT_SIZE];
  int nptrs = backtrace(buffer, BT_SIZE);

  Print("    Backtrace:\n");
  char buf[1024];

  for (int i = 1; i < nptrs; i++) {
    Dl_info info;
    ss << "        " << i << ' ' ;
    if (dladdr1(buffer[i], &info, NULL, 0)) {
      buf[0] = '\0';
      if (info.dli_sname) {
        // int status;
        // size_t buflen = sizeof(buf);
        // char *demangled =
        //   abi::__cxa_demangle(info.dli_sname, buf, &buflen, &status);
        // if (status != 0) {
        //   strncpy(buf, info.dli_sname, sizeof(buf) - 1);
        // }
        strncpy(buf, info.dli_sname, sizeof(buf) - 1);
      }

      ss << (buf[0] ? buf : "") << " in " << info.dli_fname << " ";
    }
    ss << XToHexString(buffer[i]) << "\n";
	}
}

// This routine is called when JASSERT triggers.  Something failed.
// DOES (for further diagnosis):  cp /proc/self/maps $DMTCP_TMPDIR/proc-maps
// WITHOUT malloc or spawning process (could be dangerous in fragile state).
void
jassert_internal::JAssert::PrintProcMaps()
{
  ssize_t count;
  char buf[4096] = {0};
  int fd = jalib::open("/proc/self/maps", O_RDONLY, 0);
  if (fd == -1) {
    return;
  }

  Print("    Memory maps: \n");

  while ((count = jalib::readAll(fd, buf, sizeof(buf) - 1)) > 0) {
    Print(buf);
  }

  Print("\n");

  jalib::close(fd);
}

void
jassert_internal::set_log_file(const dmtcp::string &path)
{
  if (theLogFileFd != -1) {
    jalib::close(theLogFileFd);
    theLogFileFd = -1;
  }
  strncpy(logFilePath, path.c_str(), sizeof(logFilePath) - 1);
}

void jassert_internal::open_log_file()
{
  dmtcp::string path = logFilePath;
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

  // Dump process name, environment, etc.
  if (theLogFileFd != -1) {
    dmtcp::ostringstream a;

    a << "[" << jalib::getTimestampStr() << ", " << getpid() << ", "
      << jalib::gettid() << ", INFO] at " << JASSERT_FILE << ":" << JASSERT_LINE
      << " in " << JASSERT_FUNC << "; REASON='Program: " << Filesystem::GetProgramName()
      << "'\n  Environment:";

    for (size_t i = 0; environ[i] != NULL; i++) {
      a << "\n    " << environ[i] << ";";
    }

    a << "\n";
    jwrite(theLogFileFd, a.str().c_str());
  }
}

void
jassert_internal::JAssert::writeToConsole(const char *str)
{
  if (errConsoleFd != -1) {
    jwrite(errConsoleFd, str);
  }
}

void
jassert_internal::JAssert::writeToLog(const char *str)
{
  // Lazily open log file.
  if (theLogFileFd == -1) {
    if (write(jalib::logFd(), str, 0) != -1) {
      theLogFileFd = jalib::logFd();
    } else if (logFilePath[0] != '\0') {
      open_log_file();
    }
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
