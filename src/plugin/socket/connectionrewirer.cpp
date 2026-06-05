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
#include <unistd.h>

#include "jsocket.h"
#include "dmtcp.h"
#include "protectedfds.h"
#include "util.h"
#include "base64.h"
#include "kvdb.h"

#include "connectionrewirer.h"
#include "socketconnection.h"
#include "socketwrappers.h"
#include "util_assert.h"

using namespace dmtcp;
constexpr char const *PeerDiscoveryDbRestart = "/plugin/socket/rst";

// FIXME: IP6 Support disabled for now. However, we do go through the exercise
// of creating the restore socket and all.
// #define ENABLE_IP6_SUPPORT
static void
markSocketNonBlocking(int sockfd)
{
  // Remove O_NONBLOCK flag from listener socket
  int flags = _real_fcntl(sockfd, F_GETFL, NULL);

  ASSERT_SYSCALL_SUCCESS_MSG(flags, "fcntl(F_GETFL) failed: fd={}", sockfd);
  ASSERT_SYSCALL_SUCCESS_MSG(
    _real_fcntl(sockfd, F_SETFL, (void *)(long)(flags | O_NONBLOCK)),
    "fcntl(F_SETFL, O_NONBLOCK) failed: fd={} flags={}", sockfd,
    flags | O_NONBLOCK);
}

static void
markSocketBlocking(int sockfd)
{
  // Remove O_NONBLOCK flag from listener socket
  int flags = _real_fcntl(sockfd, F_GETFL, NULL);

  ASSERT_SYSCALL_SUCCESS_MSG(flags, "fcntl(F_GETFL) failed: fd={}", sockfd);
  ASSERT_SYSCALL_SUCCESS_MSG(
    _real_fcntl(sockfd, F_SETFL, (void *)(long)(flags & ~O_NONBLOCK)),
    "fcntl(F_SETFL, blocking) failed: fd={} flags={}", sockfd,
    flags & ~O_NONBLOCK);
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
  dmtcp_close_protected_fd(PROTECTED_RESTORE_UDS_SEQ_SOCK_FD);

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
    ASSERT_VALID_FD_MSG(fd, "Accept failed: restore_fd={}", restoreSockFd);
    ConnectionIdentifier id;
    ASSERT(Util::readAll(fd, &id, sizeof id) == sizeof id,
           "failed to read incoming restore identifier: fd={} expected={}", fd,
           sizeof id);

    iterator i = conList->find(id);
    ASSERT(i != conList->end(),
           "got unexpected incoming restore request: host={} pid={} "
           "time={} con_id={}",
           id.hostid(), id.pid(), id.time(), id.conId());

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
    ASSERT_SYSCALL_SUCCESS_MSG(_real_connect(fd, (sockaddr *)&remoteAddr.addr, remoteAddr.len),
      "failed to restore connection: fd={} host={} pid={} time={} con_id={}",
      fd, id.hostid(), id.pid(), id.time(), id.conId());

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
  if (_pendingUDSSeqIncoming.size() > 0) {
    // Add O_NONBLOCK flag to the listener sockets.
    markSocketBlocking(PROTECTED_RESTORE_UDS_SEQ_SOCK_FD);
    checkForPendingIncoming(PROTECTED_RESTORE_UDS_SEQ_SOCK_FD,
                            &_pendingUDSSeqIncoming);
    _real_close(PROTECTED_RESTORE_UDS_SEQ_SOCK_FD);
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
  memset(&_udsSeqRestoreAddr, 0, sizeof(_udsSeqRestoreAddr));

  // Open IP4 Restore Socket
  if (hasIPv4Sock) {
    // Bind and listen on all local interfaces
    //
    // In order to initialize _ip4RestoreAddr.sin_addr, we'd like to access the
    // socket address of the restoreSocket object (line 181). Unfortunately,
    // the jalib::JServerSocket class does not provide any interface that can
    // allow us to access the socket address being used by the socket. So,
    // sockAddr is introduced to create the JServerSock and also to initialize
    // _ip4RestoreAddr later.
    jalib::JSockAddr sockAddr(jalib::JSockAddr::ANY);
    jalib::JServerSocket restoreSocket(sockAddr, 0);
    ASSERT(restoreSocket.isValid(), "invalid IPv4 restore socket");
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
    ASSERT_VALID_FD_MSG(ip6fd, "failed to create IPv6 restore socket");

    _ip6RestoreAddr.sin6_family = AF_INET6;
    _ip6RestoreAddr.sin6_port = 0;
    _ip6RestoreAddr.sin6_addr = in6addr_any;
    _ip6RestoreAddrlen = sizeof(_ip6RestoreAddr);
    ASSERT_SYSCALL_SUCCESS_MSG(_real_bind(ip6fd,
                                          (struct sockaddr *)&_ip6RestoreAddr,
                                          _ip6RestoreAddrlen),
                               "failed to bind IPv6 restore socket: fd={}",
                               ip6fd);
    ASSERT_SYSCALL_SUCCESS_MSG(getsockname(ip6fd,
                                           (struct sockaddr *)&_ip6RestoreAddr,
                                           &_ip6RestoreAddrlen),
                               "getsockname failed for IPv6 restore socket: "
                               "fd={}",
                               ip6fd);
    ASSERT_SYSCALL_SUCCESS_MSG(_real_listen(ip6fd, 32),
                 "listen failed for IPv6 restore socket: fd={}", ip6fd);
    Util::changeFd(ip6fd, PROTECTED_RESTORE_IP6_SOCK_FD);

    JTRACE("opened ip6 listen socket") (PROTECTED_RESTORE_IP6_SOCK_FD);
    markSocketNonBlocking(PROTECTED_RESTORE_IP6_SOCK_FD);
  }

  // Open UDS Restore Socket
  if (hasUNIXSock) {
    ostringstream o;
    // We should use the original path along with the unique pid and timestamp.
    // That will ensure that the socket is unique.
    o << dmtcp_get_uniquepid_str() << "_" << dmtcp_get_coordinator_timestamp() << "_" << _udsRestoreAddr.sun_path;
    string str = o.str();
    int udsfd = _real_socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT_VALID_FD_MSG(udsfd, "failed to create UDS restore socket");
    memset(&_udsRestoreAddr, 0, sizeof(struct sockaddr_un));
    _udsRestoreAddr.sun_family = AF_UNIX;
    strncpy(&_udsRestoreAddr.sun_path[1], str.c_str(), str.length());
    _udsRestoreAddrlen = sizeof(sa_family_t) + str.length() + 1;
    ASSERT_SYSCALL_SUCCESS_MSG(_real_bind(udsfd,
                                          (struct sockaddr *)&_udsRestoreAddr,
                                          _udsRestoreAddrlen),
                               "failed to bind UDS restore socket: fd={}",
                               udsfd);
    ASSERT_SYSCALL_SUCCESS_MSG(_real_listen(udsfd, 32),
                 "listen failed for UDS restore socket: fd={}", udsfd);
    Util::changeFd(udsfd, PROTECTED_RESTORE_UDS_SOCK_FD);

    JTRACE("opened UDS listen socket")
      (PROTECTED_RESTORE_UDS_SOCK_FD) (&_udsRestoreAddr.sun_path[1]);
    markSocketNonBlocking(PROTECTED_RESTORE_UDS_SOCK_FD);

    // Also open a seqpacket listener for AF_UNIX SOCK_SEQPACKET reconnections
    int udsseqfd = _real_socket(AF_UNIX, SOCK_SEQPACKET, 0);
    ASSERT_VALID_FD_MSG(udsseqfd,
                        "failed to create UDS seqpacket restore socket");
    memset(&_udsSeqRestoreAddr, 0, sizeof(struct sockaddr_un));
    _udsSeqRestoreAddr.sun_family = AF_UNIX;
    // Use a different abstract path suffix to avoid collision
    ostringstream o2;
    // We should use the original path along with the unique pid and timestamp.
    // That will ensure that the socket is unique.
    o2 << dmtcp_get_uniquepid_str() << "_" << dmtcp_get_coordinator_timestamp() << "_" << _udsRestoreAddr.sun_path;
    string strSeq = o2.str();
    strncpy(&_udsSeqRestoreAddr.sun_path[1], strSeq.c_str(), strSeq.length());
    _udsSeqRestoreAddrlen = sizeof(sa_family_t) + strSeq.length() + 1;
    ASSERT_SYSCALL_SUCCESS_MSG(
      _real_bind(udsseqfd,
                 (struct sockaddr *)&_udsSeqRestoreAddr,
                 _udsSeqRestoreAddrlen),
      "failed to bind UDS seqpacket restore socket: fd={}",
      udsseqfd);
    ASSERT_SYSCALL_SUCCESS_MSG(_real_listen(udsseqfd, 32),
                 "listen failed for UDS seqpacket restore socket: fd={}",
                 udsseqfd);
    Util::changeFd(udsseqfd, PROTECTED_RESTORE_UDS_SEQ_SOCK_FD);

    JTRACE("opened UDS SEQPACKET listen socket")
      (PROTECTED_RESTORE_UDS_SEQ_SOCK_FD) (&_udsSeqRestoreAddr.sun_path[1]);
    markSocketNonBlocking(PROTECTED_RESTORE_UDS_SEQ_SOCK_FD);
  }
}

void
ConnectionRewirer::registerIncoming(const ConnectionIdentifier &local,
                                    Connection *con,
                                    int domain)
{
  ASSERT(domain == AF_INET || domain == AF_INET6 || domain == AF_UNIX,
         "Unsupported domain: domain={}", domain);
  SocketConnection *sc = dynamic_cast<SocketConnection *>(con);
  ASSERT_NOT_NULL(sc);

  if (domain == AF_INET) {
    _pendingIP4Incoming[local] = con;
  } else if (domain == AF_INET6) {
#ifdef ENABLE_IP6_SUPPORT
    _pendingIP6Incoming[local] = con;
#else // ifdef ENABLE_IP6_SUPPORT
    _pendingIP4Incoming[local] = con;
#endif // ifdef ENABLE_IP6_SUPPORT
  } else if (domain == AF_UNIX) {
    // Route AF_UNIX by base type: stream vs seqpacket
    if (sc->baseType() == SOCK_SEQPACKET) {
      _pendingUDSSeqIncoming[local] = con;
    } else {
      _pendingUDSIncoming[local] = con;
    }
  } else {
    ASSERT(false, "Unsupported incoming connection domain: domain={}", domain);
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
  registerNSData((void *)&_udsSeqRestoreAddr, _udsSeqRestoreAddrlen,
                 &_pendingUDSSeqIncoming);
}

void
ConnectionRewirer::registerNSData(void *addr,
                                  socklen_t addrLen,
                                  ConnectionListT *conList)
{
  iterator i;

  ASSERT_NOT_NULL(theRewirer);
  for (i = conList->begin(); i != conList->end(); ++i) {
    const ConnectionIdentifier &id = i->first;
    string addrStr = dmtcp::base64::encode((const char*) addr, addrLen);
    dmtcp::kvdb::set(PeerDiscoveryDbRestart, id.toString(), addrStr);

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
    struct RemoteAddr remote = {};
    string val;
    kvdb::KVDBResponse response =
      kvdb::get(PeerDiscoveryDbRestart, id.toString(), &val);
    ASSERT(response == kvdb::KVDBResponse::SUCCESS,
           "failed to get peer discovery data: response={} host={} pid={} "
           "time={} con_id={}",
           static_cast<int>(response), id.hostid(), id.pid(), id.time(),
           id.conId());
    string valBinary = dmtcp::base64::decode(val);
    ASSERT_GT(valBinary.size(), static_cast<size_t>(0),
              "empty peer discovery address: host={} pid={} time={} con_id={}",
              id.hostid(), id.pid(), id.time(), id.conId());
    ASSERT_LE(valBinary.size(), sizeof(remote.addr),
              "peer discovery address is too large: size={} max={} host={} "
              "pid={} time={} con_id={}",
              valBinary.size(), sizeof(remote.addr), id.hostid(), id.pid(),
              id.time(), id.conId());
    remote.len = static_cast<socklen_t>(valBinary.size());
    memcpy(&remote.addr, valBinary.data(), remote.len);

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
