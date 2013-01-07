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

#include "coordinatorapi.h"
#include "connectionrewirer.h"
#include "dmtcpmessagetypes.h"
#include "syscallwrappers.h"
#include "util.h"

void dmtcp::ConnectionRewirer::onData(jalib::JReaderInterface* sock)
{
  JASSERT(sock->bytesRead() == sizeof(DmtcpMessage))
   (sock->bytesRead()) (sizeof(DmtcpMessage));
  DmtcpMessage& msg = *(DmtcpMessage*) sock->buffer();
  msg.assertValid();

  if (msg.type == DMT_FORCE_RESTART) {
    JTRACE("got DMT_FORCE_RESTART, exiting ConnectionRewirer")
      (_pendingOutgoing.size()) (_pendingIncoming.size());
    _pendingIncoming.clear();
    _pendingOutgoing.clear();
    finishup();
    return;
  }

  JASSERT(msg.type==DMT_RESTORE_WAITING) (msg.type).Text("unexpected message");

  // Find returns iterator 'i' w/ 0 or more elts, with first elt matching key.
  iterator i = _pendingOutgoing.find(msg.restorePid);

  if (i == _pendingOutgoing.end()) {
    // 'i' is an iterator over 0 elements.
    JTRACE("got RESTORE_WAITING MESSAGE [not used]")
      (msg.restorePid) (_pendingOutgoing.size()) (_pendingIncoming.size());
  } else {
    // 'i' is an iterator over 1 element.
    JTRACE("got RESTORE_WAITING MESSAGE, reconnecting...")
      (msg.restorePid) (msg.restorePort) (msg.restoreAddrlen)
      (_pendingOutgoing.size()) (_pendingIncoming.size());

    jalib::JSocket remote = jalib::JSocket::Create();
    remote.changeFd((i->second)->getFds()[0]);
    errno = 0;
    JASSERT(remote.connect((sockaddr*) &msg.restoreAddr, msg.restoreAddrlen,
                           msg.restorePort))
      (msg.restorePid) (msg.restorePort) (JASSERT_ERRNO)
      .Text("failed to restore connection");

    {
      DmtcpMessage peerMsg(DMT_RESTORE_RECONNECTED);
      peerMsg.restorePid = msg.restorePid;
      addWrite(new jalib::JChunkWriter(remote,(char*) &peerMsg,
                                        sizeof(peerMsg)));
    }
    Util::dupFds(remote.sockfd(), (i->second)->getFds());
    _pendingOutgoing.erase(i);
  }

  if (pendingCount() == 0) finishup();
#ifdef DEBUG
  else debugPrint();
#endif
}

void dmtcp::ConnectionRewirer::onConnect(const jalib::JSocket& sock,
                                         const struct sockaddr* /*remoteAddr*/,
                                         socklen_t /*remoteLen*/)
{
  jalib::JSocket remote = sock;
  DmtcpMessage msg;
  msg.poison();
  remote >> msg;
  msg.assertValid();
  JASSERT(msg.type == DMT_RESTORE_RECONNECTED) (msg.type)
    .Text("unexpected message");

  iterator i = _pendingIncoming.find(msg.restorePid);

  JASSERT(i != _pendingIncoming.end()) (msg.restorePid)
    .Text("got unexpected incoming restore request");

  Util::dupFds(remote.sockfd(), (i->second)->getFds());

  JTRACE("restoring incoming connection") (msg.restorePid);
  _pendingIncoming.erase(i);

  if (pendingCount() ==0) finishup();
#ifdef DEBUG
  else debugPrint();
#endif
}

void dmtcp::ConnectionRewirer::finishup()
{
  JTRACE("finishup begin") (_listenSockets.size()) (_dataSockets.size());
  //i expect both sizes above to be 1
  //close the restoreSocket
  for (size_t i=0; i<_listenSockets.size(); ++i)
    _listenSockets[i].close();
  //poison the coordinator socket listener
  for (size_t i=0; i<_dataSockets.size(); ++i)
    _dataSockets[i]->socket() = -1;
  _real_close(PROTECTED_RESTORE_SOCK_FD);
  //     JTRACE("finishup end");
}

void dmtcp::ConnectionRewirer::onDisconnect(jalib::JReaderInterface* sock)
{
  JASSERT(sock->socket().sockfd() < 0)
    .Text("dmtcp_coordinator disconnected");
}

void dmtcp::ConnectionRewirer::doReconnect()
{
  if (pendingCount() > 0) monitorSockets();
}

void dmtcp::ConnectionRewirer::openRestoreSocket()
{
  _coordFd = CoordinatorAPI::instance().coordinatorSocket().sockfd();
  addDataSocket(new jalib::JChunkReader(_coordFd, sizeof(DmtcpMessage)));

  // Add socket restore socket
  _restorePort = RESTORE_PORT_START;
  jalib::JSocket restoreSocket (-1);
  JTRACE("restoreSockets begin");
  while (!restoreSocket.isValid() && _restorePort < RESTORE_PORT_STOP) {
    restoreSocket = jalib::JServerSocket(jalib::JSockAddr::ANY,
                                         ++_restorePort);
    JTRACE("open listen socket attempt") (_restorePort);
  }
  JASSERT(restoreSocket.isValid()) (RESTORE_PORT_START)
    .Text("failed to open listen socket");
  restoreSocket.changeFd(PROTECTED_RESTORE_SOCK_FD);
  JTRACE("opening listen sockets") (restoreSocket.sockfd());
  addListenSocket(restoreSocket);
}

void
dmtcp::ConnectionRewirer::registerIncoming(const ConnectionIdentifier& local,
                                           Connection* con)
{
  _pendingIncoming[local] = con;

  DmtcpMessage msg;
  msg.type = DMT_RESTORE_WAITING;
  msg.restorePid = local;
  msg.restorePort = _restorePort;
  JTRACE("announcing pending incoming") (local) (msg.restorePort);

  JASSERT(_coordFd != -1);
  addWrite(new jalib::JChunkWriter(_coordFd,(char*) &msg,
                                    sizeof(DmtcpMessage)));
}

void
dmtcp::ConnectionRewirer::registerOutgoing(const ConnectionIdentifier& remote,
                                           Connection* con)
{
  _pendingOutgoing[remote] = con;
}

void dmtcp::ConnectionRewirer::debugPrint() const
{
  JASSERT_STDERR << "Pending Incoming:\n";
  const_iterator i;
  for (i = _pendingIncoming.begin(); i!=_pendingIncoming.end(); ++i) {
    Connection *con = i->second;
    JASSERT_STDERR << i->first << " numFds=" << con->getFds().size()
      << " firstFd=" << con->getFds()[0] << '\n';
  }
  JASSERT_STDERR << "Pending Outgoing:\n";
  for (i = _pendingOutgoing.begin(); i!=_pendingOutgoing.end(); ++i) {
    Connection *con = i->second;
    JASSERT_STDERR << i->first << " numFds=" << con->getFds().size()
      << " firstFd=" << con->getFds()[0] << '\n';
  }
}
