/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, Gene Cooperman,    *
 *                                                           and Rohan Garg *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu, and         *
 *                                                      rohgarg@ccs.neu.edu *
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

#pragma once
#ifndef SOCKETCONNECTION_H
#define SOCKETCONNECTION_H

# include <signal.h>
# include <stdint.h>
# include <sys/socket.h>
# include <sys/stat.h>
# include <sys/types.h>
# include <sys/types.h>
# include <unistd.h>

# include "jbuffer.h"

# include "connection.h"

namespace dmtcp
{
class SocketConnection
{
  public:
    enum PeerType {
      PEER_UNKNOWN,
      PEER_INTERNAL,
      PEER_EXTERNAL
    };

    uint32_t peerType() const { return _peerType; }

    SocketConnection() {}

    SocketConnection(int domain, int type, int protocol);
    SocketConnection(int domain,
                     int type,
                     int protocol,
                     ConnectionIdentifier remote);
    void addSetsockopt(int level, int option, const void *value, int len);
    void restoreSocketOptions(vector<int32_t> &fds);
    void serialize(jalib::JBinarySerializer &o);
    int sockDomain() const { return _sockDomain; }

    virtual void onBind(const struct sockaddr *addr, socklen_t len);
    virtual void onListen(int backlog);
    virtual void onConnect(const struct sockaddr *serv_addr = NULL,
                           socklen_t addrlen = 0,
                           bool connectInProgress = false);

  protected:
    int64_t _sockDomain;
    int64_t _sockType;
    int64_t _sockProtocol;
    uint32_t _peerType;
    int32_t _listenBacklog;
    union {
      socklen_t _bindAddrlen;
      socklen_t _connectAddrlen;
    };
    union {
      /* See 'man socket.h' or POSIX for 'struct sockaddr_storage' */
      struct sockaddr_storage _bindAddr;
      struct sockaddr_storage _connectAddr;
    };
    ConnectionIdentifier _remotePeerId;
    map<int64_t, map<int64_t, jalib::JBuffer> >_sockOptions;
};

class TcpConnection : public Connection, public SocketConnection
{
  public:
    enum TcpType {
      TCP_INVALID = TCP,
      TCP_ERROR,
      TCP_CREATED,
      TCP_BIND,
      TCP_LISTEN,
      TCP_ACCEPT,
      TCP_CONNECT,
      TCP_CONNECT_IN_PROGRESS,
      TCP_PREEXISTING,
      TCP_EXTERNAL_CONNECT
    };

    TcpConnection() {}

    // This accessor is needed because _type is protected.
    void markExternalConnect() { _type = TCP_EXTERNAL_CONNECT; }

    bool isBlacklistedTcp(const sockaddr *saddr, socklen_t len);

    void sendPeerInformation();

    void recvPeerInformation();

    // basic commands for updating state from wrappers

    /*onSocket*/
    TcpConnection(int domain, int type, int protocol);
    void onBind(const struct sockaddr *addr, socklen_t len);
    void onListen(int backlog);
    void onConnect(const struct sockaddr *serv_addr = NULL,
                   socklen_t addrlen = 0,
                   bool connectInProgress = false);

    /*onAccept*/
    TcpConnection(const TcpConnection &parent,
                  const ConnectionIdentifier &remote);
    void onError();
    void onDisconnect();

    void markPreExisting() { _type = TCP_PREEXISTING; }

    // basic checkpointing commands
    virtual void drain();
    virtual void refill(bool isRestart);
    virtual void postRestart();

    void doSendHandshakes(const ConnectionIdentifier &coordId);
    void doRecvHandshakes(const ConnectionIdentifier &coordId);

    void sendHandshake(int remotefd, const ConnectionIdentifier &coordId);
    void recvHandshake(int remotefd, const ConnectionIdentifier &coordId);

    virtual string str() { return "<TCP Socket>"; }

    virtual void serializeSubClass(jalib::JBinarySerializer &o);

  private:
    TcpConnection &asTcp();
};

class RawSocketConnection : public Connection, public SocketConnection
{
  public:
    enum RawType {
      RAW_INVALID = RAW,
      RAW_ERROR,
      RAW_CREATED,
      RAW_BIND,
      RAW_LISTEN,
      RAW_ACCEPT,
      RAW_CONNECT,
      RAW_CONNECT_IN_PROGRESS,
      RAW_PREEXISTING
    };
    RawSocketConnection() {}

    // basic commands for updating state from wrappers
    RawSocketConnection(int domain, int type, int protocol);

    // basic checkpointing commands
    virtual void drain();
    virtual void refill(bool isRestart);
    virtual void postRestart();

    virtual void onBind(const struct sockaddr *addr, socklen_t len);
    virtual void onListen(int backlog);
    virtual void onConnect(const struct sockaddr *serv_addr = NULL,
                           socklen_t addrlen = 0,
                           bool connectInProgress = false);

    // FIXME: Change to first arg to SocketConnection* when we fix the class
    // hierarchy
    RawSocketConnection(const RawSocketConnection &parent,
                        const ConnectionIdentifier &remote);

    virtual void serializeSubClass(jalib::JBinarySerializer &o);
    virtual string str() { return "<Raw Socket>"; }
};
}
#endif // ifndef SOCKETCONNECTION_H
