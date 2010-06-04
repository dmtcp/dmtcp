/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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
#include "uniquepid.h"
#include "dmtcpworker.h"
#include "dmtcpmessagetypes.h"
#include "protectedfds.h"
#include "constants.h"
#include "connectionmanager.h"
#include "syscallwrappers.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jconvert.h"

extern "C" void exit ( int status )
{
  dmtcp::DmtcpWorker::setExitInProgress();
  _real_exit ( status );
}

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
    sleep(10);
  }
}
#endif

extern "C" int close ( int fd )
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
    conId = dmtcp::KernelDeviceToConnection::Instance().retrieve(fd).id();
  }
#endif

  int rv = _real_close ( fd );

#ifdef EXTERNAL_SOCKET_HANDLING
  if (rv == 0) {
    processClose(conId);
  }
#endif

  // #ifdef DEBUG
  //     if(rv==0)
  //     {
  //         dmtcp::string closeDevice = dmtcp::KernelDeviceToConnection::Instance().fdToDevice( fd );
  //         if(closeDevice != "") JTRACE("close()")(fd)(closeDevice);
  //     }
  // #endif
  //     else
  //     {
  // #ifdef DEBUG
  //         if(dmtcp::SocketTable::Instance()[fd].state() != dmtcp::SocketEntry::T_INVALID)
  //         {
  //             dmtcp::SocketEntry& e = dmtcp::SocketTable::Instance()[fd];
  //             JTRACE("CLOSE()")(fd)(e.remoteId().id)(e.state());
  //         }
  // #endif
  //         dmtcp::SocketTable::Instance().resetFd(fd);
  //     }
  return rv;
}

extern "C" int fclose(FILE *fp)
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
    conId = dmtcp::KernelDeviceToConnection::Instance().retrieve(fd).id();
  }
#endif

  int rv = _real_fclose(fp);

#ifdef EXTERNAL_SOCKET_HANDLING
  if (rv == 0 ) {
    processClose(conId);
  }
#endif
  return rv;
}

/* epoll is currently not supported by DMTCP */
extern "C" int epoll_create(int size)
{
  JWARNING (false) .Text("epoll is currently not supported by DMTCP.");
  errno = EPERM;
  return -1;
}

extern "C" int socketpair ( int d, int type, int protocol, int sv[2] )
{
  WRAPPER_EXECUTION_LOCK_LOCK();

  JASSERT ( sv != NULL );
  int rv = _real_socketpair ( d,type,protocol,sv );
  JTRACE ( "socketpair()" ) ( sv[0] ) ( sv[1] );

  dmtcp::TcpConnection *a, *b;

  a = new dmtcp::TcpConnection ( d, type, protocol );
  a->onConnect();
  b = new dmtcp::TcpConnection ( *a, a->id() );

  dmtcp::KernelDeviceToConnection::Instance().create ( sv[0] , a );
  dmtcp::KernelDeviceToConnection::Instance().create ( sv[1] , b );

  WRAPPER_EXECUTION_LOCK_UNLOCK();

  return rv;
}

extern "C" int pipe ( int fds[2] )
{
  JTRACE ( "promoting pipe() to socketpair()" );
  //just promote pipes to socketpairs
  return socketpair ( AF_UNIX, SOCK_STREAM, 0, fds );
}


static int ptsname_r_work ( int fd, char * buf, size_t buflen )
{
  JTRACE ( "Calling ptsname_r" );

  dmtcp::Connection* c = &dmtcp::KernelDeviceToConnection::Instance().retrieve ( fd );
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
  static char tmpbuf[1024];

  if ( ptsname_r ( fd, tmpbuf, sizeof ( tmpbuf ) ) != 0 )
  {
    return NULL;
  }

  return tmpbuf;
}

extern "C" int ptsname_r ( int fd, char * buf, size_t buflen )
{
  WRAPPER_EXECUTION_LOCK_LOCK();

  int retVal = ptsname_r_work(fd, buf, buflen);

  WRAPPER_EXECUTION_LOCK_UNLOCK();

  return retVal;
}

#ifdef PID_VIRTUALIZATION
#include <virtualpidtable.h>

static void updateProcPath ( const char *path, char *newpath )
{
  char temp [ 10 ];
  int index, tempIndex;

  if (  path == "" || path == NULL ) 
  {
    strcpy(newpath, "");
    return;
  }

  if ( strncmp ( path, "/proc/", 6 ) == 0 )
  {
    index = 6;
    tempIndex = 0;
    while ( path [ index ] != '/' )
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
    pid_t currentPid = dmtcp::VirtualPidTable::Instance().originalToCurrentPid( originalPid );
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

static int _nextPtmxId()
{
  static int id = 0;
  return id++;
}

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

  dmtcp::KernelDeviceToConnection::Instance().createPtyDevice ( fd, deviceName, c );

  dmtcp::UniquePtsNameToPtmxConId::Instance().add ( uniquePtsNameStr, c->id() );
}

static void processDevPtsConnection (int fd, const char* uniquePtsName, const char* ptsName)
{
  dmtcp::string ptsNameStr = ptsName;
  dmtcp::string uniquePtsNameStr = uniquePtsName;

  dmtcp::string deviceName = "pts:" + ptsNameStr;

  JTRACE ( "creating pts connection" ) ( deviceName ) ( ptsNameStr ) ( uniquePtsNameStr );

  int type = dmtcp::PtyConnection::PTY_SLAVE;
  dmtcp::Connection * c = new dmtcp::PtyConnection ( ptsNameStr, uniquePtsNameStr, type );

  dmtcp::KernelDeviceToConnection::Instance().createPtyDevice ( fd, deviceName, c );
}

extern "C" int getpt()
{
  int fd = _real_getpt();
  if ( fd >= 0 ) {
    processDevPtmxConnection(fd);
  }
  return fd;
}

extern "C" int open (const char *path, ... )
{
  va_list ap;
  int flags;
  mode_t mode;
  int rc;
  char newpath [ 1024 ] = {0} ;
  int len,i;

  // Handling the variable number of arguments
  va_start( ap, path );
  flags = va_arg ( ap, int );
  mode = va_arg ( ap, mode_t );
  va_end ( ap );
  
  /* If DMTCP has not yet initialized, it might be that JASSERT_INIT() is
   * calling this function to open jassert log files. Therefore we shouldn't be
   * playing with locks etc.
   *
   * FIXME: The following check is not required anymore. JASSERT_INIT calls
   *        libc:open directly.
   */
  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::UNKNOWN ) {
    return _real_open ( path, flags, mode );
  }

  WRAPPER_EXECUTION_LOCK_LOCK();

  if ( strncmp(path, UNIQUE_PTS_PREFIX_STR, strlen(UNIQUE_PTS_PREFIX_STR)) == 0 ) {
    dmtcp::string currPtsDevName = dmtcp::UniquePtsNameToPtmxConId::Instance().retrieveCurrentPtsDeviceName(path);
    strcpy(newpath, currPtsDevName.c_str());
  } else {
    updateProcPath ( path, newpath );
  }

  int fd = _real_open( newpath, flags, mode );

  if ( fd >= 0 && strcmp(path, "/dev/ptmx") == 0 ) {
    processDevPtmxConnection(fd);
  } else if ( fd >= 0 && strncmp(path, UNIQUE_PTS_PREFIX_STR, strlen(UNIQUE_PTS_PREFIX_STR)) == 0 ) {
    processDevPtsConnection(fd, path, newpath);
  }

  WRAPPER_EXECUTION_LOCK_UNLOCK();

  return fd;
}

extern "C" FILE *fopen (const char* path, const char* mode)
{
  /* If DMTCP has not yet initialized, it might be that JASSERT_INIT() is
   * calling this function to open jassert log files. Therefore we shouldn't be
   * playing with locks etc.
   *
   * FIXME: The following check is not required anymore. JASSERT_INIT calls
   *        libc:open directly.
   */
  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::UNKNOWN ) {
    return _real_fopen ( path, mode );
  }

  WRAPPER_EXECUTION_LOCK_LOCK();

  char newpath [ 1024 ] = {0} ;
  int fd = -1;

  if ( strncmp(path, UNIQUE_PTS_PREFIX_STR, strlen(UNIQUE_PTS_PREFIX_STR)) == 0 ) {
    dmtcp::string currPtsDevName = dmtcp::UniquePtsNameToPtmxConId::Instance().retrieveCurrentPtsDeviceName(path);
    strcpy(newpath, currPtsDevName.c_str());
  } else {
    updateProcPath ( path, newpath );
  }

  FILE *file = _real_fopen ( newpath, mode );

  if (file != NULL) {
    fd = fileno(file);
  }

  if ( fd >= 0 && strcmp(path, "/dev/ptmx") == 0 ) {
    processDevPtmxConnection(fd);
  } else if ( fd >= 0 && strncmp(path, UNIQUE_PTS_PREFIX_STR, strlen(UNIQUE_PTS_PREFIX_STR)) == 0 ) {
    processDevPtsConnection(fd, path, newpath);
  }

  WRAPPER_EXECUTION_LOCK_UNLOCK();

  return file;
}

#ifdef ENABLE_MALLOC_WRAPPER
extern "C" void *calloc(size_t nmemb, size_t size)
{
  WRAPPER_EXECUTION_LOCK_LOCK();
  void *retVal = _real_calloc ( nmemb, size );
  WRAPPER_EXECUTION_LOCK_UNLOCK();
  return retVal;
}
extern "C" void *malloc(size_t size)
{
  WRAPPER_EXECUTION_LOCK_LOCK();
  void *retVal = _real_malloc ( size );
  WRAPPER_EXECUTION_LOCK_UNLOCK();
  return retVal;
}
extern "C" void free(void *ptr)
{
  WRAPPER_EXECUTION_LOCK_LOCK();
  _real_free ( ptr );
  WRAPPER_EXECUTION_LOCK_UNLOCK();
}
extern "C" void *realloc(void *ptr, size_t size)
{
  WRAPPER_EXECUTION_LOCK_LOCK();
  void *retVal = _real_realloc ( ptr, size );
  WRAPPER_EXECUTION_LOCK_UNLOCK();
  return retVal;
}
#endif


/*
extern "C" int
printf (const char *format, ...)
{
  va_list arg;
  int done;

  va_start (arg, format);
  done = vfprintf (stdout, format, arg);
  va_end (arg);

  return done;
}

extern "C" int
fprintf (FILE *stream, const char *format, ...)
{
  va_list arg;
  int done;

  va_start (arg, format);
  done = vfprintf (stream, format, arg);
  va_end (arg);

  return done;
}

extern "C" int
vprintf (const char *format, __gnuc_va_list arg)
{
  return vfprintf (stdout, format, arg);
}

extern "C" int
vfprintf (FILE *s, const char *format, va_list ap)
{
  WRAPPER_EXECUTION_LOCK_LOCK();
  int retVal = _real_vfprintf ( s, format, ap );
  WRAPPER_EXECUTION_LOCK_UNLOCK();
  return retVal;
}


extern "C" int
sprintf (char *s, const char *format, ...)
{
  va_list arg;
  int done;

  va_start (arg, format);
  done = vsprintf (s, format, arg);
  va_end (arg);

  return done;
}


extern "C" int
snprintf (char *s, size_t maxlen, const char *format, ...)
{
  va_list arg;
  int done;

  va_start (arg, format);
  done = vsnprintf (s, maxlen, format, arg);
  va_end (arg);

  return done;
}


extern "C" int vsprintf(char *str, const char *format, va_list ap);
extern "C" int vsnprintf(char *str, size_t size, const char *format, va_list ap);
*/

