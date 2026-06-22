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
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "jconvert.h"
#include "jfilesystem.h"
#include "jsocket.h"
#include "dmtcp.h"
#include "shareddata.h"
#include "util.h"
#include "base64.h"
#include "kvdb.h"

#include "connectionmessage.h"
#include "connectionrewirer.h"
#include "kernelbufferdrainer.h"
#include "socketconnection.h"
#include "socketwrappers.h"
#include "dmtcp_assert.h"

#ifdef REALLY_VERBOSE_CONNECTION_CPP
static bool really_verbose = true;
#else // ifdef REALLY_VERBOSE_CONNECTION_CPP
static bool really_verbose = false;
#endif // ifdef REALLY_VERBOSE_CONNECTION_CPP

using namespace dmtcp;

constexpr char const *PeerDiscoveryDbCkpt = "/plugin/socket/ckpt";

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
  , _peerType(PEER_UNKNOWN)
  , _listenBacklog(-1)
  , _bindAddrlen(0)
  , _remotePeerId(ConnectionIdentifier::null())
{}

SocketConnection::SocketConnection(int domain,
                                   int type,
                                   int protocol,
                                   ConnectionIdentifier remote)
  : _sockDomain(domain)
  , _sockType(type)
  , _sockProtocol(protocol)
  , _peerType(PEER_UNKNOWN)
  , _listenBacklog(-1)
  , _bindAddrlen(0)
  , _remotePeerId(remote)
{}

void
SocketConnection::onBind(const struct sockaddr *addr, socklen_t len)
{
  ASSERT(false, "Bind called on unknown socket type");
}

void
SocketConnection::onListen(int backlog)
{
  ASSERT(false, "Listen called on unknown socket type: backlog={}", backlog);
}

void
SocketConnection::onConnect(const struct sockaddr *serv_addr,
                            socklen_t addrlen,
                            bool connectInProgress)
{
  ASSERT(false,
         "Connect called on unknown socket type: addr={} len={} in_progress={}",
         serv_addr, addrlen, connectInProgress);
}

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
SocketConnection::serialize(jalib::JBinarySerializer &o)
{
  JSERIALIZE_ASSERT_POINT("SocketConnection");
  o&_sockDomain&_sockType&_sockProtocol &_peerType;

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

/*onSocket*/
TcpConnection::TcpConnection(int domain, int type, int protocol)
  : Connection(TCP_CREATED)
  , SocketConnection(domain, type, protocol)
{
  if (domain != -1) {
    // Sometimes _sockType contains SOCK_CLOEXEC/SOCK_NONBLOCK flags.
    if ((type & 077) == SOCK_DGRAM) {
      WARN(false,
              "Datagram sockets not supported; hopefully this is a short "
              "lived connection: type={}",
              type);
    } else {
// In the domain/type check (around lines 239–247):
      int bt = baseType();
      if (domain == AF_UNIX) {
        WARN(bt == SOCK_STREAM || bt == SOCK_SEQPACKET,
                "unexpected UNIX socket type: domain={} type={} protocol={} "
                "base_type={}",
                domain, type, protocol, bt);
      } else if (domain == AF_INET || domain == AF_INET6) {
        WARN(bt == SOCK_STREAM,
                "unexpected TCP socket type: domain={} type={} protocol={} "
                "base_type={}",
                domain, type, protocol, bt);
      }
    }
    TRACE("Tracking TCP socket connection: con_id={} domain={} type={} "
          "protocol={}",
          id().toString(), domain, type, protocol);
  }
  memset(&_bindAddr, 0, sizeof _bindAddr);
}

TcpConnection&
TcpConnection::asTcp()
{
  return *this;
}

#ifdef STAMPEDE_MPISPAWN_FIX
static int
getMPISpawnPortNum(const char *envVar)
{
  /* PMI_PORT is of the form: "hostname:port" */
  char *temp = getenv(envVar);

  if (temp) {
    while (*temp && *temp != ':') {
      *temp++;
    }
    if (*temp == ':') {
      int port = 0;
      if (Util::parsePortInteger(temp + 1, &port)) {
        return port;
      }
    }
  }
  return 0;
}
#endif // ifdef STAMPEDE_MPISPAWN_FIX

bool
TcpConnection::isBlacklistedTcp(const sockaddr *saddr, socklen_t len)
{
  ASSERT_NOT_NULL(saddr, "null socket address");
  if (len <= sizeof(saddr->sa_family)) {
    return false;
  }

  if (saddr->sa_family == AF_INET) {
    struct sockaddr_in *addr = (sockaddr_in *)saddr;

    // Ports 389 and 636 are the well-known ports in /etc/services that
    // are reserved for LDAP.  Bash continues to maintain a connection to
    // LDAP, leading to problems at restart time.  So, we discover the LDAP
    // remote addresses, and turn them into dead sockets at restart time.
    // However, libc.so:getpwuid() can call libnss_ldap.so which calls
    // libldap-2.4.so to create the LDAP socket while evading our connect
    // wrapper.
    int blacklistedRemotePorts[] = { 53,                 // DNS Server
                                     389, 636,          // LDAP
                                     -1 };
#ifdef STAMPEDE_MPISPAWN_FIX
    int mpispawnPort = getMPISpawnPortNum("PMI_PORT");
    TRACE("Checking PMI socket blacklist: pmi_port={} remote_port={}",
          mpispawnPort, ntohs(addr->sin_port));
    ASSERT(mpispawnPort != 0, "PMI_PORT not found");
    if (ntohs(addr->sin_port) == mpispawnPort) {
      TRACE("Blacklisting PMI socket: pmi_port={}", mpispawnPort);
      return true;
    }
#endif // ifdef STAMPEDE_MPISPAWN_FIX
    for (size_t i = 0; blacklistedRemotePorts[i] != -1; i++) {
      if (ntohs(addr->sin_port) == blacklistedRemotePorts[i]) {
        TRACE("Blacklisting external service socket: remote_port={}",
              ntohs(addr->sin_port));
        return true;
      }
    }
  } else if (saddr->sa_family == AF_UNIX) {
    struct sockaddr_un *uaddr = (struct sockaddr_un *)saddr;
    static string blacklist[] = { "" };
    for (size_t i = 0; blacklist[i] != ""; i++) {
      if (Util::strStartsWith(uaddr->sun_path, blacklist[i].c_str()) ||
          Util::strStartsWith(&uaddr->sun_path[1], blacklist[i].c_str())) {
        TRACE("Blacklisting UNIX socket address: path={}",
              uaddr->sun_path);
        return true;
      }
    }
  }

  // FIXME:  Consider adding configure or dmtcp_launch option to disable
  // all remote connections.  Handy as quick test for special cases.
  return false;
}

void
TcpConnection::onBind(const struct sockaddr *addr, socklen_t len)
{
  if (really_verbose) {
    TRACE("Recording socket bind: con_id={} addrlen={}",
          id().toString(), len);
  }

  if (_sockDomain == AF_UNIX && addr != NULL) {
    ASSERT(len <= sizeof _bindAddr,
           "socket bind address is too large: len={} max={}", len,
           sizeof _bindAddr);
    _bindAddrlen = len;
    memcpy(&_bindAddr, addr, len);
  } else {
    _bindAddrlen = sizeof(_bindAddr);

    // Do not rely on the address passed on to bind as it may contain port 0
    // which allows the OS to give any unused port. Thus we look ourselves up
    // using getsockname.
    ASSERT_NE(-1, getsockname(_fds[0],
                                           (struct sockaddr *)&_bindAddr,
                                           &_bindAddrlen),
                               "failed to query bound socket address: fd={}",
                               _fds[0]);
  }
  _type = TCP_BIND;
}

void
TcpConnection::onListen(int backlog)
{
  /* The application didn't issue a bind() call; the kernel will assign
   * a random address in this case. Call the regular onBind() post-
   * processing function which will save the address returned by the kernel
   * and change the state of the socket connection to TCP_BIND.
   */
  if (_type == TCP_CREATED) {
    onBind(NULL, 0);
  }

  if (really_verbose) {
    TRACE("Recording listening socket: con_id={} backlog={}",
          id().toString(), backlog);
  }
  ASSERT(_type == TCP_BIND,
         "Listening on a non-bind()ed socket: type={} con_id={}", _type,
         id().conId());

  _type = TCP_LISTEN;
  _listenBacklog = backlog;
}

void
TcpConnection::onConnect(const struct sockaddr *addr,
                         socklen_t len,
                         bool connectInProgress)
{
  if (really_verbose) {
    TRACE("Recording socket connect: con_id={}", id().toString());
  }
  WARN(_type == TCP_CREATED || _type == TCP_BIND,
          "Connecting with an in-use socket: type={} con_id={}", _type,
          id().conId());

  if (addr != NULL && isBlacklistedTcp(addr, len)) {
    _type = TCP_EXTERNAL_CONNECT;
    _connectAddrlen = len;
    memcpy(&_connectAddr, addr, len);
  } else if (connectInProgress) {
    // non blocking connect.
    _type = TCP_CONNECT_IN_PROGRESS;
  } else {
    _type = TCP_CONNECT;
  }
}

/*onAccept*/
TcpConnection::TcpConnection(const TcpConnection &parent,
                             const ConnectionIdentifier &remote)
  : Connection(TCP_ACCEPT)
  , SocketConnection(parent._sockDomain,
                     parent._sockType,
                     parent._sockProtocol,
                     remote)
{
  if (really_verbose) {
    TRACE("Recording accepted socket: con_id={} parent_con_id={} "
          "remote_con_id={}",
          id().toString(), parent.id().toString(), remote.toString());
  }

  memset(&_bindAddr, 0, sizeof _bindAddr);
}

void
TcpConnection::sendPeerInformation()
{
  struct sockaddr_storage key = {};
  struct sockaddr_storage value = {};
  socklen_t keysz = 0, valuesz = 0;
  bool sendPeerInfo = false;

  if (!(_sockDomain == AF_INET || _sockDomain == AF_INET6) ||
      _sockType != SOCK_STREAM) {
    return;
  }

  switch (_type) {
  case TCP_CONNECT:
  case TCP_CONNECT_IN_PROGRESS:
  {
    // Local connect socket information
    keysz = sizeof(key);
    ASSERT_NE(-1,
      getsockname(_fds[0], reinterpret_cast<struct sockaddr *>(&key), &keysz),
                               "querying local TCP socket: fd={}", _fds[0]);
    // Information about the accept socket on the server
    valuesz = sizeof(value);
    ASSERT_NE(-1,
      getpeername(_fds[0], reinterpret_cast<struct sockaddr *>(&value),
                  &valuesz),
                               "querying peer TCP socket: fd={}", _fds[0]);
    sendPeerInfo = true;
    break;
  }
  case TCP_ACCEPT:
  {
    // Local accept socket information
    keysz = sizeof(key);
    ASSERT_NE(-1,
      getsockname(_fds[0], reinterpret_cast<struct sockaddr *>(&key), &keysz),
                               "querying accepted TCP socket: fd={}",
                               _fds[0]);
    // Information about the client connect socket
    valuesz = sizeof(value);
    ASSERT_NE(-1,
      getpeername(_fds[0], reinterpret_cast<struct sockaddr *>(&value),
                  &valuesz),
                               "querying accepted TCP peer: fd={}",
                               _fds[0]);
    sendPeerInfo = true;
    break;
  }
  default:
    break;
  }

  if (sendPeerInfo) {
    ASSERT(keysz <= sizeof(key), "TCP peer key size overflow: size={} max={}",
           keysz, sizeof(key));
    ASSERT(valuesz <= sizeof(value),
           "TCP peer value size overflow: size={} max={}", valuesz,
           sizeof(value));
    string keyStr = base64::encode(reinterpret_cast<const char *>(&key), keysz);
    string valStr =
      base64::encode(reinterpret_cast<const char *>(&value), valuesz);
    ASSERT(kvdb::set(PeerDiscoveryDbCkpt, keyStr, valStr) ==
             kvdb::KVDBResponse::SUCCESS,
           "failed to store TCP peer discovery data: fd={} key_size={} "
           "value_size={}",
           _fds[0], keysz, valuesz);
  }
}

void
TcpConnection::recvPeerInformation()
{
  struct sockaddr_storage key = {};
  struct sockaddr_storage value = {};
  socklen_t keylen = 0;

  if (!(_sockDomain == AF_INET || _sockDomain == AF_INET6) ||
      _sockType != SOCK_STREAM) {
    return;
  }

  if (_type == TCP_CONNECT || _type == TCP_ACCEPT ||
      _type == TCP_CONNECT_IN_PROGRESS) {
    keylen = sizeof(key);
    ASSERT_NE(-1,
      getpeername(_fds[0], reinterpret_cast<struct sockaddr *>(&key),
                  &keylen),
      "querying TCP peer for discovery lookup: fd={}", _fds[0]);

    ASSERT(keylen <= sizeof(key), "TCP peer key size overflow: size={} max={}",
           keylen, sizeof(key));
    string keyStr =
      base64::encode(reinterpret_cast<const char *>(&key), keylen);
    string valStr;
    if (kvdb::get(PeerDiscoveryDbCkpt, keyStr, &valStr) ==
        kvdb::KVDBResponse::SUCCESS) {
      string valBinary = dmtcp::base64::decode(valStr);
      ASSERT(valBinary.size() <= sizeof(value),
             "unexpected TCP peer discovery payload size: fd={} size={} "
             "max={}",
             _fds[0], valBinary.size(), sizeof(value));
      memcpy(&value, valBinary.data(), valBinary.size());
    } else {
      WARN(false,
              "DMTCP detected an external connect socket; it will be "
              "restored as a dead socket: fd={}",
              _fds[0]);
      markExternalConnect();
    }
  }
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

  // Non blocking connect; need to hang around until it is writable.
  if (_type == TCP_CONNECT_IN_PROGRESS) {
    int retval;
    struct pollfd socketFd = { 0 };

    socketFd.fd = _fds[0];
    socketFd.events = POLLOUT;

    retval = _real_poll(&socketFd, 1, 60 * 1000);

    if (retval == -1) {
      TRACE("Poll failed while waiting for connect(): error={}",
            strerror(errno));
    } else if (socketFd.revents & POLLOUT) {
      int val = -1;
      socklen_t sz = sizeof(val);
      int ret = getsockopt(_fds[0], SOL_SOCKET, SO_ERROR, &val, &sz);
      if (ret == 0 && val == 0) {
        TRACE("Connect-in-progress socket is writable: fd={}",
              _fds[0]);
        _type = TCP_CONNECT;
      } else {
        if (ret == -1) {
          WARN_ERRNO(false,
                     "getsockopt(SO_ERROR) failed after connect() "
                     "readiness: fd={}",
                     _fds[0]);
        } else {
          WARN(false,
               "connect() completion failed; marking socket external: "
               "fd={} so_error={} error={}",
               _fds[0], val, strerror(val));
        }
        _type = TCP_EXTERNAL_CONNECT;
      }
    } else {
      WARN(false,
              "connect() returned EINPROGRESS and socket is still not "
              "writable after 60 seconds; marking as external and "
              "continuing checkpoint: fd={}",
              _fds[0]);
      _type = TCP_EXTERNAL_CONNECT;
    }
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
TcpConnection::doSendHandshakes(const ConnectionIdentifier &coordId)
{
  switch (_type) {
  case TCP_CONNECT:
  case TCP_ACCEPT:
    TRACE("Sending TCP restore handshake: con_id={} fd={}",
          id().toString(), _fds[0]);
    sendHandshake(_fds[0], coordId);
    break;
  case TCP_EXTERNAL_CONNECT:
    TRACE("Skipping handshake send for external TCP socket: fd={}",
          _fds[0]);
    break;
  }
}

void
TcpConnection::doRecvHandshakes(const ConnectionIdentifier &coordId)
{
  switch (_type) {
  case TCP_CONNECT:
  case TCP_ACCEPT:
    recvHandshake(_fds[0], coordId);
    TRACE("Received TCP restore handshake: con_id={} remote_con_id={} fd={}",
          id().toString(), _remotePeerId.toString(), _fds[0]);
    break;
  case TCP_EXTERNAL_CONNECT:
    TRACE("Skipping handshake receive for external TCP socket: fd={}",
          _fds[0]);
    break;
  }
}

void
TcpConnection::refill(bool isRestart)
{
  if ((_fcntlFlags & O_ASYNC) != 0) {
    TRACE("Restoring O_ASYNC after checkpointing TCP socket: fd={} con_id={}",
          _fds[0], id().toString());
    restoreSocketOptions(_fds);
  } else if (isRestart && _sockDomain != AF_INET6 &&
             _type != TCP_EXTERNAL_CONNECT) {
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
  case TCP_INVALID:
  case TCP_EXTERNAL_CONNECT:
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
TcpConnection::sendHandshake(int remotefd, const ConnectionIdentifier &coordId)
{
  jalib::JSocket remote(remotefd);
  ConnMsg msg(ConnMsg::HANDSHAKE);

  msg.from = id();
  msg.coordId = coordId;
  remote << msg;
}

void
TcpConnection::recvHandshake(int remotefd, const ConnectionIdentifier &coordId)
{
  jalib::JSocket remote(remotefd);
  ConnMsg msg;

  msg.poison();
  remote >> msg;

  msg.assertValid(ConnMsg::HANDSHAKE);
  ASSERT(msg.coordId == coordId,
         "Peer has a different dmtcp_coordinator than us: peer_coord={} "
         "expected_coord={}",
         msg.coordId.conId(), coordId.conId());

  if (_remotePeerId.isNull()) {
    // first time
    _remotePeerId = msg.from;
    ASSERT(!_remotePeerId.isNull(),
           "Read handshake with invalid 'from' field");
  } else {
    // next time
    ASSERT(_remotePeerId == msg.from,
           "Read handshake with a different 'from' field than a previous "
           "handshake: previous={} current={}",
           _remotePeerId.conId(), msg.from.conId());
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

/*onSocket*/
RawSocketConnection::RawSocketConnection(int domain, int type, int protocol)
  : Connection(RAW_CREATED)
  , SocketConnection(domain, type, protocol)
{
  ASSERT(type == -1 || baseType() == SOCK_RAW,
         "raw socket connection created with non-raw type: type={}", type);
  ASSERT(domain == -1 || domain == AF_NETLINK,
         "Only Netlink raw socket supported: domain={}", domain);
  TRACE("Tracking raw socket connection: con_id={} domain={} type={} "
        "protocol={}",
        id().toString(), domain, type, protocol);
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
  if ((_fcntlFlags & O_ASYNC) != 0) {
    TRACE("Restoring O_ASYNC after checkpointing raw socket: fd={} con_id={}",
          _fds[0], id().toString());
    restoreSocketOptions(_fds);
  } else if (isRestart) {
    restoreSocketOptions(_fds);
  }
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
  case RAW_CREATED:
  case RAW_BIND:
  case RAW_LISTEN:
  {
    errno = 0;
    int fd = _real_socket(_sockDomain, _sockType, _sockProtocol);
    ASSERT_NE(-1, fd,
                        "failed to recreate raw socket: con_id={} domain={} "
                        "type={} protocol={}",
                        id().conId(), _sockDomain, _sockType, _sockProtocol);
    restoreDupFds(fd);
    if (_type == RAW_CREATED) {
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
    if (_type == RAW_BIND) {
      break;
    }

    errno = 0;
    WARN_NE(-1, _real_listen(_fds[0], _listenBacklog),
                  "raw socket listen failed: fd={} con_id={} backlog={}",
                  _fds[0], id().conId(), _listenBacklog);
    if (_type == RAW_LISTEN) {
      break;
    }
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

void
RawSocketConnection::onBind(const struct sockaddr *addr, socklen_t len)
{
  TRACE("Recording raw socket bind: fd={} con_type={} con_id={}",
        _fds[0], this->conType(), this->id().toString());
  if (addr != NULL) {
    ASSERT(len <= sizeof _bindAddr,
           "raw socket bind address is too large: len={} max={}", len,
           sizeof _bindAddr);
    _bindAddrlen = len;
    memcpy(&_bindAddr, addr, len);
  }
  _type = RAW_BIND;
}

void
RawSocketConnection::onListen(int backlog)
{
  TRACE("Recording raw socket listen: fd={} con_type={} con_id={} "
        "backlog={}",
        _fds[0], this->conType(), this->id().toString(), backlog);
  _listenBacklog = backlog;
  _type = RAW_LISTEN;
}

RawSocketConnection::RawSocketConnection(const RawSocketConnection &parent,
                                         const ConnectionIdentifier &remote)
  : Connection(RAW_ACCEPT)
  , SocketConnection(parent._sockDomain,
                     parent._sockType,
                     parent._sockProtocol,
                     remote)
{
  if (really_verbose) {
    TRACE("Recording raw accepted socket: con_id={} parent_con_id={} "
          "remote_con_id={}",
          id().toString(), parent.id().toString(), remote.toString());
  }

  WARN(false,
          "Accept on raw socket type not supported; socket will not be "
          "restored: parent_con_id={} remote_con_id={}",
          parent.id().conId(), remote.conId());
  memset(&_bindAddr, 0, sizeof _bindAddr);
}

void
RawSocketConnection::onConnect(const struct sockaddr *serv_addr,
                               socklen_t addrlen,
                               bool connectInProgress)
{
  WARN(false,
          "Connect on raw socket type not supported; socket will not be "
          "restored: addr={} len={} in_progress={}",
          serv_addr, addrlen, connectInProgress);
}
