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

#include "jassert.h"
#include "jfilesystem.h"
#include <sys/types.h>
#include <unistd.h>
#include "jconvert.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>

#include <fstream>
#include <execinfo.h>  /* For backtrace() */

// Is there a cleaner way to get information from rest of DMTCP?
#include "../src/dmtcpmodule.h"
#include "../src/util.h"
#include "../src/protectedfds.h"

#undef JASSERT_CONT_A
#undef JASSERT_CONT_B

// This macro is also defined in ../src/constants.h and should always be kept
// in sync with that.
#define LIBC_FILENAME "libc.so.6"

#ifndef DMTCP
#  define DECORATE_FN(fn) ::fn
#else
#  include "syscallwrappers.h"
#  define DECORATE_FN(fn) ::_real_ ## fn
#endif

#define pthread_mutex_lock _real_pthread_mutex_lock
#define pthread_mutex_trylock _real_pthread_mutex_trylock
#define pthread_mutex_unlock _real_pthread_mutex_unlock

int jassert_quiet = 0;

static int theLogFileFd = -1;
static int errConsoleFd = -1;


#define DUP_STDERR_FD PROTECTED_STDERR_FD
#define DUP_LOG_FD    PROTECTED_JASSERTLOG_FD

static int jwrite(int fd, const char *str)
{
  return Util::writeAll(fd, str, strlen(str));
#if 0
  ssize_t offs, rc;
  ssize_t size = strlen(str);

  for (offs = 0; offs < size;) {
    rc = write (fd, str + offs, size - offs);
    if (rc == -1 && errno != EINTR && errno != EAGAIN)
      return rc;
    else if (rc > 0)
      offs += rc;
  }
  return size;
#endif
}

int jassert_internal::jassert_console_fd()
{
  //make sure stream is open
  jassert_safe_print ( "" );
  return DUP_STDERR_FD;
}

void jassert_internal::jassert_set_console_fd(int fd)
{
  errConsoleFd = fd;
}

jassert_internal::JAssert& jassert_internal::JAssert::Text ( const char* msg )
{
  Print ( "Message: " );
  Print ( msg );
  Print ( "\n" );
  return *this;
}

static pthread_mutex_t logLock = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;

bool jassert_internal::lockLog()
{
  int retVal = pthread_mutex_lock(&logLock);
  if (retVal != 0) {
    perror ( "jassert_internal::lockLog: Error acquiring mutex");
  }
  return retVal == 0;
}

void jassert_internal::unlockLog()
{
  int retVal = pthread_mutex_unlock(&logLock);
  if (retVal != 0) {
    perror ( "jassert_internal::unlockLog: Error releasing mutex");
  }
}

jassert_internal::JAssert::JAssert ( bool exitWhenDone )
    : JASSERT_CONT_A ( *this )
    , JASSERT_CONT_B ( *this )
    , _exitWhenDone ( exitWhenDone )
{
  _logLockAcquired = jassert_internal::lockLog();
}

jassert_internal::JAssert::~JAssert()
{
  if ( _exitWhenDone ) {
    Print ( jalib::Filesystem::GetProgramName() );
    Print ( " (" );
    Print ( getpid() );
    Print ( "): Terminating...\n" );
    jassert_safe_print ( ss.str().c_str() );
    ss.str("");
#ifdef DEBUG
    jbacktrace();
#endif
  }

  if (!ss.str().empty())
    jassert_safe_print ( ss.str().c_str() );
  if ( _logLockAcquired )
    jassert_internal::unlockLog();

  if ( _exitWhenDone ) {
    _exit ( 1 );
  }
}

const char* jassert_internal::jassert_basename ( const char* str )
{
  for ( const char* c = str; c[0] != '\0' && c[1] !='\0' ; ++c ) {
    if ( c[0]=='/' )
      str=c+1;
  }
  return str;
}

static int _open_log_safe ( const char* filename, int protectedFd )
{
  //open file
  int tfd = _real_open ( filename, O_WRONLY | O_APPEND | O_CREAT /*| O_SYNC*/,
                                   S_IRUSR | S_IWUSR );
  if (tfd == -1) return -1;
  //change fd to 827 (DUP_LOG_FD -- PFD(6))
  int nfd = dup2 ( tfd, protectedFd );
  if (tfd != nfd) {
    close ( tfd );
  }

  return nfd;
}

static int _open_log_safe ( const jalib::string& s, int protectedFd )
{
  return _open_log_safe ( s.c_str(), protectedFd );
}

static jalib::string& theLogFilePath() {static jalib::string s;return s;};

void jassert_internal::jassert_init ( const jalib::string& f )
{
#ifdef DEBUG
  JASSERT_SET_LOGFILE(f);
#endif
  jassert_safe_print("");
}

const jalib::string writeJbacktraceMsg() {
  dmtcp::ostringstream o;
  jalib::string msg = jalib::string("")
    + "\n   *** Stack trace is available ***\n" \
    "   Try using:  utils/dmtcp_backtrace.py  (found in DMTCP_ROOT)\n" \
    "   Try the following command line:\n" \
    "     utils/dmtcp_backtrace.py dmtcphijack.so ";
  o << msg << dmtcp_get_tmpdir() << "/backtrace."
    << dmtcp_get_uniquepid_str() << " "
    << dmtcp_get_tmpdir() << "/proc-maps."
    << dmtcp_get_uniquepid_str()
    << "\n   (For further help, try:  utils/dmtcp_backtrace.py --help)\n";
  return o.str();
}

void writeBacktrace() {
  void *buffer[BT_SIZE];
  int nptrs = backtrace(buffer, BT_SIZE);
  dmtcp::ostringstream o;
  o << dmtcp_get_tmpdir() << "/backtrace."
    << dmtcp_get_uniquepid_str();
  int fd = _real_open(o.str().c_str(), O_WRONLY|O_CREAT|O_TRUNC,
                      S_IRUSR|S_IWUSR);
  if (fd != -1) {
    backtrace_symbols_fd( buffer, nptrs, fd );
    close(fd);
  }
}

// This routine is called when JASSERT triggers.  Something failed.
// DOES (for further diagnosis):  cp /proc/self/maps $DMTCP_TMPDIR/proc-maps
// WITHOUT malloc or spawning process (could be dangerous in fragile state).
void writeProcMaps() {
  char mapsBuf[50000];
  int  count;
  int fd = _real_open("/proc/self/maps", O_RDONLY, 0);
  if (fd == -1) return;
  count = Util::readAll(fd, mapsBuf, sizeof(mapsBuf) - 1);
  close(fd);

  dmtcp::ostringstream o;
  o << dmtcp_get_tmpdir() << "/proc-maps."
    << dmtcp_get_uniquepid_str();
  fd = open(o.str().c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR|S_IWUSR);
  if (fd == -1) return;
  count = Util::writeAll(fd, mapsBuf, count);
  close(fd);
}

jassert_internal::JAssert& jassert_internal::JAssert::jbacktrace ()
{
  writeBacktrace();
  writeProcMaps();
  // This goes to stdout.  Could also print to DUP_LOG_FD
  Print( writeJbacktraceMsg() );
  return *this;  // Needed as part of JASSERT macro
}

void jassert_internal::reset_on_fork ( )
{
  pthread_mutex_t newLock = PTHREAD_MUTEX_INITIALIZER;
  logLock = newLock;
}

void jassert_internal::set_log_file ( const jalib::string& path )
{
  theLogFilePath() = path;
  if ( theLogFileFd != -1 ) close ( theLogFileFd );
  theLogFileFd = -1;
  if ( path.length() > 0 )
  {
    theLogFileFd = _open_log_safe ( path, DUP_LOG_FD );
    if ( theLogFileFd == -1 )
      theLogFileFd = _open_log_safe ( path + "_2", DUP_LOG_FD );
    if ( theLogFileFd == -1 )
      theLogFileFd = _open_log_safe ( path + "_3", DUP_LOG_FD );
    if ( theLogFileFd == -1 )
      theLogFileFd = _open_log_safe ( path + "_4", DUP_LOG_FD );
    if ( theLogFileFd == -1 )
      theLogFileFd = _open_log_safe ( path + "_5", DUP_LOG_FD );
  }
}

static int _initJassertOutputDevices()
{
  pthread_mutex_t newLock = PTHREAD_MUTEX_INITIALIZER;
  logLock = newLock;

  const char* errpath = getenv ( "JALIB_STDERR_PATH" );

#ifdef DEBUG
  if ( errpath != NULL && theLogFileFd == -1 ) {
    JASSERT_SET_LOGFILE ( jalib::XToString(getenv("DMTCP_TMPDIR"))
                          + "/jassertlog." + jalib::XToString ( getpid() ) );
  }
#endif

  if ( errpath != NULL )
    errConsoleFd = _open_log_safe ( errpath, DUP_STDERR_FD );
  else
    errConsoleFd = dup2 ( fileno ( stderr ), DUP_STDERR_FD );

  if( errConsoleFd == -1 ) {
    jwrite ( fileno (stderr ), "dmtcp: cannot open output channel for error logging\n");
    return false;
  }
  return true;
}

void jassert_internal::jassert_safe_print ( const char* str )
{
  static bool useErrorConsole = _initJassertOutputDevices();

  if ( useErrorConsole )
    jwrite ( errConsoleFd, str );

  if ( theLogFileFd != -1 ) {
    int rv = jwrite ( theLogFileFd, str );

    if ( rv < 0 ) {
      if ( useErrorConsole ) {
        jwrite ( errConsoleFd, "JASSERT: write failed, reopening log file.\n" );
      }
      JASSERT_SET_LOGFILE ( theLogFilePath() );
      if ( theLogFileFd != -1 ) {
        jwrite ( theLogFileFd, "JASSERT: write failed, reopened log file:\n");
        jwrite ( theLogFileFd, str );
      }
    }
  }
}
