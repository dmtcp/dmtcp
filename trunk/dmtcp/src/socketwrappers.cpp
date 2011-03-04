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

#ifdef RECORD_REPLAY
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
#endif

extern "C"
{
int socket ( int domain, int type, int protocol )
{
#ifdef RECORD_REPLAY
  BASIC_SYNC_WRAPPER(int, socket, _almost_real_socket, domain, type, protocol);
#else
  static int sockfd = -1;
  PASSTHROUGH_DMTCP_HELPER ( socket, domain, type, protocol );
#endif
}

/*
 * Calculate X11 listener port using the env var "DISPLAY". It is computed in
 * the following manner:
 *   hostname:D.S means screen S on display D of host hostname; 
 *     the X server for this display is listening at TCP port 6000+D.
 */
static short int _X11ListenerPort() {
  short int port = -1;
  const char *str = getenv("DISPLAY");
  if (str != NULL) {
    dmtcp::string display = str;
    int idx = display.find_last_of(':');
    char *dummy;
    port = X11_LISTENER_PORT_START 
         + strtol(display.c_str() + idx + 1, &dummy, 10);
    JTRACE("X11 Listener Port found") (port);
  }
  return port;
}

static bool _isBlacklistedTcp ( int sockfd, const sockaddr* saddr, socklen_t len )
{
  JASSERT( saddr != NULL );

  if ( saddr->sa_family == AF_FILE ) {
    const char* un_path = ( ( sockaddr_un* ) saddr )->sun_path;
    if (un_path[0] == '\0') {
      /* The first byte is null, which indicates abstract socket name */
      un_path++;
    }
    dmtcp::string path = jalib::Filesystem::GetDirName( un_path );

    if (path == "/var/run/nscd") { 
      JTRACE("connect() to nscd process. Will not be drained")
        (sockfd) (path);
      return true;
    }

    // Block only connections to nscd daemon. Allow X11 connections. We have
    // already unset DISPLAY environment variable. This is done to allow vnc
    // application to connect to the vncserver (X11 proxie server).
    return false;

    if (path == "/tmp/.ICE-unix" || path == "/tmp/.X11-unix") { 
      JTRACE("connect() to external process (X-server). Will not be drained")
        (sockfd) (path);
      return true;
    }
  } 

  // Block only connections to nscd daemon. Allow X11 connections. We have
  // already unset DISPLAY environment variable. This is done to allow vnc
  // application to connect to the vncserver (X11 proxie server).
  return false;

  if ( saddr->sa_family == AF_INET ) {
    struct sockaddr_in* addr = ( sockaddr_in* ) saddr;
    int port = ntohs(addr->sin_port);
    char inet_addr[32];
    inet_ntop(AF_INET, &(addr->sin_addr), inet_addr, sizeof(inet_addr));
    if (strcmp(inet_addr, "127.0.0.1") == 0 && port == _X11ListenerPort()) {
      JTRACE("connect() to external process. Will not be drained") 
        (sockfd) (inet_addr) (port);
      return true;
    }
  }
  return false;
}

int connect ( int sockfd,  const  struct sockaddr *serv_addr, socklen_t addrlen )
{
  if (_isBlacklistedTcp(sockfd, serv_addr, addrlen)) {
    errno = ECONNREFUSED;
    return -1;
  }
#ifdef RECORD_REPLAY
  BASIC_SYNC_WRAPPER(int, connect, _almost_real_connect, sockfd, serv_addr, addrlen);
#else
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
#endif
}

int bind ( int sockfd,  const struct  sockaddr  *my_addr,  socklen_t addrlen )
{
#ifdef RECORD_REPLAY
  BASIC_SYNC_WRAPPER(int, bind, _almost_real_bind, sockfd, my_addr, addrlen);
#else
  PASSTHROUGH_DMTCP_HELPER ( bind, sockfd, my_addr, addrlen );
#endif
}

int listen ( int sockfd, int backlog )
{
#ifdef RECORD_REPLAY
  BASIC_SYNC_WRAPPER(int, listen, _almost_real_listen, sockfd, backlog);
#else
  PASSTHROUGH_DMTCP_HELPER ( listen, sockfd, backlog );
#endif
}

#ifdef RECORD_REPLAY
static int _almost_real_accept(int sockfd, struct sockaddr *addr,
    socklen_t *addrlen)
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

static int _almost_real_setsockopt(int sockfd, int level, int optname,
    const void *optval, socklen_t optlen)
{
  PASSTHROUGH_DMTCP_HELPER ( setsockopt,sockfd,level,optname,optval,optlen );
}
#endif

int accept ( int sockfd, struct sockaddr *addr, socklen_t *addrlen )
{
#ifdef RECORD_REPLAY
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    int retval = _almost_real_accept(sockfd, addr, addrlen);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retval;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    int retval = _almost_real_accept(sockfd, addr, addrlen);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retval;
  }
  int retval = 0;
  log_entry_t my_entry = create_accept_entry(my_clone_id, accept_event, sockfd,
      (unsigned long int)addr, (unsigned long int)addrlen);
  log_entry_t my_return_entry = create_accept_entry(my_clone_id, 
      accept_event_return, sockfd, (unsigned long int)addr,
      (unsigned long int)addrlen);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &accept_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &accept_turn_check);
    // Set the errno to what was logged (e.g. EAGAIN).
    if (GET_COMMON(currentLogEntry, my_errno) != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
      retval = GET_COMMON(currentLogEntry, retval);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _almost_real_accept(sockfd, addr, addrlen);
    SET_COMMON(my_return_entry, retval);
    if (retval == -1)
      SET_COMMON2(my_return_entry, my_errno, errno);
    addNextLogEntry(my_return_entry);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
#else
  if ( addr == NULL || addrlen == NULL )
  {
    struct sockaddr_storage tmp_addr;
    socklen_t tmp_len = 0;
    memset ( &tmp_addr,0,sizeof ( tmp_addr ) );
    PASSTHROUGH_DMTCP_HELPER ( accept, sockfd, ( ( struct sockaddr * ) &tmp_addr ) , ( &tmp_len ) );
  }
  else
    PASSTHROUGH_DMTCP_HELPER ( accept, sockfd, addr, addrlen );
#endif
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)) && __GLIBC_PREREQ(2,10)
int accept4 ( int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags )
{
  if ( addr == NULL || addrlen == NULL )
  {
    struct sockaddr_storage tmp_addr;
    socklen_t tmp_len = 0;
    memset ( &tmp_addr,0,sizeof ( tmp_addr ) );
    PASSTHROUGH_DMTCP_HELPER ( accept4, sockfd, ( ( struct sockaddr * ) &tmp_addr ) , ( &tmp_len ), flags );
  }
  else
    PASSTHROUGH_DMTCP_HELPER ( accept4, sockfd, addr, addrlen, flags );
}
#endif

#ifdef RECORD_REPLAY
int getsockname ( int sockfd, struct sockaddr *addr, socklen_t *addrlen )
{
  // TODO: This wrapper is incomplete. We don't actually restore the contents
  // of 'addr'. This is ok as long as MySQL is not using the contents of 'addr'
  // after this call. All I've seen so far is it using this as an error check.
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    int retval = _real_getsockname(sockfd, addr, addrlen);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retval;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    int retval = _real_getsockname(sockfd, addr, addrlen);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retval;
  }
  int retval = 0;
  log_entry_t my_entry = create_getsockname_entry(my_clone_id, getsockname_event,
      sockfd, (unsigned long int)addr, (unsigned long int)addrlen);
  log_entry_t my_return_entry = create_getsockname_entry(my_clone_id,
      getsockname_event_return,
      sockfd, (unsigned long int)addr, (unsigned long int)addrlen);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &getsockname_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &getsockname_turn_check);
    // Don't call _real_ function. Lie to the user.
    // Set the errno to what was logged (e.g. EAGAIN).
    if (GET_COMMON(currentLogEntry, my_errno) != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    retval = GET_COMMON(currentLogEntry, retval);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_getsockname(sockfd, addr, addrlen);
    SET_COMMON(my_return_entry, retval);
    if (retval == -1)
      SET_COMMON2(my_return_entry, my_errno, errno);
    addNextLogEntry(my_return_entry);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
  // TODO: This wrapper is incomplete. We don't actually restore the contents
  // of 'addr'. This is ok as long as MySQL is not using the contents of 'addr'
  // after this call. All I've seen so far is it using this as an error check.
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    int retval = _real_getpeername(sockfd, addr, addrlen);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retval;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    int retval = _real_getpeername(sockfd, addr, addrlen);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retval;
  }
  int retval = 0;
  log_entry_t my_entry = create_getpeername_entry(my_clone_id, getpeername_event,
      sockfd, *addr, (unsigned long int)addrlen);
  log_entry_t my_return_entry = create_getpeername_entry(my_clone_id,
      getpeername_event_return,
      sockfd, *addr, (unsigned long int)addrlen);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &getpeername_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &getpeername_turn_check);
    // Don't call _real_ function. Lie to the user.
    // Set the errno to what was logged (e.g. EAGAIN).
    if (GET_COMMON(currentLogEntry, my_errno) != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    *addr = GET_FIELD(currentLogEntry, getpeername, sockaddr);
    retval = GET_COMMON(currentLogEntry, retval);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_getpeername(sockfd, addr, addrlen);
    SET_COMMON(my_return_entry, retval);
    SET_FIELD2(my_return_entry, getpeername, sockaddr, *addr);
    if (retval == -1)
      SET_COMMON2(my_return_entry, my_errno, errno);
    addNextLogEntry(my_return_entry);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}
#endif

int setsockopt ( int sockfd, int  level,  int  optname,  const  void  *optval,
                 socklen_t optlen )
{
#ifdef RECORD_REPLAY
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    int retval = _almost_real_setsockopt(sockfd,level,optname,optval,optlen);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retval;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    int retval = _almost_real_setsockopt(sockfd,level,optname,optval,optlen);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retval;
  }
  int retval = 0;
  log_entry_t my_entry = create_setsockopt_entry(my_clone_id, setsockopt_event, sockfd, 
      level, optname, (unsigned long int)optval, optlen);
  log_entry_t my_return_entry = create_setsockopt_entry(my_clone_id, 
      setsockopt_event_return, sockfd, level, optname, (unsigned long int)optval,
      optlen);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &setsockopt_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &setsockopt_turn_check);
    // Set the errno to what was logged (e.g. EAGAIN).
    if (GET_COMMON(currentLogEntry, my_errno) != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    retval = GET_COMMON(currentLogEntry, retval);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _almost_real_setsockopt(sockfd,level,optname,optval,optlen);
    SET_COMMON(my_return_entry, retval);
    if (retval == -1)
      SET_COMMON2(my_return_entry, my_errno, errno);
    addNextLogEntry(my_return_entry);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
#else
  PASSTHROUGH_DMTCP_HELPER ( setsockopt,sockfd,level,optname,optval,optlen );
#endif
}

}
