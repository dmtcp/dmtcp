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

#include "sockettable.h"
#include  "../jalib/jassert.h"
#include "dmtcpmessagetypes.h"
#include  "../jalib/jsocket.h"
#include "dmtcpworker.h"
#include "syscallwrappers.h"
#include  "../jalib/jfilesystem.h"
#include <netdb.h>
#include <map>
#include <sys/types.h>
#include <sys/socket.h>
#include "connectionidentifier.h"
#include "connectionmanager.h"
#include "connectionmanager.h"

// All dmtcp_on_XXX functions are called automatically after a successful user
// function call.

extern "C"
int dmtcp_on_socket(int ret, int domain, int type, int protocol)
{

  JTRACE("socket created") (ret) (domain) (type) (protocol);

  dmtcp::Connection *con;
  if ((type & 0xff) == SOCK_RAW) {
    JASSERT(domain == AF_NETLINK) (domain) (type)
      .Text("Only Netlink Raw sockets supported");
    con = new dmtcp::RawSocketConnection(domain, type, protocol);
  } else {
    con = new dmtcp::TcpConnection(domain, type, protocol);
  }
  dmtcp::KernelDeviceToConnection::instance().create(ret, con);

  return ret;
}

extern "C"
int dmtcp_on_connect(int ret, int sockfd, const struct sockaddr *serv_addr,
                     socklen_t addrlen)
{
  dmtcp::TcpConnection& con =
    dmtcp::KernelDeviceToConnection::instance().retrieve(sockfd).asTcp();
  con.onConnect(sockfd, serv_addr, addrlen);

#if HANDSHAKE_ON_CONNECT == 1
  JTRACE("connected, sending 1-way handshake") (sockfd) (con.id());
  jalib::JSocket remote(sockfd);
  con.sendHandshake(remote, dmtcp::DmtcpWorker::instance().coordinatorId());
  JTRACE("1-way handshake sent");
#else
  JTRACE("connected") (sockfd) (con.id());
#endif

  return ret;
}

extern "C"
int dmtcp_on_bind(int ret, int sockfd, const struct sockaddr *my_addr,
                  socklen_t my_addrlen)
{
  struct sockaddr_storage addr;
  socklen_t               addrlen;

  // Do not rely on the address passed on to bind as it may contain port 0
  // which allows the OS to give any unused port. Thus we look ourselves up
  // using getsockname.
  JASSERT(getsockname(sockfd, (struct sockaddr *)&addr, &addrlen) == 0) (JASSERT_ERRNO);

  dmtcp::TcpConnection& con =
    dmtcp::KernelDeviceToConnection::instance().retrieve(sockfd).asTcp();

  con.onBind((struct sockaddr*) &addr, addrlen);
  JTRACE("bind") (sockfd) (con.id());
  return ret;
}

extern "C"
int dmtcp_on_listen(int ret, int sockfd, int backlog)
{
  dmtcp::TcpConnection& con =
    dmtcp::KernelDeviceToConnection::instance().retrieve(sockfd).asTcp();

  con.onListen(backlog);

  JTRACE("listen") (sockfd) (con.id()) (backlog);
  return ret;
}

extern "C"
int dmtcp_on_accept(int ret, int sockfd, struct sockaddr *addr,
                    socklen_t *addrlen)
{
  dmtcp::TcpConnection& parent =
    dmtcp::KernelDeviceToConnection::instance().retrieve(sockfd).asTcp();

  dmtcp::TcpConnection* con =
    new dmtcp::TcpConnection(parent, dmtcp::ConnectionIdentifier::Null());
  dmtcp::KernelDeviceToConnection::instance().create(ret, con);

#if HANDSHAKE_ON_CONNECT == 1
  JTRACE("accepted, waiting for 1-way handshake") (sockfd) (con->id());
  jalib::JSocket remote(ret);
  con->recvHandshake(remote, dmtcp::DmtcpWorker::instance().coordinatorId());
  JTRACE("1-way handshake received") (con->getRemoteId());
#else
  JTRACE("accepted incoming connection") (sockfd) (con->id());
#endif

  return ret;
}

//#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)) && __GLIBC_PREREQ(2,10)
extern "C"
int dmtcp_on_accept4(int ret, int sockfd, struct sockaddr *addr,
                     socklen_t *addrlen, int flags)
{
  return dmtcp_on_accept(ret, sockfd, addr, addrlen);
}
//#endif

///called automatically when a socket error is returned by user function
extern "C"
int dmtcp_on_error(int ret, int sockfd, const char* fname, int savedErrno)
{
  //Ignore EAGAIN errors
  if (savedErrno == EAGAIN) return ret;
  if (savedErrno == EADDRINUSE && strncmp(fname, "bind", 4) == 0)
    return ret;

  JTRACE("socket error") (fname) (ret) (sockfd) (strerror(savedErrno));

  // We shouldn't be marking a socket as TCP_ERROR. It can be undesired for any
  // system call other than socket().
#if 0
  dmtcp::Connection& con = dmtcp::KernelDeviceToConnection::instance().retrieve(sockfd);

  if (con.conType() == dmtcp::Connection::TCP)
  {
    con.asTcp().onError();
  }
#endif

  return ret;
}

extern "C"
int dmtcp_on_setsockopt(int ret, int sockfd, int level, int optname,
                        const void *optval, socklen_t optlen)
{
  JTRACE("setsockopt") (ret) (sockfd) (optname);

  dmtcp::TcpConnection& con =
    dmtcp::KernelDeviceToConnection::instance().retrieve(sockfd).asTcp();

  con.addSetsockopt(level, optname,(char*) optval, optlen);

  return ret;
}

extern "C"
int dmtcp_on_getsockopt(int ret, int sockfd, int level, int optname,
                        void *optval, socklen_t* optlen)
{
  JTRACE("getsockopt") (ret) (sockfd) (optname);
  return ret;
}
