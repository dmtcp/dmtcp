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
#include <sys/types.h>
#include <unistd.h>
#include "jconvert.h"
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <fstream>

#undef JASSERT_CONT_A
#undef JASSERT_CONT_B

int jassert_quiet = 0;

/*
   The values of DUP_STDERR_FD and DUP_LOG_FD correspond to the values of
   PFD(5) and PFD(6) in protectedfds.h. They should always be kept in sync.
*/
static const int DUP_STDERR_FD = 826; // PFD(5)
static const int DUP_LOG_FD    = 827; // PFD(6)

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

static pthread_mutex_t logLock = PTHREAD_MUTEX_INITIALIZER;

void jassert_internal::lockLog()
{
  pthread_mutex_lock(&logLock);
}

void jassert_internal::unlockLog()
{
  pthread_mutex_unlock(&logLock);
}

jassert_internal::JAssert::JAssert ( bool exitWhenDone )
    : JASSERT_CONT_A ( *this )
    , JASSERT_CONT_B ( *this )
    , _exitWhenDone ( exitWhenDone )
{
  jassert_internal::lockLog();
}

jassert_internal::JAssert::~JAssert()
{
  jassert_internal::unlockLog();
  if ( _exitWhenDone )
  {
    Print ( "Terminating...\n" );
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
  int tfd = open ( filename, O_WRONLY | O_APPEND | O_CREAT /*| O_SYNC*/, S_IRUSR | S_IWUSR );
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
  const char* errpath = getenv ( "JALIB_STDERR_PATH" );

  if ( errpath != NULL )
    return _fopen_log_safe ( errpath, DUP_STDERR_FD );
  else
    return dup2 ( fileno ( stderr ), DUP_STDERR_FD );
}

static int writeall(int fd, const void *buf, size_t count) {
    int rc;
    do
      rc = write(fd, buf, count);
    while (rc == -1 && (errno == EAGAIN  || errno == EINTR));
    return rc; /* rc >= 0; success */
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
//     static pid_t logPd = -1;
//     static int log = -1;
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

