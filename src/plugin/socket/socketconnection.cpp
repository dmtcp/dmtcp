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
#include <linux/limits.h>
#include <linux/netlink.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "jconvert.h"
#include "jfilesystem.h"
#include "dmtcp.h"
#include "shareddata.h"
#include "util.h"
#include "base64.h"
#include "kvdb.h"

#include "connectionrewirer.h"
#include "kernelbufferdrainer.h"
#include "socketconnection.h"
#include "syscallwrappers.h"
#include "socketwrappers.h"
#include "dmtcp_assert.h"

#ifdef REALLY_VERBOSE_CONNECTION_CPP
static bool really_verbose = true;
#else // ifdef REALLY_VERBOSE_CONNECTION_CPP
static bool really_verbose = false;
#endif // ifdef REALLY_VERBOSE_CONNECTION_CPP

using namespace dmtcp;

// this function creates a socket that is in an error state
static int
_makeDeadSocket(const char *refillData = NULL, ssize_t len = -1)
{
  // it does it by creating a socket pair and closing one side
  int sp[2] = { -1, -1 };

  ASSERT_NE(-1, _real_socketpair(AF_UNIX, SOCK_STREAM, 0, sp));
  ASSERT_NE(-1, sp[0], "socketpair() returned invalid first fd: fd1={}",
                      sp[1]);
  ASSERT_NE(-1, sp[1], "socketpair() returned invalid second fd: fd0={}",
                      sp[0]);
  if (refillData != NULL) {
    ASSERT(Util::writeAll(sp[1], refillData, len) == len,
           "failed to seed dead socket: fd={} size={}", sp[1], len);
  }
  _real_close(sp[1]);
  if (really_verbose) {
    TRACE("Created dead socket placeholder: fd={}", sp[0]);
  }
  return sp[0];
}

SocketConnection::SocketConnection(int domain, int type, int protocol)
  : _sockDomain(domain)
  , _sockType(type)
  , _sockProtocol(protocol)
  , _listenBacklog(-1)
  , _bindAddrlen(0)
  , _remotePeerId(ConnectionIdentifier::null())
{}

void
SocketConnection::addSetsockopt(int level,
                                int option,
                                const void *value,
                                int len)
{
  ASSERT_GE(len, 0, "invalid socket option length");
  const char *data = (const char *)value;
  vector<char> &buffer = _sockOptions[level][option];
  buffer.clear();
  if (len > 0) {
    buffer.assign(data, data + len);
  }
}

void
SocketConnection::captureSocketOptions(int fd)
{
  _sockOptions.clear();
  auto capture = [this, fd](int level, int option, void *value,
                            socklen_t size) {
    socklen_t length = size;
    if (_real_getsockopt(fd, level, option, value, &length) == 0) {
      addSetsockopt(level, option, value, length);
    }
  };
  auto captureInt = [&capture](int level, int option) {
    int value = 0;
    capture(level, option, &value, sizeof(value));
  };

  captureInt(SOL_SOCKET, SO_REUSEADDR);
#ifdef SO_REUSEPORT
  captureInt(SOL_SOCKET, SO_REUSEPORT);
#endif
  captureInt(SOL_SOCKET, SO_KEEPALIVE);
  captureInt(SOL_SOCKET, SO_RCVLOWAT);
  struct linger lingerValue = {};
  capture(SOL_SOCKET, SO_LINGER, &lingerValue, sizeof(lingerValue));
  struct timeval timeout = {};
  capture(SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  capture(SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

  for (int option : { SO_RCVBUF, SO_SNDBUF }) {
    int value = 0;
    socklen_t length = sizeof(value);
    if (_real_getsockopt(fd, SOL_SOCKET, option, &value, &length) == 0) {
      value /= 2;
      addSetsockopt(SOL_SOCKET, option, &value, sizeof(value));
    }
  }

  if ((_sockDomain == AF_INET || _sockDomain == AF_INET6) &&
      baseType() == SOCK_STREAM) {
    captureInt(IPPROTO_TCP, TCP_NODELAY);
    captureInt(IPPROTO_TCP, TCP_KEEPIDLE);
    captureInt(IPPROTO_TCP, TCP_KEEPINTVL);
    captureInt(IPPROTO_TCP, TCP_KEEPCNT);
#ifdef TCP_USER_TIMEOUT
    captureInt(IPPROTO_TCP, TCP_USER_TIMEOUT);
#endif
  }
  if (_sockDomain == AF_INET) {
    captureInt(IPPROTO_IP, IP_TOS);
    captureInt(IPPROTO_IP, IP_TTL);
  } else if (_sockDomain == AF_INET6) {
    captureInt(IPPROTO_IPV6, IPV6_V6ONLY);
    captureInt(IPPROTO_IPV6, IPV6_UNICAST_HOPS);
  }

#if defined(SOL_NETLINK) && defined(NETLINK_LIST_MEMBERSHIPS)
  if (_sockDomain == AF_NETLINK) {
    socklen_t length = 0;
    if (_real_getsockopt(fd, SOL_NETLINK, NETLINK_LIST_MEMBERSHIPS,
                         nullptr, &length) == 0 &&
        length > 0) {
      _netlinkGroups.resize(length / sizeof(_netlinkGroups[0]));
      if (_real_getsockopt(fd, SOL_NETLINK, NETLINK_LIST_MEMBERSHIPS,
                           _netlinkGroups.data(), &length) != 0) {
        _netlinkGroups.clear();
      } else {
        _netlinkGroups.resize(length / sizeof(_netlinkGroups[0]));
      }
    }
  }
#endif
}

void
SocketConnection::restoreSocketOptions(vector<int> &fds)
{
  typedef map<int64_t, map<int64_t, vector<char> > >::iterator levelIterator;
  typedef map<int64_t, vector<char> >::iterator optionIterator;

  for (levelIterator lvl = _sockOptions.begin();
       lvl != _sockOptions.end(); ++lvl) {
     for (optionIterator opt = lvl->second.begin();
          opt != lvl->second.end(); ++opt) {
      TRACE("Restoring socket option: fd={} option={} size={}",
            fds[0], opt->first, opt->second.size());
      int ret = _real_setsockopt(fds[0], lvl->first, opt->first,
                                 opt->second.data(),
                                 opt->second.size());
      WARN_ERRNO(ret == 0,
                 "Restoring setsockopt failed: fd={} level={} option={} "
                 "size={}",
                 fds[0], lvl->first, opt->first, opt->second.size());
    }
  }
}

void
SocketConnection::restoreNetlinkMemberships(int fd)
{
#if defined(SOL_NETLINK) && defined(NETLINK_ADD_MEMBERSHIP)
  for (size_t word = 0; word < _netlinkGroups.size(); ++word) {
    uint32_t memberships = _netlinkGroups[word];
    for (unsigned int bit = 0; memberships != 0;
         ++bit, memberships >>= 1) {
      if ((memberships & 1) == 0) {
        continue;
      }
      uint32_t group = word * 32 + bit + 1;
      WARN_ERRNO(_real_setsockopt(fd, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
                                 &group, sizeof(group)) == 0,
                 "Restoring Netlink membership failed: fd={} group={}",
                 fd, group);
    }
  }
#else
  (void)fd;
#endif
}

void
SocketConnection::serialize(jalib::JBinarySerializer &o)
{
  JSERIALIZE_ASSERT_POINT("SocketConnection");
  o&_sockDomain&_sockType&_sockProtocol &_netlinkGroups;

  JSERIALIZE_ASSERT_POINT("SocketOptions:");
  uint64_t numSockOpts = _sockOptions.size();
  o &numSockOpts;
    if (o.isWriter()) {
    // TRACE("TCP Serialize: type={} con_id={}", _type, _id.conId());
    typedef map<int64_t, map<int64_t, vector<char> > >::iterator levelIterator;
    typedef map<int64_t, vector<char> >::iterator optionIterator;

    uint64_t numLvl = _sockOptions.size();
    o &numLvl;

    for (levelIterator lvl = _sockOptions.begin();
         lvl != _sockOptions.end(); ++lvl) {
      int64_t lvlVal = lvl->first;
      uint64_t numOpts = lvl->second.size();

      JSERIALIZE_ASSERT_POINT("Lvl");

      o&lvlVal &numOpts;

      for (optionIterator opt = lvl->second.begin();
           opt != lvl->second.end(); ++opt) {
        int64_t optType = opt->first;
        vector<char> &buffer = opt->second;
        int64_t bufLen = buffer.size();

        JSERIALIZE_ASSERT_POINT("Opt");

        o&optType &bufLen;
        o.readOrWrite(buffer.data(), bufLen);
      }
    }
  } else {
    uint64_t numLvl = 0;
    o &numLvl;

    while (numLvl-- > 0) {
      int64_t lvlVal = -1;
      int64_t numOpts = 0;

      JSERIALIZE_ASSERT_POINT("Lvl");

      o&lvlVal &numOpts;

      while (numOpts-- > 0) {
        int64_t optType = -1;
        int64_t bufLen = -1;

        JSERIALIZE_ASSERT_POINT("Opt");

        o&optType &bufLen;

        ASSERT_GE(bufLen, 0, "invalid socket option buffer length");
        vector<char> buffer(bufLen);
        o.readOrWrite(buffer.data(), bufLen);

        _sockOptions[lvlVal][optType] = buffer;
      }
    }
  }

  JSERIALIZE_ASSERT_POINT("EndSockOpts");
}

/*****************************************************************************
 * TCP Connection
 *****************************************************************************/

TcpConnection::TcpConnection(int domain,
                             int type,
                             int protocol,
                             const ConnectionIdentifier& id,
                             bool hasLock,
                             const InspectedSocket *inspected)
  : Connection(TCP_CREATED, id, hasLock,
               inspected == nullptr ? -1 : inspected->statusFlags,
               inspected == nullptr ? -1 : inspected->realOwner,
               inspected == nullptr ? -1 : inspected->signal)
  , SocketConnection(domain, type, protocol)
{
  memset(&_bindAddr, 0, sizeof _bindAddr);
}

void
TcpConnection::initializeFromDiscovery(const InspectedSocket& inspected,
                                       bool restorable)
{
  if (!restorable) {
    _type = TCP_PREEXISTING;
    return;
  }

  ASSERT_LE(inspected.localLen, sizeof(_bindAddr));
  _localAddrlen = inspected.localLen;
  memcpy(&_bindAddr, &inspected.local, inspected.localLen);
  ASSERT_LE(inspected.peerLen, sizeof(_peerAddr));
  _peerAddrlen = inspected.peerLen;
  memcpy(&_peerAddr, &inspected.peer, inspected.peerLen);
  _peerInode = inspected.peerInode;

  if (inspected.hasPeer) {
    bool disconnected =
      (_sockDomain == AF_UNIX && inspected.hasDiagnostics &&
       inspected.peerInode == 0) ||
      (inspected.hasDiagnostics &&
       inspected.kernelState != TCP_ESTABLISHED &&
       inspected.kernelState != TCP_SYN_SENT);
    _type = disconnected ? TCP_ERROR : TCP_CONNECT;
    _bindAddrlen = 0;
    return;
  }

  _bindAddrlen = inspected.localLen;

  if (inspected.acceptConn != 0) {
    _type = TCP_LISTEN;
    _listenBacklog = inspected.listenBacklog;
  } else {
    bool bound = false;
    if (_sockDomain == AF_INET) {
      const sockaddr_in *address =
        reinterpret_cast<const sockaddr_in *>(&_bindAddr);
      bound = address->sin_port != 0;
    } else if (_sockDomain == AF_INET6) {
      const sockaddr_in6 *address =
        reinterpret_cast<const sockaddr_in6 *>(&_bindAddr);
      bound = address->sin6_port != 0;
    } else if (_sockDomain == AF_UNIX) {
      bound = _bindAddrlen > offsetof(sockaddr_un, sun_path);
    }
    _type = bound ? TCP_BIND : TCP_CREATED;
  }
}

void
TcpConnection::assignRestoreRole()
{
  if (_type != TCP_CONNECT || _remotePeerId.isNull()) {
    return;
  }
  ASSERT(id() != _remotePeerId,
         "Socket peer lookup returned its own identity: fd={} con_id={}",
         _fds[0], id().conId());

  bool localListener = hasListener(false);
  bool peerListener = hasListener(true);
  WARN(_sockDomain == AF_UNIX || localListener || peerListener,
       "Could not identify which TCP endpoint accepted the connection; "
       "one local socket address may change after restart: fd={} con_id={}",
       _fds[0], id().conId());

  _type = localListener != peerListener ?
          (localListener ? TCP_ACCEPT : TCP_CONNECT) :
          (id() < _remotePeerId ? TCP_ACCEPT : TCP_CONNECT);
  TRACE("Assigned TCP restore role: fd={} role={} local_listener={} "
        "peer_listener={}",
        _fds[0], _type, localListener, peerListener);
  _bindAddrlen =
    _type == TCP_CONNECT &&
    (_sockDomain == AF_INET || _sockDomain == AF_INET6) ?
      _localAddrlen : 0;
}

static string
socketIdentity(const ConnectionIdentifier& id)
{
  ostringstream value;
  value << id.hostid() << ':' << id.conId() << ':'
        << dmtcp_get_generation();
  return value.str();
}

static bool
parseSocketIdentity(std::string_view value, ConnectionIdentifier *id)
{
  size_t first = value.find(':');
  size_t second =
    first == std::string_view::npos ?
      std::string_view::npos : value.find(':', first + 1);
  if (first == std::string_view::npos ||
      second == std::string_view::npos) {
    return false;
  }

  uint64_t host = 0;
  int64_t inode = 0;
  uint32_t generation = 0;
  if (!Util::parseInteger(value.substr(0, first), &host) ||
      !Util::parseInteger(value.substr(first + 1, second - first - 1),
                          &inode) ||
      !Util::parseInteger(value.substr(second + 1), &generation)) {
    return false;
  }

  DmtcpUniqueProcessId process = {};
  process._hostid = host;
  process._computation_generation = generation;
  *id = ConnectionIdentifier(process, inode);
  return true;
}

bool
TcpConnection::endpointKey(bool peer, bool wildcard, string *key) const
{
  if (_sockDomain == AF_INET) {
    const sockaddr_storage& address = peer ? _peerAddr : _bindAddr;
    socklen_t length = peer ? _peerAddrlen : _localAddrlen;
    if (length < sizeof(sockaddr_in)) {
      return false;
    }
    const sockaddr_in *source =
      reinterpret_cast<const sockaddr_in *>(&address);
    sockaddr_in normalized = {};
    normalized.sin_family = AF_INET;
    normalized.sin_port = source->sin_port;
    normalized.sin_addr.s_addr =
      wildcard ? htonl(INADDR_ANY) : source->sin_addr.s_addr;
    *key = "inet:" +
           base64::encode(reinterpret_cast<const char *>(&normalized),
                          sizeof(normalized));
    return true;
  }

  if (_sockDomain == AF_INET6) {
    const sockaddr_storage& address = peer ? _peerAddr : _bindAddr;
    socklen_t length = peer ? _peerAddrlen : _localAddrlen;
    if (length < sizeof(sockaddr_in6)) {
      return false;
    }
    const sockaddr_in6 *source =
      reinterpret_cast<const sockaddr_in6 *>(&address);
    if (!wildcard && IN6_IS_ADDR_V4MAPPED(&source->sin6_addr)) {
      sockaddr_in normalized = {};
      normalized.sin_family = AF_INET;
      normalized.sin_port = source->sin6_port;
      memcpy(&normalized.sin_addr,
             &source->sin6_addr.s6_addr[sizeof(source->sin6_addr) -
                                        sizeof(normalized.sin_addr)],
             sizeof(normalized.sin_addr));
      *key = "inet:" +
             base64::encode(reinterpret_cast<const char *>(&normalized),
                            sizeof(normalized));
      return true;
    }
    sockaddr_in6 normalized = {};
    normalized.sin6_family = AF_INET6;
    normalized.sin6_port = source->sin6_port;
    normalized.sin6_addr = wildcard ? in6addr_any : source->sin6_addr;
    normalized.sin6_scope_id = wildcard ? 0 : source->sin6_scope_id;
    *key = "inet6:" +
           base64::encode(reinterpret_cast<const char *>(&normalized),
                          sizeof(normalized));
    return true;
  }
  return false;
}

bool
TcpConnection::discoveryKey(bool peer, string *key) const
{
  if (_sockDomain == AF_INET || _sockDomain == AF_INET6) {
    string first;
    string second;
    if (!endpointKey(peer, false, &first) ||
        !endpointKey(!peer, false, &second)) {
      return false;
    }
    bool loopback = false;
    if (_sockDomain == AF_INET) {
      const sockaddr_in *local =
        reinterpret_cast<const sockaddr_in *>(&_bindAddr);
      const sockaddr_in *remote =
        reinterpret_cast<const sockaddr_in *>(&_peerAddr);
      loopback =
        (ntohl(local->sin_addr.s_addr) >> 24) == IN_LOOPBACKNET &&
        (ntohl(remote->sin_addr.s_addr) >> 24) == IN_LOOPBACKNET;
    } else {
      const sockaddr_in6 *local =
        reinterpret_cast<const sockaddr_in6 *>(&_bindAddr);
      const sockaddr_in6 *remote =
        reinterpret_cast<const sockaddr_in6 *>(&_peerAddr);
      if (IN6_IS_ADDR_V4MAPPED(&local->sin6_addr) &&
          IN6_IS_ADDR_V4MAPPED(&remote->sin6_addr)) {
        uint32_t localAddress;
        uint32_t remoteAddress;
        memcpy(&localAddress,
               &local->sin6_addr.s6_addr[sizeof(local->sin6_addr) -
                                         sizeof(localAddress)],
               sizeof(localAddress));
        memcpy(&remoteAddress,
               &remote->sin6_addr.s6_addr[sizeof(remote->sin6_addr) -
                                          sizeof(remoteAddress)],
               sizeof(remoteAddress));
        loopback = (ntohl(localAddress) >> 24) == IN_LOOPBACKNET &&
                   (ntohl(remoteAddress) >> 24) == IN_LOOPBACKNET;
      } else {
        loopback = IN6_IS_ADDR_LOOPBACK(&local->sin6_addr) &&
                   IN6_IS_ADDR_LOOPBACK(&remote->sin6_addr);
      }
    }
    *key = "peer:";
    if (loopback) {
      *key += std::to_string(id().hostid()) + ':';
    }
    *key += first + ':' + second;
    return true;
  }

  if (_sockDomain == AF_UNIX) {
    uint64_t inode = peer ? _peerInode : id().conId();
    if (inode == 0) {
      return false;
    }
    ostringstream unixKey;
    unixKey << "peer:unix:" << id().hostid() << ':' << inode;
    *key = unixKey.str();
    return true;
  }
  return false;
}

bool
TcpConnection::listenerKey(bool peer, bool wildcard, string *key) const
{
  string endpoint;
  if (!endpointKey(peer, wildcard, &endpoint)) {
    return false;
  }
  ostringstream listener;
  uint64_t host = peer ? _remotePeerId.hostid() : id().hostid();
  listener << "listener:" << host << ':' << endpoint;
  *key = listener.str();
  return true;
}

bool
TcpConnection::hasListener(bool peer) const
{
  string key;
  string value;
  return
    (listenerKey(peer, false, &key) &&
     kvdb::get(PeerDiscoveryDbCkpt, key, &value) ==
       kvdb::KVDBResponse::SUCCESS) ||
    (listenerKey(peer, true, &key) &&
     kvdb::get(PeerDiscoveryDbCkpt, key, &value) ==
       kvdb::KVDBResponse::SUCCESS);
}

void
TcpConnection::publishPeerIdentity()
{
  string key;
  if (_type == TCP_LISTEN && listenerKey(false, false, &key)) {
    ASSERT(kvdb::set(PeerDiscoveryDbCkpt, key, "1") ==
             kvdb::KVDBResponse::SUCCESS,
           "Failed to publish socket listener identity: fd={} key={}",
           _fds[0], key);
    if (_sockDomain == AF_INET6) {
      const sockaddr_in6 *address =
        reinterpret_cast<const sockaddr_in6 *>(&_bindAddr);
      int v6Only = 1;
      socklen_t length = sizeof(v6Only);
      if (IN6_IS_ADDR_UNSPECIFIED(&address->sin6_addr) &&
          _real_getsockopt(_fds[0], IPPROTO_IPV6, IPV6_V6ONLY,
                           &v6Only, &length) == 0 &&
          v6Only == 0) {
        sockaddr_in wildcard = {};
        wildcard.sin_family = AF_INET;
        wildcard.sin_port = address->sin6_port;
        ostringstream alias;
        alias << "listener:" << id().hostid() << ":inet:"
              << base64::encode(reinterpret_cast<const char *>(&wildcard),
                                sizeof(wildcard));
        ASSERT(kvdb::set(PeerDiscoveryDbCkpt, alias.str(), "1") ==
                 kvdb::KVDBResponse::SUCCESS,
               "Failed to publish dual-stack listener identity: fd={}",
               _fds[0]);
      }
    }
    return;
  }
  if (_type != TCP_CONNECT) {
    return;
  }

  if (discoveryKey(false, &key)) {
    ASSERT(kvdb::set(PeerDiscoveryDbCkpt, key, socketIdentity(id())) ==
             kvdb::KVDBResponse::SUCCESS,
           "Failed to publish socket endpoint identity: fd={} key={}",
           _fds[0], key);
  }
}

void
TcpConnection::lookupPeerIdentity()
{
  if (_type != TCP_CONNECT) {
    return;
  }

  string key;
  string value;
  if (!discoveryKey(true, &key) ||
      kvdb::get(PeerDiscoveryDbCkpt, key, &value) !=
        kvdb::KVDBResponse::SUCCESS ||
      !parseSocketIdentity(value, &_remotePeerId)) {
    WARN(false,
         "Socket peer is outside this checkpoint and will become a dead "
         "socket after restart: fd={} domain={} con_id={} peer_inode={}",
         _fds[0], _sockDomain, id().conId(), _peerInode);
    _type = TCP_EXTERNAL_CONNECT;
    return;
  }
  assignRestoreRole();
}

void
TcpConnection::onError()
{
  TRACE("Marking TCP connection as errored: con_id={}", id().toString());
  _type = TCP_ERROR;
  TRACE("Replacing errored TCP connection with dead socket: fd={} fd_count={}",
        _fds[0], _fds.size());
  const vector<char> &buffer =
    KernelBufferDrainer::instance().getDrainedData(_id);
  restoreDupFds(_makeDeadSocket(&buffer[0], buffer.size()));
}

void
TcpConnection::drain()
{
  ASSERT(_fds.size() > 0, "TCP connection has no fds during drain: con_id={}",
         id().conId());

  if ((_fcntlFlags & O_ASYNC) != 0) {
    if (really_verbose) {
      TRACE("Removing O_ASYNC before checkpointing TCP socket: fd={} "
            "con_id={}",
            _fds[0], id().toString());
    }
    errno = 0;
    ASSERT_NE(-1,
      fcntl(_fds[0], F_SETFL, _fcntlFlags & ~O_ASYNC),
      "removing O_ASYNC during TCP drain: fd={} con_id={}",
      _fds[0], id().conId());
  }

  switch (_type) {
  case TCP_ERROR:

  // Treat TCP_ERROR as a regular socket for draining purposes. There still
  // might be some stale data on it.
  case TCP_CONNECT:
  case TCP_ACCEPT:
    TRACE("Draining TCP socket before checkpoint: has_lock={} fd={} "
          "con_id={} remote_con_id={}",
          _hasLock, _fds[0], _id.toString(), _remotePeerId.toString());
    KernelBufferDrainer::instance().beginDrainOf(_fds[0], _id, baseType());
    break;
  case TCP_LISTEN:
    KernelBufferDrainer::instance().addListenSocket(_fds[0]);
    break;
  case TCP_BIND:
    WARN(_type != TCP_BIND,
            "Pending connections on this socket will not be checkpointed "
            "because it is not yet listening: fd={}",
            _fds[0]);
    break;
  case TCP_EXTERNAL_CONNECT:
    TRACE("Skipping drain for external TCP socket: fd={}",
          _fds[0]);
    break;
  }
}

void
TcpConnection::refill(bool isRestart)
{
  if (isRestart && _type == TCP_ACCEPT) {
    restoreSocketOptions(_fds);
  }
}

void
TcpConnection::postRestart()
{
  int fd;

  ASSERT(_fds.size() > 0, "TCP connection has no fds during postRestart");
  switch (_type) {
  case TCP_PREEXISTING:
  case TCP_EXTERNAL_CONNECT:
    WARN(false,
         "Socket state was not restorable at checkpoint and is being "
         "replaced with a dead socket: fd={} con_id={}",
         _fds[0], id().conId());
    restoreDupFds(_makeDeadSocket());
    break;

  case TCP_INVALID:
    TRACE("Restoring TCP connection as dead socket: fd={} fd_count={}",
          _fds[0], _fds.size());
    restoreDupFds(_makeDeadSocket());
    break;

  case TCP_ERROR:

    // Disconnected socket. Need to refill the drained data
  {
    const vector<char> &buffer =
      KernelBufferDrainer::instance().getDrainedData(_id);
    restoreDupFds(_makeDeadSocket(&buffer[0], buffer.size()));
    break;
  }

  case TCP_CREATED:
  case TCP_BIND:
  case TCP_LISTEN:

    // Sometimes _sockType contains SOCK_CLOEXEC/SOCK_NONBLOCK flags.
    {
      if (_sockDomain == AF_UNIX) {
        WARN(baseType() == SOCK_STREAM || baseType() == SOCK_SEQPACKET,
                "unexpected UNIX socket type while restoring: con_id={} "
                "domain={} type={} protocol={} base_type={}",
                id().conId(), _sockDomain, _sockType, _sockProtocol,
                baseType());
      } else if (_sockDomain == AF_INET || _sockDomain == AF_INET6) {
        WARN(baseType() == SOCK_STREAM,
                "unexpected TCP socket type while restoring: con_id={} "
                "domain={} type={} protocol={} base_type={}",
                id().conId(), _sockDomain, _sockType, _sockProtocol,
                baseType());
      }
    }

    if (really_verbose) {
      TRACE("Recreating TCP socket after restart: con_id={} fd={}",
            id().toString(), _fds[0]);
    }

    fd = _real_socket(_sockDomain, _sockType, _sockProtocol);
    ASSERT_NE(-1, fd,
                        "failed to recreate TCP socket: con_id={} domain={} "
                        "type={} protocol={}",
                        id().conId(), _sockDomain, _sockType, _sockProtocol);
    restoreDupFds(fd);
    restoreSocketOptions(_fds);

    if (_type == TCP_CREATED) {
      break;
    }

    if (_sockDomain == AF_UNIX &&
        _bindAddrlen > sizeof(_bindAddr.ss_family)) {
      struct sockaddr_un *uaddr = (sockaddr_un *)&_bindAddr;
      if (uaddr->sun_path[0] != '\0') {
        TRACE("Unlinking stale UNIX socket before bind: path={}",
              uaddr->sun_path);
        WARN_NE(-1, unlink(uaddr->sun_path),
                      "failed to unlink stale UNIX socket: path={}",
                      uaddr->sun_path);
      }
    }

    /*
     * During restart, some socket options must be restored(using
     * setsockopt) before the socket is used(bind etc.), otherwise we might
     * not be able to restore them at all. One such option is set in the
     * following way for IPV6 family:
     * setsockopt(sd, IPPROTO_IPV6, IPV6_V6ONLY,...)
     * This fix works for now. A better approach would be to restore the
     * socket options in the order in which they are set by the user
     * program.  This fix solves a bug that caused Open MPI to fail to
     * restart under DMTCP.
     *                               --Kapil
     */

    if (_sockDomain == AF_INET6) {
      TRACE("Restoring IPv6 socket options before bind: fd={}", _fds[0]);
      typedef map<int64_t,
                  map<int64_t, vector<char> > >::iterator levelIterator;
      typedef map<int64_t, vector<char> >::iterator optionIterator;

      for (levelIterator lvl = _sockOptions.begin();
           lvl != _sockOptions.end(); ++lvl) {
        if (lvl->first == IPPROTO_IPV6) {
          for (optionIterator opt = lvl->second.begin();
               opt != lvl->second.end(); ++opt) {
            if (opt->first == IPV6_V6ONLY) {
              if (really_verbose) {
                TRACE("Restoring socket option: fd={} option={} size={}",
                      _fds[0], opt->first, opt->second.size());
              }
              int ret = _real_setsockopt(_fds[0], lvl->first, opt->first,
                                         opt->second.data(),
                                         opt->second.size());
              ASSERT_ERRNO(ret == 0,
                           "Restoring IPV6_V6ONLY setsockopt failed: fd={} "
                           "level={} option={} value={} size={}",
                           _fds[0], lvl->first, opt->first,
                           opt->second.data(), opt->second.size());
            }
          }
        }
      }
    }

    if (really_verbose) {
      TRACE("Binding recreated TCP socket: con_id={} fd={}",
            id().toString(), _fds[0]);
    }
    errno = 0;
    WARN_NE(-1, _real_bind(_fds[0],
                                           (sockaddr *)&_bindAddr,
                                           _bindAddrlen),
                                "Bind failed: fd={} con_id={} addrlen={}",
                                _fds[0], id().conId(), _bindAddrlen);
    if (_type == TCP_BIND) {
      break;
    }

    if (really_verbose) {
      TRACE("Listening on recreated TCP socket: con_id={} fd={}",
            id().toString(), _fds[0]);
    }
    errno = 0;
    WARN_NE(-1, _real_listen(_fds[0], _listenBacklog),
                  "listen failed: fd={} con_id={} backlog={}", _fds[0],
                  id().conId(), _listenBacklog);
    if (_type == TCP_LISTEN) {
      break;
    }

    break;

  case TCP_ACCEPT:
    ASSERT(!_remotePeerId.isNull(),
           "Can't restore a TCP_ACCEPT socket with null acceptRemoteId; "
           "perhaps handshake went wrong: con_id={} fd={}",
           id().conId(), _fds[0]);

    TRACE("Registering incoming TCP reconnect: con_id={} remote_con_id={} "
          "fd={}",
          id().toString(), _remotePeerId.toString(), _fds[0]);
    ConnectionRewirer::instance().registerIncoming(id(), this, _sockDomain);
    break;

  case TCP_CONNECT:
#ifdef ENABLE_IP6_SUPPORT
    fd = _real_socket(_sockDomain, _sockType, _sockProtocol);
#else // ifdef ENABLE_IP6_SUPPORT
    fd = _real_socket(_sockDomain == AF_INET6 ? AF_INET : _sockDomain,
                      _sockType, _sockProtocol);
#endif // ifdef ENABLE_IP6_SUPPORT
    ASSERT_NE(-1, fd,
                        "failed to recreate connecting TCP socket: con_id={} "
                        "domain={} type={} protocol={}",
                        id().conId(), _sockDomain, _sockType, _sockProtocol);
    restoreDupFds(fd);
    restoreSocketOptions(_fds);
    if (_bindAddrlen != 0) {
      WARN_ERRNO(_real_bind(_fds[0], (sockaddr *)&_bindAddr,
                               _bindAddrlen) != -1,
                    "failed to bind recreated connecting TCP socket: fd={} "
                    "con_id={} addrlen={}",
                    _fds[0], id().conId(), _bindAddrlen);
    }
    TRACE("Registering outgoing TCP reconnect: con_id={} remote_con_id={} "
          "fd={}",
          id().toString(), _remotePeerId.toString(), _fds[0]);
    ConnectionRewirer::instance().registerOutgoing(_remotePeerId, this);
    break;

  }
}

void
TcpConnection::serializeSubClass(jalib::JBinarySerializer &o)
{
  JSERIALIZE_ASSERT_POINT("TcpConnection");
  o&_listenBacklog&_bindAddrlen&_bindAddr &_remotePeerId;
  SocketConnection::serialize(o);
}

/*****************************************************************************
 * RawSocket Connection
 *****************************************************************************/

RawSocketConnection::RawSocketConnection(
  int domain,
  int type,
  int protocol,
  const ConnectionIdentifier& id,
  bool hasLock,
  const InspectedSocket *inspected)
  : Connection(RAW_CREATED, id, hasLock,
               inspected == nullptr ? -1 : inspected->statusFlags,
               inspected == nullptr ? -1 : inspected->realOwner,
               inspected == nullptr ? -1 : inspected->signal)
  , SocketConnection(domain, type, protocol)
{}

void
RawSocketConnection::initializeFromDiscovery(
  const InspectedSocket& inspected,
  bool restorable)
{
  if (!restorable) {
    _type = RAW_PREEXISTING;
    return;
  }

  ASSERT_LE(inspected.localLen, sizeof(_bindAddr));
  _bindAddrlen = inspected.localLen;
  memcpy(&_bindAddr, &inspected.local, inspected.localLen);

  sockaddr_nl *address = reinterpret_cast<sockaddr_nl *>(&_bindAddr);
  if (_bindAddrlen >= sizeof(*address) &&
      (address->nl_pid != 0 || address->nl_groups != 0)) {
    _type = RAW_BIND;
    if (address->nl_pid == static_cast<uint32_t>(_real_getpid())) {
      address->nl_pid = 0;
    }
  }
}

void
RawSocketConnection::drain()
{
  ASSERT(_fds.size() > 0,
         "raw socket connection has no fds during drain: con_id={}",
         id().conId());

  if ((_fcntlFlags & O_ASYNC) != 0) {
    if (really_verbose) {
      TRACE("Removing O_ASYNC before checkpointing raw socket: fd={} "
            "con_id={}",
            _fds[0], id().toString());
    }
    errno = 0;
    ASSERT_NE(-1,
      fcntl(_fds[0], F_SETFL, _fcntlFlags & ~O_ASYNC),
      "removing O_ASYNC during raw socket drain: fd={} con_id={}",
      _fds[0], id().conId());
  }
}

void
RawSocketConnection::refill(bool isRestart)
{
  (void)isRestart;
}

void
RawSocketConnection::postRestart()
{
  ASSERT(_fds.size() > 0,
         "raw socket connection has no fds during postRestart");

  if (really_verbose) {
    TRACE("Recreating raw socket after restart: con_id={} fd={}",
          id().toString(), _fds[0]);
  }

  switch (_type) {
  case RAW_PREEXISTING:
    WARN(false,
         "Netlink socket state was not restorable at checkpoint and is being "
         "replaced with a dead socket: fd={} con_id={}",
         _fds[0], id().conId());
    restoreDupFds(_makeDeadSocket());
    break;

  case RAW_CREATED:
  case RAW_BIND:
  {
    errno = 0;
    int fd = _real_socket(_sockDomain, _sockType, _sockProtocol);
    ASSERT_NE(-1, fd,
                        "failed to recreate raw socket: con_id={} domain={} "
                        "type={} protocol={}",
                        id().conId(), _sockDomain, _sockType, _sockProtocol);
    restoreDupFds(fd);
    restoreSocketOptions(_fds);
    if (_type == RAW_CREATED) {
      restoreNetlinkMemberships(_fds[0]);
      break;
    }

    if (_sockDomain == AF_NETLINK) {
      TRACE("Restoring raw socket options before bind: fd={}", _fds[0]);
      typedef map<int64_t,
                  map<int64_t, vector<char> > >::iterator levelIterator;
      typedef map<int64_t, vector<char> >::iterator optionIterator;

      for (levelIterator lvl = _sockOptions.begin();
           lvl != _sockOptions.end(); ++lvl) {
        if (lvl->first == SOL_SOCKET) {
          for (optionIterator opt = lvl->second.begin();
               opt != lvl->second.end(); ++opt) {
            if (opt->first == SO_ATTACH_FILTER) {
              if (really_verbose) {
                TRACE("Restoring socket option: fd={} option={} size={}",
                      _fds[0], opt->first, opt->second.size());
              }
              int ret = _real_setsockopt(_fds[0], lvl->first, opt->first,
                                         opt->second.data(),
                                         opt->second.size());
              ASSERT_ERRNO(ret == 0,
                           "Restoring raw socket filter setsockopt failed: "
                           "fd={} level={} option={} value={} size={}",
                           _fds[0], lvl->first, opt->first,
                           opt->second.data(), opt->second.size());
            }
          }
        }
      }
    }

    errno = 0;
    WARN_NE(-1, _real_bind(_fds[0],
                                           (sockaddr *)&_bindAddr,
                                           _bindAddrlen),
                                "raw socket bind failed: fd={} con_id={} "
                                "addrlen={}",
                                _fds[0], id().conId(), _bindAddrlen);
    restoreNetlinkMemberships(_fds[0]);
    break;
  }
  default:
    break;
  }
}

void
RawSocketConnection::serializeSubClass(jalib::JBinarySerializer &o)
{
  JSERIALIZE_ASSERT_POINT("RawSocketConnection");
  SocketConnection::serialize(o);
}
