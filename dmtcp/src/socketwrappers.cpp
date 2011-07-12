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

#include "syscallwrappers.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
#include "sockettable.h"
#include <pthread.h>
#include <sys/select.h>
#include <sys/un.h>
#include <arpa/inet.h>

/* According to earlier standards */
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include "../jalib/jassert.h"
#include "../jalib/jfilesystem.h"
#ifdef RECORD_REPLAY
#include <fcntl.h>
#include "dmtcpworker.h"
#include "synchronizationlogging.h"
#endif

/*
 * XXX: TODO: Add wrapper protection for socket() family of system calls
 */

//////////////////////////////////////
//////// Now we define our wrappers

/* Prevent recursive calls to dmtcp_on_XXX() */
static int in_dmtcp_on_helper_fnc = 0;

#define PASSTHROUGH_DMTCP_HELPER(func, ...) {\
    int ret = _real_ ## func (__VA_ARGS__); \
    int saved_errno; \
    saved_errno = errno; \
      PASSTHROUGH_DMTCP_HELPER2(func,__VA_ARGS__); \
    }

#define PASSTHROUGH_DMTCP_HELPER2(func, ...) {\
    _dmtcp_lock();\
    if (in_dmtcp_on_helper_fnc == 0) { \
      in_dmtcp_on_helper_fnc = 1; \
      if(ret < 0) ret = dmtcp_on_error(ret, sockfd, #func, saved_errno); \
      else ret = dmtcp_on_ ## func (ret, __VA_ARGS__);\
      in_dmtcp_on_helper_fnc = 0; \
    } \
    _dmtcp_unlock();\
    errno =saved_errno; \
    return ret;}

static int _almost_real_socket(int domain, int type, int protocol)
{
  static int sockfd = -1;
  PASSTHROUGH_DMTCP_HELPER ( socket, domain, type, protocol );
}

static int _almost_real_connect(int sockfd, const struct sockaddr *serv_addr,
    socklen_t addrlen)
{
  int ret = _real_connect ( sockfd,serv_addr,addrlen );
  int saved_errno = errno;

  //no blocking connect... need to hang around until it is writable
  if ( ret < 0 && errno == EINPROGRESS )
  {
    fd_set wfds;
    struct timeval tv;
    int retval;

    FD_ZERO ( &wfds );
    FD_SET ( sockfd, &wfds );

    tv.tv_sec = 15;
    tv.tv_usec = 0;

    retval = select ( sockfd+1, NULL, &wfds, NULL, &tv );
    /* Don't rely on the value of tv now! */

    if ( retval == -1 )
      perror ( "select()" );
    else if ( FD_ISSET ( sockfd, &wfds ) )
    {
      int val = -1;
      socklen_t sz = sizeof ( val );
      getsockopt ( sockfd,SOL_SOCKET,SO_ERROR,&val,&sz );
      if ( val==0 ) ret = 0;
    }
    else
      JTRACE ( "No data within five seconds." );
  }

  saved_errno = errno;
  PASSTHROUGH_DMTCP_HELPER2 ( connect,sockfd,serv_addr,addrlen );
}

static int _almost_real_bind (int sockfd, const struct sockaddr *my_addr,
    socklen_t addrlen)
{
  PASSTHROUGH_DMTCP_HELPER ( bind, sockfd, my_addr, addrlen );
}

static int _almost_real_listen ( int sockfd, int backlog )
{
  PASSTHROUGH_DMTCP_HELPER ( listen, sockfd, backlog );
}

static int _almost_real_accept(int sockfd, struct sockaddr *addr,
    socklen_t *addrlen)
{
  if ( addr == NULL || addrlen == NULL )
  {
    struct sockaddr_storage tmp_addr;
    socklen_t tmp_len = 0;
    memset ( &tmp_addr,0,sizeof ( tmp_addr ) );
    PASSTHROUGH_DMTCP_HELPER (accept, sockfd, (struct sockaddr *) &tmp_addr,
                              &tmp_len);
  }
  else
    PASSTHROUGH_DMTCP_HELPER ( accept, sockfd, addr, addrlen );
}

static int _almost_real_accept4 ( int sockfd, struct sockaddr *addr,
                           socklen_t *addrlen, int flags )
{
  if ( addr == NULL || addrlen == NULL )
  {
    struct sockaddr_storage tmp_addr;
    socklen_t tmp_len = 0;
    memset ( &tmp_addr,0,sizeof ( tmp_addr ) );
    PASSTHROUGH_DMTCP_HELPER (accept4, sockfd, (struct sockaddr *) &tmp_addr,
                              &tmp_len, flags);
  }
  else
    PASSTHROUGH_DMTCP_HELPER ( accept4, sockfd, addr, addrlen, flags );
}

static int _almost_real_setsockopt(int sockfd, int level, int optname,
    const void *optval, socklen_t optlen)
{
  PASSTHROUGH_DMTCP_HELPER ( setsockopt,sockfd,level,optname,optval,optlen );
}

static int _almost_real_getsockopt(int sockfd, int level, int optname,
    void *optval, socklen_t *optlen)
{
  PASSTHROUGH_DMTCP_HELPER ( getsockopt,sockfd,level,optname,optval,optlen );
}

extern "C"
{
int socket ( int domain, int type, int protocol )
{
#ifdef RECORD_REPLAY
  BASIC_SYNC_WRAPPER(int, socket, _almost_real_socket, domain, type, protocol);
#else
  return _almost_real_socket(domain, type, protocol);
#endif
}

int connect ( int sockfd,  const  struct sockaddr *serv_addr, socklen_t addrlen )
{
#ifdef RECORD_REPLAY
  BASIC_SYNC_WRAPPER(int, connect, _almost_real_connect, sockfd, serv_addr, addrlen);
#else
  return _almost_real_connect(sockfd, serv_addr, addrlen);
#endif
}

int bind ( int sockfd,  const struct  sockaddr  *addr,  socklen_t addrlen )
{
#ifdef RECORD_REPLAY
  BASIC_SYNC_WRAPPER(int, bind, _almost_real_bind, sockfd, addr, addrlen);
#else
  return _almost_real_bind(sockfd, addr, addrlen);
#endif
}

int listen ( int sockfd, int backlog )
{
#ifdef RECORD_REPLAY
  BASIC_SYNC_WRAPPER(int, listen, _almost_real_listen, sockfd, backlog);
#else
  return _almost_real_listen(sockfd, backlog );
#endif
}

int accept ( int sockfd, struct sockaddr *addr, socklen_t *addrlen )
{
#ifdef RECORD_REPLAY
  WRAPPER_HEADER(int, accept, _almost_real_accept, sockfd, addr, addrlen);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(accept);
    if (retval != -1) {
      *addr = GET_FIELD(currentLogEntry, accept, ret_addr);
      *addrlen = GET_FIELD(currentLogEntry, accept, ret_addrlen);
    }
    WRAPPER_REPLAY_END(accept);
  } else if (SYNC_IS_RECORD) {
    retval = _almost_real_accept(sockfd, addr, addrlen);
    if (retval != -1) {
      SET_FIELD2(my_entry, accept, ret_addr, *addr);
      SET_FIELD2(my_entry, accept, ret_addrlen, *addrlen);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
#else
  return _almost_real_accept(sockfd, addr, addrlen);
#endif
}

//#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)) && __GLIBC_PREREQ(2,10)
int accept4 ( int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags )
{
#ifdef RECORD_REPLAY
  WRAPPER_HEADER(int, accept4, _almost_real_accept4, sockfd, addr, addrlen, flags);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(accept4);
    if (retval != -1) {
      *addr = GET_FIELD(currentLogEntry, accept4, ret_addr);
      *addrlen = GET_FIELD(currentLogEntry, accept4, ret_addrlen);
    }
    WRAPPER_REPLAY_END(accept4);
  } else if (SYNC_IS_RECORD) {
    retval = _almost_real_accept4(sockfd, addr, addrlen, flags);
    if (retval != -1) {
      SET_FIELD2(my_entry, accept4, ret_addr, *addr);
      SET_FIELD2(my_entry, accept4, ret_addrlen, *addrlen);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
#else
  return _almost_real_accept4(sockfd, addr, addrlen, flags);
#endif
}
//#endif

int setsockopt ( int sockfd, int  level,  int  optname,  const  void  *optval,
                 socklen_t optlen )
{
#ifdef RECORD_REPLAY
  BASIC_SYNC_WRAPPER_WITH_CKPT_LOCK(int, setsockopt, _almost_real_setsockopt,
                                    sockfd, level, optname, optval, optlen);
#else
  return _almost_real_setsockopt(sockfd,level,optname,optval,optlen);
#endif
}

int getsockopt ( int sockfd, int  level,  int  optname,  void  *optval,
                 socklen_t *optlen )
{
#ifdef RECORD_REPLAY
  BASIC_SYNC_WRAPPER_WITH_CKPT_LOCK(int, getsockopt, _almost_real_getsockopt,
                                    sockfd, level, optname, optval, optlen);
#else
  return _almost_real_getsockopt(sockfd,level,optname,optval,optlen);
#endif
}

#ifdef RECORD_REPLAY
int getsockname ( int sockfd, struct sockaddr *addr, socklen_t *addrlen )
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  WRAPPER_HEADER_CKPT_DISABLED(int, getsockname, _real_getsockname,
                               sockfd, addr, addrlen);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(getsockname);
    if (retval != -1) {
      *addr = GET_FIELD(currentLogEntry, getsockname, ret_addr);
      *addrlen = GET_FIELD(currentLogEntry, getsockname, ret_addrlen);
    }
    WRAPPER_REPLAY_END(getsockname);
  } else if (SYNC_IS_RECORD) {
    retval = _real_getsockname(sockfd, addr, addrlen);
    if (retval != -1) {
      SET_FIELD2(my_entry, getsockname, ret_addr, *addr);
      SET_FIELD2(my_entry, getsockname, ret_addrlen, *addrlen);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  WRAPPER_HEADER_CKPT_DISABLED(int, getpeername, _real_getpeername,
                               sockfd, addr, addrlen);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(getpeername);
    if (retval != -1) {
      *addr = GET_FIELD(currentLogEntry, getpeername, ret_addr);
      *addrlen = GET_FIELD(currentLogEntry, getpeername, ret_addrlen);
    }
    WRAPPER_REPLAY_END(getpeername);
  } else if (SYNC_IS_RECORD) {
    retval = _real_getpeername(sockfd, addr, addrlen);
    if (retval != -1) {
      SET_FIELD2(my_entry, getpeername, ret_addr, *addr);
      SET_FIELD2(my_entry, getpeername, ret_addrlen, *addrlen);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}
#endif
}
