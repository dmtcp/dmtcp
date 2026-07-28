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

# include "connection.h"
# include "socketdiscovery.h"

namespace dmtcp
{
inline constexpr char const *PeerDiscoveryDbCkpt = "/plugin/socket/ckpt";

class SocketConnection
{
  public:
    SocketConnection() {}

    SocketConnection(int domain, int type, int protocol);
    void addSetsockopt(int level, int option, const void *value, int len);
    void captureSocketOptions(int fd);
    void restoreSocketOptions(vector<int32_t> &fds);
    void restoreNetlinkMemberships(int fd);
    void serialize(jalib::JBinarySerializer &o);
    int sockDomain() const { return _sockDomain; }
    int sockType() const { return _sockType; }
    int baseType() const { return _sockType & 077; }

  protected:
    int64_t _sockDomain;
    int64_t _sockType;
    int64_t _sockProtocol;
    int32_t _listenBacklog;
    socklen_t _bindAddrlen;
    struct sockaddr_storage _bindAddr;
    ConnectionIdentifier _remotePeerId;
    map<int64_t, map<int64_t, vector<char> > >_sockOptions;
    vector<uint32_t> _netlinkGroups;
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
      TCP_PREEXISTING,
      TCP_EXTERNAL_CONNECT
    };

    TcpConnection() {}

    void publishPeerIdentity();
    void lookupPeerIdentity();

    TcpConnection(int domain,
                  int type,
                  int protocol,
                  const ConnectionIdentifier& id,
                  bool hasLock,
                  const InspectedSocket *inspected);
    void initializeFromDiscovery(const InspectedSocket& inspected,
                                 bool restorable);
    void onError();
    void onDisconnect();

    // basic checkpointing commands
    virtual void drain() override;
    virtual void refill(bool isRestart) override;
    virtual void postRestart() override;

    virtual string str() override { return "<TCP Socket>"; }

    virtual TcpConnection* clone() override {
      return new TcpConnection(*this);
    }

    virtual void serializeSubClass(jalib::JBinarySerializer &o) override;

  private:
    bool endpointKey(bool peer, bool wildcard, string *key) const;
    bool discoveryKey(bool peer, string *key) const;
    bool hasListener(bool peer) const;
    bool listenerKey(bool peer, bool wildcard, string *key) const;
    void assignRestoreRole();

    socklen_t _localAddrlen = 0;
    sockaddr_storage _peerAddr = {};
    socklen_t _peerAddrlen = 0;
    uint64_t _peerInode = 0;
};

class RawSocketConnection : public Connection, public SocketConnection
{
  public:
    enum RawType {
      RAW_INVALID = RAW,
      RAW_CREATED,
      RAW_BIND,
      RAW_PREEXISTING
    };
    RawSocketConnection() {}

    RawSocketConnection(int domain,
                        int type,
                        int protocol,
                        const ConnectionIdentifier& id,
                        bool hasLock,
                        const InspectedSocket *inspected);
    void initializeFromDiscovery(const InspectedSocket& inspected,
                                 bool restorable);

    // basic checkpointing commands
    virtual void drain() override;
    virtual void refill(bool isRestart) override;
    virtual void postRestart() override;

    virtual void serializeSubClass(jalib::JBinarySerializer &o) override;
    virtual string str() override { return "<Raw Socket>"; }

    virtual RawSocketConnection* clone() override {
      return new RawSocketConnection(*this);
    }
};
}
#endif // ifndef SOCKETCONNECTION_H
