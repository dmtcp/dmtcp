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
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/socket.h>
#include <unistd.h>

#include "jsocket.h"
#include "dmtcp.h"
#include "protectedfds.h"
#include "util.h"

#include "connectionrewirer.h"
#include "socketconnection.h"
#include "socketwrappers.h"

using namespace dmtcp;

// FIXME: IP6 Support disabled for now. However, we do go through the exercise
// of creating the restore socket and all.
// #define ENABLE_IP6_SUPPORT
static void
markSocketNonBlocking(int sockfd)
{
  // Remove O_NONBLOCK flag from listener socket
  int flags = _real_fcntl(sockfd, F_GETFL, NULL);

  JASSERT(flags != -1);
  JASSERT(_real_fcntl(sockfd, F_SETFL,
                      (void *)(long)(flags | O_NONBLOCK)) != -1);
}

static void
markSocketBlocking(int sockfd)
{
  // Remove O_NONBLOCK flag from listener socket
  int flags = _real_fcntl(sockfd, F_GETFL, NULL);

  JASSERT(flags != -1);
  JASSERT(_real_fcntl(sockfd, F_SETFL,
                      (void *)(long)(flags & ~O_NONBLOCK)) != -1);
}

static ConnectionRewirer *theRewirer = NULL;
ConnectionRewirer&
ConnectionRewirer::instance()
{
  if (theRewirer == NULL) {
    theRewirer = new ConnectionRewirer();
  }
  return *theRewirer;
}

void
ConnectionRewirer::destroy()
{
  dmtcp_close_protected_fd(PROTECTED_RESTORE_IP4_SOCK_FD);
  dmtcp_close_protected_fd(PROTECTED_RESTORE_IP6_SOCK_FD);
  dmtcp_close_protected_fd(PROTECTED_RESTORE_UDS_SOCK_FD);

  // Free up the object.
  delete theRewirer;
  theRewirer = NULL;
}

void
ConnectionRewirer::checkForPendingIncoming(int restoreSockFd,
                                           ConnectionListT *conList)
{
  while (conList->size() > 0) {
    int fd = _real_accept(restoreSockFd, NULL, NULL);
    if (fd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return;
    }
    JASSERT(fd != -1) (JASSERT_ERRNO).Text("Accept failed.");
    ConnectionIdentifier id;
    JASSERT(Util::readAll(fd, &id, sizeof id) == sizeof id);

    iterator i = conList->find(id);
    JASSERT(i != conList->end()) (id)
    .Text("got unexpected incoming restore request");

    (i->second)->restoreDupFds(fd);

    JTRACE("restoring incoming connection") (id);
    conList->erase(i);
  }
}

void
ConnectionRewirer::doReconnect()
{
  iterator i;

  for (i = _pendingOutgoing.begin(); i != _pendingOutgoing.end(); i++) {
    const ConnectionIdentifier &id = i->first;
    Connection *con = i->second;
    struct RemoteAddr &remoteAddr = _remoteInfo[id];
    int fd = con->getFds()[0];
    errno = 0;
    JASSERT(_real_connect(fd, (sockaddr *)&remoteAddr.addr, remoteAddr.len)
            == 0)
      (id) (JASSERT_ERRNO).Text("failed to restore connection");

    Util::writeAll(fd, &id, sizeof id);

    checkForPendingIncoming(PROTECTED_RESTORE_IP4_SOCK_FD,
                            &_pendingIP4Incoming);
    checkForPendingIncoming(PROTECTED_RESTORE_IP6_SOCK_FD,
                            &_pendingIP6Incoming);
    checkForPendingIncoming(PROTECTED_RESTORE_UDS_SOCK_FD,
                            &_pendingUDSIncoming);
  }
  _pendingOutgoing.clear();
  _remoteInfo.clear();

  if (_pendingIP4Incoming.size() > 0) {
    // Add O_NONBLOCK flag to the listener sockets.
    markSocketBlocking(PROTECTED_RESTORE_IP4_SOCK_FD);
    checkForPendingIncoming(PROTECTED_RESTORE_IP4_SOCK_FD,
                            &_pendingIP4Incoming);
    _real_close(PROTECTED_RESTORE_IP4_SOCK_FD);
  }
  if (_pendingIP6Incoming.size() > 0) {
    // Add O_NONBLOCK flag to the listener sockets.
    markSocketBlocking(PROTECTED_RESTORE_IP6_SOCK_FD);
    checkForPendingIncoming(PROTECTED_RESTORE_IP6_SOCK_FD,
                            &_pendingIP6Incoming);
    _real_close(PROTECTED_RESTORE_IP6_SOCK_FD);
  }
  if (_pendingUDSIncoming.size() > 0) {
    // Add O_NONBLOCK flag to the listener sockets.
    markSocketBlocking(PROTECTED_RESTORE_UDS_SOCK_FD);
    checkForPendingIncoming(PROTECTED_RESTORE_UDS_SOCK_FD,
                            &_pendingUDSIncoming);
    _real_close(PROTECTED_RESTORE_UDS_SOCK_FD);
  }
  JTRACE("Closed restore sockets");
}

void
ConnectionRewirer::openRestoreSocket(bool hasIPv4Sock,
                                     bool hasIPv6Sock,
                                     bool hasUNIXSock)
{
  memset(&_ip4RestoreAddr, 0, sizeof(_ip4RestoreAddr));
  memset(&_ip6RestoreAddr, 0, sizeof(_ip6RestoreAddr));
  memset(&_udsRestoreAddr, 0, sizeof(_udsRestoreAddr));

  // Open IP4 Restore Socket
  if (hasIPv4Sock) {
    // Bind and listen on all local interfaces
    //
    // In order to initialize _ip4RestoreAddr.sin_addr, we'd like to access the
    // socket address of the restoreSocket object (line 181). Unfortunately,
    // the jalib::JServerSocket class does not provide any interface that can
    // allow us to access the socket address being used by the socket. So,
    // sockAddr is introducted to create the JServerSock and also to initialize
    // _ip4RestoreAddr later.
    jalib::JSockAddr sockAddr(jalib::JSockAddr::ANY);
    jalib::JServerSocket restoreSocket(sockAddr, 0);
    JASSERT(restoreSocket.isValid());
    restoreSocket.changeFd(PROTECTED_RESTORE_IP4_SOCK_FD);

    // Setup restore socket for name service
    _ip4RestoreAddr.sin_family = AF_INET;
    memcpy(&_ip4RestoreAddr.sin_addr,
           &sockAddr.addr()->sin_addr,
           sizeof(_ip4RestoreAddr.sin_addr));
    _ip4RestoreAddr.sin_port = htons(restoreSocket.port());
    _ip4RestoreAddrlen = sizeof(_ip4RestoreAddr);

    JTRACE("opened listen socket") (restoreSocket.sockfd())
      (inet_ntoa(_ip4RestoreAddr.sin_addr)) (ntohs(_ip4RestoreAddr.sin_port));
    markSocketNonBlocking(PROTECTED_RESTORE_IP4_SOCK_FD);
  }

  // Open IP6 Restore Socket
  if (hasIPv6Sock) {
    int ip6fd = _real_socket(AF_INET6, SOCK_STREAM, 0);
    JASSERT(ip6fd != -1) (JASSERT_ERRNO);

    _ip6RestoreAddr.sin6_family = AF_INET6;
    _ip6RestoreAddr.sin6_port = 0;
    _ip6RestoreAddr.sin6_addr = in6addr_any;
    _ip6RestoreAddrlen = sizeof(_ip6RestoreAddr);
    JASSERT(_real_bind(ip6fd, (struct sockaddr *)&_ip6RestoreAddr,
                       _ip6RestoreAddrlen) == 0)
      (JASSERT_ERRNO);
    JASSERT(getsockname(ip6fd, (struct sockaddr *)&_ip6RestoreAddr,
                        &_ip6RestoreAddrlen) == 0)
      (JASSERT_ERRNO);
    JASSERT(_real_listen(ip6fd, 32) == 0) (JASSERT_ERRNO);
    Util::changeFd(ip6fd, PROTECTED_RESTORE_IP6_SOCK_FD);

    JTRACE("opened ip6 listen socket") (PROTECTED_RESTORE_IP6_SOCK_FD);
    markSocketNonBlocking(PROTECTED_RESTORE_IP6_SOCK_FD);
  }

  // Open UDS Restore Socket
  if (hasUNIXSock) {
    ostringstream o;
    o << dmtcp_get_uniquepid_str() << "_" << dmtcp_get_coordinator_timestamp();
    string str = o.str();
    int udsfd = _real_socket(AF_UNIX, SOCK_STREAM, 0);
    JASSERT(udsfd != -1);
    memset(&_udsRestoreAddr, 0, sizeof(struct sockaddr_un));
    _udsRestoreAddr.sun_family = AF_UNIX;
    strncpy(&_udsRestoreAddr.sun_path[1], str.c_str(), str.length());
    _udsRestoreAddrlen = sizeof(sa_family_t) + str.length() + 1;
    JASSERT(_real_bind(udsfd, (struct sockaddr *)&_udsRestoreAddr,
                       _udsRestoreAddrlen) == 0)
      (JASSERT_ERRNO);
    JASSERT(_real_listen(udsfd, 32) == 0) (JASSERT_ERRNO);
    Util::changeFd(udsfd, PROTECTED_RESTORE_UDS_SOCK_FD);

    JTRACE("opened UDS listen socket")
      (PROTECTED_RESTORE_UDS_SOCK_FD) (&_udsRestoreAddr.sun_path[1]);
    markSocketNonBlocking(PROTECTED_RESTORE_UDS_SOCK_FD);
  }
}

void
ConnectionRewirer::registerIncoming(const ConnectionIdentifier &local,
                                    Connection *con,
                                    int domain)
{
  JASSERT(domain == AF_INET || domain == AF_INET6 || domain == AF_UNIX)
    (domain).Text("Unsupported domain.");

  if (domain == AF_INET) {
    _pendingIP4Incoming[local] = con;
  } else if (domain == AF_INET6) {
#ifdef ENABLE_IP6_SUPPORT
    _pendingIP6Incoming[local] = con;
#else // ifdef ENABLE_IP6_SUPPORT
    _pendingIP4Incoming[local] = con;
#endif // ifdef ENABLE_IP6_SUPPORT
  } else if (domain == AF_UNIX) {
    _pendingUDSIncoming[local] = con;
  } else {
    JASSERT(false).Text("Not implemented");
  }

  JTRACE("announcing pending incoming") (local);
}

void
ConnectionRewirer::registerOutgoing(const ConnectionIdentifier &remote,
                                    Connection *con)
{
  _pendingOutgoing[remote] = con;
  JTRACE("announcing pending outgoing") (remote);
}

void
ConnectionRewirer::registerNSData()
{
  registerNSData((void *)&_ip4RestoreAddr, _ip4RestoreAddrlen,
                 &_pendingIP4Incoming);
  registerNSData((void *)&_ip6RestoreAddr, _ip6RestoreAddrlen,
                 &_pendingIP6Incoming);
  registerNSData((void *)&_udsRestoreAddr, _udsRestoreAddrlen,
                 &_pendingUDSIncoming);
}

void
ConnectionRewirer::registerNSData(void *addr,
                                  socklen_t addrLen,
                                  ConnectionListT *conList)
{
  iterator i;

  JASSERT(theRewirer != NULL);
  for (i = conList->begin(); i != conList->end(); ++i) {
    const ConnectionIdentifier &id = i->first;
    dmtcp_send_key_val_pair_to_coordinator("Socket",
                                           (const void *)&id,
                                           (uint32_t)sizeof(id),
                                           addr,
                                           (uint32_t)addrLen);

    /*
    sockaddr_in *sn = (sockaddr_in*) &_restoreAddr;
    unsigned short port = htons(sn->sin_port);
    char *ip = inet_ntoa(sn->sin_addr);
    JTRACE("Send NS information:")(id)(sn->sin_family)(port)(ip);
    */
  }

  // debugPrint();
}

void
ConnectionRewirer::sendQueries()
{
  iterator i;

  for (i = _pendingOutgoing.begin(); i != _pendingOutgoing.end(); ++i) {
    const ConnectionIdentifier &id = i->first;
    struct RemoteAddr remote;
    uint32_t len = sizeof(remote.addr);
    JASSERT(dmtcp_send_query_to_coordinator("Socket",
                                            (const void *)&id,
                                            (uint32_t)sizeof(id),
                                            &remote.addr,
                                            &len) != 0);
    remote.len = len;

    /*
    sockaddr_in *sn = (sockaddr_in*) &remote.addr;
    unsigned short port = htons(sn->sin_port);
    char *ip = inet_ntoa(sn->sin_addr);
    JTRACE("Send Queries. Get remote from coordinator:")(id)(sn->sin_family)(port)(ip);
    */
    _remoteInfo[id] = remote;
  }
}

#if 0
void
ConnectionRewirer::debugPrint() const
{
# ifdef LOGGING
  ostringstream o;
  o << "Pending Incoming:\n";
  const_iterator i;
  for (i = _pendingIncoming.begin(); i != _pendingIncoming.end(); ++i) {
    Connection *con = i->second;
    o << i->first << " numFds=" << con->getFds().size()
      << " firstFd=" << con->getFds()[0] << '\n';
  }
  o << "Pending Outgoing:\n";
  for (i = _pendingOutgoing.begin(); i != _pendingOutgoing.end(); ++i) {
    Connection *con = i->second;
    o << i->first << " numFds=" << con->getFds().size()
      << " firstFd=" << con->getFds()[0] << '\n';
  }
  JNOTE("Pending connections") (o.str());
# endif // ifdef LOGGING
}
#endif // if 0
