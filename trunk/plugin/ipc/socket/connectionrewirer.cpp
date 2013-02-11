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

#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "dmtcpplugin.h"
#include "protectedfds.h"
#include "util.h"
#include "jsocket.h"

#include "connectionrewirer.h"
#include "socketconnection.h"
#include "socketwrappers.h"

using namespace dmtcp;

static dmtcp::ConnectionRewirer *theRewirer = NULL;
dmtcp::ConnectionRewirer& dmtcp::ConnectionRewirer::instance()
{
  if (theRewirer == NULL) {
    theRewirer = new ConnectionRewirer();
  }
  return *theRewirer;
}

void dmtcp::ConnectionRewirer::checkForPendingIncoming()
{
  while (_pendingIncoming.size() > 0) {
    int fd = _real_accept(PROTECTED_RESTORE_SOCK_FD, NULL, NULL);
    if (fd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return;
    }
    JASSERT(fd != -1) (JASSERT_ERRNO) .Text("Accept failed.");
    ConnectionIdentifier id;
    JASSERT(Util::readAll(fd, &id, sizeof id) == sizeof id);

    iterator i = _pendingIncoming.find(id);
    JASSERT(i != _pendingIncoming.end()) (id)
      .Text("got unexpected incoming restore request");

    Util::dupFds(fd, (i->second)->getFds());

    JTRACE("restoring incoming connection") (id);
    _pendingIncoming.erase(i);
  }
}

void dmtcp::ConnectionRewirer::doReconnect()
{
  iterator i;
  for (i = _pendingOutgoing.begin(); i != _pendingOutgoing.end(); i++) {
    const ConnectionIdentifier& id = i->first;
    Connection *con = i->second;
    struct RemoteAddr& remoteAddr = _remoteInfo[id];
    int fd = jalib::JSocket::Create().sockfd();
    errno = 0;
    JASSERT(_real_connect(fd, (sockaddr*) &remoteAddr.addr, remoteAddr.len) == 0)
      (id) (JASSERT_ERRNO) .Text("failed to restore connection");

    Util::writeAll(fd, &id, sizeof id);
    Util::dupFds(fd, con->getFds());

    checkForPendingIncoming();
  }
  _pendingOutgoing.clear();
  _remoteInfo.clear();

  if (_pendingIncoming.size() > 0) {
    // Remove O_NONBLOCK flag from listener socket
    int flags = _real_fcntl(PROTECTED_RESTORE_SOCK_FD, F_GETFL, NULL);
    JASSERT(flags != -1);
    JASSERT(_real_fcntl(PROTECTED_RESTORE_SOCK_FD, F_SETFL,
                        (void*) (long) (flags & ~O_NONBLOCK)) != -1);
    checkForPendingIncoming();
  }
  JTRACE("Closing restore socket");
  _real_close(PROTECTED_RESTORE_SOCK_FD);

  // Free up the object.
  delete theRewirer;
  theRewirer = NULL;
}

void dmtcp::ConnectionRewirer::openRestoreSocket()
{
  jalib::JServerSocket restoreSocket(jalib::JSockAddr::ANY, 0);
  JASSERT(restoreSocket.isValid());
  restoreSocket.changeFd(PROTECTED_RESTORE_SOCK_FD);


  // Setup restore socket for name service
  struct sockaddr_storage listenSock;
  memset(&_restoreAddr, 0, sizeof(_restoreAddr));
  _restoreAddrlen = sizeof(_restoreAddr);
  dmtcp_get_coordinator_sockname(&_restoreAddr);
  memset(&listenSock, 0, sizeof(_restoreAddr));
  JASSERT(getsockname(PROTECTED_RESTORE_SOCK_FD,
                      (struct sockaddr *)&listenSock,
                      &_restoreAddrlen) == 0);
  struct sockaddr_in* rsock = (struct sockaddr_in*)&_restoreAddr;
  struct sockaddr_in* lsock = (struct sockaddr_in*)&listenSock;
  rsock->sin_port = lsock->sin_port;
  {
    sockaddr_in *sn = (sockaddr_in*) &_restoreAddr;
    unsigned short port = htons(sn->sin_port);
    char *ip = inet_ntoa(sn->sin_addr);
    JTRACE("_restoreAddr for others is:")(sn->sin_family)(port)(ip);
  }

  // Setup socket
  JTRACE("opened listen socket") (restoreSocket.sockfd());

  int flags = _real_fcntl(PROTECTED_RESTORE_SOCK_FD, F_GETFL, NULL);
  JASSERT(flags != -1);
  JASSERT(_real_fcntl(PROTECTED_RESTORE_SOCK_FD, F_SETFL,
                      (void*) (long) (flags | O_NONBLOCK)) != -1);
}

void
dmtcp::ConnectionRewirer::registerIncoming(const ConnectionIdentifier& local,
                                           Connection* con)
{
  _pendingIncoming[local] = con;
  JTRACE("announcing pending incoming") (local);
}

void
dmtcp::ConnectionRewirer::registerOutgoing(const ConnectionIdentifier& remote,
                                           Connection* con)
{
  _pendingOutgoing[remote] = con;
  JTRACE("announcing pending outgoing") (remote);
}

void dmtcp::ConnectionRewirer::registerNSData()
{
  iterator i;
  JASSERT(theRewirer != NULL);
  for (i = _pendingIncoming.begin(); i != _pendingIncoming.end(); ++i) {
    const ConnectionIdentifier& id = i->first;
    dmtcp_send_key_val_pair_to_coordinator((const void *)&id,
                                           sizeof(id),
                                           &_restoreAddr,
                                           _restoreAddrlen);
    /*
    sockaddr_in *sn = (sockaddr_in*) &_restoreAddr;
    unsigned short port = htons(sn->sin_port);
    char *ip = inet_ntoa(sn->sin_addr);
    JTRACE("Send NS information:")(id)(sn->sin_family)(port)(ip);
    */
  }
  debugPrint();
}

void dmtcp::ConnectionRewirer::sendQueries()
{
  iterator i;
  for (i = _pendingOutgoing.begin(); i != _pendingOutgoing.end(); ++i) {
    const ConnectionIdentifier& id = i->first;
    struct RemoteAddr remote;
    remote.len = sizeof(remote.addr);
    dmtcp_send_query_to_coordinator((const void *)&id, sizeof(id),
                                    &remote.addr, (size_t*) &remote.len);
    /*
    sockaddr_in *sn = (sockaddr_in*) &remote.addr;
    unsigned short port = htons(sn->sin_port);
    char *ip = inet_ntoa(sn->sin_addr);
    JTRACE("Send Queries. Get remote from coordinator:")(id)(sn->sin_family)(port)(ip);
    */
    _remoteInfo[id] = remote;
  }
}

void dmtcp::ConnectionRewirer::debugPrint() const
{
#ifdef DEBUG
  ostringstream o;
  o << "Pending Incoming:\n";
  const_iterator i;
  for (i = _pendingIncoming.begin(); i!=_pendingIncoming.end(); ++i) {
    Connection *con = i->second;
    o << i->first << " numFds=" << con->getFds().size()
      << " firstFd=" << con->getFds()[0] << '\n';
  }
  o << "Pending Outgoing:\n";
  for (i = _pendingOutgoing.begin(); i!=_pendingOutgoing.end(); ++i) {
    Connection *con = i->second;
    o << i->first << " numFds=" << con->getFds().size()
      << " firstFd=" << con->getFds()[0] << '\n';
  }
  JNOTE("Pending connections") (o.str());
#endif
}
