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
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>

#include <fstream>
#include <execinfo.h>  /* For backtrace() */

// Needed for dmtcp::UniquePid::getTmpDir()
// Is there a cleaner way to get information from rest of DMTCP?
#include "../src/uniquepid.h"

#undef JASSERT_CONT_A
#undef JASSERT_CONT_B

// This macro is also defined in ../src/constants.h and should always be kept
// in sync with that.
#define LIBC_FILENAME "libc.so.6"

int jassert_quiet = 0;

/*
   The values of DUP_STDERR_FD and DUP_LOG_FD correspond to the values of
   PFD(5) and PFD(6) in protectedfds.h. They should always be kept in sync.
*/
static const int DUP_STDERR_FD = 825; // PFD(5)
static const int DUP_LOG_FD    = 826; // PFD(6)

// DMTCP provides a wrapper for open/fopen. We don't want to go to that wrapper
// and so we call open() directly from libc. This implementation follows the
// one used in ../src/syscallsreal.c
static int _real_open ( const char *pathname, int flags, mode_t mode )
{
  static void* handle = NULL;
  if ( handle == NULL && ( handle = dlopen ( LIBC_FILENAME,RTLD_NOW ) ) == NULL )
  {
    fprintf ( stderr, "dmtcp: get_libc_symbol: ERROR in dlopen: %s \n",
              dlerror() );
    abort();
  }

  typedef int ( *funcptr ) (const char*, int, mode_t);
  funcptr openFuncptr = NULL;
  openFuncptr = (funcptr)dlsym ( handle, "open" );
  if ( openFuncptr == NULL )
  {
    fprintf ( stderr, "dmtcp: get_libc_symbol: ERROR in dlsym: %s \n",
              dlerror() );
    abort();
  }
  return (*openFuncptr)(pathname, flags, mode);
}

static int jwrite(FILE *stream, const char *str)
{
#ifndef JASSERT_USE_FPRINTF
  ssize_t offs, rc=-1;
  ssize_t size = strlen(str);
  int fd = fileno(stream);

  if (fd != -1) {
    for (offs = 0; offs < size; offs += rc) {
      rc = TEMP_FAILURE_RETRY(write (fd, str + offs, size - offs));
      if (rc <= 0) break;
    }
  }
  return rc;
#else
  return fprintf( stream, "%s", str );
#endif
}

int jassert_internal::jassert_console_fd()
{
  //make sure stream is open
  jassert_safe_print ( "" );
  return DUP_STDERR_FD;
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
  if ( _logLockAcquired )
    jassert_internal::unlockLog();

  if ( _exitWhenDone )
  {
    Print ( jalib::Filesystem::GetProgramName() );
    Print ( " (" );
    Print ( getpid() );
    Print ( "): Terminating...\n" );
    _exit ( 1 );
  }
}

const char* jassert_internal::jassert_basename ( const char* str )
{
  for ( const char* c = str; c[0] != '\0' && c[1] !='\0' ; ++c )
    if ( c[0]=='/' )
      str=c+1;
  return str;
}

// std::ostream& jassert_internal::jassert_output_stream(){
//     return std::cerr;
// }


static int _fopen_log_safe ( const char* filename, int protectedFd )
{
  //open file
  int tfd = _real_open ( filename, O_WRONLY | O_APPEND | O_CREAT /*| O_SYNC*/,
                                   S_IRUSR | S_IWUSR );
  if ( tfd < 0 ) return -1;
  //change fd to 827 (DUP_LOG_FD -- PFD(6))
  int nfd = dup2 ( tfd, protectedFd );
  close ( tfd );
  // Previously used fdopen() to return a newly allocated FILE stream.
  // Removed use of fdopen() to avoid modifiying the user malloc arena.
  return nfd;
}

static int _fopen_log_safe ( const jalib::string& s, int protectedFd )
{
  return _fopen_log_safe ( s.c_str(), protectedFd );
}


static int theLogFile = -1;

static jalib::string& theLogFilePath() {static jalib::string s;return s;};

void jassert_internal::jassert_init ( const jalib::string& f )
{
#ifdef DEBUG
  set_log_file(f);
#endif
  jassert_safe_print("");
}

const jalib::string writeJbacktraceMsg() {
  jalib::string msg = jalib::string("")
    + "   *** Stack trace is available ***\n" \
    "   Execute:  .../utils/dmtcp_backtrace.py\n" \
    "   For usage:  .../utils/dmtcp_backtrace.py --help\n" \
    "   Files saved: ";
  msg += dmtcp::UniquePid::getTmpDir()
                          + "/backtrace." + jalib::XToString ( getpid() );
  msg += ", ";
  msg += dmtcp::UniquePid::getTmpDir()
                          + "/proc-maps." + jalib::XToString ( getpid() );
  msg += "\n";
  return msg;
}

void writeBacktrace() {
  void *buffer[BT_SIZE];
  int nptrs = backtrace(buffer, BT_SIZE);
  jalib::string backtrace = dmtcp::UniquePid::getTmpDir()
                          + "/backtrace." + jalib::XToString ( getpid() );
  int fd = open(backtrace.c_str(), O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
  if (fd != -1) {
    backtrace_symbols_fd( buffer, nptrs, fd );
    close(fd);
    jalib::string lnk = dmtcp::UniquePid::getTmpDir() + "/backtrace";
    unlink(lnk.c_str());  // just in case it had previously been created.
    symlink(backtrace.c_str(), lnk.c_str());
  }
}

// DOES:  cp /proc/self/maps $DMTCP_TMPDIR/proc-maps
// But it could be dangerous to spawn a process in fragile state of JASSERT.
void writeProcMaps() {
  char mapsBuf[50000];
  int rc, count, total;
  int fd = open("/proc/self/maps", O_RDONLY);
  while ((rc = read(fd, mapsBuf+count, sizeof(mapsBuf)-count)) != 0) {
    if (rc == -1 && errno != EAGAIN && errno != EINTR)
      break;
    else
      count += rc;
  }
  close(fd);
  jalib::string procMaps = dmtcp::UniquePid::getTmpDir()
                          + "/proc-maps." + jalib::XToString ( getpid() );
  fd = open(procMaps.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR|S_IWUSR);
  if (fd != -1) {
    total = count;
    count = 0;
    while (total>count && (rc = write(fd, mapsBuf+count, total-count)) != 0) {
      if (rc == -1 && errno != EAGAIN && errno != EINTR)
        break;
      else
        count += rc;
    }
    close(fd);
    jalib::string lnk = dmtcp::UniquePid::getTmpDir() + "/proc-maps";
    unlink(lnk.c_str());  // just in case it had previously been created.
    symlink(procMaps.c_str(), lnk.c_str());
  }
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
  if ( theLogFile != -1 ) close ( theLogFile );
  theLogFile = -1;
  if ( path.length() > 0 )
  {
    theLogFile = _fopen_log_safe ( path, DUP_LOG_FD );
    if ( theLogFile == -1 )
      theLogFile = _fopen_log_safe ( path + "_2",DUP_LOG_FD );
    if ( theLogFile == -1 )
      theLogFile = _fopen_log_safe ( path + "_3",DUP_LOG_FD );
    if ( theLogFile == -1 )
      theLogFile = _fopen_log_safe ( path + "_4",DUP_LOG_FD );
    if ( theLogFile == -1 )
      theLogFile = _fopen_log_safe ( path + "_5",DUP_LOG_FD );
  }
}

static int _initJassertOutputDevices()
{
  pthread_mutex_t newLock = PTHREAD_MUTEX_INITIALIZER;
  logLock = newLock;

  const char* errpath = getenv ( "JALIB_STDERR_PATH" );

  if ( errpath != NULL )
    return _fopen_log_safe ( errpath, DUP_STDERR_FD );
  else
    return dup2 ( fileno ( stderr ), DUP_STDERR_FD );
}

static ssize_t writeall(int fd, const void *buf, size_t count) {
  ssize_t cum_count = 0;
  while (cum_count < count) {
    ssize_t rc = write(fd, (char *)buf+cum_count, count-cum_count);
    if (rc == -1 && errno != EAGAIN && errno != EINTR)
      break;  /* Give up; bad error */
    cum_count += rc;
  }
  return (cum_count < count ? -1 : cum_count);
}

void jassert_internal::jassert_safe_print ( const char* str )
{
  static int errconsoleFd = _initJassertOutputDevices();
#ifdef DEBUG
  if (theLogFile == -1 && getenv("DMTCP_TMPDIR") != NULL)
    JASSERT_SET_LOGFILE ( jalib::XToString(getenv("DMTCP_TMPDIR"))
                          + "/jassertlog." + jalib::XToString ( getpid() ) );
#endif
  writeall ( errconsoleFd, str, strlen(str) );

  if ( theLogFile != -1 )
  {
    int rv = writeall ( theLogFile, str, strlen(str) );

    if ( rv < 0 )
    {
      const char temp[] = "JASSERT: write failed, reopening log file.\n";
      writeall ( errconsoleFd, temp, sizeof(temp) );
      JASSERT_SET_LOGFILE ( theLogFilePath() );
      if ( theLogFile != -1 ) {
        const char temp2[] = "JASSERT: write failed, reopened log file.\n";
        writeall ( theLogFile, temp2, sizeof(temp2) );
        writeall ( theLogFile, str, strlen(str) );
      }
    }
  }

// #ifdef DEBUG
//     static pid_t logPd = -1;//     static int log = -1;
//
//     if(logPd != getpid())
//     {
//         if(log != -1) close(log);
//         logPd = getpid();
//         log = _fopen_log_safe((getenv("DMTCP_TMPDIR") + "/jassertlog." + jalib::XToString(logPd)).c_str());
//     }
//
//     if(log != -1)
//     {
//         writeall(log,str,strlen(str));
//     }
// #endif
}
