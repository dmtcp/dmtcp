/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
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

#include <stdarg.h>
#include <stdlib.h>
#include <vector>
#include <list>
#include <string>
#include <fcntl.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/version.h>
#include <limits.h>
#include "uniquepid.h"
#include "dmtcpworker.h"
#include "dmtcpmessagetypes.h"
#include "protectedfds.h"
#include "constants.h"
#include "connectionmanager.h"
#include "syscallwrappers.h"
#include "sysvipc.h"
#include "util.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jconvert.h"


#ifdef EXTERNAL_SOCKET_HANDLING
extern dmtcp::vector <dmtcp::ConnectionIdentifier> externalTcpConnections;
static void processClose(dmtcp::ConnectionIdentifier conId)
{
  if ( dmtcp::DmtcpWorker::waitingForExternalSocketsToClose() == true ) {
    dmtcp::vector <dmtcp::ConnectionIdentifier>::iterator i = externalTcpConnections.begin();
    for ( i = externalTcpConnections.begin(); i != externalTcpConnections.end(); ++i ) {
      if ( conId == *i ) {
        externalTcpConnections.erase(i);
        break;
      }
    }
    if ( externalTcpConnections.empty() == true ) {
    }
    sleep(4);
  }
}
#endif

#ifdef RECORD_REPLAY
int _almost_real_close ( int fd )
#else
extern "C" int close ( int fd )
#endif
{
  if ( dmtcp::ProtectedFDs::isProtected ( fd ) )
  {
    JTRACE ( "blocked attempt to close protected fd" ) ( fd );
    errno = EBADF;
    return -1;
  }

#ifdef EXTERNAL_SOCKET_HANDLING
  dmtcp::ConnectionIdentifier conId;
  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::RUNNING &&
       dmtcp::DmtcpWorker::waitingForExternalSocketsToClose() == true &&
       dup2(fd,fd) != -1 ) {
    conId = dmtcp::KernelDeviceToConnection::instance().retrieve(fd).id();
  }

  int rv = _real_close ( fd );

  if (rv == 0) {
    processClose(conId);
  }
#else
  int rv = _real_close ( fd );
#endif

  return rv;
}

#ifdef RECORD_REPLAY
int _almost_real_fclose(FILE *fp)
#else
extern "C" int fclose(FILE *fp)
#endif
{
  int fd = fileno(fp);
  if ( dmtcp::ProtectedFDs::isProtected ( fd ) )
  {
    JTRACE ( "blocked attempt to fclose protected fd" ) ( fd );
    errno = EBADF;
    return -1;
  }

#ifdef EXTERNAL_SOCKET_HANDLING
  dmtcp::ConnectionIdentifier conId;

  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::RUNNING &&
       dmtcp::DmtcpWorker::waitingForExternalSocketsToClose() == true &&
       dup2(fd,fd) != -1 ) {
    conId = dmtcp::KernelDeviceToConnection::instance().retrieve(fd).id();
  }

  int rv = _real_fclose(fp);

  if (rv == 0 ) {
    processClose(conId);
  }
#else
  int rv = _real_fclose(fp);
#endif

  return rv;
}

static int ptsname_r_work ( int fd, char * buf, size_t buflen )
{
  JTRACE ( "Calling ptsname_r" );

  dmtcp::Connection* c = &dmtcp::KernelDeviceToConnection::instance().retrieve ( fd );
  dmtcp::PtyConnection* ptyCon = (dmtcp::PtyConnection*) c;

  dmtcp::string uniquePtsName = ptyCon->uniquePtsName();

  JTRACE("ptsname_r") (uniquePtsName);

  if ( uniquePtsName.length() >= buflen )
  {
    JWARNING ( false ) ( uniquePtsName ) ( uniquePtsName.length() ) ( buflen )
      .Text ( "fake ptsname() too long for user buffer" );
    errno = ERANGE;
    return -1;
  }

  strcpy ( buf, uniquePtsName.c_str() );

  return 0;
}

extern "C" char *ptsname ( int fd )
{
  /* No need to acquire Wrapper Protection lock since it will be done in ptsname_r */
  JTRACE ( "ptsname() promoted to ptsname_r()" );
  static char tmpbuf[PATH_MAX];

  if ( ptsname_r ( fd, tmpbuf, sizeof ( tmpbuf ) ) != 0 )
  {
    return NULL;
  }

  return tmpbuf;
}

extern "C" int ptsname_r ( int fd, char * buf, size_t buflen )
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  int retVal = ptsname_r_work(fd, buf, buflen);

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return retVal;
}

#ifdef PID_VIRTUALIZATION
#include <virtualpidtable.h>

static void updateProcPath ( const char *path, char *newpath )
{
  char temp [ 10 ];
  int index, tempIndex;

  if ( path == NULL || strlen(path) == 0 )
  {
    strcpy(newpath, "");
    return;
  }

  if ( dmtcp::Util::strStartsWith ( path, "/proc/" ) )
  {
    index = 6;
    tempIndex = 0;
    while ( path [ index ] != '/' && path [ index ] != '\0')
    {
      if ( path [ index ] >= '0' && path [ index ] <= '9' )
        temp [ tempIndex++ ] = path [ index++ ];
      else
      {
        strcpy ( newpath, path );
        return;
      }
    }
    temp [ tempIndex ] = '\0';
    pid_t originalPid = atoi ( temp );
    pid_t currentPid = dmtcp::VirtualPidTable::instance().originalToCurrentPid( originalPid );
    if (currentPid == -1)
      currentPid = originalPid;

    sprintf ( newpath, "/proc/%d%s", currentPid, &path [ index ] );
  }
  else strcpy ( newpath, path );
  return;
}
#else
void updateProcPath ( const char *path, char *newpath )
{
  if (  path == "" || path == NULL ) {
    strcpy( newpath, "" );
    return;
  }
  strcpy ( newpath, path );
  return;
}
#endif

// The current implementation simply increments the last count and returns it.
// Although highly unlikely, this can cause a problem if the counter resets to
// zero. In that case we should have some more sophisticated code which checks
// to see if the value pointed by counter is in use or not.
static int getNextFreeSlavePtyNum()
{
  static int counter = -1;
  counter++;
  JASSERT(counter != -1) .Text ("See the comment above");
  return counter;
}

#define DMTCP_PTS_PREFIX_STR  "dmtcp_"
#define UNIQUE_PTS_PREFIX_STR "/dev/pts/dmtcp_"
//DMTCP_PTS_PREFIX_STR

/*
static int _nextPtmxId()
{
  static int id = 0;
  return id++;
}
*/

// XXX: The current implementation for handling Pseudo-Terminal Master-Slave pairs
// works only if the process involved in it are restarted from the same
// dmtcp_restart command.                               -- KAPIL

static void processDevPtmxConnection (int fd)
{
  char ptsName[21];

  JASSERT(_real_ptsname_r(fd, ptsName, 21) == 0) (JASSERT_ERRNO);

  dmtcp::string ptsNameStr = ptsName;
  dmtcp::string uniquePtsNameStr;

  // glibc allows only 20 char long ptsname
  // Check if there is enough room to insert the string "dmtcp_" before the
  //   terminal number, if not then we ASSERT here.
  JASSERT((strlen(ptsName) + strlen("dmtcp_")) <= 20)
    .Text("string /dev/pts/<n> too long, can not be virtualized."
          "Once possible workarong here is to replace the string"
          "\"dmtcp_\" with something short like \"d_\" or even "
          "\"d\" and recompile DMTCP");

  // Generate new Unique ptsName
  uniquePtsNameStr = UNIQUE_PTS_PREFIX_STR;
  uniquePtsNameStr += jalib::XToString(getNextFreeSlavePtyNum());

  dmtcp::string deviceName = "ptmx[" + ptsNameStr + "]:" + "/dev/ptmx";

//   dmtcp::string deviceName = "ptmx[" + dmtcp::UniquePid::ThisProcess().toString()
//                            + ":" + jalib::XToString ( _nextPtmxId() )
//                            + "]:" + device;

  JTRACE ( "creating ptmx connection" ) ( deviceName ) ( ptsNameStr ) ( uniquePtsNameStr );

  int type = dmtcp::PtyConnection::PTY_MASTER;
  dmtcp::Connection * c = new dmtcp::PtyConnection ( ptsNameStr, uniquePtsNameStr, type );

  dmtcp::KernelDeviceToConnection::instance().createPtyDevice ( fd, deviceName, c );

  dmtcp::UniquePtsNameToPtmxConId::instance().add ( uniquePtsNameStr, c->id() );
}

static void processDevPtsConnection (int fd, const char* uniquePtsName, const char* ptsName)
{
  dmtcp::string ptsNameStr = ptsName;
  dmtcp::string uniquePtsNameStr = uniquePtsName;

  dmtcp::string deviceName = "pts:" + ptsNameStr;

  JTRACE ( "creating pts connection" ) ( deviceName ) ( ptsNameStr ) ( uniquePtsNameStr );

  int type = dmtcp::PtyConnection::PTY_SLAVE;
  dmtcp::Connection * c = new dmtcp::PtyConnection ( ptsNameStr, uniquePtsNameStr, type );

  dmtcp::KernelDeviceToConnection::instance().createPtyDevice ( fd, deviceName, c );
}

extern "C" int getpt()
{
  int fd = _real_getpt();
  if ( fd >= 0 ) {
    processDevPtmxConnection(fd);
  }
  return fd;
}

static int _open_open64_work(int (*fn)(const char *path, int flags, ...),
                             const char *path, int flags, mode_t mode)
{
  char newpath [ 1024 ] = {0} ;

  WRAPPER_EXECUTION_DISABLE_CKPT();

  if ( dmtcp::Util::strStartsWith(path, UNIQUE_PTS_PREFIX_STR) ) {
    dmtcp::string currPtsDevName =
      dmtcp::UniquePtsNameToPtmxConId::instance().retrieveCurrentPtsDeviceName(path);
    strcpy(newpath, currPtsDevName.c_str());
  } else {
    updateProcPath ( path, newpath );
  }

  int fd = (*fn)( newpath, flags, mode );

  if ( fd >= 0 && strcmp(path, "/dev/ptmx") == 0 ) {
    processDevPtmxConnection(fd);
  } else if ( fd >= 0 && dmtcp::Util::strStartsWith(path, UNIQUE_PTS_PREFIX_STR) ) {
    processDevPtsConnection(fd, path, newpath);
  }

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return fd;
}

/* Used by open() wrapper to do other tracking of open apart from
   synchronization stuff. */
#ifdef RECORD_REPLAY
int _almost_real_open(const char *path, int flags, mode_t mode)
{
#else
extern "C" int open (const char *path, int flags, ... )
{
  mode_t mode = 0;
  // Handling the variable number of arguments
  if (flags & O_CREAT) {
    va_list arg;
    va_start (arg, flags);
    mode = va_arg (arg, int);
    va_end (arg);
  }
#endif
  return _open_open64_work(_real_open, path, flags, mode);
}

// FIXME: The 'fn64' version of functions is defined only when within
// __USE_LARGEFILE64 is #defined. The wrappers in this file need to consider
// this fact. The problem can occur, for example, when DMTCP is not compiled
// with __USE_LARGEFILE64 whereas the user-binary is. In that case the open64()
// call from user will come to DMTCP and DMTCP might fail to execute it
// properly.

// FIXME: Add the 'fn64' wrapper test cases to dmtcp test suite.
#ifdef RECORD_REPLAY
int _almost_real_open64(const char *path, int flags, mode_t mode)
{
#else
extern "C" int open64 (const char *path, int flags, ... )
{
  mode_t mode;
  // Handling the variable number of arguments
  if (flags & O_CREAT) {
    va_list arg;
    va_start (arg, flags);
    mode = va_arg (arg, int);
    va_end (arg);
  }
#endif
  return _open_open64_work(_real_open64, path, flags, mode);
}


static FILE *_fopen_fopen64_work(FILE* (*fn)(const char *path, const char *mode),
                                 const char *path, const char *mode)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  char newpath [ PATH_MAX ] = {0} ;

  if ( dmtcp::Util::strStartsWith(path, UNIQUE_PTS_PREFIX_STR) ) {
    dmtcp::string currPtsDevName =
      dmtcp::UniquePtsNameToPtmxConId::instance().retrieveCurrentPtsDeviceName(path);
    strcpy(newpath, currPtsDevName.c_str());
  } else {
    updateProcPath ( path, newpath );
  }

  FILE *file = (*fn) ( newpath, mode );

  if (file != NULL) {
    int fd = fileno(file);
    if ( strcmp(path, "/dev/ptmx") == 0 ) {
      processDevPtmxConnection(fd);
    } else if ( dmtcp::Util::strStartsWith(path, UNIQUE_PTS_PREFIX_STR) ) {
      processDevPtsConnection(fd, path, newpath);
    }
  }

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return file;
}

#ifdef RECORD_REPLAY
FILE *_almost_real_fopen(const char *path, const char *mode)
#else
extern "C" FILE *fopen (const char* path, const char* mode)
#endif
{
  return _fopen_fopen64_work(_real_fopen, path, mode);
}

#ifdef RECORD_REPLAY
FILE *_almost_real_fopen64(const char *path, const char *mode)
#else
extern "C" FILE *fopen64 (const char* path, const char* mode)
#endif
{
  return _fopen_fopen64_work(_real_fopen64, path, mode);
}


static void updateStatPath(const char *path, char *newpath)
{
  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::UNKNOWN ) {
    strncpy(newpath, path, PATH_MAX);
  } else if ( dmtcp::Util::strStartsWith(path, UNIQUE_PTS_PREFIX_STR) ) {
    dmtcp::string currPtsDevName = dmtcp::UniquePtsNameToPtmxConId::instance().retrieveCurrentPtsDeviceName(path);
    strcpy(newpath, currPtsDevName.c_str());
  } else {
    updateProcPath ( path, newpath );
  }
}

#ifdef RECORD_REPLAY
int _almost_real_xstat(int vers, const char *path, struct stat *buf)
#else
extern "C" int __xstat(int vers, const char *path, struct stat *buf)
#endif
{
  char newpath [ PATH_MAX ] = {0} ;
  WRAPPER_EXECUTION_DISABLE_CKPT();
  updateStatPath(path, newpath);
  int retval = _real_xstat( vers, newpath, buf );
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

#ifdef RECORD_REPLAY
int _almost_real_xstat64(int vers, const char *path, struct stat64 *buf)
#else
extern "C" int __xstat64(int vers, const char *path, struct stat64 *buf)
#endif
{
  char newpath [ PATH_MAX ] = {0};
  WRAPPER_EXECUTION_DISABLE_CKPT();
  updateStatPath(path, newpath);
  int retval = _real_xstat64( vers, newpath, buf );
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

#if 0
#ifdef RECORD_REPLAY
int _almost_real_fxstat(int vers, int fd, struct stat *buf)
#else
extern "C" int __fxstat(int vers, int fd, struct stat *buf)
#endif
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  int retval = _real_fxstat(vers, fd, buf);
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

#ifdef RECORD_REPLAY
int _almost_real_fxstat64(int vers, int fd, struct stat64 *buf)
#else
extern "C" int __fxstat64(int vers, int fd, struct stat64 *buf)
#endif
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  int retval = _real_fxstat64(vers, fd, buf);
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}
#endif

#ifdef RECORD_REPLAY
int _almost_real_lxstat(int vers, const char *path, struct stat *buf)
#else
extern "C" int __lxstat(int vers, const char *path, struct stat *buf)
#endif
{
  char newpath [ PATH_MAX ] = {0} ;
  WRAPPER_EXECUTION_DISABLE_CKPT();
  updateStatPath(path, newpath);
  int retval = _real_lxstat( vers, newpath, buf );
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

#ifdef RECORD_REPLAY
int _almost_real_lxstat64(int vers, const char *path, struct stat64 *buf)
#else
extern "C" int __lxstat64(int vers, const char *path, struct stat64 *buf)
#endif
{
  char newpath [ PATH_MAX ] = {0} ;
  WRAPPER_EXECUTION_DISABLE_CKPT();
  updateStatPath(path, newpath);
  int retval = _real_lxstat64( vers, newpath, buf );
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

#ifdef RECORD_REPLAY
READLINK_RET_TYPE _almost_real_readlink(const char *path, char *buf,
                                               size_t bufsiz)
#else
extern "C" READLINK_RET_TYPE readlink(const char *path, char *buf,
                                      size_t bufsiz)
#endif
{
  char newpath [ PATH_MAX ] = {0} ;
  WRAPPER_EXECUTION_DISABLE_CKPT();
  updateProcPath(path, newpath);
  READLINK_RET_TYPE retval;
  retval = _real_readlink(newpath, buf, bufsiz);
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}


#ifdef PID_VIRTUALIZATION
// TODO:  ioctl must use virtualized pids for request = TIOCGPGRP / TIOCSPGRP
// These are synonyms for POSIX standard tcgetpgrp / tcsetpgrp
extern "C" {
int send_sigwinch = 0;
}


#ifdef RECORD_REPLAY
int _almost_real_ioctl(int d,  unsigned long int request, ...)
#else
extern "C" int ioctl(int d,  unsigned long int request, ...)
#endif
{
  va_list ap;
  int retval;

  if (send_sigwinch && request == TIOCGWINSZ) {
    send_sigwinch = 0;
    va_list local_ap;
    va_copy(local_ap, ap);
    va_start(local_ap, request);
    struct winsize * win = va_arg(local_ap, struct winsize *);
    va_end(local_ap);
    retval = _real_ioctl(d, request, win);  // This fills in win
    win->ws_col--; // Lie to application, and force it to resize window,
		   //  reset any scroll regions, etc.
    kill(getpid(), SIGWINCH); // Tell application to look up true winsize
			      // and resize again.
  } else {
    void * arg;
    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);
    retval = _real_ioctl(d, request, arg);
  }
  return retval;
}
#endif
