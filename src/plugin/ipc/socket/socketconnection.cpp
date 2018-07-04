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
#include <sys/un.h>
#include <unistd.h>

#include "jassert.h"
#include "jconvert.h"
#include "jfilesystem.h"
#include "jsocket.h"
#include "dmtcp.h"
#include "shareddata.h"
#include "util.h"

#include "connectionmessage.h"
#include "connectionrewirer.h"
#include "kernelbufferdrainer.h"
#include "socketconnection.h"
#include "socketwrappers.h"

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

  JASSERT(_real_socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0) (JASSERT_ERRNO)
  .Text("socketpair() failed");
  JASSERT(sp[0] >= 0 && sp[1] >= 0) (sp[0]) (sp[1])
  .Text("socketpair() failed");
  if (refillData != NULL) {
    JASSERT(Util::writeAll(sp[1], refillData, len) == len);
  }
  _real_close(sp[1]);
  if (really_verbose) {
    JTRACE("Created dead socket.") (sp[0]);
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
  JASSERT(false).Text("Bind called on unknown socket type");
}

void
SocketConnection::onListen(int backlog)
{
  JASSERT(false).Text("Listen called on unknown socket type");
}

void
SocketConnection::onConnect(const struct sockaddr *serv_addr,
                            socklen_t addrlen,
                            bool connectInProgress)
{
  JASSERT(false).Text("Connect called on unknown socket type");
}

void
SocketConnection::addSetsockopt(int level,
                                int option,
                                const void *value,
                                int len)
{
  _sockOptions[level][option] = jalib::JBuffer(value, len);
}

void
SocketConnection::restoreSocketOptions(vector<int> &fds)
{
  typedef map<int64_t, map<int64_t, jalib::JBuffer> >::iterator levelIterator;
  typedef map<int64_t, jalib::JBuffer>::iterator optionIterator;

  for (levelIterator lvl = _sockOptions.begin();
       lvl != _sockOptions.end(); ++lvl) {
    for (optionIterator opt = lvl->second.begin();
         opt != lvl->second.end(); ++opt) {
      JTRACE("Restoring socket option.")
        (fds[0]) (opt->first) (opt->second.size());
      int ret = _real_setsockopt(fds[0], lvl->first, opt->first,
                                 opt->second.buffer(),
                                 opt->second.size());
      JWARNING(ret == 0) (JASSERT_ERRNO) (fds[0])
        (lvl->first) (opt->first) (opt->second.size())
      .Text("Restoring setsockopt failed.");
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
    // JTRACE("TCP Serialize ") (_type) (_id.conId());
    typedef map<int64_t, map<int64_t, jalib::JBuffer> >::iterator levelIterator;
    typedef map<int64_t, jalib::JBuffer>::iterator optionIterator;

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
        jalib::JBuffer &buffer = opt->second;
        int64_t bufLen = buffer.size();

        JSERIALIZE_ASSERT_POINT("Opt");

        o&optType &bufLen;
        o.readOrWrite(buffer.buffer(), bufLen);
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

        jalib::JBuffer buffer(bufLen);
        o.readOrWrite(buffer.buffer(), bufLen);

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
      JWARNING(false) (type)
      .Text("Datagram Sockets not supported. "
            "Hopefully, this is a short lived connection!");
    } else {
      JWARNING((domain == AF_INET || domain == AF_UNIX || domain == AF_INET6)
               && (type & 077) == SOCK_STREAM)
        (domain) (type) (protocol);
    }
    JTRACE("Creating TcpConnection.") (id()) (domain) (type) (protocol);
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
      return atoi(temp + 1);
    }
  }
  return 0;
}
#endif // ifdef STAMPEDE_MPISPAWN_FIX

bool
TcpConnection::isBlacklistedTcp(const sockaddr *saddr, socklen_t len)
{
  JASSERT(saddr != NULL);
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
    JTRACE("PMI_PORT port") (mpispawnPort) (ntohs(addr->sin_port));
    JASSERT(mpispawnPort != 0).Text("PMI_PORT not found");
    if (ntohs(addr->sin_port) == mpispawnPort) {
      JTRACE("PMI_PORT port found") (mpispawnPort);
      return true;
    }
#endif // ifdef STAMPEDE_MPISPAWN_FIX
    for (size_t i = 0; blacklistedRemotePorts[i] != -1; i++) {
      if (ntohs(addr->sin_port) == blacklistedRemotePorts[i]) {
        JTRACE("LDAP port found") (ntohs(addr->sin_port))
          (blacklistedRemotePorts[0]) (blacklistedRemotePorts[1]);
        return true;
      }
    }
  } else if (saddr->sa_family == AF_UNIX) {
    struct sockaddr_un *uaddr = (struct sockaddr_un *)saddr;
    static string blacklist[] = { "" };
    for (size_t i = 0; blacklist[i] != ""; i++) {
      if (Util::strStartsWith(uaddr->sun_path, blacklist[i].c_str()) ||
          Util::strStartsWith(&uaddr->sun_path[1], blacklist[i].c_str())) {
        JTRACE("Blacklisted socket address") (uaddr->sun_path);
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
    JTRACE("Binding.") (id()) (len);
  }

  // If the bind succeeded, we do not need any additional assert.
  // JASSERT(_type == TCP_CREATED) (_type) (id())
  // .Text("Binding a socket in use????");

  if (_sockDomain == AF_UNIX && addr != NULL) {
    JASSERT(len <= sizeof _bindAddr) (len) (sizeof _bindAddr)
    .Text("That is one huge sockaddr buddy.");
    _bindAddrlen = len;
    memcpy(&_bindAddr, addr, len);
  } else {
    _bindAddrlen = sizeof(_bindAddr);

    // Do not rely on the address passed on to bind as it may contain port 0
    // which allows the OS to give any unused port. Thus we look ourselves up
    // using getsockname.
    JASSERT(getsockname(_fds[0],
                        (struct sockaddr *)&_bindAddr,
                        &_bindAddrlen) == 0)
      (JASSERT_ERRNO);
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
    JTRACE("Listening.") (id()) (backlog);
  }
  JASSERT(_type == TCP_BIND) (_type) (id())
  .Text("Listening on a non-bind()ed socket????");

  // A -1 backlog is not an error.
  // JASSERT(backlog > 0) (backlog)
  // .Text("That is an odd backlog????");

  _type = TCP_LISTEN;
  _listenBacklog = backlog;
}

void
TcpConnection::onConnect(const struct sockaddr *addr,
                         socklen_t len,
                         bool connectInProgress)
{
  if (really_verbose) {
    JTRACE("Connecting.") (id());
  }
  JWARNING(_type == TCP_CREATED || _type == TCP_BIND) (_type) (id())
  .Text("Connecting with an in-use socket????");

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
    JTRACE("Accepting.") (id()) (parent.id()) (remote);
  }

  // JASSERT(parent._type == TCP_LISTEN) (parent._type) (parent.id())
  // .Text("Accepting from a non listening socket????");
  memset(&_bindAddr, 0, sizeof _bindAddr);
}

void
TcpConnection::sendPeerInformation()
{
  struct sockaddr key = {0}, value = {0};
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
    JASSERT(getsockname(_fds[0], &key, &keysz) == 0);
    // Information about the accept socket on the server
    valuesz = sizeof(value);
    JASSERT(getpeername(_fds[0], &value, &valuesz) == 0);
    sendPeerInfo = true;
    break;
  }
  case TCP_ACCEPT:
  {
    // Local accept socket information
    keysz = sizeof(key);
    JASSERT(getsockname(_fds[0], &key, &keysz) == 0);
    // Information about the client connect socket
    valuesz = sizeof(value);
    JASSERT(getpeername(_fds[0], &value, &valuesz) == 0);
    sendPeerInfo = true;
    break;
  }
  default:
    break;
  }
  if (sendPeerInfo) {
    dmtcp_send_key_val_pair_to_coordinator("SCons",
                                           &key, keysz,
                                           &value, valuesz);
  }
}

void
TcpConnection::recvPeerInformation()
{
  struct sockaddr key = {0}, value = {0};
  socklen_t keylen = 0, vallen = 0;

  if (!(_sockDomain == AF_INET || _sockDomain == AF_INET6) ||
      _sockType != SOCK_STREAM) {
    return;
  }

  if (_type == TCP_CONNECT || _type == TCP_ACCEPT ||
      _type == TCP_CONNECT_IN_PROGRESS) {
    keylen = sizeof(key);
    JASSERT(getpeername(_fds[0], &key, &keylen) == 0);
    vallen = sizeof(value);
    int ret = dmtcp_send_query_to_coordinator("SCons",
                                              &key, keylen,
                                              &value, &vallen);
    if (ret != 0) {
      JASSERT(vallen == sizeof(value))(vallen)(sizeof(value));
    } else {
      JWARNING(false) (_fds[0])
       .Text("DMTCP detected an \"external\" connect socket."
             "The socket will be restored as a dead socket.");
      markExternalConnect();
    }
  }
}

void
TcpConnection::onError()
{
  JTRACE("Error.") (id());
  _type = TCP_ERROR;
  JTRACE("Creating dead socket.") (_fds[0]) (_fds.size());
  const vector<char> &buffer =
    KernelBufferDrainer::instance().getDrainedData(_id);
  restoreDupFds(_makeDeadSocket(&buffer[0], buffer.size()));
}

void
TcpConnection::drain()
{
  JASSERT(_fds.size() > 0) (id());

  if ((_fcntlFlags & O_ASYNC) != 0) {
    if (really_verbose) {
      JTRACE("Removing O_ASYNC flag during checkpoint.") (_fds[0]) (id());
    }
    errno = 0;
    JASSERT(fcntl(_fds[0], F_SETFL, _fcntlFlags & ~O_ASYNC) == 0)
      (JASSERT_ERRNO) (_fds[0]) (id());
  }

  if (dmtcp_no_coordinator()) {
    markExternalConnect();
  }

  // Non blocking connect; need to hang around until it is writable.
  if (_type == TCP_CONNECT_IN_PROGRESS) {
    int retval;
    struct pollfd socketFd = { 0 };

    socketFd.fd = _fds[0];
    socketFd.events = POLLOUT;

    retval = _real_poll(&socketFd, 1, 60 * 1000);

    if (retval == -1) {
      JTRACE("poll() failed") (JASSERT_ERRNO);
    } else if (socketFd.revents & POLLOUT) {
      int val = -1;
      socklen_t sz = sizeof(val);
      getsockopt(_fds[0], SOL_SOCKET, SO_ERROR, &val, &sz);
      JTRACE("Connect-in-progress socket is now writable.") (_fds[0]);
      _type = TCP_CONNECT;
    } else {
      JWARNING(false) (_fds[0])
      .Text("connect() returned EINPROGRESS.  Socket still not writable\n"
            "after 60 seconds.   The socket is probably connected to an\n"
            "external process not under DMTCP control.\n"
            "Marking this socket as external and continuing to checkpoint.");
      _type = TCP_EXTERNAL_CONNECT;
    }
  }

  switch (_type) {
  case TCP_ERROR:

  // Treat TCP_ERROR as a regular socket for draining purposes. There still
  // might be some stale data on it.
  case TCP_CONNECT:
  case TCP_ACCEPT:
    JTRACE("Will drain socket") (_hasLock) (_fds[0]) (_id) (_remotePeerId);
    KernelBufferDrainer::instance().beginDrainOf(_fds[0], _id);
    break;
  case TCP_LISTEN:
    KernelBufferDrainer::instance().addListenSocket(_fds[0]);
    break;
  case TCP_BIND:
    JWARNING(_type != TCP_BIND) (_fds[0])
    .Text("If there are pending connections on this socket,\n"
          " they won't be checkpointed because"
          " it is not yet in a listen state.");
    break;
  case TCP_EXTERNAL_CONNECT:
    JTRACE("Socket to External Process, won't be drained") (_fds[0]);
    break;
  }
}

void
TcpConnection::doSendHandshakes(const ConnectionIdentifier &coordId)
{
  switch (_type) {
  case TCP_CONNECT:
  case TCP_ACCEPT:
    JTRACE("Sending handshake ...") (id()) (_fds[0]);
    sendHandshake(_fds[0], coordId);
    break;
  case TCP_EXTERNAL_CONNECT:
    JTRACE("Socket to External Process, skipping handshake send") (_fds[0]);
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
    JTRACE("Received handshake.") (id()) (_remotePeerId) (_fds[0]);
    break;
  case TCP_EXTERNAL_CONNECT:
    JTRACE("Socket to External Process, skipping handshake recv") (_fds[0]);
    break;
  }
}

void
TcpConnection::refill(bool isRestart)
{
  if ((_fcntlFlags & O_ASYNC) != 0) {
    JTRACE("Re-adding O_ASYNC flag.") (_fds[0]) (id());
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

  JASSERT(_fds.size() > 0);
  switch (_type) {
  case TCP_PREEXISTING:
  case TCP_INVALID:
  case TCP_EXTERNAL_CONNECT:
    JTRACE("Creating dead socket.") (_fds[0]) (_fds.size());
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
    JWARNING((_sockDomain == AF_INET || _sockDomain == AF_UNIX ||
              _sockDomain == AF_INET6) && (_sockType & 077) == SOCK_STREAM)
      (id()) (_sockDomain) (_sockType) (_sockProtocol)
    .Text("Socket type not yet [fully] supported.");

    if (really_verbose) {
      JTRACE("Restoring socket.") (id()) (_fds[0]);
    }

    fd = _real_socket(_sockDomain, _sockType, _sockProtocol);
    JASSERT(fd != -1) (JASSERT_ERRNO);
    restoreDupFds(fd);

    if (_type == TCP_CREATED) {
      break;
    }

    if (_sockDomain == AF_UNIX &&
        _bindAddrlen > sizeof(_bindAddr.ss_family)) {
      struct sockaddr_un *uaddr = (sockaddr_un *)&_bindAddr;
      if (uaddr->sun_path[0] != '\0') {
        JTRACE("Unlinking stale unix domain socket.") (uaddr->sun_path);
        JWARNING(unlink(uaddr->sun_path) == 0) (uaddr->sun_path);
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
      JTRACE("Restoring some socket options before binding.");
      typedef map<int64_t,
                  map<int64_t, jalib::JBuffer> >::iterator levelIterator;
      typedef map<int64_t, jalib::JBuffer>::iterator optionIterator;

      for (levelIterator lvl = _sockOptions.begin();
           lvl != _sockOptions.end(); ++lvl) {
        if (lvl->first == IPPROTO_IPV6) {
          for (optionIterator opt = lvl->second.begin();
               opt != lvl->second.end(); ++opt) {
            if (opt->first == IPV6_V6ONLY) {
              if (really_verbose) {
                JTRACE("Restoring socket option.")
                  (_fds[0]) (opt->first) (opt->second.size());
              }
              int ret = _real_setsockopt(_fds[0], lvl->first, opt->first,
                                         opt->second.buffer(),
                                         opt->second.size());
              JASSERT(ret == 0) (JASSERT_ERRNO) (_fds[0]) (lvl->first)
                (opt->first) (opt->second.buffer()) (opt->second.size())
              .Text("Restoring setsockopt failed.");
            }
          }
        }
      }
    }

    if (really_verbose) {
      JTRACE("Binding socket.") (id());
    }
    errno = 0;
    JWARNING(_real_bind(_fds[0], (sockaddr *)&_bindAddr, _bindAddrlen) == 0)
      (JASSERT_ERRNO) (id()).Text("Bind failed.");
    if (_type == TCP_BIND) {
      break;
    }

    if (really_verbose) {
      JTRACE("Listening socket.") (id());
    }
    errno = 0;
    JWARNING(_real_listen(_fds[0], _listenBacklog) == 0)
      (JASSERT_ERRNO) (id()) (_listenBacklog).Text("listen failed.");
    if (_type == TCP_LISTEN) {
      break;
    }

    break;

  case TCP_ACCEPT:
    JASSERT(!_remotePeerId.isNull()) (id()) (_remotePeerId) (_fds[0])
    .Text("Can't restore a TCP_ACCEPT socket with null acceptRemoteId.\n"
          "  Perhaps handshake went wrong?");

    JTRACE("registerIncoming") (id()) (_remotePeerId) (_fds[0]);
    ConnectionRewirer::instance().registerIncoming(id(), this, _sockDomain);
    break;

  case TCP_CONNECT:
#ifdef ENABLE_IP6_SUPPORT
    fd = _real_socket(_sockDomain, _sockType, _sockProtocol);
#else // ifdef ENABLE_IP6_SUPPORT
    fd = _real_socket(_sockDomain == AF_INET6 ? AF_INET : _sockDomain,
                      _sockType, _sockProtocol);
#endif // ifdef ENABLE_IP6_SUPPORT
    JASSERT(fd != -1) (JASSERT_ERRNO);
    restoreDupFds(fd);
    if (_bindAddrlen != 0) {
      JWARNING(_real_bind(_fds[0], (sockaddr *)&_bindAddr, _bindAddrlen) != -1)
        (JASSERT_ERRNO);
    }
    JTRACE("registerOutgoing") (id()) (_remotePeerId) (_fds[0]);
    ConnectionRewirer::instance().registerOutgoing(_remotePeerId, this);
    break;

    // case TCP_EXTERNAL_CONNECT:
    // int sockFd = _real_socket(_sockDomain, _sockType, _sockProtocol);
    // JASSERT(sockFd >= 0);
    // JASSERT(_real_dup2(sockFd, _fds[0]) == _fds[0]);
    // JWARNING(0 == _real_connect(sockFd,(sockaddr*) &_connectAddr,
    // _connectAddrlen))
    // (_fds[0]) (JASSERT_ERRNO)
    // .Text("Unable to connect to external process");
    // break;
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
  JASSERT(msg.coordId == coordId) (msg.coordId) (coordId)
  .Text("Peer has a different dmtcp_coordinator than us!\n"
        "  It must be the same.");

  if (_remotePeerId.isNull()) {
    // first time
    _remotePeerId = msg.from;
    JASSERT(!_remotePeerId.isNull())
    .Text("Read handshake with invalid 'from' field.");
  } else {
    // next time
    JASSERT(_remotePeerId == msg.from)
      (_remotePeerId) (msg.from)
    .Text("Read handshake with a different 'from' field"
          " than a previous handshake.");
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
  JASSERT(type == -1 || (type & SOCK_RAW));
  JASSERT(domain == -1 || domain == AF_NETLINK) (domain)
  .Text("Only Netlink raw socket supported");
  JTRACE("Creating Raw socket.") (id()) (domain) (type) (protocol);
}

void
RawSocketConnection::drain()
{
  JASSERT(_fds.size() > 0) (id());

  if ((_fcntlFlags & O_ASYNC) != 0) {
    if (really_verbose) {
      JTRACE("Removing O_ASYNC flag during checkpoint.") (_fds[0]) (id());
    }
    errno = 0;
    JASSERT(fcntl(_fds[0], F_SETFL, _fcntlFlags & ~O_ASYNC) == 0)
      (JASSERT_ERRNO) (_fds[0]) (id());
  }
}

void
RawSocketConnection::refill(bool isRestart)
{
  if ((_fcntlFlags & O_ASYNC) != 0) {
    JTRACE("Re-adding O_ASYNC flag.") (_fds[0]) (id());
    restoreSocketOptions(_fds);
  } else if (isRestart) {
    restoreSocketOptions(_fds);
  }
}

void
RawSocketConnection::postRestart()
{
  JASSERT(_fds.size() > 0);

  if (really_verbose) {
    JTRACE("Restoring socket.") (id()) (_fds[0]);
  }

  switch (_type) {
  case RAW_CREATED:
  case RAW_BIND:
  case RAW_LISTEN:
  {
    errno = 0;
    int fd = _real_socket(_sockDomain, _sockType, _sockProtocol);
    JASSERT(fd != -1) (JASSERT_ERRNO);
    restoreDupFds(fd);
    if (_type == RAW_CREATED) {
      break;
    }

    if (_sockDomain == AF_NETLINK) {
      JTRACE("Restoring some socket options before binding.");
      typedef map<int64_t,
                  map<int64_t, jalib::JBuffer> >::iterator levelIterator;
      typedef map<int64_t, jalib::JBuffer>::iterator optionIterator;

      for (levelIterator lvl = _sockOptions.begin();
           lvl != _sockOptions.end(); ++lvl) {
        if (lvl->first == SOL_SOCKET) {
          for (optionIterator opt = lvl->second.begin();
               opt != lvl->second.end(); ++opt) {
            if (opt->first == SO_ATTACH_FILTER) {
              if (really_verbose) {
                JTRACE("Restoring socket option.")
                  (_fds[0]) (opt->first) (opt->second.size());
              }
              int ret = _real_setsockopt(_fds[0], lvl->first, opt->first,
                                         opt->second.buffer(),
                                         opt->second.size());
              JASSERT(ret == 0) (JASSERT_ERRNO) (_fds[0]) (lvl->first)
                (opt->first) (opt->second.buffer()) (opt->second.size())
              .Text("Restoring setsockopt failed.");
            }
          }
        }
      }
    }

    errno = 0;
    JWARNING(_real_bind(_fds[0], (sockaddr *)&_bindAddr, _bindAddrlen) == 0)
      (JASSERT_ERRNO) (id()).Text("Bind failed.");
    if (_type == RAW_BIND) {
      break;
    }

    errno = 0;
    JWARNING(_real_listen(_fds[0], _listenBacklog) == 0)
      (JASSERT_ERRNO) (id()) (_listenBacklog).Text("listen failed.");
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
  JTRACE("bind on raw socket") (_fds[0]) (this->conType()) (this->id());
  if (addr != NULL) {
    JASSERT(len <= sizeof _bindAddr) (len) (sizeof _bindAddr)
    .Text("That is one huge sockaddr buddy.");
    _bindAddrlen = len;
    memcpy(&_bindAddr, addr, len);
  }
  _type = RAW_BIND;
}

void
RawSocketConnection::onListen(int backlog)
{
  JTRACE("listen on raw socket") (_fds[0]) (this->conType()) (this->id()) (
    backlog);
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
    JTRACE("Accepting.") (id()) (parent.id()) (remote);
  }

  // JASSERT(parent._type == RAW_LISTEN) (parent._type) (parent.id())
  // .Text("Accepting from a non listening socket????");
  JWARNING(false).Text("Accept on raw socket type not supported...\n"
                       "Socket won't be restored");
  memset(&_bindAddr, 0, sizeof _bindAddr);
}

void
RawSocketConnection::onConnect(const struct sockaddr *serv_addr,
                               socklen_t addrlen,
                               bool connectInProgress)
{
  JWARNING(false).Text("Connect on raw socket type not supported...\n"
                       "Socket won't be restored");
}
