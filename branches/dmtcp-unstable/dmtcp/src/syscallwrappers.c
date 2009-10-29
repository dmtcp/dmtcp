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

#include "syscallwrappers.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
#include "sockettable.h"
#include <pthread.h>
#include <sys/select.h>

/* According to earlier standards */
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

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
      if(ret < 0) ret = dmtcp_on_error(ret, sockfd, #func); \
      else ret = dmtcp_on_ ## func (ret, __VA_ARGS__);\
      in_dmtcp_on_helper_fnc = 0; \
    } \
    _dmtcp_unlock(); \
    errno = saved_errno; \
    return ret;}

/* Support for epoll will be added in future */
int epoll_create(int size)
{
  JTRACE("Support for epoll will be added in the future.");
  return -1;
}

int socket ( int domain, int type, int protocol )
{
  static int sockfd = -1;
  PASSTHROUGH_DMTCP_HELPER ( socket, domain, type, protocol );
}

int connect ( int sockfd,  const  struct sockaddr *serv_addr, socklen_t addrlen )
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
      printf ( "No data within five seconds.\n" );
  }

  saved_errno = errno;
  PASSTHROUGH_DMTCP_HELPER2 ( connect,sockfd,serv_addr,addrlen );
}

int bind ( int sockfd,  const struct  sockaddr  *my_addr,  socklen_t addrlen )
{
  PASSTHROUGH_DMTCP_HELPER ( bind, sockfd, my_addr, addrlen );
}

int listen ( int sockfd, int backlog )
{
  PASSTHROUGH_DMTCP_HELPER ( listen, sockfd, backlog );
}

int accept ( int sockfd, struct sockaddr *addr, socklen_t *addrlen )
{
  if ( addr == NULL || addrlen == NULL )
  {
    struct sockaddr_storage tmp_addr;
    socklen_t tmp_len = 0;
    memset ( &tmp_addr,0,sizeof ( tmp_addr ) );
    PASSTHROUGH_DMTCP_HELPER ( accept, sockfd, ( ( struct sockaddr * ) &tmp_addr ) , ( &tmp_len ) );
  }
  else
    PASSTHROUGH_DMTCP_HELPER ( accept, sockfd, addr, addrlen );
}

int setsockopt ( int sockfd, int  level,  int  optname,  const  void  *optval,
                 socklen_t optlen )
{
  PASSTHROUGH_DMTCP_HELPER ( setsockopt,sockfd,level,optname,optval,optlen );
}
