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

#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <termios.h>
#include <iostream>
#include <ios>
#include <fstream>
#include <linux/limits.h>
#include <arpa/inet.h>

#include "connection.h"
#include "connectionlist.h"
#include "kernelbufferdrainer.h"
#include "connectionrewirer.h"
#include "connwrappers.h"
#include "dmtcpplugin.h"
#include "shareddata.h"
#include "util.h"
#include "util_descriptor.h"
#include "../jalib/jsocket.h"
#include "../jalib/jassert.h"
#include "../jalib/jfilesystem.h"
#include "../jalib/jconvert.h"

static bool ptmxTestPacketMode(int masterFd);
static ssize_t ptmxReadAll(int fd, const void *origBuf, size_t maxCount);
static ssize_t ptmxWriteAll(int fd, const void *buf, bool isPacketMode);
static void CopyFile(const dmtcp::string& src, const dmtcp::string& dest);
static void CreateDirectoryStructure(const dmtcp::string& path);

#ifdef REALLY_VERBOSE_CONNECTION_CPP
static bool really_verbose = true;
#else
static bool really_verbose = false;
#endif

//this function creates a socket that is in an error state
static int _makeDeadSocket()
{
  //it does it by creating a socket pair and closing one side
  int sp[2] = {-1,-1};
  JASSERT(_real_socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0) (JASSERT_ERRNO)
    .Text("socketpair() failed");
  JASSERT(sp[0]>=0 && sp[1]>=0) (sp[0]) (sp[1])
    .Text("socketpair() failed");
  _real_close(sp[1]);
  if (really_verbose) {
    JTRACE("Created dead socket.") (sp[0]);
  }
  return sp[0];
}

static bool _isVimApp()
{
  static int isVimApp = -1;

  if (isVimApp == -1) {
    dmtcp::string progName = jalib::Filesystem::GetProgramName();

    if (progName == "vi" || progName == "vim" || progName == "vim-normal" ||
        progName == "vim.basic"  || progName == "vim.tiny" ||
        progName == "vim.gtk" || progName == "vim.gnome") {
      isVimApp = 1;
    } else {
      isVimApp = 0;
    }
  }
  return isVimApp;
}

static bool _isBlacklistedFile(dmtcp::string& path)
{
  if ((dmtcp::Util::strStartsWith(path, "/dev/") &&
       !dmtcp::Util::strStartsWith(path, "/dev/shm/")) ||
      dmtcp::Util::strStartsWith(path, "/proc/") ||
      dmtcp::Util::strStartsWith(path, dmtcp_get_tmpdir())) {
    return true;
  }
  return false;
}

static bool _isBlacklistedTcp(int sockfd,
                              const sockaddr* saddr, socklen_t len)
{
  JASSERT(saddr != NULL);
  if (len >= sizeof(saddr->sa_family) && saddr->sa_family == AF_INET) {
    struct sockaddr_in* addr =(sockaddr_in*)saddr;
    // Ports 389 and 636 are the well-known ports in /etc/services that
    // are reserved for LDAP.  Bash continues to maintain a connection to
    // LDAP, leading to problems at restart time.  So, we discover the LDAP
    // remote addresses, and turn them into dead sockets at restart time.
    //However, libc.so:getpwuid() can call libnss_ldap.so which calls
    // libldap-2.4.so to create the LDAP socket while evading our connect
    // wrapper.
    int blacklistedRemotePorts[] = {53,                 // DNS Server
      389, 636,           // LDAP
      -1};
    int i;
    for (i = 0; blacklistedRemotePorts[i] != -1; i++) {
      if (ntohs(addr->sin_port) == blacklistedRemotePorts[i]) {
        JTRACE("LDAP port found") (ntohs(addr->sin_port))
          (blacklistedRemotePorts[0]) (blacklistedRemotePorts[1]);
        return true;
      }
    }
  }
  // FIXME:  Consider adding configure or dmtcp_checkpoint option to disable
  //   all remote connections.  Handy as quick test for special cases.
  return false;
}

  dmtcp::Connection::Connection(int t)
  : _id(ConnectionIdentifier::Create())
  , _type((ConnectionType) t)
  , _fcntlFlags(-1)
  , _fcntlOwner(-1)
  , _fcntlSignal(-1)
  , _hasLock(false)
{}

void dmtcp::Connection::addFd(int fd)
{
  _fds.push_back(fd);
}

void dmtcp::Connection::removeFd(int fd)
{
  JASSERT(_fds.size() > 0);
  if (_fds.size() == 1) {
    JASSERT(_fds[0] == fd);
    _fds.clear();
  } else {
    for (size_t i = 0; i < _fds.size(); i++) {
      if (_fds[i] == fd) {
        _fds.erase(_fds.begin() + i);
        break;
      }
    }
  }
}

dmtcp::TcpConnection& dmtcp::Connection::asTcp()
{
  JASSERT(false) (_id) (_type) .Text("Invalid conversion.");
  return *((TcpConnection*) 0);
}

dmtcp::EpollConnection& dmtcp::Connection::asEpoll()
{
  JASSERT(false) (_id) (_type) .Text("Invalid conversion.");
  return *((EpollConnection*) 0);
}

#ifdef DMTCP_USE_INOTIFY
dmtcp::InotifyConnection& dmtcp::Connection::asInotify()
{
  JASSERT(false) (_id) (_type) .Text("Invalid conversion.");
  return *((InotifyConnection*) 0);
}
#endif

void dmtcp::Connection::saveOptions()
{
  errno = 0;
  _fcntlFlags = fcntl(_fds[0],F_GETFL);
  JASSERT(_fcntlFlags >= 0) (_fds[0]) (_fcntlFlags) (_type) (JASSERT_ERRNO);
  errno = 0;
  _fcntlOwner = fcntl(_fds[0],F_GETOWN);
  JASSERT(_fcntlOwner != -1) (_fcntlOwner) (JASSERT_ERRNO);
  errno = 0;
  _fcntlSignal = fcntl(_fds[0],F_GETSIG);
  JASSERT(_fcntlSignal >= 0) (_fcntlSignal) (JASSERT_ERRNO);
}

void dmtcp::Connection::restoreOptions()
{
  //restore F_GETFL flags
  JASSERT(_fcntlFlags >= 0) (_fcntlFlags);
  JASSERT(_fcntlOwner != -1) (_fcntlOwner);
  JASSERT(_fcntlSignal >= 0) (_fcntlSignal);
  errno = 0;
  JASSERT(fcntl(_fds[0], F_SETFL, _fcntlFlags) == 0)
    (_fds[0]) (_fcntlFlags) (JASSERT_ERRNO);

  errno = 0;
  JASSERT(fcntl(_fds[0], F_SETOWN, _fcntlOwner) == 0)
   (_fds[0]) (_fcntlOwner) (JASSERT_ERRNO);

  // This JASSERT will almost always trigger until we fix the above mentioned
  // bug.
  //JASSERT(fcntl(_fds[0], F_GETOWN) == _fcntlOwner)
  //(fcntl(_fds[0], F_GETOWN)) (_fcntlOwner) (VIRTUAL_TO_REAL_PID(_fcntlOwner));

  errno = 0;
  JASSERT(fcntl(_fds[0], F_SETSIG,_fcntlSignal) == 0)
    (_fds[0]) (_fcntlSignal) (JASSERT_ERRNO);
}

void dmtcp::Connection::doLocking()
{
  errno = 0;
  _hasLock = false;
  JASSERT(fcntl(_fds[0], F_SETOWN, getpid()) == 0)
   (_fds[0]) (JASSERT_ERRNO);
}

void dmtcp::Connection::checkLock()
{
  pid_t pid = fcntl(_fds[0], F_GETOWN);
  JASSERT(pid != -1);
  _hasLock = pid == getpid();
}

void dmtcp::Connection::serialize(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::Connection");
  o & _id & _type & _fcntlFlags & _fcntlOwner & _fcntlSignal;
  o.serializeVector(_fds);
  serializeSubClass(o);
}

/*****************************************************************************
 * Socket Connection
 *****************************************************************************/
  dmtcp::SocketConnection::SocketConnection(int domain, int type, int protocol)
  : _sockDomain(domain)
  , _sockType(type)
  , _sockProtocol(protocol)
  , _peerType(PEER_UNKNOWN)
  , _socketPairRestored(false)
{ }

void dmtcp::SocketConnection::addSetsockopt(int level, int option,
                                            const char* value, int len)
{
  _sockOptions[level][option] = jalib::JBuffer(value, len);
}

void dmtcp::SocketConnection::restoreSocketOptions(dmtcp::vector<int>& fds)
{
  typedef map<int, map< int, jalib::JBuffer> >::iterator levelIterator;
  typedef map<int, jalib::JBuffer>::iterator optionIterator;

  for (levelIterator lvl = _sockOptions.begin();
       lvl!=_sockOptions.end(); ++lvl) {
    for (optionIterator opt = lvl->second.begin();
         opt!=lvl->second.end(); ++opt) {
      JTRACE("Restoring socket option.")
        (fds[0]) (opt->first) (opt->second.size());
      int ret = _real_setsockopt(fds[0], lvl->first, opt->first,
                                 opt->second.buffer(),
                                 opt->second.size());
      JASSERT(ret == 0) (JASSERT_ERRNO) (fds[0])
        (lvl->first) (opt->first) (opt->second.size())
        .Text("Restoring setsockopt failed.");
    }
  }
}

void dmtcp::SocketConnection::serialize(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::SocketConnection");
  o & _sockDomain  & _sockType & _sockProtocol
    & _peerType & _socketPairRestored;

  JSERIALIZE_ASSERT_POINT("SocketOptions:");
  size_t numSockOpts = _sockOptions.size();
  o & numSockOpts;
  if (o.isWriter()) {
    //JTRACE("TCP Serialize ") (_type) (_id.conId());
    typedef map< int, map< int, jalib::JBuffer > >::iterator levelIterator;
    typedef map< int, jalib::JBuffer >::iterator optionIterator;

    size_t numLvl = _sockOptions.size();
    o & numLvl;

    for (levelIterator lvl = _sockOptions.begin();
         lvl!=_sockOptions.end(); ++lvl) {
      int lvlVal = lvl->first;
      size_t numOpts = lvl->second.size();

      JSERIALIZE_ASSERT_POINT("Lvl");

      o & lvlVal & numOpts;

      for (optionIterator opt = lvl->second.begin();
           opt!=lvl->second.end(); ++opt) {
        int optType = opt->first;
        jalib::JBuffer& buffer = opt->second;
        int bufLen = buffer.size();

        JSERIALIZE_ASSERT_POINT("Opt");

        o & optType & bufLen;
        o.readOrWrite(buffer.buffer(), bufLen);
      }
    }
  } else {
    size_t numLvl = 0;
    o & numLvl;

    while (numLvl-- > 0) {
      int lvlVal = -1;
      size_t numOpts = 0;

      JSERIALIZE_ASSERT_POINT("Lvl");

      o & lvlVal & numOpts;

      while (numOpts-- > 0) {
        int optType = -1;
        int bufLen = -1;

        JSERIALIZE_ASSERT_POINT("Opt");

        o & optType & bufLen;

        jalib::JBuffer buffer(bufLen);
        o.readOrWrite(buffer.buffer(), bufLen);

        _sockOptions[lvlVal][optType]=buffer;
      }
    }
  }

  JSERIALIZE_ASSERT_POINT("EndSockOpts");
}

/*****************************************************************************
 * TCP Connection
 *****************************************************************************/

/*onSocket*/
  dmtcp::TcpConnection::TcpConnection(int domain, int type, int protocol)
  : Connection(TCP_CREATED)
  , SocketConnection(domain, type, protocol)
  , _listenBacklog(-1)
  , _bindAddrlen(0)
  , _remotePeerId(ConnectionIdentifier::Null())
{
  if (domain != -1) {
    JTRACE("Creating TcpConnection.") (id()) (domain) (type) (protocol);
  }
  memset(&_bindAddr, 0, sizeof _bindAddr);
}

dmtcp::TcpConnection& dmtcp::TcpConnection::asTcp()
{
  return *this;
}

void dmtcp::TcpConnection::onBind(int sockfd, const struct sockaddr* addr,
                                  socklen_t len)
{
  if (really_verbose) {
    JTRACE("Binding.") (id()) (len);
  }

  JASSERT(tcpType() == TCP_CREATED) (tcpType()) (id())
    .Text("Binding a socket in use????");
  JASSERT(len <= sizeof _bindAddr) (len) (sizeof _bindAddr)
    .Text("That is one huge sockaddr buddy.");

  if (_sockDomain == AF_UNIX) {
    _bindAddrlen = len;
    memcpy(&_bindAddr, addr, len);
  } else {
    _bindAddrlen = sizeof(_bindAddr);
    // Do not rely on the address passed on to bind as it may contain port 0
    // which allows the OS to give any unused port. Thus we look ourselves up
    // using getsockname.
    JASSERT(getsockname(sockfd, (struct sockaddr *)&_bindAddr, &_bindAddrlen) == 0)
      (JASSERT_ERRNO);
  }
  _type = TCP_BIND;
}

void dmtcp::TcpConnection::onListen(int backlog)
{
  if (really_verbose) {
    JTRACE("Listening.") (id()) (backlog);
  }
  JASSERT(tcpType() == TCP_BIND) (tcpType()) (id())
    .Text("Listening on a non-bind()ed socket????");
  // A -1 backlog is not an error.
  //JASSERT(backlog > 0) (backlog)
  //.Text("That is an odd backlog????");

  _type = TCP_LISTEN;
  _listenBacklog = backlog;
}

void dmtcp::TcpConnection::onConnect(int sockfd,
                                     const struct sockaddr *addr,
                                     socklen_t len)
{
  if (really_verbose) {
    JTRACE("Connecting.") (id());
  }
  JASSERT(tcpType() == TCP_CREATED || tcpType() == TCP_BIND) (tcpType()) (id())
    .Text("Connecting with an in-use socket????");

  /* socketpair wrapper calls onConnect with sockfd == -1 and addr == NULL */
  if (addr != NULL && _isBlacklistedTcp(sockfd, addr, len)) {
    _type = TCP_EXTERNAL_CONNECT;
    _connectAddrlen = len;
    memcpy(&_connectAddr, addr, len);
  } else {
    _type = TCP_CONNECT;
  }
}

/*onAccept*/
dmtcp::TcpConnection::TcpConnection(const TcpConnection& parent,
                                    const ConnectionIdentifier& remote)
  : Connection(TCP_ACCEPT)
  , SocketConnection(parent._sockDomain, parent._sockType, parent._sockProtocol)
  , _listenBacklog(-1)
  , _bindAddrlen(0)
  , _remotePeerId(remote)
{
  if (really_verbose) {
    JTRACE("Accepting.") (id()) (parent.id()) (remote);
  }

  //     JASSERT(parent.tcpType() == TCP_LISTEN) (parent.tcpType()) (parent.id())
  //             .Text("Accepting from a non listening socket????");
  memset(&_bindAddr, 0, sizeof _bindAddr);
}

void dmtcp::TcpConnection::onError()
{
  JTRACE("Error.") (id());
  _type = TCP_ERROR;
}

void dmtcp::TcpConnection::preCheckpoint(KernelBufferDrainer& drain)
{
  JASSERT(_fds.size() > 0) (id());

  if ((_fcntlFlags & O_ASYNC) != 0) {
    if (really_verbose) {
      JTRACE("Removing O_ASYNC flag during checkpoint.") (_fds[0]) (id());
    }
    errno = 0;
    JASSERT(fcntl(_fds[0],F_SETFL,_fcntlFlags & ~O_ASYNC) == 0)
      (JASSERT_ERRNO) (_fds[0]) (id());
  }

  if (dmtcp_no_coordinator()) {
    markExternalConnect();
  }

  switch (tcpType()) {
    case TCP_CONNECT:
    case TCP_ACCEPT:
      JTRACE("Will drain socket") (_hasLock) (_fds[0]) (_id) (_remotePeerId);
      drain.beginDrainOf(_fds[0], _id);
      break;
    case TCP_LISTEN:
      drain.addListenSocket(_fds[0]);
      break;
    case TCP_BIND:
      JWARNING(tcpType() != TCP_BIND) (_fds[0])
        .Text("If there are pending connections on this socket,\n"
              " they won't be checkpointed because"
              " it is not yet in a listen state.");
      break;
    case TCP_EXTERNAL_CONNECT:
      JTRACE("Socket to External Process, won't be drained") (_fds[0]);
      break;
  }
}

void dmtcp::TcpConnection::doSendHandshakes(const ConnectionIdentifier& coordId)
{
  switch (tcpType()) {
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

void dmtcp::TcpConnection::doRecvHandshakes(const ConnectionIdentifier& coordId)
{
  switch (tcpType()) {
    case TCP_CONNECT:
    case TCP_ACCEPT:
      recvHandshake(_fds[0], coordId);
      JTRACE("Received handshake.") (id()) (getRemoteId()) (_fds[0]);
      break;
    case TCP_EXTERNAL_CONNECT:
      JTRACE("Socket to External Process, skipping handshake recv") (_fds[0]);
      break;
  }
}

void dmtcp::TcpConnection::refill(bool isRestart)
{
  if ((_fcntlFlags & O_ASYNC) != 0) {
    JTRACE("Re-adding O_ASYNC flag.") (_fds[0]) (id());
    SocketConnection::restoreSocketOptions(_fds);
  }
}

void dmtcp::TcpConnection::restoreOptions()
{
  if (_sockDomain != AF_INET6 && tcpType() != TCP_EXTERNAL_CONNECT) {
    restoreSocketOptions(_fds);
  }

  //call base version(F_GETFL etc)
  Connection::restoreOptions();
}

void dmtcp::TcpConnection::restoreSocketPair(dmtcp::TcpConnection *peer)
{
  int sv[2];
  JASSERT(_peerType == PEER_SOCKETPAIR && _socketpairPeerId == peer->id())
   (_peerType) (_socketpairPeerId) (peer->id());
  JASSERT(_fds.size() > 0);
  JASSERT(peer->numFds() > 0);

  if (_socketPairRestored) {
    _socketPairRestored = false;
    return;
  }
  JASSERT(_real_socketpair(_sockDomain,_sockType,_sockProtocol, sv) == 0)
   (JASSERT_ERRNO);

  Util::dupFds(sv[0], _fds);
  Util::dupFds(sv[1], peer->_fds);
  peer->_socketPairRestored = true;
  JTRACE("Restored Socketpair") (id()) (peer->id()) (_fds[0]) (peer->_fds[0]);
}

void dmtcp::TcpConnection::restore(ConnectionRewirer *rewirer)
{
  JASSERT(_fds.size() > 0);
  switch (tcpType()) {
    case TCP_PREEXISTING:
    case TCP_ERROR: //not a valid socket
    case TCP_INVALID:
    case TCP_EXTERNAL_CONNECT:
      JTRACE("Creating dead socket.") (_fds[0]) (_fds.size());
      Util::dupFds(_makeDeadSocket(), _fds);
      break;
    case TCP_CREATED:
    case TCP_BIND:
    case TCP_LISTEN:
      {
        JWARNING((_sockDomain == AF_INET || _sockDomain == AF_UNIX ||
                  _sockDomain == AF_INET6) && _sockType == SOCK_STREAM)
          (id()) (_sockDomain) (_sockType) (_sockProtocol)
          .Text("Socket type not yet [fully] supported.");

        if (really_verbose) {
          JTRACE("Restoring socket.") (id()) (_fds[0]);
        }

        jalib::JSocket sock(_real_socket(_sockDomain, _sockType,
                                         _sockProtocol));
        JASSERT(sock.isValid());
        sock.changeFd(_fds[0]);

        Util::dupFds(_fds[0], _fds);

        if (tcpType() == TCP_CREATED) break;

        if (_sockDomain == AF_UNIX) {
          const char* un_path =((sockaddr_un*) &_bindAddr)->sun_path;
          JTRACE("Unlinking stale unix domain socket.") (un_path);
          JWARNING(unlink(un_path) == 0) (un_path);
        }
        /*
         * During restart, some socket options must be restored(using
         * setsockopt) before the socket is used(bind etc.), otherwise we might
         * not be able to restore them at all. One such option is set in the
         * following way for IPV6 family:
         * setsockopt(sd, IPPROTO_IPV6, IPV6_V6ONLY,...)
         * This fix works for now. A better approach would be to restore the
         * socket options in the order in which they are set by the user
         * program.  This fix solves a bug that caused OpenMPI to fail to
         * restart under DMTCP.
         *                               --Kapil
         */

        if (_sockDomain == AF_INET6) {
          JTRACE("Restoring some socket options before binding.");
          typedef map< int, map< int,
                                 jalib::JBuffer > >::iterator levelIterator;
          typedef map< int, jalib::JBuffer >::iterator optionIterator;

          for (levelIterator lvl = _sockOptions.begin();
               lvl!=_sockOptions.end(); ++lvl) {
            if (lvl->first == IPPROTO_IPV6) {
              for (optionIterator opt = lvl->second.begin();
                   opt!=lvl->second.end(); ++opt) {
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
        JWARNING(sock.bind((sockaddr*) &_bindAddr,_bindAddrlen))
          (JASSERT_ERRNO) (id())
          .Text("Bind failed.");
        if (tcpType() == TCP_BIND) break;

        if (really_verbose) {
          JTRACE("Listening socket.") (id());
        }
        errno = 0;
        JWARNING(sock.listen(_listenBacklog))
          (JASSERT_ERRNO) (id()) (_listenBacklog)
          .Text("Bind failed.");
        if (tcpType() == TCP_LISTEN) break;

      }
      break;
    case TCP_ACCEPT:
      JASSERT(!_remotePeerId.isNull()) (id()) (_remotePeerId) (_fds[0])
        .Text("Can't restore a TCP_ACCEPT socket with null acceptRemoteId.\n"
              "  Perhaps handshake went wrong?");
      JTRACE("registerOutgoing") (id()) (_remotePeerId) (_fds[0]);
      rewirer->registerOutgoing(_remotePeerId, this);
      break;
    case TCP_CONNECT:
      JTRACE("registerIncoming") (id()) (_remotePeerId) (_fds[0]);
      rewirer->registerIncoming(id(), this);
      break;
      //    case TCP_EXTERNAL_CONNECT:
      //      int sockFd = _real_socket(_sockDomain, _sockType, _sockProtocol);
      //      JASSERT(sockFd >= 0);
      //      JASSERT(_real_dup2(sockFd, _fds[0]) == _fds[0]);
      //      JWARNING(0 == _real_connect(sockFd,(sockaddr*) &_connectAddr,
      //                                  _connectAddrlen))
      //        (_fds[0]) (JASSERT_ERRNO)
      //        .Text("Unable to connect to external process");
      //      break;
  }
}

void dmtcp::TcpConnection::sendHandshake(int remotefd,
                                         const ConnectionIdentifier& coordId)
{
  jalib::JSocket remote(remotefd);
  ConnMsg msg(ConnMsg::HANDSHAKE);
  msg.from = id();
  msg.coordId = coordId;
  remote << msg;
}

void dmtcp::TcpConnection::recvHandshake(int remotefd,
                                         const ConnectionIdentifier& coordId)
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
    //first time
    _remotePeerId = msg.from;
    JASSERT(!_remotePeerId.isNull())
      .Text("Read handshake with invalid 'from' field.");
  } else {
    //next time
    JASSERT(_remotePeerId == msg.from)
      (_remotePeerId) (msg.from)
      .Text("Read handshake with a different 'from' field"
            " than a previous handshake.");
  }
}

void dmtcp::TcpConnection::serializeSubClass(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::TcpConnection");
  o & _listenBacklog & _bindAddrlen & _bindAddr & _remotePeerId;
  SocketConnection::serialize(o);
}

/*****************************************************************************
 * RawSocket Connection
 *****************************************************************************/
/*onSocket*/
dmtcp::RawSocketConnection::RawSocketConnection(int domain, int type,
                                                int protocol)
  : Connection(RAW)
    , SocketConnection(domain, type, protocol)
{
  JASSERT(type == -1 ||(type & SOCK_RAW));
  JASSERT(domain == -1 || domain == AF_NETLINK) (domain)
    .Text("Only Netlink raw socket supported");
  JTRACE("Creating Raw socket.") (id()) (domain) (type) (protocol);
}

void dmtcp::RawSocketConnection::preCheckpoint(KernelBufferDrainer& drain)
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

void dmtcp::RawSocketConnection::restoreOptions()
{
  if (_sockDomain != AF_INET6) {
    restoreSocketOptions(_fds);
  }

  //call base version(F_GETFL etc)
  Connection::restoreOptions();
}

void dmtcp::RawSocketConnection::refill(bool isRestart)
{
  if ((_fcntlFlags & O_ASYNC) != 0) {
    JTRACE("Re-adding O_ASYNC flag.") (_fds[0]) (id());
    SocketConnection::restoreSocketOptions(_fds);
  }
}

void dmtcp::RawSocketConnection::restore(ConnectionRewirer *rewirer)
{
  JASSERT(_fds.size() > 0);
  if (really_verbose) {
    JTRACE("Restoring socket.") (id()) (_fds[0]);
  }

  int sockfd = _real_socket(_sockDomain,_sockType,_sockProtocol);
  JASSERT(sockfd != -1);
  Util::dupFds(sockfd, _fds);
}

void dmtcp::RawSocketConnection::serializeSubClass(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::RawSocketConnection");
  SocketConnection::serialize(o);
}

/*****************************************************************************
 * Pseudo-TTY Connection
 *****************************************************************************/

static bool ptmxTestPacketMode(int masterFd)
{
  char tmp_buf[100];
  int slave_fd, ioctlArg, rc;
  fd_set read_fds;
  struct timeval zeroTimeout = {0, 0}; /* Zero: will use to poll, not wait.*/

  _real_ptsname_r(masterFd, tmp_buf, 100);
  /* permissions not used, but _real_open requires third arg */
  slave_fd = _real_open(tmp_buf, O_RDWR, 0666);

  /* A. Drain master before testing.
     Ideally, DMTCP has already drained it and preserved any information
     about exceptional conditions in command byte, but maybe we accidentally
     caused a new command byte in packet mode. */
  /* Note:  if terminal was in packet mode, and the next read would be
     a non-data command byte, then there's no easy way for now to detect and
     restore this. ?? */
  /* Note:  if there was no data to flush, there might be no command byte,
     even in packet mode. */
  tcflush(slave_fd, TCIOFLUSH);
  /* If character already transmitted(usual case for pty), then this flush
     will tell master to flush it. */
  tcflush(masterFd, TCIFLUSH);

  /* B. Now verify that read_fds has no more characters to read. */
  ioctlArg = 1;
  ioctl(masterFd, TIOCINQ, &ioctlArg);
  /* Now check if there's a command byte still to read. */
  FD_ZERO(&read_fds);
  FD_SET(masterFd, &read_fds);
  select(masterFd + 1, &read_fds, NULL, NULL, &zeroTimeout);
  if (FD_ISSET(masterFd, &read_fds)) {
    // Clean up someone else's command byte from packet mode.
    // FIXME:  We should restore this on resume/restart.
    rc = read(masterFd, tmp_buf, 100);
    JASSERT(rc == 1) (rc) (masterFd);
  }

  /* C. Now we're ready to do the real test.  If in packet mode, we should
     see command byte of TIOCPKT_DATA(0) with data. */
  tmp_buf[0] = 'x'; /* Don't set '\n'.  Could be converted to "\r\n". */
  /* Give the masterFd something to read. */
  JWARNING((rc = write(slave_fd, tmp_buf, 1)) == 1) (rc) .Text("write failed");
  //tcdrain(slave_fd);
  _real_close(slave_fd);

  /* Read the 'x':  If we also see a command byte, it's packet mode */
  rc = read(masterFd, tmp_buf, 100);

  /* D. Check if command byte packet exists, and chars rec'd is longer by 1. */
  return(rc == 2 && tmp_buf[0] == TIOCPKT_DATA && tmp_buf[1] == 'x');
}

// Also record the count read on each iteration, in case it's packet mode
static bool readyToRead(int fd)
{
  fd_set read_fds;
  struct timeval zeroTimeout = {0, 0}; /* Zero: will use to poll, not wait.*/
  FD_ZERO(&read_fds);
  FD_SET(fd, &read_fds);
  select(fd + 1, &read_fds, NULL, NULL, &zeroTimeout);
  return FD_ISSET(fd, &read_fds);
}

// returns 0 if not ready to read; else returns -1, or size read incl. header
static ssize_t readOnePacket(int fd, const void *buf, size_t maxCount)
{
  typedef int hdr;
  ssize_t rc = 0;
  // Read single packet:  rc > 0 will be true for at most one iteration.
  while (readyToRead(fd) && rc <= 0) {
    rc = read(fd,(char *)buf+sizeof(hdr), maxCount-sizeof(hdr));
    *(hdr *)buf = rc; // Record the number read in header
    if (rc >=(ssize_t) (maxCount-sizeof(hdr))) {
      rc = -1; errno = E2BIG; // Invoke new errno for buf size not large enough
    }
    if (rc == -1 && errno != EAGAIN && errno != EINTR)
      break;  /* Give up; bad error */
  }
  return(rc <= 0 ? rc : rc+sizeof(hdr));
}

// rc < 0 => error; rc == sizeof(hdr) => no data to read;
// rc > 0 => saved w/ count hdr
static ssize_t ptmxReadAll(int fd, const void *origBuf, size_t maxCount)
{
  typedef int hdr;
  char *buf =(char *)origBuf;
  int rc;
  while ((rc = readOnePacket(fd, buf, maxCount)) > 0) {
    buf += rc;
  }
  *(hdr *)buf = 0; /* Header count of zero means we're done */
  buf += sizeof(hdr);
  JASSERT(rc < 0 || buf -(char *)origBuf > 0) (rc) (origBuf) ((void *)buf);
  return(rc < 0 ? rc : buf -(char *)origBuf);
}

// The hdr contains the size of the full buffer([hdr, data]).
// Return size of origBuf written:  includes packets of form:  [hdr, data]
//   with hdr holding size of data.  Last hdr has value zero.
// Also record the count written on each iteration, in case it's packet mode.
static ssize_t writeOnePacket(int fd, const void *origBuf, bool isPacketMode)
{
  typedef int hdr;
  int count = *(hdr *)origBuf;
  int cum_count = 0;
  int rc = 0; // Trigger JASSERT if not modified below.
  if (count == 0)
    return sizeof(hdr);  // count of zero means we're done, hdr consumed
  // FIXME:  It would be nice to restore packet mode(flow control, etc.)
  //         For now, we ignore it.
  if (count == 1 && isPacketMode)
    return sizeof(hdr) + 1;
  while (cum_count < count) {
    rc = write(fd,(char *)origBuf+sizeof(hdr)+cum_count, count-cum_count);
    if (rc == -1 && errno != EAGAIN && errno != EINTR)
      break;  /* Give up; bad error */
    if (rc >= 0)
      cum_count += rc;
  }
  JASSERT(rc != 0 && cum_count == count)
    (JASSERT_ERRNO) (rc) (count) (cum_count);
  return(rc < 0 ? rc : cum_count+sizeof(hdr));
}

static ssize_t ptmxWriteAll(int fd, const void *buf, bool isPacketMode)
{
  typedef int hdr;
  ssize_t cum_count = 0;
  ssize_t rc;
  while ((rc = writeOnePacket(fd,(char *)buf+cum_count, isPacketMode))
         >(ssize_t)sizeof(hdr)) {
    cum_count += rc;
  }
  JASSERT(rc < 0 || rc == sizeof(hdr)) (rc) (cum_count);
  cum_count += sizeof(hdr);  /* Account for last packet: 'done' hdr w/ 0 data */
  return(rc <= 0 ? rc : cum_count);
}

dmtcp::PtyConnection::PtyConnection(int fd, const char *path,
                                    int flags, mode_t mode, int type)
  : Connection (PTY)
  , _flags(flags)
  , _mode(mode)
{
  char buf[PTS_PATH_MAX];
  _type = type;
  switch (_type) {

    case PTY_DEV_TTY:
      _ptsName = path;
      break;

    case PTY_CTTY:
      _ptsName = path;
      SharedData::getVirtPtyName(path, buf, sizeof(buf));
      if (strlen(buf) == 0) {
        SharedData::createVirtualPtyName(path, buf, sizeof(buf));
      }
      _virtPtsName = buf;
      JTRACE("creating ptmx connection") (_ptsName) (_virtPtsName);

      break;

    case PTY_MASTER:
      _masterName = path;
      JASSERT(_real_ptsname_r(fd, buf, sizeof(buf)) == 0) (JASSERT_ERRNO);
      _ptsName = buf;

      // glibc allows only 20 char long buf
      // Check if there is enough room to insert the string "dmtcp_" before the
      //   terminal number, if not then we ASSERT here.
      JASSERT((strlen(buf) + strlen("v")) <= 20)
        .Text("string /dev/pts/<n> too long, can not be virtualized."
              "Once possible workarong here is to replace the string"
              "\"dmtcp_\" with something short like \"d_\" or even "
              "\"d\" and recompile DMTCP");

      // Generate new Unique buf
      SharedData::createVirtualPtyName(_ptsName.c_str(), buf, sizeof(buf));
      _virtPtsName = buf;
      JTRACE("creating ptmx connection") (_ptsName) (_virtPtsName);
      break;

    case PTY_SLAVE:
      _ptsName = path;
      SharedData::getVirtPtyName(path, buf, sizeof(buf));
      _virtPtsName = buf;
      JASSERT(strlen(buf) != 0) (path);
      JTRACE("creating pts connection") (_ptsName) (_virtPtsName);
      break;

    case PTY_BSD_MASTER:
      _masterName = path;
      break;

    case PTY_BSD_SLAVE:
      _ptsName = path;
      break;

    default:
      break;
  }
}

void dmtcp::PtyConnection::preCheckpoint(KernelBufferDrainer& drain)
{
  if (ptyType() == PTY_MASTER) {
    const int maxCount = 10000;
    char buf[maxCount];
    int numRead, numWritten;
    // _fds[0] is master fd
    numRead = ptmxReadAll(_fds[0], buf, maxCount);
    _ptmxIsPacketMode = ptmxTestPacketMode(_fds[0]);
    JTRACE("_fds[0] is master(/dev/ptmx)") (_fds[0]) (_ptmxIsPacketMode);
    numWritten = ptmxWriteAll(_fds[0], buf, _ptmxIsPacketMode);
    JASSERT(numRead == numWritten) (numRead) (numWritten);
  }
}

void dmtcp::PtyConnection::refill(bool isRestart)
{
  if (ptyType() == PTY_SLAVE || ptyType() == PTY_BSD_SLAVE) {
    JASSERT(_ptsName.compare("?") != 0);
    JTRACE("Restoring PTY slave") (_fds[0]) (_ptsName) (_virtPtsName);
    if (ptyType() == PTY_SLAVE) {
      char buf[32];
      SharedData::getRealPtyName(_virtPtsName.c_str(), buf, sizeof(buf));
      JASSERT(strlen(buf) > 0) (_virtPtsName) (_ptsName);
      _ptsName = buf;
    }

    int tempfd = _real_open(_ptsName.c_str(), O_RDWR);
    JASSERT(tempfd >= 0) (_virtPtsName) (_ptsName) (JASSERT_ERRNO)
      .Text("Error Opening PTS");

    JTRACE("Restoring PTS real") (_ptsName) (_virtPtsName) (_fds[0]);
    Util::dupFds(tempfd, _fds);
  }
  restoreOptions();
}

void dmtcp::PtyConnection::restore(ConnectionRewirer*)
{
  JASSERT(_fds.size() > 0);
  if (ptyType() == PTY_SLAVE || ptyType() == PTY_BSD_SLAVE) {
    return;
  }

  int tempfd;
  char buf[PTS_PATH_MAX];

  switch (ptyType()) {
    case PTY_INVALID:
      //tempfd = _real_open("/dev/null", O_RDWR);
      JTRACE("Restoring invalid PTY.") (id());
      return;

    case PTY_DEV_TTY:
      {
        dmtcp::string tty = "/dev/tty";
        tempfd = _real_open(tty.c_str(), _fcntlFlags);
        JASSERT(tempfd >= 0) (tempfd) (tty) (JASSERT_ERRNO)
          .Text("Error Opening the terminal device");

        JTRACE("Restoring /dev/tty for the process") (tty) (_fds[0]);
        _ptsName = _virtPtsName = tty;
        break;
      }

    case PTY_CTTY:
      {
        dmtcp::string controllingTty = jalib::Filesystem::GetControllingTerm();
        JASSERT(controllingTty.length() > 0) (STDIN_FILENO)
          . Text("Unable to restore terminal attached with the process");

        tempfd = _real_open(controllingTty.c_str(), _fcntlFlags);
        JASSERT(tempfd >= 0) (tempfd) (controllingTty) (JASSERT_ERRNO)
          .Text("Error Opening the terminal attached with the process");

        JTRACE("Restoring CTTY for the process") (controllingTty) (_fds[0]);

        _ptsName = controllingTty;
        SharedData::insertPtyNameMap(_virtPtsName.c_str(), _ptsName.c_str());
        break;
      }

    case PTY_MASTER:
      {
        char pts_name[80];

        tempfd = _real_open("/dev/ptmx", O_RDWR);
        JASSERT(tempfd >= 0) (tempfd) (JASSERT_ERRNO)
          .Text("Error Opening /dev/ptmx");

        JASSERT(grantpt(tempfd) >= 0) (tempfd) (JASSERT_ERRNO);
        JASSERT(unlockpt(tempfd) >= 0) (tempfd) (JASSERT_ERRNO);
        JASSERT(_real_ptsname_r(tempfd, pts_name, 80) == 0)
          (tempfd) (JASSERT_ERRNO);

        _ptsName = pts_name;
        SharedData::insertPtyNameMap(_virtPtsName.c_str(), _ptsName.c_str());

        if (ptyType() == PTY_MASTER) {
          int packetMode = _ptmxIsPacketMode;
          ioctl(_fds[0], TIOCPKT, &packetMode); /* Restore old packet mode */
        }

        JTRACE("Restoring /dev/ptmx") (_fds[0]) (_ptsName) (_virtPtsName);
        break;
      }
    case PTY_BSD_MASTER:
      {
        JTRACE("Restoring BSD Master Pty") (_masterName) (_fds[0]);
        //dmtcp::string slaveDeviceName =
          //_masterName.replace(0, strlen("/dev/pty"), "/dev/tty");

        tempfd = _real_open(_masterName.c_str(), O_RDWR);

        // FIXME: If unable to open the original BSD Master Pty, we should try to
        // open another one until we succeed and then open slave device
        // accordingly.
        // This can be done by creating a function openBSDMaster, which will try
        // to open the original master device, but if unable to do so, it would
        // keep on trying all the possible BSD Master devices until one is
        // opened. It should then create a mapping between original Master/Slave
        // device name and current Master/Slave device name.
        JASSERT(tempfd >= 0) (tempfd) (JASSERT_ERRNO)
          .Text("Error Opening BSD Master Pty.(Already in use?)");
        break;
      }
    default:
      {
        // should never reach here
        JASSERT(false) .Text("Should never reach here.");
      }
  }
  Util::dupFds(tempfd, _fds);
}

void dmtcp::PtyConnection::serializeSubClass(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::PtyConnection");
  o & _ptsName & _virtPtsName & _masterName & _type & _ptmxIsPacketMode;
  JTRACE("Serializing PtyConn.") (_ptsName) (_virtPtsName);
}

/*****************************************************************************
 * File Connection
 *****************************************************************************/

// Upper limit on filesize for files that are automatically chosen for ckpt.
// Default 100MB
#define MAX_FILESIZE_TO_AUTOCKPT (100 * 1024 * 1024)

void dmtcp::FileConnection::doLocking()
{
  if (dmtcp::Util::strStartsWith(_path, "/proc/")) {
    int index = 6;
    char *rest;
    pid_t proc_pid = strtol(&_path[index], &rest, 0);
    if (proc_pid > 0 && *rest == '/') {
      _type = FILE_PROCFS;
      if (proc_pid != getpid()) {
        return;
      }
    }
  }
  Connection::doLocking();
  _checkpointed = false;
}

void dmtcp::FileConnection::handleUnlinkedFile()
{
  if (!jalib::Filesystem::FileExists(_path)) {
    /* File not present in Filesystem.
     * /proc/self/fd lists filename of unlink()ed files as:
     *   "<original_file_name>(deleted)"
     */

    if (Util::strEndsWith(_path, DELETED_FILE_SUFFIX)) {
      _path.erase(_path.length() - strlen(DELETED_FILE_SUFFIX));
      _type = FILE_DELETED;
    } else {
      JASSERT(_type == FILE_DELETED) (_path)
        .Text("File not found on disk and yet the filename doesn't "
              "contain the suffix '(deleted)'");
    }
  } else if (Util::strStartsWith(jalib::Filesystem::BaseName(_path), ".nfs")) {
    JWARNING(access(_path.c_str(), W_OK) == 0) (JASSERT_ERRNO);
    JTRACE(".nfsXXXX: files that are unlink()'d, "
           "but still in use by some process(es)")
      (_path);
    _type = FILE_DELETED;
  }
}

void dmtcp::FileConnection::calculateRelativePath()
{
  dmtcp::string cwd = jalib::Filesystem::GetCWD();
  if (_path.compare(0, cwd.length(), cwd) == 0) {
    /* CWD = "/A/B", FileName = "/A/B/C/D" ==> relPath = "C/D" */
    _rel_path = _path.substr(cwd.length() + 1);
  } else {
    _rel_path = "*";
  }
}
#if 0
void dmtcp::FileConnection::preCheckpointResMgrFile()
{
  JTRACE("Pre-checkpoint Torque files") (_fds.size());
  for (unsigned int i=0; i< _fds.size(); i++)
    JTRACE("_fds[i]=") (i) (_fds[i]);

  if (isTorqueIOFile(_path)) {
    _rmtype = TORQUE_IO;
    // Save the content of stdio or node file
    // to restore it later in new IO file or in temporal Torque nodefile
    saveFile(_fds[0]);
  } else if (isTorqueNodeFile(_path) || _rmtype == TORQUE_NODE) {
    _rmtype = TORQUE_NODE;
    // Save the content of stdio or node file
    // to restore it later in new IO file or in temporal Torque nodefile
    saveFile(_fds[0]);
  }
}
#endif

void dmtcp::FileConnection::preCheckpoint(KernelBufferDrainer& drain)
{
  JASSERT(_fds.size() > 0);

  handleUnlinkedFile();

  calculateRelativePath();

  _ckptFilesDir = UniquePid::getCkptFilesSubDir();

  // Read the current file descriptor offset
  _offset = lseek(_fds[0], 0, SEEK_CUR);
  fstat(_fds[0], &_stat);

  // If this file is related to supported Resource Management system
  // handle it specially
  if (_type == FILE_BATCH_QUEUE &&
      dmtcp_bq_should_ckpt_file &&
      dmtcp_bq_should_ckpt_file(_path.c_str(), &_rmtype)) {
    saveFile(_fds[0]);
    return;
  }

  if (_isBlacklistedFile(_path)) {
    return;
  }
  if (getenv(ENV_VAR_CKPT_OPEN_FILES) != NULL &&
      _stat.st_uid == getuid()) {
    saveFile(_fds[0]);
  } else if (_type == FILE_DELETED) {
    saveFile(_fds[0]);
//   FIXME: Disable the following heuristic until we can comeup with a better
//   one
// } else if ((_fcntlFlags &(O_WRONLY|O_RDWR)) != 0 &&
//             _offset < _stat.st_size &&
//             _stat.st_size < MAX_FILESIZE_TO_AUTOCKPT &&
//             _stat.st_uid == getuid()) {
//    saveFile(_fds[0]);
  } else if (_isVimApp() &&
             (Util::strEndsWith(_path, ".swp") == 0 ||
              Util::strEndsWith(_path, ".swo") == 0)) {
    saveFile(_fds[0]);
  } else if (Util::strStartsWith(jalib::Filesystem::GetProgramName(),
                                 "emacs")) {
    saveFile(_fds[0]);
  } else {
  }
}

void dmtcp::FileConnection::refill(bool isRestart)
{
  struct stat buf;
  if (!_checkpointed) {
    JASSERT(jalib::Filesystem::FileExists(_path)) (_path)
      .Text("File not found.");

    if (stat(_path.c_str() ,&buf) == 0 && S_ISREG(buf.st_mode)) {
      if (buf.st_size > _stat.st_size &&
          (_fcntlFlags &(O_WRONLY|O_RDWR)) != 0) {
        errno = 0;
        JASSERT(truncate(_path.c_str(), _stat.st_size) ==  0)
          (_path.c_str()) (_stat.st_size) (JASSERT_ERRNO);
      } else if (buf.st_size < _stat.st_size) {
        JWARNING(false) .Text("Size of file smaller than what we expected");
      }
    }
    int tempfd = openFile();
    Util::dupFds(tempfd, _fds);
  }

  errno = 0;
  if (jalib::Filesystem::FileExists(_path) &&
      stat(_path.c_str() ,&buf) == 0 && S_ISREG(buf.st_mode)) {
    if (_offset <= buf.st_size && _offset <= _stat.st_size) {
      JASSERT(lseek(_fds[0], _offset, SEEK_SET) == _offset)
        (_path) (_offset) (JASSERT_ERRNO);
      //JTRACE("lseek(_fds[0], _offset, SEEK_SET)") (_fds[0]) (_offset);
    } else if (_offset > buf.st_size || _offset > _stat.st_size) {
      JWARNING(false) (_path) (_offset) (_stat.st_size) (buf.st_size)
        .Text("No lseek done:  offset is larger than min of old and new size.");
    }
  }
  refreshPath();
  restoreOptions();
}

void dmtcp::FileConnection::resume(bool isRestart)
{
  if (_checkpointed && isRestart && _type == FILE_DELETED) {
    /* Here we want to unlink the file. We want to do it only at the time of
     * restart, but there is no way of finding out if we are restarting or not.
     * That is why we look for the file on disk and if it is present(it was
     * deleted at ckpt time), then we assume that we are restarting and hence
     * we unlink the file.
     */
    if (jalib::Filesystem::FileExists(_path)) {
      JWARNING(unlink(_path.c_str()) != -1) (_path)
        .Text("The file was unlinked at the time of checkpoint. "
              "Unlinking it after restart failed");
    }
  }
}

void dmtcp::FileConnection::refreshPath()
{
  dmtcp::string cwd = jalib::Filesystem::GetCWD();

  if (_type == FILE_BATCH_QUEUE) {
    // get new file name
    dmtcp::string procpath = "/proc/self/fd/" + jalib::XToString(_fds[0]);
    dmtcp::string newpath = jalib::Filesystem::ResolveSymlink(procpath);
    JTRACE("This is Resource Manager file!") (newpath) (_path) (this);
    if (newpath != _path) {
      JTRACE("File Manager connection _path is changed => _path = newpath!")
        (_path) (newpath);
      _path = newpath;
    }
  } else if (_rel_path != "*" && !jalib::Filesystem::FileExists(_path)) {
    // If file at absolute path doesn't exist and file path is relative to
    // executable current dir
    string oldPath = _path;
    dmtcp::string fullPath = cwd + "/" + _rel_path;
    if (jalib::Filesystem::FileExists(fullPath)) {
      _path = fullPath;
      JTRACE("Change _path based on relative path")
        (oldPath) (_path) (_rel_path);
    }
  } else if (_type == FILE_PROCFS) {
    int index = 6;
    char *rest;
    char buf[64];
    pid_t proc_pid = strtol(&_path[index], &rest, 0);
    if (proc_pid > 0 && *rest == '/') {
      sprintf(buf, "/proc/%d/%s", getpid(), rest);
      _path = buf;
    }
  }
}

void dmtcp::FileConnection::restore(ConnectionRewirer*)
{
  int tempfd;

  JASSERT(_fds.size() > 0);
  if (!_checkpointed) return;

  JTRACE("Restoring File Connection") (id()) (_path);
  dmtcp::string savedFilePath = getSavedFilePath(_path);
  JASSERT(jalib::Filesystem::FileExists(savedFilePath))
    (savedFilePath) (_path) .Text("Unable to Find checkpointed copy of File");

  if (_type == FILE_BATCH_QUEUE) {
    JASSERT(dmtcp_bq_restore_file);
    tempfd = dmtcp_bq_restore_file(_path.c_str(), savedFilePath.c_str(),
                               _fcntlFlags, _rmtype);
    JTRACE("Restore Resource Manager File") (_path);
  } else {
    refreshPath();
    JASSERT(jalib::Filesystem::FileExists(_path) == false) (_path)
      .Text("\n**** File already exists! Checkpointed copy can't be "
            "restored.\n"
            "****Delete the existing file and try again!");

    JNOTE("File not present, copying from saved checkpointed file") (_path);
    CreateDirectoryStructure(_path);
    JTRACE("Copying saved checkpointed file to original location")
      (savedFilePath) (_path);
    CopyFile(savedFilePath, _path);
    tempfd = openFile();
  }
  Util::dupFds(tempfd, _fds);
}

static void CreateDirectoryStructure(const dmtcp::string& path)
{
  size_t index = path.rfind('/');

  if (index == dmtcp::string::npos)
    return;

  dmtcp::string dir = path.substr(0, index);

  index = path.find('/');
  while (index != dmtcp::string::npos) {
    if (index > 1) {
      dmtcp::string dirName = path.substr(0, index);

      int res = mkdir(dirName.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
      JASSERT(res != -1 || errno==EEXIST) (dirName) (path)
        .Text("Unable to create directory in File Path");
    }
    index = path.find('/', index+1);
  }
}

static void CopyFile(const dmtcp::string& src, const dmtcp::string& dest)
{
  dmtcp::string command = "cp -f " + src + " " + dest;
  JASSERT(_real_system(command.c_str()) != -1);
}

int dmtcp::FileConnection::openFile()
{
  JASSERT(jalib::Filesystem::FileExists(_path)) (_path)
    .Text("File not present");

  int fd = _real_open(_path.c_str(), _fcntlFlags);
  JASSERT(fd != -1) (_path) (JASSERT_ERRNO) .Text("open() failed");

  JTRACE("open(_path.c_str(), _fcntlFlags)") (fd) (_path.c_str()) (_fcntlFlags);
  return fd;
}

void dmtcp::FileConnection::saveFile(int fd)
{
  _checkpointed = true;

  dmtcp::string savedFilePath = getSavedFilePath(_path);
  CreateDirectoryStructure(savedFilePath);
  JTRACE("Saving checkpointed copy of the file") (_path) (savedFilePath);

  if (_type == FILE_REGULAR ||
      jalib::Filesystem::FileExists(_path)) {
    CopyFile(_path, savedFilePath);
    return;
  } else if (_type == FileConnection::FILE_DELETED) {
    long page_size = sysconf(_SC_PAGESIZE);
    const size_t bufSize = 2 * page_size;
    char *buf =(char*)JALLOC_HELPER_MALLOC(bufSize);

    int destFd = _real_open(savedFilePath.c_str(), O_CREAT | O_WRONLY | O_TRUNC,
                      S_IRWXU | S_IRWXG | S_IROTH |
                      S_IXOTH);
    JASSERT(destFd != -1) (_path) (savedFilePath) .Text("Read Failed");

    lseek(fd, 0, SEEK_SET);

    int readBytes, writtenBytes;
    while (1) {
      readBytes = Util::readAll(fd, buf, bufSize);
      JASSERT(readBytes != -1)
        (_path) (JASSERT_ERRNO) .Text("Read Failed");
      if (readBytes == 0) break;
      writtenBytes = Util::writeAll(destFd, buf, readBytes);
      JASSERT(writtenBytes != -1)
        (savedFilePath) (JASSERT_ERRNO) .Text("Write failed.");
    }

    close(destFd);
    JALLOC_HELPER_FREE(buf);
  }

  JASSERT(lseek(fd, _offset, SEEK_SET) != -1) (_path);
}

dmtcp::string dmtcp::FileConnection::getSavedFilePath(const dmtcp::string& path)
{
  dmtcp::ostringstream os;
  os << _ckptFilesDir
    << "/" << jalib::Filesystem::BaseName(_path) << "_" << _id.conId();

  return os.str();
}

void dmtcp::FileConnection::serializeSubClass(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::FileConnection");
  o & _path & _rel_path & _ckptFilesDir;
  o & _offset & _stat & _checkpointed & _rmtype;
  JTRACE("Serializing FileConn.") (_path) (_rel_path) (_ckptFilesDir)
   (_checkpointed) (_fcntlFlags);
}

/*****************************************************************************
 * FIFO Connection
 *****************************************************************************/

void dmtcp::FifoConnection::preCheckpoint(KernelBufferDrainer& drain)
{
  JASSERT(_fds.size() > 0);

  stat(_path.c_str(),&_stat);
  JTRACE("Checkpoint fifo.") (_fds[0]);

  int new_flags =(_fcntlFlags &(~(O_RDONLY|O_WRONLY))) | O_RDWR | O_NONBLOCK;
  ckptfd = _real_open(_path.c_str(),new_flags);
  JASSERT(ckptfd >= 0) (ckptfd) (JASSERT_ERRNO);

  _in_data.clear();
  size_t bufsize = 256;
  char buf[bufsize];
  int size;

  while (1) { // flush fifo
    size = read(ckptfd,buf,bufsize);
    if (size < 0) {
      break; // nothing to flush
    }
    for (int i=0;i<size;i++) {
      _in_data.push_back(buf[i]);
    }
  }
  close(ckptfd);
  JTRACE("Checkpointing fifo:  end.") (_fds[0]) (_in_data.size());
}

void dmtcp::FifoConnection::refill(bool isRestart)
{
  int new_flags =(_fcntlFlags &(~(O_RDONLY|O_WRONLY))) | O_RDWR | O_NONBLOCK;
  ckptfd = _real_open(_path.c_str(),new_flags);
  JASSERT(ckptfd >= 0) (ckptfd) (JASSERT_ERRNO);

  size_t bufsize = 256;
  char buf[bufsize];
  size_t j;
  ssize_t ret;
  for (size_t i=0;i<(_in_data.size()/bufsize);i++) { // refill fifo
    for (j=0; j<bufsize; j++) {
      buf[j] = _in_data[j+i*bufsize];
    }
    ret = Util::writeAll(ckptfd,buf,j);
    JASSERT(ret ==(ssize_t)j) (JASSERT_ERRNO) (ret) (j) (_fds[0]) (i);
  }
  int start =(_in_data.size()/bufsize)*bufsize;
  for (j=0; j<_in_data.size()%bufsize; j++) {
    buf[j] = _in_data[start+j];
  }
  errno=0;
  buf[j] ='\0';
  JTRACE("Buf internals.") ((const char*)buf);
  ret = Util::writeAll(ckptfd,buf,j);
  JASSERT(ret ==(ssize_t)j) (JASSERT_ERRNO) (ret) (j) (_fds[0]);

  close(ckptfd);
  // unlock fifo
  flock(_fds[0],LOCK_UN);
  JTRACE("End checkpointing fifo.") (_fds[0]);
  restoreOptions();
}

void dmtcp::FifoConnection::refreshPath()
{
  dmtcp::string cwd = jalib::Filesystem::GetCWD();
  if (_rel_path != "*") { // file path is relative to executable current dir
    string oldPath = _path;
    ostringstream fullPath;
    fullPath << cwd << "/" << _rel_path;
    if (jalib::Filesystem::FileExists(fullPath.str())) {
      _path = fullPath.str();
      JTRACE("Change _path based on relative path") (oldPath) (_path);
    }
  }
}

void dmtcp::FifoConnection::restore(ConnectionRewirer*)
{
  JASSERT(_fds.size() > 0);
  JTRACE("Restoring Fifo Connection") (id()) (_path);
  refreshPath();
  int tempfd = openFile();
  Util::dupFds(tempfd, _fds);
  refreshPath();
}

int dmtcp::FifoConnection::openFile()
{
  int fd;

  if (!jalib::Filesystem::FileExists(_path)) {
    JTRACE("Fifo file not present, creating new one") (_path);
    mkfifo(_path.c_str(),_stat.st_mode);
  }

  fd = _real_open(_path.c_str(), O_RDWR | O_NONBLOCK);
  JTRACE("Is opened") (_path.c_str()) (fd);

  JASSERT(fd != -1) (_path) (JASSERT_ERRNO);
  return fd;
}

void dmtcp::FifoConnection::serializeSubClass(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::FifoConnection");
  o & _path & _rel_path & _savedRelativePath & _stat & _in_data;
  JTRACE("Serializing FifoConn.") (_path) (_rel_path) (_savedRelativePath);
}

/*****************************************************************************
 * Stdio Connection
 *****************************************************************************/

void dmtcp::StdioConnection::preCheckpoint(KernelBufferDrainer& drain)
{
  //JTRACE("Checkpointing stdio") (_fds[0]) (id());
}

void dmtcp::StdioConnection::refill(bool isRestart)
{
  restoreOptions();
}

void dmtcp::StdioConnection::restore(ConnectionRewirer*)
{
  for (size_t i=0; i<_fds.size(); ++i) {
    int fd = _fds[i];
    if (fd <= 2) {
      JTRACE("Skipping restore of STDIO, just inherit from parent") (fd);
      continue;
    }
    int oldFd = -1;
    switch (_type) {
      case STDIO_IN:
        JTRACE("Restoring STDIN") (fd);
        oldFd=0;
        break;
      case STDIO_OUT:
        JTRACE("Restoring STDOUT") (fd);
        oldFd=1;
        break;
      case STDIO_ERR:
        JTRACE("Restoring STDERR") (fd);
        oldFd=2;
        break;
      default:
        JASSERT(false);
    }
    errno = 0;
    JWARNING(_real_dup2(oldFd, fd) == fd) (oldFd) (fd) (JASSERT_ERRNO);
  }
}

void dmtcp::StdioConnection::serializeSubClass(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::StdioConnection");
  //JTRACE("Serializing STDIO") (id());
}

/*****************************************************************************
 * Epoll Connection
 *****************************************************************************/

void dmtcp::EpollConnection::preCheckpoint(KernelBufferDrainer& drain)
{
  JASSERT(_fds.size() > 0);
}

void dmtcp::EpollConnection::refill(bool isRestart)
{
  JASSERT(_fds.size() > 0);
  if (isRestart) {
    typedef dmtcp::map< int, struct epoll_event >::iterator fdEventIterator;
    fdEventIterator fevt = _fdToEvent.begin();
    for (; fevt != _fdToEvent.end(); fevt++) {
      JTRACE("restore sfd options") (fevt->first);
      int ret = _real_epoll_ctl(_fds[0], EPOLL_CTL_ADD, fevt->first,
                                &(fevt->second));
      JWARNING(ret == 0) (_fds[0]) (ret) (strerror(errno))
        .Text("Error in restoring options");
    }
  }
}

void dmtcp::EpollConnection::restore(ConnectionRewirer*)
{
  JASSERT(_fds.size()>0);
  JTRACE("Recreating epoll connection") (_fds[0]) (id());
  int tempFd =  _real_epoll_create(_size) ;
  JASSERT(tempFd >= 0);
  Util::dupFds(tempFd, _fds);
}

void dmtcp::EpollConnection::serializeSubClass(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::EpollConnection");
  o & _type & _stat;
  o.serializeMap(_fdToEvent);
}

dmtcp::EpollConnection& dmtcp::EpollConnection::asEpoll()
{
  return *this;
}

void dmtcp::EpollConnection::onCTL(int op, int fd, struct epoll_event *event)
{
  if (really_verbose) {
    JTRACE("EPOLL CTL-ing.") (id()) (fd) (op);
  }

  JASSERT(((op==EPOLL_CTL_MOD || op==EPOLL_CTL_ADD) && event != NULL) ||
          op==EPOLL_CTL_DEL) (epollType())
   (id())
    .Text("Passing a NULL event! HUH!");

  struct epoll_event myEvent;
  _type = EPOLL_CTL;
  if (op == EPOLL_CTL_DEL) {
    _fdToEvent.erase(fd);
    return;
  }
  memcpy(&myEvent, event, sizeof myEvent);
  _fdToEvent[fd] = myEvent;
}

/*****************************************************************************
 * Eventfd Connection
 *****************************************************************************/
void dmtcp::EventFdConnection::preCheckpoint(KernelBufferDrainer& drain)
{
  JASSERT(_fds.size() > 0);
  JTRACE("Checkpoint eventfd.") (_fds[0]);

  int new_flags =(_fcntlFlags &(~(O_RDONLY|O_WRONLY))) | O_RDWR | O_NONBLOCK;
  int evtfd = _fds[0];
  JASSERT(evtfd >= 0) (evtfd) (JASSERT_ERRNO);
  // set the new flags
  JASSERT(fcntl(evtfd, F_SETFL, new_flags) == 0)
   (evtfd) (new_flags) (JASSERT_ERRNO);
  ssize_t size;
  uint64_t u;
  unsigned int counter = 1;

  // Read whatever is there on top of evtfd
  size = read(evtfd, &u, sizeof(uint64_t));
  if (-1 != size) {
    JTRACE("Read value u: ") (evtfd) (u);
    // EFD_SEMAPHORE flag not specified,
    // the counter value would have been reset to 0 upon read
    // Save the value, so that it can be restored in post-checkpoint
    if (!(_flags & EFD_SEMAPHORE)) {
      _initval = u;
    } else {
      // EFD_SEMAPHORE specified, so can't read the current counter value
      // Keep reading till "semaphore" becomes 0.
      while (-1 != read(evtfd, &u, sizeof(uint64_t)))
        counter++;
      _initval = counter;
    }
  } else {
    JTRACE("Nothing to be read from eventfd.")
      (evtfd) (errno) (strerror(errno));
    _initval = 0;
  }
  JTRACE("Checkpointing eventfd:  end.") (_fds[0]) (_initval);
}

void dmtcp::EventFdConnection::refill(bool isRestart)
{
  JTRACE("Begin refill eventfd.") (_fds[0]);
  JASSERT(_fds.size() > 0);
  evtfd = _fds[0];
  if (!isRestart) {
    uint64_t u =(unsigned long long) _initval;
    JTRACE("Writing") (u);
    JWARNING(write(evtfd, &u, sizeof(uint64_t)) == sizeof(uint64_t))
      (evtfd) (errno) (strerror(errno))
      .Text("Write to eventfd failed during refill");
  }
  restoreOptions();
  JTRACE("End refill eventfd.") (_fds[0]);
}

void dmtcp::EventFdConnection::restore(ConnectionRewirer*)
{
  JASSERT(_fds.size() > 0);

  JTRACE("Restoring EventFd Connection") (id());
  errno = 0;
  int tempfd = _real_eventfd(_initval, _flags);
  JASSERT(tempfd > 0) (tempfd) (JASSERT_ERRNO);
  Util::dupFds(tempfd, _fds);
}

void dmtcp::EventFdConnection::serializeSubClass(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::EventFdConnection");
  o & _initval & _flags;
  JTRACE("Serializing EvenFdConn.") ;
}

/*****************************************************************************
 * Signalfd Connection
 *****************************************************************************/
void dmtcp::SignalFdConnection::preCheckpoint(KernelBufferDrainer& drain)
{
  JASSERT(_fds.size() > 0);

  JTRACE("Checkpoint signalfd.") (_fds[0]);

  int new_flags =(_fcntlFlags &(~(O_RDONLY|O_WRONLY))) | O_RDWR | O_NONBLOCK;
  signlfd = _fds[0];
  JASSERT(signlfd >= 0) (signlfd) (JASSERT_ERRNO);
  // set the new flags
  JASSERT(fcntl(signlfd, F_SETFL, new_flags) == 0)
   (signlfd) (new_flags) (JASSERT_ERRNO);
  ssize_t size;
  struct signalfd_siginfo _fdsi;

  // Read whatever is there on top of signalfd
  size = read(signlfd, &_fdsi, sizeof(struct signalfd_siginfo));
  if (-1 != size) {
    // Save the value, so that it can be restored in post-checkpoint
    memcpy(&_fdsi, &_fdsi, sizeof(struct signalfd_siginfo));

  } else {
    JTRACE("Nothing to be read from signalfd.")
      (signlfd) (errno) (strerror(errno));
  }
  JTRACE("Checkpointing signlfd:  end.") (_fds[0]) ;
}

void dmtcp::SignalFdConnection::refill(bool isRestart)
{
  JTRACE("Begin refill signalfd.") (_fds[0]);
  JASSERT(_fds.size() > 0);
  //raise the signals
  JTRACE("Raising the signal...") (_fdsi.ssi_signo);
  raise(_fdsi.ssi_signo);
  restoreOptions();
  JTRACE("End refill signalfd.") (_fds[0]);
}

void dmtcp::SignalFdConnection::restore(ConnectionRewirer*)
{
  JASSERT(_fds.size() > 0);

  JTRACE("Restoring SignalFd Connection") (id());
  errno = 0;
  int tempfd = _real_signalfd(-1, &_mask, _flags);
  JASSERT(tempfd > 0) (tempfd) (JASSERT_ERRNO);
  Util::dupFds(tempfd, _fds);
}

void dmtcp::SignalFdConnection::serializeSubClass(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::SignalFdConnection");
  o &  _flags & _mask & _fdsi;
  JTRACE("Serializing SignalFdConn.") ;
}

#ifdef DMTCP_USE_INOTIFY
/*****************************************************************************
 * Inotify Connection
 *****************************************************************************/
void dmtcp::InotifyConnection::preCheckpoint(KernelBufferDrainer& drain)
{
  JASSERT(_fds.size() > 0);
}

void dmtcp::InotifyConnection::refill(bool isRestart)
{
  JASSERT(_fds.size() > 0);
  if (isRestart) {
    int num_of_descriptors;
    Util::Descriptor descriptor;
    descriptor_types_u  watch_descriptor;

    //get the number of watch descriptors stored in dmtcp
    num_of_descriptors = descriptor.count_descriptors();

    JTRACE("inotify restoreOptions") (_fds[0]) (id()) (num_of_descriptors);

    for (int i = 0; i < num_of_descriptors; i++) {
      if (true == descriptor.get_descriptor(i, INOTIFY_ADD_WATCH_DESCRIPTOR,
                                            &watch_descriptor)) {
        int old_wd = watch_descriptor.add_watch.watch_descriptor;

        int new_wd =
          _real_inotify_add_watch(watch_descriptor.add_watch.file_descriptor,
                                  watch_descriptor.add_watch.pathname,
                                  watch_descriptor.add_watch.mask);

        JWARNING(_real_dup2(new_wd, old_wd) == old_wd)
          (new_wd) (old_wd) (JASSERT_ERRNO);
        JTRACE("restore watch descriptors")
          (old_wd) (new_wd) (watch_descriptor.add_watch.file_descriptor)
          (watch_descriptor.add_watch.pathname)
          (watch_descriptor.add_watch.mask);
      }
    }
  }
}

void dmtcp::InotifyConnection::restore(ConnectionRewirer*)
{
  //create a new inotify instance and clone it as the old one
  int tempfd =  _real_inotify_init1(_flags);
  JASSERT(tempfd >= 0);
  Util::dupFds(tempfd, _fds);
}

void dmtcp::InotifyConnection::serializeSubClass(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::InotifyConnection");
  o & _type & _stat;
  //o.serializeMap(_inotify_fd_to_wd);
  //o.serializeMap(_wd_to_pathname);
  //o.serializeMap(_pathname_to_mask);
}

dmtcp::InotifyConnection& dmtcp::InotifyConnection::asInotify()
{
  JTRACE("Return the connection as Inotify connection");
  return *this;
}

void dmtcp::InotifyConnection::add_watch_descriptors(int wd, int fd,
                                                     const char *pathname,
                                                     uint32_t mask)
{
   int string_len;

   JTRACE("save inotify watch descriptor within dmtcp")
     (wd) (fd) (pathname) (mask);
   JASSERT(pathname != NULL) .Text("pathname is NULL");
   if (NULL != pathname) {
      Util::Descriptor descriptor;
      descriptor_types_u  watch_descriptor;

      //set watch_descriptor to zeros
      memset(&watch_descriptor, 0, sizeof(watch_descriptor));

      // get the string length
      string_len = strlen(pathname);

      // fill up the structure
      watch_descriptor.add_watch.file_descriptor = fd;
      watch_descriptor.add_watch.mask = mask;
      watch_descriptor.add_watch.watch_descriptor = wd;
      watch_descriptor.add_watch.type = INOTIFY_ADD_WATCH_DESCRIPTOR;
      strncpy(watch_descriptor.add_watch.pathname, pathname, string_len);

      // save the watch descriptor structure
      descriptor.add_descriptor(&watch_descriptor);
   }
}

void  dmtcp::InotifyConnection::remove_watch_descriptors(int wd)
{
   Util::Descriptor descriptor;
   descriptor.remove_descriptor(INOTIFY_ADD_WATCH_DESCRIPTOR, (void *)&wd);
}
#endif

/*****************************************************************************
 * POSIX Message Queue Connection
 *****************************************************************************/

void dmtcp::PosixMQConnection::on_mq_close()
{
}

void dmtcp::PosixMQConnection::on_mq_notify(const struct sigevent *sevp)
{
  if (sevp == NULL && _notifyReg) {
    _notifyReg = false;
  } else {
    _notifyReg = true;
    _sevp = *sevp;
  }
}

void dmtcp::PosixMQConnection::preCheckpoint(KernelBufferDrainer& drain)
{
  JASSERT(_fds.size() > 0);

  JTRACE("Checkpoint Posix Message Queue.") (_fds[0]);

  struct stat statbuf;
  JASSERT(fstat(_fds[0], &statbuf) != -1) (JASSERT_ERRNO);
  if (_mode == 0) {
    _mode = statbuf.st_mode;
  }

  struct mq_attr attr;
  JASSERT(mq_getattr(_fds[0], &attr) != -1) (JASSERT_ERRNO);
  _attr = attr;
  if (attr.mq_curmsgs < 0) {
    return;
  }

  int fd = _real_mq_open(_name.c_str(), O_RDWR, 0, NULL);
  JASSERT(fd != -1);

  _qnum = attr.mq_curmsgs;
  char *buf =(char*) JALLOC_HELPER_MALLOC(attr.mq_msgsize);
  for (long i = 0; i < _qnum; i++) {
    unsigned prio;
    ssize_t numBytes = _real_mq_timedreceive(_fds[0], buf, attr.mq_msgsize,
                                             &prio, NULL);
    JASSERT(numBytes != -1) (JASSERT_ERRNO);
    _msgInQueue.push_back(jalib::JBuffer((const char*)buf, numBytes));
    _msgInQueuePrio.push_back(prio);
  }
  JALLOC_HELPER_FREE(buf);
  _real_mq_close(fd);
}

void dmtcp::PosixMQConnection::refill(bool isRestart)
{
  for (long i = 0; i < _qnum; i++) {
    JASSERT(_real_mq_timedsend(_fds[0], _msgInQueue[i].buffer(),
                               _msgInQueue[i].size(), _msgInQueuePrio[i],
                               NULL) != -1);
  }
  _msgInQueue.clear();
  _msgInQueuePrio.clear();
}

void dmtcp::PosixMQConnection::restore(ConnectionRewirer*)
{
  JASSERT(_fds.size() > 0);

  errno = 0;
  if (_oflag & O_EXCL) {
    mq_unlink(_name.c_str());
  }

  int tempfd = _real_mq_open(_name.c_str(), _oflag, _mode, &_attr);
  JASSERT(tempfd != -1) (JASSERT_ERRNO);
  Util::dupFds(tempfd, _fds);
}

void dmtcp::PosixMQConnection::serializeSubClass(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::PosixMQConnection");
  o & _name & _oflag & _mode & _attr;
}
