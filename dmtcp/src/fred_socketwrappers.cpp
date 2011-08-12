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
#include <fcntl.h>
#include "../jalib/jassert.h"
#include "../jalib/jfilesystem.h"
#include "dmtcpworker.h"
#ifdef RECORD_REPLAY
#include "fred_wrappers.h"
#include "synchronizationlogging.h"

extern "C"
{
int socket ( int domain, int type, int protocol )
{
  BASIC_SYNC_WRAPPER(int, socket, _almost_real_socket, domain, type, protocol);
}

int connect ( int sockfd,  const  struct sockaddr *serv_addr, socklen_t addrlen )
{
  BASIC_SYNC_WRAPPER(int, connect, _almost_real_connect, sockfd, serv_addr, addrlen);
}

int bind ( int sockfd,  const struct  sockaddr  *addr,  socklen_t addrlen )
{
  BASIC_SYNC_WRAPPER(int, bind, _almost_real_bind, sockfd, addr, addrlen);
}

int listen ( int sockfd, int backlog )
{
  BASIC_SYNC_WRAPPER(int, listen, _almost_real_listen, sockfd, backlog);
}

int accept ( int sockfd, struct sockaddr *addr, socklen_t *addrlen )
{
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
}

//#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)) && __GLIBC_PREREQ(2,10)
int accept4 ( int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags )
{
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
}
//#endif

int setsockopt ( int sockfd, int  level,  int  optname,  const  void  *optval,
                 socklen_t optlen )
{
  WRAPPER_HEADER(int, setsockopt, _almost_real_setsockopt,
                 sockfd, level, optname, optval, optlen);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY(setsockopt);
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    retval = _almost_real_setsockopt(sockfd, level, optname, optval, optlen);
    isOptionalEvent = false;
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

int getsockopt ( int sockfd, int  level,  int  optname,  void  *optval,
                 socklen_t *optlen )
{
  WRAPPER_HEADER(int, getsockopt, _almost_real_getsockopt,
                 sockfd, level, optname, optval, optlen);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(getsockopt);
    if (retval == 0 && optval != NULL) {
      *optlen = GET_FIELD(currentLogEntry, getsockopt, ret_optlen);
      WRAPPER_REPLAY_READ_FROM_READ_LOG(getsockopt, optval, *optlen);
    }
    WRAPPER_REPLAY_END(getsockopt);
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    retval = _almost_real_getsockopt(sockfd, level, optname, optval, optlen);
    isOptionalEvent = false;
    if (retval == 0 && optval != NULL) {
      WRAPPER_LOG_WRITE_INTO_READ_LOG(getsockopt, optval, *optlen);
      SET_FIELD2(my_entry, getsockopt, ret_optlen, *optlen);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

}
#endif
