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

#include <arpa/inet.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/un.h>

/* According to earlier standards */
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include "pluginmanager.h"
#include "../jalib/jfilesystem.h"
#include "socketconnection.h"
#include "socketconnlist.h"
#include "socketwrappers.h"
#include "dmtcp_assert.h"
#include "wrapperlock.h"

using namespace dmtcp;

/* getaddrinfo() (and possibly getnameinfo()) open socket to external services
 * (DNS etc.) to resolve hostname/address etc. In doing so, the call to
 * socket() is captured by our wrappers, however, the call to close() is
 * internal and thus not captured by the close wrapper. This results in a stale
 * connection. To avoid this problem, we disable socket processing for the
 * thread calling getaddrinfo().
 */
static __thread bool _doNotProcessSockets ATTR_TLS_INITIAL_EXEC = false;

extern "C" int
socket(int domain, int type, int protocol)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SOCKET)) {
    return _real_socket(domain, type, protocol);
  }

  WrapperLock wrapperLock;
  int ret = _real_socket(domain, type, protocol);
  if (ret != -1 && dmtcp_is_running_state() && !_doNotProcessSockets) {
    Connection *con;
    TRACE("Created socket: fd={} domain={} type={} protocol={}",
          ret, domain, type, protocol);
    if ((type & 0xff) == SOCK_RAW) {
      ASSERT(domain == AF_NETLINK,
             "only Netlink raw sockets supported: domain={} type={}",
             domain, type);
      con = new RawSocketConnection(domain, type, protocol);
    } else {
      con = new TcpConnection(domain, type, protocol);
    }
    SocketConnList::instance().add(ret, con);
  }
  return ret;
}

extern "C" int
connect(int sockfd, const struct sockaddr *serv_addr, socklen_t addrlen)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SOCKET)) {
    return _real_connect(sockfd, serv_addr, addrlen);
  }

  WrapperLock wrapperLock;

  int ret = _real_connect(sockfd, serv_addr, addrlen);
  int savedErrno = errno; // Save errno to prevent modifications by the
                          // following code
  if ((ret != -1 || errno == EINPROGRESS) &&
      dmtcp_is_running_state() &&
      !_doNotProcessSockets) {
    SocketConnection *con =
      dynamic_cast<SocketConnection *>(SocketConnList::instance().getConnection(
                                         sockfd));
    if (con == NULL) {
      TRACE("Called connect() on untracked socket type: fd={}", sockfd);
    } else {
      con->onConnect(serv_addr, addrlen, (ret == -1 && errno == EINPROGRESS));
    }
  }

  errno = savedErrno;
  return ret;
}

extern "C" int
bind(int sockfd, const struct sockaddr *my_addr, socklen_t addrlen)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SOCKET)) {
    return _real_bind(sockfd, my_addr, addrlen);
  }

  WrapperLock wrapperLock;
  int ret = _real_bind(sockfd, my_addr, addrlen);
  if (ret != -1 && dmtcp_is_running_state() && !_doNotProcessSockets) {
    SocketConnection *con =
      dynamic_cast<SocketConnection *>(SocketConnList::instance().getConnection(
                                         sockfd));
    if (con == NULL) {
      TRACE("Called bind() on untracked socket type: fd={}", sockfd);
    } else {
      con->onBind((struct sockaddr *)my_addr, addrlen);
    }
  }
  return ret;
}

extern "C" int
listen(int sockfd, int backlog)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SOCKET)) {
    return _real_listen(sockfd, backlog);
  }

  WrapperLock wrapperLock;
  int ret = _real_listen(sockfd, backlog);
  if (ret != -1 && dmtcp_is_running_state() && !_doNotProcessSockets) {
    SocketConnection *con =
      dynamic_cast<SocketConnection *>(SocketConnList::instance().getConnection(
                                         sockfd));
    if (con == NULL) {
      TRACE("Called listen() on untracked socket type: fd={}", sockfd);
    } else {
      con->onListen(backlog);
    }
  }
  return ret;
}

static void
process_accept(int ret, int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
  ASSERT_NE(-1, ret,
                      "accept result must be valid before socket state update: "
                      "sockfd={}",
                      sockfd);
  Connection *parent = SocketConnList::instance().getConnection(sockfd);
  if (parent == NULL) {
    TRACE("Called accept() on untracked listening socket: fd={}", sockfd);
    return;
  }

  SocketConnection *con = NULL;

  // FIXME: Checking for conType is ugly; fix class design
  if (parent->conType() == Connection::TCP) {
    TcpConnection *tcpParent = dynamic_cast<TcpConnection *>(parent);
    con = new TcpConnection(*tcpParent, ConnectionIdentifier::null());
  } else if (parent->conType() == Connection::RAW) {
    RawSocketConnection *rawSockParent =
      dynamic_cast<RawSocketConnection *>(parent);
    con = new RawSocketConnection(*rawSockParent, ConnectionIdentifier::null());
  }

  if (con == NULL) {
    TRACE("Call to accept() returned unsupported socket type: listen_fd={} "
          "fd={}",
          sockfd, ret);
    return;
  } else {
    SocketConnList::instance().add(ret, dynamic_cast<Connection *>(con));
  }
}

extern "C" int
accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SOCKET)) {
    return _real_accept(sockfd, addr, addrlen);
  }

  /* FIXME: accept() is a blocking call that can alter the process state(by
   * creating a new socket-fd). This can cause problems if it happens at a time
   * when some other thread is processing inside a fork() or exec() wrapper.
   * For more details, please look at the comment in
   * DmtcpWorker::wrapperExecutionLockLockExcl().
   *
   * Since it's a blocking call, we cannot grab the actual wrapper-execution
   * lock here.
   */
  struct sockaddr_storage tmp_addr;
  socklen_t tmp_len = 0;

  if (addr == NULL || addrlen == NULL) {
    memset(&tmp_addr, 0, sizeof(tmp_addr));
    addr = (struct sockaddr *)&tmp_addr;
    addrlen = &tmp_len;
  }
  // This blocking call intentionally does not hold WrapperLock across the real
  // call. State updates after a successful return are protected separately.
  int ret = _real_accept(sockfd, addr, addrlen);
  if (ret != -1) {
    WrapperLock wrapperLock;
    if (dmtcp_is_running_state() && !_doNotProcessSockets) {
      process_accept(ret, sockfd, addr, addrlen);
    }
  }
  return ret;
}

extern "C" int
accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SOCKET)) {
    return _real_accept4(sockfd, addr, addrlen, flags);
  }

  // Look at the comment for accept()
  struct sockaddr_storage tmp_addr;
  socklen_t tmp_len = 0;

  if (addr == NULL || addrlen == NULL) {
    memset(&tmp_addr, 0, sizeof(tmp_addr));
    addr = (struct sockaddr *)&tmp_addr;
    addrlen = &tmp_len;
  }
  // This blocking call intentionally does not hold WrapperLock across the real
  // call. State updates after a successful return are protected separately.
  int ret = _real_accept4(sockfd, addr, addrlen, flags);
  if (ret != -1) {
    WrapperLock wrapperLock;
    if (dmtcp_is_running_state() && !_doNotProcessSockets) {
      process_accept(ret, sockfd, addr, addrlen);
    }
  }
  return ret;
}

extern "C" int
setsockopt(int sockfd,
           int level,
           int optname,
           const void *optval,
           socklen_t optlen)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SOCKET)) {
    return _real_setsockopt(sockfd, level, optname, optval, optlen);
  }

  int ret = _real_setsockopt(sockfd, level, optname, optval, optlen);

  if (ret != -1 && dmtcp_is_running_state() && !_doNotProcessSockets) {
    TRACE("Recording socket option: fd={} level={} option={} size={}",
          sockfd, level, optname, optlen);
    SocketConnection *con =
      dynamic_cast<SocketConnection *>(SocketConnList::instance().getConnection(
                                         sockfd));
    if (con == NULL) {
      TRACE("Called setsockopt() on untracked socket type: fd={}", sockfd);
      return ret;
    } else {
      con->addSetsockopt(level, optname, optval, optlen);
    }
  }
  return ret;
}

#if 0
extern "C" int
getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
  /* We don't want to acquire the lock here as this is not needed. Also,
   * aquiring the lock here might cause a deadlock when this function is called
   * from within connect(). Here is the deadlock situation:
   * User-thread connect():    acquire lock
   * Ckpt-thread ckpt():       Queued on wr-lock
   * User-thread getsockopt(): block on read lock().
   */
  int ret = _real_getsockopt(sockfd, level, optname, optval, optlen);

  PASSTHROUGH_DMTCP_HELPER(getsockopt, sockfd, level, optname, optval, optlen);
}
#endif // if 0

extern "C" int
socketpair(int d, int type, int protocol, int sv[2])
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SOCKET)) {
    return _real_socketpair(d, type, protocol, sv);
  }

  WrapperLock wrapperLock;

  ASSERT_NOT_NULL(sv);
  int rv = _real_socketpair(d, type, protocol, sv);
  if (rv != -1 && dmtcp_is_running_state() && !_doNotProcessSockets) {
    TRACE("Created socketpair: fd0={} fd1={} domain={} type={} protocol={}",
          sv[0], sv[1], d, type, protocol);

    TcpConnection *a, *b;

    a = new TcpConnection(d, type, protocol);
    a->onConnect();
    b = new TcpConnection(*a, a->id());

    SocketConnList::instance().add(sv[0], a);
    SocketConnList::instance().add(sv[1], b);
  }

  return rv;
}

extern "C" int
getaddrinfo(const char *node,
            const char *service,
            const struct addrinfo *hints,
            struct addrinfo **res)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SOCKET)) {
    return _real_getaddrinfo(node, service, hints, res);
  }

  // See comment near definition of _doNotProcessSockets;
  {
    WrapperLock wrapperLock;
    _doNotProcessSockets = true;
  }
  int ret = _real_getaddrinfo(node, service, hints, res);
  {
    WrapperLock wrapperLock;
    _doNotProcessSockets = false;
  }
  return ret;
}

extern "C" int
getnameinfo(const struct sockaddr *sa,
            socklen_t salen,
            char *host,
            socklen_t hostlen,
            char *serv,
            socklen_t servlen,
            int flags)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SOCKET)) {
    return _real_getnameinfo(sa, salen, host, hostlen, serv, servlen, flags);
  }

  {
    WrapperLock wrapperLock;
    _doNotProcessSockets = true;
  }
  int ret = _real_getnameinfo(sa, salen, host, hostlen, serv, servlen, flags);
  {
    WrapperLock wrapperLock;
    _doNotProcessSockets = false;
  }
  return ret;
}

extern "C" struct hostent *
gethostbyname(const char *name)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SOCKET)) {
    return _real_gethostbyname(name);
  }

  {
    WrapperLock wrapperLock;
    _doNotProcessSockets = true;
  }
  struct hostent *ret = _real_gethostbyname(name);
  {
    WrapperLock wrapperLock;
    _doNotProcessSockets = false;
  }
  return ret;
}

extern "C" struct hostent *
gethostbyaddr(const void *addr, socklen_t len, int type)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SOCKET)) {
    return _real_gethostbyaddr(addr, len, type);
  }

  {
    WrapperLock wrapperLock;
    _doNotProcessSockets = true;
  }
  struct hostent *ret = _real_gethostbyaddr(addr, len, type);
  {
    WrapperLock wrapperLock;
    _doNotProcessSockets = false;
  }
  return ret;
}
