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
    if (size == 0) rc = 0;
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


static FILE* _fopen_log_safe ( const char* filename, int protectedFd )
{
  //open file
  int tfd = _real_open ( filename, O_WRONLY | O_APPEND | O_CREAT /*| O_SYNC*/, S_IRUSR | S_IWUSR );
  if ( tfd < 0 ) return NULL;
  //change fd to 827 (DUP_LOG_FD -- PFD(6))
  int nfd = dup2 ( tfd, protectedFd );
  close ( tfd );
  if ( nfd < 0 ) return NULL;
  //promote it to a stream
  return fdopen ( nfd,"a" );
}

static FILE* _fopen_log_safe ( const jalib::string& s, int protectedFd )
{
  return _fopen_log_safe ( s.c_str(), protectedFd );
}


static FILE* theLogFile = NULL;

static jalib::string& theLogFilePath() {static jalib::string s;return s;};

void jassert_internal::jassert_init ( const jalib::string& f )
{
#ifdef DEBUG
  set_log_file(f);
#endif
  jassert_safe_print("");
}

void jassert_internal::reset_on_fork ( )
{
  pthread_mutex_t newLock = PTHREAD_MUTEX_INITIALIZER;
  logLock = newLock;
}

void jassert_internal::set_log_file ( const jalib::string& path )
{
  theLogFilePath() = path;
  if ( theLogFile != NULL ) fclose ( theLogFile );
  theLogFile = NULL;
  if ( path.length() > 0 )
  {
    theLogFile = _fopen_log_safe ( path, DUP_LOG_FD );
    if ( theLogFile == NULL )
      theLogFile = _fopen_log_safe ( path + "_2",DUP_LOG_FD );
    if ( theLogFile == NULL )
      theLogFile = _fopen_log_safe ( path + "_3",DUP_LOG_FD );
    if ( theLogFile == NULL )
      theLogFile = _fopen_log_safe ( path + "_4",DUP_LOG_FD );
    if ( theLogFile == NULL )
      theLogFile = _fopen_log_safe ( path + "_5",DUP_LOG_FD );
  }
}

static FILE* _initJassertOutputDevices()
{
  pthread_mutex_t newLock = PTHREAD_MUTEX_INITIALIZER;
  logLock = newLock;

#ifdef DEBUG
  if (theLogFile == NULL)
    JASSERT_SET_LOGFILE ( jalib::XToString(getenv("DMTCP_TMPDIR"))
                          + "/jassertlog." + jalib::XToString ( getpid() ) );
#endif

  const char* errpath = getenv ( "JALIB_STDERR_PATH" );

  if ( errpath != NULL )
    return _fopen_log_safe ( errpath, DUP_STDERR_FD );
  else
    return fdopen ( dup2 ( fileno ( stderr ),DUP_STDERR_FD ),"w" );;;
}



void jassert_internal::jassert_safe_print ( const char* str )
{
  static FILE* errconsole = _initJassertOutputDevices();
  static bool useErrorconsole = true;

  if( errconsole == NULL && useErrorconsole ) {
    jwrite ( stderr, "dmtcp: cannot open output channel for error logging\n");
    useErrorconsole = false;
  }

  if ( useErrorconsole )
    jwrite ( errconsole, str );

  if ( theLogFile != NULL ) {
    int rv = jwrite ( theLogFile, str );

    if ( rv < 0 ) {
      if ( useErrorconsole )
        jwrite ( errconsole, "JASSERT: write failed, reopening log file.\n" );
      JASSERT_SET_LOGFILE ( theLogFilePath() );
      if ( theLogFile != NULL ) {
        jwrite ( theLogFile, "JASSERT: write failed, reopened log file:\n");
        jwrite ( theLogFile, str );
      }
    }
    fflush ( theLogFile );
  }

// #ifdef DEBUG
//     static pid_t logPd = -1;
//     static FILE* log = NULL;
//
//     if(logPd != getpid())
//     {
//         if(log != NULL) fclose(log);
//         logPd = getpid();
//         log = _fopen_log_safe((getenv("DMTCP_TMPDIR") + "/jassertlog." + jalib::XToString(logPd)).c_str());
//     }
//
//     if(log != NULL)
//     {
//         fprintf(log,"%s",str);
//         fflush(log);
//     }
// #endif
}

