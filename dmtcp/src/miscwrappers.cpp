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
#include "protectedfds.h"
#include "constants.h"
#include "connectionmanager.h"
#include "syscallwrappers.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jconvert.h"

extern "C" int close ( int fd )
{
  if ( dmtcp::ProtectedFDs::isProtected ( fd ) )
  {
    JTRACE ( "blocked attempt to close protected fd" ) ( fd );
    errno = EBADF;
    return -1;
  }

  int rv = _real_close ( fd );

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

/* epoll is currently not supported by DMTCP */
extern "C" int epoll_create(int size)
{
  JWARNING (false) .Text("epoll is currently not supported by DMTCP.");
  errno = EPERM;
  return -1;
}

static int ptsname_r_work ( int fd, char * buf, size_t buflen )
{
  JTRACE ( "Calling ptsname_r" );
  char device[1024];
  const char *ptr;

  int rv = _real_ptsname_r ( fd, device, sizeof ( device ) );
  if ( rv != 0 )
  {
    JTRACE ( "ptsname_r failed" );
    return rv;
  }

  ptr = dmtcp::UniquePid::ptsSymlinkFilename ( device );

  if ( strlen ( ptr ) >=buflen )
  {
    JWARNING ( false ) ( ptr ) ( strlen ( ptr ) ) ( buflen )
      .Text ( "fake ptsname() too long for user buffer" );
    errno = ERANGE;
    return -1;
  }

  if ( dmtcp::PtsToSymlink::Instance().exists(device) == true )
  {
    dmtcp::string name = dmtcp::PtsToSymlink::Instance().getFilename(device);
    strcpy ( buf, name.c_str() );
    return rv;
  }

  JASSERT ( symlink ( device, ptr ) == 0 ) ( device ) ( ptr ) ( JASSERT_ERRNO )
    .Text ( "symlink() failed" );

  strcpy ( buf, ptr );

  //  dmtcp::PtyConnection::PtyType type = dmtcp::PtyConnection::PTY_MASTER;
  //  dmtcp::PtyConnection *master = new dmtcp::PtyConnection ( device, ptr, type );
  //  dmtcp::KernelDeviceToConnection::Instance().create ( fd, master );

  dmtcp::PtsToSymlink::Instance().add ( device, buf );

  return rv;
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

