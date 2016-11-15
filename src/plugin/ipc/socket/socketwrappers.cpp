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
#include "../jalib/jassert.h"
#include "../jalib/jfilesystem.h"
#include "socketconnection.h"
#include "socketconnlist.h"
#include "socketwrappers.h"

using namespace dmtcp;

/* getaddrinfo() (and possibly getnameinfo()) open socket to external services
 * (DNS etc.) to resolve hostname/address etc. In doing so, the call to
 * socket() is captured by our wrappers, however, the call to close() is
 * internal and thus not captured by the close wrapper. This results in a stale
 * connection. To avoid this problem, we disable socket processing for the
 * thread calling getaddrinfo().
 */
static __thread bool _doNotProcessSockets = false;

extern "C" int
socket(int domain, int type, int protocol)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int ret = _real_socket(domain, type, protocol);
  if (ret != -1 && dmtcp_is_running_state() && !_doNotProcessSockets) {
    Connection *con;
    JTRACE("socket created") (ret) (domain) (type) (protocol);
    if ((type & 0xff) == SOCK_RAW) {
      JASSERT(domain == AF_NETLINK) (domain) (type)
      .Text("Only Netlink Raw sockets supported");
      con = new RawSocketConnection(domain, type, protocol);
    } else {
      con = new TcpConnection(domain, type, protocol);
    }
    SocketConnList::instance().add(ret, con);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

extern "C" int
connect(int sockfd, const struct sockaddr *serv_addr, socklen_t addrlen)
{
  DMTCP_PLUGIN_DISABLE_CKPT(); // The lock is released inside the macro.

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
      JTRACE("Connect operation on unsupported socket type.");
    } else {
      con->onConnect(serv_addr, addrlen, (ret == -1 && errno == EINPROGRESS));
    }
  }

  errno = savedErrno;
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

extern "C" int
bind(int sockfd, const struct sockaddr *my_addr, socklen_t addrlen)
{
  DMTCP_PLUGIN_DISABLE_CKPT(); // The lock is released inside the macro.
  int ret = _real_bind(sockfd, my_addr, addrlen);
  if (ret != -1 && dmtcp_is_running_state() && !_doNotProcessSockets) {
    SocketConnection *con =
      dynamic_cast<SocketConnection *>(SocketConnList::instance().getConnection(
                                         sockfd));
    if (con == NULL) {
      JTRACE("bind operation on unsupported socket type.");
    } else {
      con->onBind((struct sockaddr *)my_addr, addrlen);
    }
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

extern "C" int
listen(int sockfd, int backlog)
{
  DMTCP_PLUGIN_DISABLE_CKPT(); // The lock is released inside the macro.
  int ret = _real_listen(sockfd, backlog);
  if (ret != -1 && dmtcp_is_running_state() && !_doNotProcessSockets) {
    SocketConnection *con =
      dynamic_cast<SocketConnection *>(SocketConnList::instance().getConnection(
                                         sockfd));
    if (con == NULL) {
      JTRACE("listen operation on unsupported socket type.");
    } else {
      con->onListen(backlog);
    }
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

static void
process_accept(int ret, int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
  JASSERT(ret != -1);
  Connection *parent = SocketConnList::instance().getConnection(sockfd);
  if (parent == NULL) {
    JTRACE("unable to get the connection.");
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
    JTRACE("accept operation on unsupported socket type.");
    return;
  } else {
    SocketConnList::instance().add(ret, dynamic_cast<Connection *>(con));
  }
}

extern "C" int
accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
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
  int ret = _real_accept(sockfd, addr, addrlen);
  if (ret != -1 && dmtcp_is_running_state() && !_doNotProcessSockets) {
    process_accept(ret, sockfd, addr, addrlen);
  }
  return ret;
}

extern "C" int
accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
  // Look at the comment for accept()
  struct sockaddr_storage tmp_addr;
  socklen_t tmp_len = 0;

  if (addr == NULL || addrlen == NULL) {
    memset(&tmp_addr, 0, sizeof(tmp_addr));
    addr = (struct sockaddr *)&tmp_addr;
    addrlen = &tmp_len;
  }
  int ret = _real_accept4(sockfd, addr, addrlen, flags);
  if (ret != -1 && dmtcp_is_running_state() && !_doNotProcessSockets) {
    process_accept(ret, sockfd, addr, addrlen);
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
  int ret = _real_setsockopt(sockfd, level, optname, optval, optlen);

  if (ret != -1 && dmtcp_is_running_state() && !_doNotProcessSockets) {
    JTRACE("setsockopt") (ret) (sockfd) (optname);
    SocketConnection *con =
      dynamic_cast<SocketConnection *>(SocketConnList::instance().getConnection(
                                         sockfd));
    if (con == NULL) {
      JTRACE("setsockopt operation on unsupported socket type.");
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
  DMTCP_PLUGIN_DISABLE_CKPT();

  JASSERT(sv != NULL);
  int rv = _real_socketpair(d, type, protocol, sv);
  if (rv != -1 && dmtcp_is_running_state() && !_doNotProcessSockets) {
    JTRACE("socketpair()") (sv[0]) (sv[1]);

    TcpConnection *a, *b;

    a = new TcpConnection(d, type, protocol);
    a->onConnect();
    b = new TcpConnection(*a, a->id());

    SocketConnList::instance().add(sv[0], a);
    SocketConnList::instance().add(sv[1], b);
  }

  DMTCP_PLUGIN_ENABLE_CKPT();

  return rv;
}

extern "C" int
getaddrinfo(const char *node,
            const char *service,
            const struct addrinfo *hints,
            struct addrinfo **res)
{
  DMTCP_PLUGIN_DISABLE_CKPT();

  // See comment near definition of _doNotProcessSockets;
  _doNotProcessSockets = true;
  int ret = _real_getaddrinfo(node, service, hints, res);
  _doNotProcessSockets = false;
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

extern "C" int
getnameinfo(const struct sockaddr *sa,
            socklen_t salen,
            char *host,
            size_t hostlen,
            char *serv,
            size_t servlen,
            int flags)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  _doNotProcessSockets = true;
  int ret = _real_getnameinfo(sa, salen, host, hostlen, serv, servlen, flags);
  _doNotProcessSockets = false;
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

extern "C" struct hostent *
gethostbyname(const char *name)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  _doNotProcessSockets = true;
  struct hostent *ret = _real_gethostbyname(name);
  _doNotProcessSockets = false;
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

extern "C" struct hostent *
gethostbyaddr(const void *addr, socklen_t len, int type)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  _doNotProcessSockets = true;
  struct hostent *ret = _real_gethostbyaddr(addr, len, type);
  _doNotProcessSockets = false;
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}
