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

#include "constants.h"
#include "syscallwrappers.h"
#include "connection.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jfilesystem.h"
#include  "../jalib/jconvert.h"
#include "kernelbufferdrainer.h"
#include "syscallwrappers.h"
#include "connectionrewirer.h"
#include "connectionmanager.h"
#include "dmtcpmessagetypes.h"
#include "virtualpidtable.h"
#include "util.h"
#include "resource_manager.h"
#include  "../jalib/jsocket.h"
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

static bool ptmxTestPacketMode(int masterFd);
static ssize_t ptmxReadAll(int fd, const void *origBuf, size_t maxCount);
static ssize_t ptmxWriteAll(int fd, const void *buf, bool isPacketMode);
static void CatFile(const dmtcp::string& src, const dmtcp::string& dest);

#ifdef REALLY_VERBOSE_CONNECTION_CPP
static bool really_verbose = true;
#else
static bool really_verbose = false;
#endif

static dmtcp::string _procFDPath ( int fd )
{
  return "/proc/self/fd/" + jalib::XToString ( fd );
}

static bool hasLock ( const dmtcp::vector<int>& fds )
{
  JASSERT ( fds.size() > 0 );
  int owner = fcntl ( fds[0], F_GETOWN );
  JASSERT ( owner != 0 ) (owner) (JASSERT_ERRNO);
  int self = _real_getpid();
  JASSERT ( self >= 0 );
  return owner == self;
}

//this function creates a socket that is in an error state
static int _makeDeadSocket()
{
  //it does it by creating a socket pair and closing one side
  int sp[2] = {-1,-1};
  JASSERT ( _real_socketpair ( AF_UNIX, SOCK_STREAM, 0, sp ) == 0 ) ( JASSERT_ERRNO )
    .Text ( "socketpair() failed" );
  JASSERT ( sp[0]>=0 && sp[1]>=0 ) ( sp[0] ) ( sp[1] )
    .Text ( "socketpair() failed" );
  _real_close ( sp[1] );
  if (really_verbose) {
    JTRACE ( "Created dead socket." ) ( sp[0] );
  }
  return sp[0];
}

static bool _isVimApp ( )
{
  static int isVimApp = -1;

  if (isVimApp == -1) {
    dmtcp::string progName = jalib::Filesystem::GetProgramName();

    if (progName == "vi" || progName == "vim" || progName == "vim-normal" ||
        progName == "vim.basic"  || progName == "vim.tiny" ||
        progName == "vim.gtk" || progName == "vim.gnome" ) {
      isVimApp = 1;
    } else {
      isVimApp = 0;
    }
  }
  return isVimApp;
}

static bool _isBlacklistedFile ( dmtcp::string& path )
{
  if ((dmtcp::Util::strStartsWith(path, "/dev/") &&
       !dmtcp::Util::strStartsWith(path, "/dev/shm/")) ||
      dmtcp::Util::strStartsWith(path, dmtcp::UniquePid::getTmpDir().c_str())) {
    return true;
  }
  return false;
}

static bool _isBlacklistedTcp ( int sockfd,
				const sockaddr* saddr, socklen_t len )
{
  JASSERT( saddr != NULL );
  if ( len >= sizeof(saddr->sa_family) && saddr->sa_family == AF_INET ) {
    struct sockaddr_in* addr = (sockaddr_in*)saddr;
    // Ports 389 and 636 are the well-known ports in /etc/services that
    // are reserved for LDAP.  Bash continues to maintain a connection to
    // LDAP, leading to problems at restart time.  So, we discover the LDAP
    // remote addresses, and turn them into dead sockets at restart time.
    //However, libc.so:getpwuid() can call libnss_ldap.so which calls
    // libldap-2.4.so to create the LDAP socket while evading our connect
    // wrapper.
    int blacklistedRemotePorts[] = {389, 636, -1}; /* LDAP ports */
    int i;
    for (i = 0; blacklistedRemotePorts[i] != -1; i++) {
      if (ntohs(addr->sin_port) == blacklistedRemotePorts[i]) {
        JTRACE("LDAP port found")(ntohs(addr->sin_port))
              (blacklistedRemotePorts[0])(blacklistedRemotePorts[1]);
        return true;
      }
    }
  }
  // FIXME:  Consider adding configure or dmtcp_checkpoint option to disable
  //   all remote connections.  Handy as quick test for special cases.
  return false;
}

dmtcp::Connection::Connection ( int t )
  : _id ( ConnectionIdentifier::Create() )
  , _type ( ( ConnectionType ) t )
  , _fcntlFlags ( -1 )
  , _fcntlOwner ( -1 )
  , _fcntlSignal ( -1 )
  , _restoreInSecondIteration ( false )
{}

dmtcp::TcpConnection& dmtcp::Connection::asTcp()
{
  JASSERT ( false ) ( _id ) ( _type ).Text ( "Invalid conversion." );
  return * ( ( TcpConnection* ) 0 );
}

void dmtcp::Connection::restartDup2(int oldFd, int fd){
  errno = 0;
  JWARNING ( _real_dup2 ( oldFd, fd ) == fd ) ( oldFd ) ( fd ) ( JASSERT_ERRNO );
}

void dmtcp::Connection::saveOptions ( const dmtcp::vector<int>& fds )
{
  errno = 0;
  _fcntlFlags = fcntl ( fds[0],F_GETFL );
  JASSERT ( _fcntlFlags >= 0 ) ( _fcntlFlags ) ( JASSERT_ERRNO );
  errno = 0;
  _fcntlOwner = fcntl ( fds[0],F_GETOWN );
  JASSERT ( _fcntlOwner != -1 ) ( _fcntlOwner ) ( JASSERT_ERRNO );
  errno = 0;
  _fcntlSignal = fcntl ( fds[0],F_GETSIG );
  JASSERT ( _fcntlSignal >= 0 ) ( _fcntlSignal ) ( JASSERT_ERRNO );
}

void dmtcp::Connection::restoreOptions ( const dmtcp::vector<int>& fds )
{
  //restore F_GETFL flags
  JASSERT ( _fcntlFlags >= 0 ) ( _fcntlFlags );
  JASSERT ( _fcntlOwner != -1 ) ( _fcntlOwner );
  JASSERT ( _fcntlSignal >= 0 ) ( _fcntlSignal );
  errno = 0;
  JASSERT ( fcntl ( fds[0], F_SETFL, _fcntlFlags ) == 0 ) ( fds[0] ) ( _fcntlFlags ) ( JASSERT_ERRNO );

  // FIXME: When restarting, the VirtualPidTable original to current pid
  // mapping might not be restored at this point and thus the following
  // F_SETOWN call will fail. At times it can set the wrong owner as well.
  //  The correct fix would be to restore the fcntlowner after we have the
  // original->current pid mappings.
  errno = 0;
  JASSERT ( fcntl ( fds[0], F_SETOWN, ORIGINAL_TO_CURRENT_PID(_fcntlOwner) ) == 0 )
    ( fds[0] ) ( _fcntlOwner ) ( JASSERT_ERRNO );

  // This JASSERT will almost always trigger until we fix the above mentioned
  // bug.
  //JASSERT (fcntl(fds[0], F_GETOWN) == _fcntlOwner) (fcntl(fds[0], F_GETOWN))(_fcntlOwner) (ORIGINAL_TO_CURRENT_PID(_fcntlOwner));

  errno = 0;
  JASSERT ( fcntl ( fds[0], F_SETSIG,_fcntlSignal ) == 0 ) ( fds[0] ) ( _fcntlSignal ) ( JASSERT_ERRNO );
}

void dmtcp::Connection::doLocking ( const dmtcp::vector<int>& fds )
{
  errno = 0;
  JASSERT ( fcntl ( fds[0], F_SETOWN, _real_getpid() ) == 0 )
    ( fds[0] ) ( JASSERT_ERRNO );
}

/////////////////////////
////// TCP UPDATE COMMANDS

/*onSocket*/
dmtcp::TcpConnection::TcpConnection ( int domain, int type, int protocol )
  : Connection ( TCP_CREATED )
#ifdef EXTERNAL_SOCKET_HANDLING
  , _peerType ( PEER_UNKNOWN )
#endif
  , _sockDomain ( domain )
  , _sockType ( type )
  , _sockProtocol ( protocol )
  , _listenBacklog ( -1 )
  , _bindAddrlen ( 0 )
  , _acceptRemoteId ( ConnectionIdentifier::Null() )
{
  if (domain != -1) {
    JTRACE ("Creating TcpConnection.") ( id() ) ( domain ) ( type ) ( protocol );
  }
  memset ( &_bindAddr, 0, sizeof _bindAddr );
}

dmtcp::TcpConnection& dmtcp::TcpConnection::asTcp()
{
  return *this;
}

void dmtcp::TcpConnection::onBind (const struct sockaddr* addr, socklen_t len)
{
  if (really_verbose) {
    JTRACE ("Binding.") ( id() ) ( len );
  }
  JASSERT ( tcpType() == TCP_CREATED ) ( tcpType() ) ( id() )
    .Text ( "Binding a socket in use????" );
  JASSERT ( len <= sizeof _bindAddr ) ( len ) ( sizeof _bindAddr )
    .Text ( "That is one huge sockaddr buddy." );

  _type = TCP_BIND;
  _bindAddrlen = len;
  memcpy ( &_bindAddr, addr, len );
}
void dmtcp::TcpConnection::onListen ( int backlog )
{
  if (really_verbose) {
    JTRACE ( "Listening." ) ( id() ) ( backlog );
  }
  JASSERT ( tcpType() == TCP_BIND ) ( tcpType() ) ( id() )
    .Text ( "Listening on a non-bind()ed socket????" );
  // A -1 backlog is not an error.
  //JASSERT ( backlog > 0 ) ( backlog )
    //.Text ( "That is an odd backlog????" );

  _type = TCP_LISTEN;
  _listenBacklog = backlog;
}
void dmtcp::TcpConnection::onConnect( int sockfd,
                                      const struct sockaddr *addr,
                                      socklen_t len )
{
  if (really_verbose) {
    JTRACE ( "Connecting." ) ( id() );
  }
  JASSERT ( tcpType() == TCP_CREATED ) ( tcpType() ) ( id() )
    .Text ( "Connecting with an in-use socket????" );

  /* socketpair wrapper calls onConnect with sockfd == -1 and addr == NULL */
  if (addr != NULL && _isBlacklistedTcp(sockfd, addr, len)) {
    _type = TCP_EXTERNAL_CONNECT;
    _connectAddrlen = len;
    memcpy ( &_connectAddr, addr, len );
  } else {
    _type = TCP_CONNECT;
  }
}

/*onAccept*/
dmtcp::TcpConnection::TcpConnection ( const TcpConnection& parent, const ConnectionIdentifier& remote )
  : Connection ( TCP_ACCEPT )
#ifdef EXTERNAL_SOCKET_HANDLING
  , _peerType ( PEER_UNKNOWN )
#endif
  , _sockDomain ( parent._sockDomain )
  , _sockType ( parent._sockType )
  , _sockProtocol ( parent._sockProtocol )
  , _listenBacklog ( -1 )
  , _bindAddrlen ( 0 )
  , _acceptRemoteId ( remote )
{
  if (really_verbose) {
    JTRACE ( "Accepting." ) ( id() ) ( parent.id() ) ( remote );
  }

  //     JASSERT(parent.tcpType() == TCP_LISTEN)(parent.tcpType())(parent.id())
  //             .Text("Accepting from a non listening socket????");
  memset ( &_bindAddr, 0, sizeof _bindAddr );
}
void dmtcp::TcpConnection::onError()
{
  JTRACE ( "Error." ) ( id() );
  _type = TCP_ERROR;
}

void dmtcp::TcpConnection::addSetsockopt ( int level, int option, const char* value, int len )
{
  _sockOptions[level][option] = jalib::JBuffer ( value, len );
}


////////////
///// TCP CHECKPOINTING

#ifdef EXTERNAL_SOCKET_HANDLING
void dmtcp::TcpConnection::preCheckpointPeerLookup ( const dmtcp::vector<int>& fds,
                                                     dmtcp::vector<TcpConnectionInfo>& conInfoTable)
{
  JASSERT ( fds.size() > 0 ) ( id() );

  switch ( tcpType() )
  {
    case TCP_CONNECT:
    case TCP_ACCEPT:
      if ( hasLock ( fds ) && peerType() == PEER_UNKNOWN )
      {
        socklen_t addrlen_local = sizeof(struct sockaddr_storage);
        socklen_t addrlen_remote = sizeof(struct sockaddr_storage);
        struct sockaddr_storage local, remote;

        JASSERT ( 0 == getsockname ( fds[0], (sockaddr*)&local, &addrlen_local ) ) (JASSERT_ERRNO);
        JASSERT ( 0 == getpeername ( fds[0], (sockaddr*)&remote, &addrlen_remote ) ) (JASSERT_ERRNO);
        JASSERT ( addrlen_local == addrlen_remote ) ( addrlen_local ) ( addrlen_remote );
        JASSERT ( local.ss_family == remote.ss_family ) ( local.ss_family ) ( remote.ss_family );
        TcpConnectionInfo conInfo(id(), addrlen_local, local, remote );
        conInfoTable.push_back ( conInfo );
      }
      else
      {
        if (really_verbose) {
          JTRACE ( "Did not get lock.  Won't lookup." ) ( fds[0] ) ( id() );
        }
      }
      break;
    case TCP_LISTEN:
    case TCP_BIND:
      JASSERT ( peerType() == PEER_UNKNOWN );
      break;
  }
}
#endif

void dmtcp::TcpConnection::preCheckpoint ( const dmtcp::vector<int>& fds
    , KernelBufferDrainer& drain )
{
  JASSERT ( fds.size() > 0 ) ( id() );

  if ( ( _fcntlFlags & O_ASYNC ) != 0 )
  {
    if (really_verbose) {
      JTRACE ( "Removing O_ASYNC flag during checkpoint." ) ( fds[0] ) ( id() );
    }
    errno = 0;
    JASSERT ( fcntl ( fds[0],F_SETFL,_fcntlFlags & ~O_ASYNC ) == 0 ) ( JASSERT_ERRNO ) ( fds[0] ) ( id() );
  }

  switch ( tcpType() )
  {
    case TCP_CONNECT:
    case TCP_ACCEPT:
      if ( hasLock ( fds ) )
      {
        const ConnectionIdentifier& toDrainId = id();
        JTRACE ( "Will drain socket" ) ( fds[0] ) ( toDrainId )
          ( _acceptRemoteId );
        drain.beginDrainOf ( fds[0], toDrainId );
      }
      else
      {
        if (really_verbose) {
          JTRACE ( "Did not get lock.  Won't drain" ) ( fds[0] ) ( id() );
        }
      }
      break;
    case TCP_LISTEN:
      drain.addListenSocket ( fds[0] );
      break;
    case TCP_BIND:
      JWARNING ( tcpType() != TCP_BIND ) ( fds[0] )
        .Text ( "If there are pending connections on this socket,\n"
		" they won't be checkpointed because"
		" it is not yet in a listen state." );
      break;
    case TCP_EXTERNAL_CONNECT:
      JTRACE ( "Socket to External Process, won't be drained" ) ( fds[0] );
      break;
  }
}

void dmtcp::TcpConnection::doSendHandshakes( const dmtcp::vector<int>& fds, const dmtcp::UniquePid& coordinator ){
    switch ( tcpType() )
    {
      case TCP_CONNECT:
      case TCP_ACCEPT:
        if ( hasLock ( fds ) )
        {
          JTRACE ("Sending handshake ...") (id()) (fds[0]);
          jalib::JSocket sock(fds[0]);
          sendHandshake( sock, coordinator );
        }
        else
        {
          if (really_verbose) {
            JTRACE("Skipping handshake send (shared socket, not owner).")
		(id()) (fds[0]);
          }
        }
        break;
      case TCP_EXTERNAL_CONNECT:
        JTRACE ( "Socket to External Process, skipping handshake send" ) ( fds[0] );
        break;
    }
  }
  void dmtcp::TcpConnection::doRecvHandshakes( const dmtcp::vector<int>& fds, const dmtcp::UniquePid& coordinator ){
    switch ( tcpType() )
    {
      case TCP_CONNECT:
      case TCP_ACCEPT:
        if ( hasLock ( fds ) )
        {
          JTRACE ("Receiving handshake ...") (id()) (fds[0]);
          jalib::JSocket sock(fds[0]);
          recvHandshake( sock, coordinator );
          if (really_verbose) {
            JTRACE ("Received handshake.") (getRemoteId()) (fds[0]);
          }
        }
        else
        {
          if (really_verbose) {
            JTRACE ("Skipping handshake recv (shared socket, not owner).")
              (id()) (fds[0]);
          }
        }
        break;
      case TCP_EXTERNAL_CONNECT:
        JTRACE ( "Socket to External Process, skipping handshake recv" ) ( fds[0] );
        break;
    }
  }

void dmtcp::TcpConnection::postCheckpoint ( const dmtcp::vector<int>& fds, bool isRestart )
{
  if ( ( _fcntlFlags & O_ASYNC ) != 0 )
  {
    if (really_verbose) {
      JTRACE ( "Re-adding O_ASYNC flag." ) ( fds[0] ) ( id() );
    }
    restoreOptions ( fds );
  }
}
void dmtcp::TcpConnection::restore ( const dmtcp::vector<int>& fds, ConnectionRewirer& rewirer )
{
  JASSERT ( fds.size() > 0 );
  switch ( tcpType() )
  {
    case TCP_PREEXISTING:
    case TCP_ERROR: //not a valid socket
    case TCP_INVALID:
    case TCP_EXTERNAL_CONNECT:
    {
      JTRACE("Creating dead socket.") (fds[0]) (fds.size());
      jalib::JSocket deadSock ( _makeDeadSocket() );
      deadSock.changeFd ( fds[0] );
      for ( size_t i=1; i<fds.size(); ++i )
      {
        JASSERT ( _real_dup2 ( fds[0], fds[i] ) == fds[i] ) ( fds[0] ) ( fds[i] )
          .Text ( "dup2() failed" );
      }
      break;
     }
    case TCP_CREATED:
    case TCP_BIND:
    case TCP_LISTEN:
    {
      JWARNING (  (_sockDomain == AF_INET || _sockDomain == AF_UNIX || _sockDomain == AF_INET6) && _sockType == SOCK_STREAM )
        ( id() )
        ( _sockDomain )
        ( _sockType )
        ( _sockProtocol )
        .Text ( "Socket type not yet [fully] supported." );

      if (really_verbose) {
        JTRACE ( "Restoring socket." ) ( id() ) ( fds[0] );
      }

      jalib::JSocket sock ( _real_socket ( _sockDomain,_sockType,_sockProtocol ) );
      JASSERT ( sock.isValid() );
      sock.changeFd ( fds[0] );

      for ( size_t i=1; i<fds.size(); ++i )
      {
        JASSERT ( _real_dup2 ( fds[0], fds[i] ) == fds[i] ) ( fds[0] ) ( fds[i] )
          .Text ( "dup2() failed" );
      }

      if ( tcpType() == TCP_CREATED ) break;

      if ( _sockDomain == AF_UNIX )
      {
        const char* un_path = ( ( sockaddr_un* ) &_bindAddr )->sun_path;
        JTRACE ( "Unlinking stale unix domain socket." ) ( un_path );
        JWARNING ( unlink ( un_path ) == 0 ) ( un_path );
      }
      /*
       * During restart, some socket options must be restored (using
       * setsockopt) before the socket is used (bind etc.), otherwise we might
       * not be able to restore them at all. One such option is set in the
       * following way for IPV6 family:
       * setsockopt (sd, IPPROTO_IPV6, IPV6_V6ONLY,...)
       * This fix works for now. A better approach would be to restore the
       * socket options in the order in which they are set by the user program.
       * This fix solves a bug that caused OpenMPI to fail to restart under
       * DMTCP.
       *                               --Kapil
       */

      if (_sockDomain == AF_INET6) {
        JTRACE("Restoring some socket options before binding.");
        typedef dmtcp::map< int, dmtcp::map< int, jalib::JBuffer > >::iterator levelIterator;
        typedef dmtcp::map< int, jalib::JBuffer >::iterator optionIterator;

        for ( levelIterator lvl = _sockOptions.begin();
	      lvl!=_sockOptions.end(); ++lvl ) {
          if (lvl->first == IPPROTO_IPV6) {
            for ( optionIterator opt = lvl->second.begin();
		  opt!=lvl->second.end(); ++opt ) {
              if (opt->first == IPV6_V6ONLY) {
                if (really_verbose) {
                  JTRACE ( "Restoring socket option." )
                    ( fds[0] ) ( opt->first ) ( opt->second.size() );
                }
                int ret = _real_setsockopt ( fds[0], lvl->first, opt->first,
                                             opt->second.buffer(),
                                             opt->second.size() );
                JASSERT ( ret == 0 ) ( JASSERT_ERRNO ) ( fds[0] ) (lvl->first)
                  ( opt->first ) (opt->second.buffer()) ( opt->second.size() )
                  .Text ( "Restoring setsockopt failed." );
              }
            }
          }
        }
      }

      if (really_verbose) {
        JTRACE ( "Binding socket." ) ( id() );
      }
      errno = 0;
      JWARNING ( sock.bind ( ( sockaddr* ) &_bindAddr,_bindAddrlen ) )
        ( JASSERT_ERRNO ) ( id() )
        .Text ( "Bind failed." );
      if ( tcpType() == TCP_BIND ) break;

      if (really_verbose) {
        JTRACE ( "Listening socket." ) ( id() );
      }
      errno = 0;
      JWARNING ( sock.listen ( _listenBacklog ) )
        ( JASSERT_ERRNO ) ( id() ) ( _listenBacklog )
        .Text ( "Bind failed." );
      if ( tcpType() == TCP_LISTEN ) break;

    }
    break;
    case TCP_ACCEPT:
      JASSERT(!_acceptRemoteId.isNull())( id() ) ( _acceptRemoteId ) ( fds[0] )
        .Text("Can't restore a TCP_ACCEPT socket with null acceptRemoteId.\n"
	      "  Perhaps handshake went wrong?");
      JTRACE ( "registerOutgoing" ) ( id() ) ( _acceptRemoteId ) ( fds[0] );
      rewirer.registerOutgoing ( _acceptRemoteId, fds );
      break;
    case TCP_CONNECT:
      JTRACE ( "registerIncoming" ) ( id() ) ( _acceptRemoteId ) ( fds[0] );
      rewirer.registerIncoming ( id(), fds );
      break;
//    case TCP_EXTERNAL_CONNECT:
//      int sockFd = _real_socket ( _sockDomain, _sockType, _sockProtocol );
//      JASSERT ( sockFd >= 0);
//      JASSERT ( _real_dup2 ( sockFd, fds[0] ) == fds[0] );
//      JWARNING(0 == _real_connect(sockFd, (sockaddr*) &_connectAddr, _connectAddrlen))
//        (fds[0]) (JASSERT_ERRNO) .Text("Unable to connect to external process");
//      break;
  }
}


void dmtcp::TcpConnection::restoreOptions ( const dmtcp::vector<int>& fds )
{
  typedef dmtcp::map< int, dmtcp::map< int, jalib::JBuffer > >::iterator levelIterator;
  typedef dmtcp::map< int, jalib::JBuffer >::iterator optionIterator;

  if (_sockDomain != AF_INET6 && tcpType() != TCP_EXTERNAL_CONNECT ) {
    for ( levelIterator lvl = _sockOptions.begin();
        lvl!=_sockOptions.end(); ++lvl ) {
      for ( optionIterator opt = lvl->second.begin();
          opt!=lvl->second.end(); ++opt ) {
        if (really_verbose) {
          JTRACE ( "Restoring socket option." )
            ( fds[0] ) ( opt->first ) ( opt->second.size() );
        }
        int ret = _real_setsockopt ( fds[0], lvl->first, opt->first,
            opt->second.buffer(), opt->second.size() );
        JASSERT ( ret == 0 ) ( JASSERT_ERRNO ) ( fds[0] )
          (lvl->first) ( opt->first ) ( opt->second.size() )
          .Text ( "Restoring setsockopt failed." );
      }
    }
  }

  //call base version (F_GETFL etc)
  Connection::restoreOptions ( fds );
}

void dmtcp::TcpConnection::sendHandshake(jalib::JSocket& remote, const dmtcp::UniquePid& coordinator){
  dmtcp::DmtcpMessage hello_local;
  hello_local.type = dmtcp::DMT_HELLO_PEER;
  hello_local.from = id();
  hello_local.coordinator = coordinator;
  remote << hello_local;
}

void dmtcp::TcpConnection::recvHandshake(jalib::JSocket& remote, const dmtcp::UniquePid& coordinator){
  dmtcp::DmtcpMessage hello_remote;
  hello_remote.poison();
  remote >> hello_remote;
  hello_remote.assertValid();
  JASSERT ( hello_remote.type == dmtcp::DMT_HELLO_PEER );
  JASSERT ( hello_remote.coordinator == coordinator )( hello_remote.coordinator ) ( coordinator )
    .Text ( "Peer has a different dmtcp_coordinator than us!\n"
	    "  It must be the same." );

  if(_acceptRemoteId.isNull()){
    //first time
    _acceptRemoteId = hello_remote.from;
    JASSERT (!_acceptRemoteId.isNull())
	    .Text("Read handshake with invalid 'from' field.");
  }else{
    //next time
    JASSERT (_acceptRemoteId == hello_remote.from)
	    (_acceptRemoteId)(hello_remote.from)
            .Text("Read handshake with a different 'from' field"
		  " than a previous handshake.");
  }
}

////////////
///// PTY CHECKPOINTING

void dmtcp::PtyConnection::preCheckpoint ( const dmtcp::vector<int>& fds
    , KernelBufferDrainer& drain )
{
  if (ptyType() == PTY_MASTER && hasLock(fds)) {
    const int maxCount = 10000;
    char buf[maxCount];
    int numRead, numWritten;
    // fds[0] is master fd
    numRead = ptmxReadAll(fds[0], buf, maxCount);
    _ptmxIsPacketMode = ptmxTestPacketMode(fds[0]);
    JTRACE("fds[0] is master (/dev/ptmx)")(fds[0])(_ptmxIsPacketMode);
    numWritten = ptmxWriteAll(fds[0], buf, _ptmxIsPacketMode);
    JASSERT(numRead == numWritten)(numRead)(numWritten);
  }

  if (ptyType() == PTY_SLAVE || ptyType() == PTY_BSD_SLAVE) {
    _restoreInSecondIteration = true;
  }
}

void dmtcp::PtyConnection::postCheckpoint ( const dmtcp::vector<int>& fds, bool isRestart )
{
  restoreOptions ( fds );
}

void dmtcp::PtyConnection::restore ( const dmtcp::vector<int>& fds, ConnectionRewirer& rewirer )
{
  JASSERT ( fds.size() > 0 );

  int tempfd;

  switch ( ptyType() )
  {
    case PTY_INVALID:
      //tempfd = open("/dev/null", O_RDWR);
      JTRACE ("Restoring invalid PTY.") (id());
      return;

    case PTY_DEV_TTY:
    {
      dmtcp::string tty = "/dev/tty";

      tempfd = open ( tty.c_str(), _fcntlFlags );
      JASSERT ( tempfd >= 0 ) ( tempfd ) ( tty ) ( JASSERT_ERRNO )
        .Text ( "Error Opening the terminal device" );

      JASSERT ( _real_dup2 ( tempfd, fds[0] ) == fds[0] ) ( tempfd ) ( fds[0] )
        .Text ( "dup2() failed" );

      close(tempfd);

      JTRACE ( "Restoring /dev/tty for the process" ) ( tty ) ( fds[0] );

      _ptsName = _uniquePtsName = tty;

      break;
    }

    case PTY_CTTY:
    {
      dmtcp::string controllingTty = jalib::Filesystem::GetControllingTerm();
      JASSERT ( controllingTty.length() > 0 ) ( STDIN_FILENO )
        . Text ("Unable to restore terminal attached with the process");

      tempfd = open ( controllingTty.c_str(), _fcntlFlags );
      JASSERT ( tempfd >= 0 ) ( tempfd ) ( controllingTty ) ( JASSERT_ERRNO )
        .Text ( "Error Opening the terminal attached with the process" );

      JASSERT ( _real_dup2 ( tempfd, fds[0] ) == fds[0] ) ( tempfd ) ( fds[0] )
        .Text ( "dup2() failed" );

      close(tempfd);

      JTRACE ( "Restoring CTTY for the process" ) ( controllingTty ) ( fds[0] );

      _ptsName = _uniquePtsName = controllingTty;

      break;
    }

    case PTY_MASTER:
    {
      char pts_name[80];
      JTRACE ( "Restoring /dev/ptmx" ) ( fds[0] );

      tempfd = open ( "/dev/ptmx", O_RDWR );

      JASSERT ( tempfd >= 0 ) ( tempfd ) ( JASSERT_ERRNO )
        .Text ( "Error Opening /dev/ptmx" );

      JASSERT ( grantpt ( tempfd ) >= 0 ) ( tempfd ) ( JASSERT_ERRNO );

      JASSERT ( unlockpt ( tempfd ) >= 0 ) ( tempfd ) ( JASSERT_ERRNO );

      JASSERT ( _real_ptsname_r ( tempfd, pts_name, 80 ) == 0 ) ( tempfd ) ( JASSERT_ERRNO );

      JASSERT ( _real_dup2 ( tempfd, fds[0] ) == fds[0] ) ( tempfd ) ( fds[0] )
        .Text ( "dup2() failed" );

      close(tempfd);

      _ptsName = pts_name;

      //dmtcp::string deviceName = "ptmx[" + _ptsName + "]:" + "/dev/ptmx";

      //dmtcp::KernelDeviceToConnection::instance().erase ( id() );

      //dmtcp::KernelDeviceToConnection::instance().createPtyDevice ( fds[0], deviceName, (Connection*) this );

      UniquePtsNameToPtmxConId::instance().add ( _uniquePtsName, id() );

      if (ptyType() == PTY_MASTER) {
        int packetMode = _ptmxIsPacketMode;
        ioctl(fds[0], TIOCPKT, &packetMode); /* Restore old packet mode */
      }

      break;
    }
    case PTY_SLAVE:
    {
      JASSERT( _ptsName.compare ( "?" ) != 0 );

      _ptsName = dmtcp::UniquePtsNameToPtmxConId::instance().retrieveCurrentPtsDeviceName ( _uniquePtsName );

      tempfd = open ( _ptsName.c_str(), O_RDWR );
      JASSERT ( tempfd >= 0 ) ( _uniquePtsName ) ( _ptsName ) ( JASSERT_ERRNO )
        .Text ( "Error Opening PTS" );

      JASSERT ( _real_dup2 ( tempfd, fds[0] ) == fds[0] ) ( tempfd ) ( fds[0] )
        .Text ( "dup2() failed" );

      close(tempfd);

      JTRACE ( "Restoring PTS real" ) ( _ptsName ) ( _uniquePtsName ) ( fds[0] );

      //dmtcp::string deviceName = "pts:" + _ptsName;

      //dmtcp::KernelDeviceToConnection::instance().erase ( id() );

      //dmtcp::KernelDeviceToConnection::instance().createPtyDevice ( fds[0], deviceName, (Connection*) this );
      break;
    }
    case PTY_BSD_MASTER:
    {
      JTRACE ( "Restoring BSD Master Pty" ) ( _bsdDeviceName ) ( fds[0] );
      dmtcp::string slaveDeviceName = _bsdDeviceName.replace(0, strlen("/dev/pty"), "/dev/tty");

      tempfd = open ( _bsdDeviceName.c_str(), O_RDWR );

      // FIXME: If unable to open the original BSD Master Pty, we should try to
      // open another one until we succeed and then open slave device accordingly.
      // This can be done by creating a function openBSDMaster, which will try
      // to open the original master device, but if unable to do so, it would
      // keep on trying all the possible BSD Master devices until one is
      // opened. It should then create a mapping between original Master/Slave
      // device name and current Master/Slave device name.
      JASSERT ( tempfd >= 0 ) ( tempfd ) ( JASSERT_ERRNO )
        .Text ( "Error Opening BSD Master Pty. (Already in use?)" );

      JASSERT ( _real_dup2 ( tempfd, fds[0] ) == fds[0] ) ( tempfd ) ( fds[0] )
        .Text ( "dup2() failed" );

      close(tempfd);

      break;
    }
    case PTY_BSD_SLAVE:
    {
      JTRACE ( "Restoring BSD Slave Pty" ) ( _bsdDeviceName ) ( fds[0] );
      dmtcp::string masterDeviceName = _bsdDeviceName.replace(0, strlen("/dev/tty"), "/dev/pty");

      tempfd = open ( _bsdDeviceName.c_str(), O_RDWR );

      JASSERT ( tempfd >= 0 ) ( tempfd ) ( JASSERT_ERRNO )
        .Text ( "Error Opening BSD Slave Pty. (Already in use?)" );

      JASSERT ( _real_dup2 ( tempfd, fds[0] ) == fds[0] ) ( tempfd ) ( fds[0] )
        .Text ( "dup2() failed" );

      close(tempfd);

      break;
    }
    default:
    {
      // should never reach here
      JASSERT ( false ) .Text( "Should never reach here." );
    }
  }

  for ( size_t i=1; i<fds.size(); ++i )
  {
    JASSERT ( _real_dup2 ( fds[0], fds[i] ) == fds[i] ) ( fds[0] ) ( fds[i] )
      .Text ( "dup2() failed" );
  }
}

void dmtcp::PtyConnection::restoreOptions ( const dmtcp::vector<int>& fds )
{
  switch ( ptyType() )
  {
    case PTY_INVALID:
      return;

    case PTY_DEV_TTY:
    {
      dmtcp::string device = jalib::Filesystem::ResolveSymlink ( _procFDPath ( fds[0] ) );
      JASSERT(device.compare("/dev/tty") == 0);
      _ptsName = _uniquePtsName = device;
      break;
    }

    case PTY_CTTY:
    {
      dmtcp::string device = jalib::Filesystem::ResolveSymlink ( _procFDPath ( fds[0] ) );
      _ptsName = _uniquePtsName = device;
      break;
    }

    case PTY_MASTER:
    {
      char pts_name[80];

      JASSERT ( _real_ptsname_r ( fds[0], pts_name, 80 ) == 0 ) ( fds[0] ) ( JASSERT_ERRNO );

      _ptsName = pts_name;

      JTRACE ( "Restoring Options /dev/ptmx real" ) ( _ptsName ) ( _uniquePtsName ) ( fds[0] );

      UniquePtsNameToPtmxConId::instance().add ( _uniquePtsName, id() );

      break;
    }
    case PTY_SLAVE:
    {
      JASSERT( _ptsName.compare ( "?" ) != 0 );

      _ptsName = jalib::Filesystem::ResolveSymlink ( _procFDPath ( fds[0] ) );

      JTRACE ( "Restoring Options PTS real" ) ( _ptsName ) ( _uniquePtsName ) ( fds[0] );

      break;
    }
    default:
    {
      // should never reach here
      JASSERT ( false ) .Text( "Should never reach here." );
    }
  }
  Connection::restoreOptions ( fds );
}

////////////
///// FILE CHECKPOINTING

// Upper limit on filesize for files that are automatically chosen for ckpt.
// Default 100MB
#define MAX_FILESIZE_TO_AUTOCKPT (100 * 1024 * 1024)

void dmtcp::FileConnection::handleUnlinkedFile()
{
  if (!jalib::Filesystem::FileExists(_path)) {
    /* File not present in Filesystem.
     * /proc/self/fd lists filename of unlink()ed files as:
     *   "<original_file_name> (deleted)"
     */

    if (Util::strEndsWith(_path, DELETED_FILE_SUFFIX)) {
      _path.erase( _path.length() - strlen(DELETED_FILE_SUFFIX) );
      _type = FILE_DELETED;
    } else {
      JASSERT(_type == FILE_DELETED) (_path)
        .Text ("File not found on disk and yet the filename doesn't "
               "contain the suffix '(deleted)'");
    }
  } else if (Util::strStartsWith(jalib::Filesystem::BaseName(_path),
                                 ".nfs")) {
    JWARNING(access(_path.c_str(), W_OK) == 0) (JASSERT_ERRNO);
    JTRACE(".nfsXXXX: files that are unlink()'d but still in use by some process(es)")
      (_path);
    _type = FILE_DELETED;
  }
}

void dmtcp::FileConnection::calculateRelativePath ()
{
  dmtcp::string cwd = jalib::Filesystem::GetCWD();
  if (_path.compare(0, cwd.length(), cwd) == 0) {
    /* CWD = "/A/B", FileName = "/A/B/C/D" ==> relPath = "C/D" */
    _rel_path = _path.substr(cwd.length() + 1);
  } else {
    _rel_path = "*";
  }
}

void dmtcp::FileConnection::preCheckpointResMgrFile(const dmtcp::vector<int>& fds)
{

  JTRACE("Pre-checkpoint Torque files")(fds.size());
  for(int i=0; i< fds.size(); i++)
    JTRACE("fds[i]=")(i)(fds[i]);

  if( isTorqueIOFile(_path) ){
    _rmtype = TORQUE_IO;
    // Save the content of stdio or node file
    // to restore it later in new IO file or in temporal Torque nodefile
    saveFile(fds[0]);
  } else if( isTorqueNodeFile(_path) ){
    _rmtype = TORQUE_NODE;
    // Save the content of stdio or node file
    // to restore it later in new IO file or in temporal Torque nodefile
    saveFile(fds[0]);
  }
}

void dmtcp::FileConnection::preCheckpoint ( const dmtcp::vector<int>& fds
    , KernelBufferDrainer& drain )
{
  JASSERT ( fds.size() > 0 );

  handleUnlinkedFile();

  calculateRelativePath();

  _ckptFilesDir = UniquePid::checkpointFilesSubDirName();

  // Read the current file descriptor offset
  _offset = lseek(fds[0], 0, SEEK_CUR);
  fstat(fds[0], &_stat);
  _checkpointed = false;
  _restoreInSecondIteration = true;

  // If this file is related to supported Resource Management system
  // handle it specially
  if( _type == FILE_RESMGR ){
    preCheckpointResMgrFile(fds);
    return;
  }

  if (_isBlacklistedFile(_path)) {
    return;
  }
  if (hasLock(fds)) {
    if (getenv(ENV_VAR_CKPT_OPEN_FILES) != NULL) {
      saveFile(fds[0]);
    } else if (_type == FILE_DELETED) {
      saveFile(fds[0]);
    } else if ((_fcntlFlags & (O_WRONLY|O_RDWR)) != 0 &&
               _offset < _stat.st_size &&
               _stat.st_size < MAX_FILESIZE_TO_AUTOCKPT &&
               _stat.st_uid == getuid()) {
      saveFile(fds[0]);
    } else if (_isVimApp() &&
               (Util::strEndsWith(_path, ".swp") == 0 ||
                Util::strEndsWith(_path, ".swo") == 0)) {
      saveFile(fds[0]);
    } else if (Util::strStartsWith(jalib::Filesystem::GetProgramName(), "emacs")) {
      saveFile(fds[0]);
    } else {
      _restoreInSecondIteration = true;
    }
  } else {
    _restoreInSecondIteration = true;
  }
}

void dmtcp::FileConnection::postCheckpoint ( const dmtcp::vector<int>& fds, bool isRestart )
{
  restoreOptions ( fds );
  if (_checkpointed && isRestart && _type == FILE_DELETED) {
    /* Here we want to unlink the file. We want to do it only at the time of
     * restart, but there is no way of finding out if we are restarting or not.
     * That is why we look for the file on disk and if it is present (it was
     * deleted at ckpt time), then we assume that we are restarting and hence
     * we unlink the file.
     */
     if (jalib::Filesystem::FileExists(_path)) {
      JWARNING( unlink(_path.c_str()) != -1 ) (_path)
        .Text("The file was unlinked at the time of checkpoint. "
              "Unlinking it after restart failed");
     }
  }
}

void dmtcp::FileConnection::refreshPath( const dmtcp::vector<int>& fds)
{
  dmtcp::string cwd = jalib::Filesystem::GetCWD();

  if( _type == FILE_RESMGR ){
    // get new file name
    dmtcp::string procpath = "/proc/self/fd/" + jalib::XToString ( fds[0] );
    dmtcp::string newpath = jalib::Filesystem::ResolveSymlink ( procpath );
    JTRACE("This is Resource Manager file!")(newpath)(_path)(this);
    if( newpath != _path ){
      JTRACE("File Manager connection _path is changed => _path = newpath!")(_path)(newpath);
      _path = newpath;
    }
  } else {
    if( _rel_path != "*" ){ // file path is relative to executable current dir
      string oldPath = _path;
      dmtcp::string fullPath = cwd + "/" + _rel_path;
      if( jalib::Filesystem::FileExists(fullPath) ){
        _path = fullPath;
        JTRACE("Change _path based on relative path") (oldPath) (_path) (_rel_path);
      }
    }
  }
}

void dmtcp::FileConnection::restoreOptions ( const dmtcp::vector<int>& fds )
{
  refreshPath(fds);
  //call base version (F_GETFL etc)
  Connection::restoreOptions ( fds );
}


bool dmtcp::FileConnection::restoreResMgrFile(const dmtcp::vector<int>& fds)
{
  char *ptr = getenv("PBS_HOME");
  if( ptr )
    JTRACE("Have access to pbs env:")(ptr);
  else
    JTRACE("Have NO access to pbs env");

  int tmpfd = 0;

  if( _rmtype == TORQUE_NODE ){
    JTRACE("Restore Torque Node file")(fds.size());
    dmtcp::string newpath = "/tmp/dmtcp_torque_nodefile";
    restoreFile(newpath);
    _path = newpath;
    return false;
  }else if( _rmtype == TORQUE_IO ){
    JTRACE("Restore Torque IO file")(fds.size());
    if( isTorqueStdout(_path) ){
      JTRACE("Restore Torque STDOUT file")(fds.size());
      tmpfd = 1;
    } else if( isTorqueStderr(_path ) ){
      JTRACE("Restore Torque STDERR file")(fds.size());
      tmpfd = 2;
    } else{
      return false;
    }

    // get new file name
    dmtcp::string procpath = "/proc/self/fd/" + jalib::XToString ( tmpfd );
    dmtcp::string newpath = jalib::Filesystem::ResolveSymlink ( procpath );

    for(size_t i=0; i<fds.size(); ++i)
    {
      JASSERT ( _real_dup2 ( tmpfd , fds[i] ) == fds[i] ) (tmpfd) ( fds[i] )
        .Text ( "dup2() failed" );
    }
    restoreFile(newpath);
    return true;
  }
  return false;
}


void dmtcp::FileConnection::restore ( const dmtcp::vector<int>& fds,
                                      ConnectionRewirer& rewirer )
{
  struct stat buf;
  bool skip_open;

  JASSERT ( fds.size() > 0 );

  JTRACE("Restoring File Connection") (id()) (_path);

  if( _type == FILE_RESMGR ){
    dmtcp::string old_path = _path;
    JTRACE("Restore Resource Manager File")(_path);
    skip_open = restoreResMgrFile(fds);
    JTRACE("Restore Resource Manager File #2")(old_path)(_path)(skip_open);
  }else{
    refreshPath(fds);

    if (_checkpointed) {
      JASSERT (jalib::Filesystem::FileExists(_path) == false) (_path)
        .Text("\n**** File already exists! Checkpointed copy can't be restored.\n"
            "****Delete the existing file and try again!");

//      JWARNING (jalib::Filesystem::FileExists(_path) == false) (_path)
//        .Text("\n**** File already exists! Deleting it ");
//
//      unlink(_path.c_str());

      restoreFile();

    } else if (jalib::Filesystem::FileExists(_path)) {

      if (stat(_path.c_str() ,&buf) == 0 && S_ISREG(buf.st_mode)) {
        if (buf.st_size > _stat.st_size &&
            (_fcntlFlags & (O_WRONLY|O_RDWR)) != 0) {
          errno = 0;
          JASSERT ( truncate ( _path.c_str(), _stat.st_size ) ==  0 )
                ( _path.c_str() ) ( _stat.st_size ) ( JASSERT_ERRNO );
        } else if (buf.st_size < _stat.st_size) {
        JWARNING (false) .Text("Size of file smaller than what we expected");
        }
      }
    }
  }

  if( !skip_open ){
    int tempfd = openFile ();

    JASSERT ( tempfd > 0 ) ( tempfd ) ( _path ) ( JASSERT_ERRNO );

    for(size_t i=0; i<fds.size(); ++i)
    {
      JASSERT ( _real_dup2 ( tempfd, fds[i] ) == fds[i] ) ( tempfd ) ( fds[i] )
        .Text ( "dup2() failed" );
    }
    _real_close(tempfd);
  }

  errno = 0;
  if (S_ISREG(buf.st_mode)) {
    if (_offset <= buf.st_size && _offset <= _stat.st_size) {
      JASSERT ( lseek ( fds[0], _offset, SEEK_SET ) == _offset )
              ( _path ) ( _offset ) ( JASSERT_ERRNO );
      //JTRACE ("lseek ( fds[0], _offset, SEEK_SET )") (fds[0]) (_offset);
    } else if (_offset > buf.st_size || _offset > _stat.st_size) {
      JWARNING(false) ( _path ) (_offset ) ( _stat.st_size ) ( buf.st_size )
        .Text("No lseek done:  offset is larger than min of old and new size.");
    }
  }
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

static void CatFile(const dmtcp::string& src, const dmtcp::string& dest)
{
  dmtcp::string command = "cat " + src + " > " + dest;
  JASSERT(_real_system(command.c_str()) != -1);
}

int dmtcp::FileConnection::openFile()
{
  int fd;
  JASSERT(WorkerState::currentState() == WorkerState::RESTARTING);

  /*
   * This file was not checkpointed by this process so it won't be restored by
   * this process. Thus, we wait while some other process restores this file
   */
  int count = 1;
  while (!_checkpointed && !jalib::Filesystem::FileExists(_path)) {
    struct timespec sleepTime = {0, 10*1000*1000};
    nanosleep(&sleepTime, NULL);
    count++;
    if (count % 200 == 0) {
      // Print this message every second
      JTRACE("Waiting for the file to be created/restored by some other process")
        (_path);
    }
    if (count%1000 == 0) {
      JWARNING(false) (_path)
        .Text ("Still waiting for the file to be created/restored by some other process");
    }
  }

  fd = open(_path.c_str(), _fcntlFlags);
  JTRACE ("open(_path.c_str(), _fcntlFlags)")
     (fd) (_path.c_str() )(_fcntlFlags);

  JASSERT(fd != -1) (_path) (JASSERT_ERRNO)
    .Text ("open() failed");
  return fd;
}

void dmtcp::FileConnection::restoreFile(dmtcp::string newpath)
{
  JASSERT(WorkerState::currentState() == WorkerState::RESTARTING);
  JASSERT(_checkpointed);

  if( newpath == "" )
    newpath = _path;
  if (_checkpointed && !jalib::Filesystem::FileExists(_path)) {

    JNOTE("File not present, copying from saved checkpointed file") (_path);

    dmtcp::string savedFilePath = getSavedFilePath(_path);

    JASSERT( jalib::Filesystem::FileExists(savedFilePath) )
      (savedFilePath) (_path) .Text("Unable to Find checkpointed copy of File");

    if( _type == FILE_RESMGR ){
      JTRACE("Copying saved Resource Manager file to NEW location")
        (savedFilePath) (_path);
      CatFile(savedFilePath, newpath);
    } else {  
      CreateDirectoryStructure(_path);
      JTRACE("Copying saved checkpointed file to original location")
        (savedFilePath) (_path);
      CopyFile(savedFilePath, newpath);
    }
    //HACK: This was deleting our checkpoint files on RHEL5.2,
    //      perhaps we are leaking file descriptors in the restart process.
    //      Deleting files is scary... maybe we want to make a stricter test.
    //
    // // Unlink the File if the File was unlinked at the time of checkpoint
    // if (_type == FILE_DELETED) {
    //   JASSERT( unlink(_path.c_str()) != -1 )
    //     .Text("Unlinking of pre-checkpoint-deleted file failed");
    // }

  }
}

void dmtcp::FileConnection::saveFile(int fd)
{
  _checkpointed = true;
  _restoreInSecondIteration = false;

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
    char *buf = (char*)JALLOC_HELPER_MALLOC(bufSize);

    int destFd = open(savedFilePath.c_str(), O_CREAT | O_WRONLY | O_TRUNC,
                                             S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    JASSERT(destFd != -1) (_path) (savedFilePath) .Text("Read Failed");

    lseek(fd, 0, SEEK_SET);

    int readBytes, writtenBytes;
    while(1) {
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

  JASSERT( lseek(fd, _offset, SEEK_SET) != -1 ) (_path);
}

dmtcp::string dmtcp::FileConnection::getSavedFilePath(const dmtcp::string& path)
{
  dmtcp::ostringstream os;
  os << _ckptFilesDir
     << "/" << jalib::Filesystem::BaseName(_path) << "_" << _id.conId();

  return os.str();
}

/* We want to check if the two file descriptor corresponding to the two
 * FileConnections are different or identical. This function verifies that the
 * filenames and offset on both fds are same. If they are same, and if
 * lseek()ing one fd changes the offset for other fd as well, then the two fds
 * are identical i.e. they were created by dup() and not by open().
 */
bool dmtcp::FileConnection::isDupConnection ( const Connection& _that, dmtcp::ConnectionToFds& conToFds)
{
  bool retVal = false;

  JASSERT ( _that.conType() == Connection::FILE );

  const FileConnection& that = (const FileConnection&)_that;

  const dmtcp::vector<int>& thisFds = conToFds[_id];
  const dmtcp::vector<int>& thatFds = conToFds[that._id];

  if ( _path == that._path &&
       ( lseek(thisFds[0], 0, SEEK_CUR) == lseek(thatFds[0], 0, SEEK_CUR) ) ) {
    off_t newOffset = lseek (thisFds[0], 1, SEEK_CUR);
    JASSERT (newOffset != -1) (JASSERT_ERRNO) .Text("lseek failed");

    if ( newOffset == lseek (thatFds[0], 0, SEEK_CUR) ) {
      retVal = true;
    }
    // Now restore the old offset
    JASSERT (-1 != lseek (thisFds[0], -1, SEEK_CUR)) .Text("lseek failed");
  }
  return retVal;
}

////////////
///// FIFO CHECKPOINTING

//void dmtcp::FifoConnection::doLocking ( const dmtcp::vector<int>& fds )
//{
//  int i=0,trials = 4;
//
//  JTRACE ("doLocking for FIFO");
//  while( i < trials ){
//    JTRACE ("Loop iteration") (i);
//    errno = 0;
//    int ret = flock(fds[0],LOCK_EX | LOCK_NB);
//    JTRACE ("flock ret") (ret);
//    if( !ret ){
//      JTRACE ("fd has lock") (ret);
//      _has_lock = true;
//      return;
//    }else if( errno == EWOULDBLOCK ){
//      JTRACE ("fd has no lock") (ret);
//      _has_lock = false;
//      return;
//    }
//  }
//  _has_lock=false;
//  JTRACE ("Error while locking FIFO") (errno);
//}
//

void dmtcp::FifoConnection::preCheckpoint ( const dmtcp::vector<int>& fds
    , KernelBufferDrainer& drain )
{
  JASSERT ( fds.size() > 0 );

  if (!hasLock(fds)) {
    return;
  }

  _has_lock = true;

  stat(_path.c_str(),&_stat);


  JTRACE ("Checkpoint fifo.") (fds[0]);

  int new_flags = (_fcntlFlags & (~(O_RDONLY|O_WRONLY))) | O_RDWR | O_NONBLOCK;
  ckptfd = open(_path.c_str(),new_flags);
  JASSERT(ckptfd >= 0)(ckptfd)(JASSERT_ERRNO);

  _in_data.clear();
  size_t bufsize = 256;
  char buf[bufsize];
  int size;


  while(1){ // flush fifo
    size = read(ckptfd,buf,bufsize);
    if( size < 0 ){
      break; // nothing to flush
    }
	for(int i=0;i<size;i++){
		_in_data.push_back(buf[i]);
	}
  }
  close(ckptfd);
  JTRACE ("Checkpointing fifo:  end.") (fds[0]) (_in_data.size());

}

void dmtcp::FifoConnection::postCheckpoint ( const dmtcp::vector<int>& fds, bool isRestart )
{
  if( !_has_lock )
    return; // nothing to do now

  int new_flags = (_fcntlFlags & (~(O_RDONLY|O_WRONLY))) | O_RDWR | O_NONBLOCK;
  ckptfd = open(_path.c_str(),new_flags);
  JASSERT (ckptfd >= 0) (ckptfd) (JASSERT_ERRNO);

  size_t bufsize = 256;
  char buf[bufsize];
  size_t j;
  ssize_t ret;
  for(size_t i=0;i<(_in_data.size()/bufsize);i++){ // refill fifo
    for(j=0; j<bufsize; j++){
      buf[j] = _in_data[j+i*bufsize];
    }
    ret = Util::writeAll(ckptfd,buf,j);
    JASSERT (ret == (ssize_t)j) (JASSERT_ERRNO) (ret)(j) (fds[0])(i);
  }
  int start = (_in_data.size()/bufsize)*bufsize;
  for(j=0; j<_in_data.size()%bufsize; j++){
    buf[j] = _in_data[start+j];
  }
  errno=0;
  buf[j] ='\0';
  JTRACE ("Buf internals.") ((const char*)buf);
  ret = Util::writeAll(ckptfd,buf,j);
  JASSERT (ret == (ssize_t)j) (JASSERT_ERRNO)(ret)(j) (fds[0]);

  close(ckptfd);
  // unlock fifo
  flock(fds[0],LOCK_UN);
  JTRACE ("End checkpointing fifo.") (fds[0]);
  restoreOptions ( fds );
}

void dmtcp::FifoConnection::refreshPath()
{
  dmtcp::string cwd = jalib::Filesystem::GetCWD();
  if( _rel_path != "*" ){ // file path is relative to executable current dir
    string oldPath = _path;
    ostringstream fullPath;
    fullPath << cwd << "/" << _rel_path;
    if( jalib::Filesystem::FileExists(fullPath.str()) ){
      _path = fullPath.str();
	  JTRACE("Change _path based on relative path")(oldPath)(_path);
    }
  }
}

void dmtcp::FifoConnection::restoreOptions ( const dmtcp::vector<int>& fds )
{
  refreshPath();
  //call base version (F_GETFL etc)
  Connection::restoreOptions ( fds );
}


void dmtcp::FifoConnection::restore ( const dmtcp::vector<int>& fds, ConnectionRewirer& rewirer )
{
  JASSERT ( fds.size() > 0 );

  JTRACE("Restoring Fifo Connection") (id()) (_path);
  errno = 0;
  refreshPath();
  int tempfd = openFile ();
  JASSERT ( tempfd > 0 ) ( tempfd ) ( _path ) ( JASSERT_ERRNO );

  for(size_t i=0; i<fds.size(); ++i)
  {
    JASSERT ( _real_dup2 ( tempfd, fds[i] ) == fds[i] ) ( tempfd ) ( fds[i] )
      .Text ( "dup2() failed." );
  }
}

int dmtcp::FifoConnection::openFile()
{
  int fd;

  if (!jalib::Filesystem::FileExists(_path)) {
    JTRACE("Fifo file not present, creating new one") (_path);
    mkfifo(_path.c_str(),_stat.st_mode);
  }

  fd = open(_path.c_str(), O_RDWR | O_NONBLOCK);
  JTRACE("Is opened")(_path.c_str())(fd);

  JASSERT(fd != -1) (_path) (JASSERT_ERRNO);
  return fd;
}

////////////
//// SERIALIZATION

void dmtcp::Connection::serialize ( jalib::JBinarySerializer& o )
{
  JSERIALIZE_ASSERT_POINT ( "dmtcp::Connection" );
  o & _id & _type & _fcntlFlags & _fcntlOwner & _fcntlSignal & _restoreInSecondIteration;
  serializeSubClass ( o );
}

void dmtcp::TcpConnection::serializeSubClass ( jalib::JBinarySerializer& o )
{
  JSERIALIZE_ASSERT_POINT ( "dmtcp::TcpConnection" );
  o & _sockDomain  & _sockType & _sockProtocol & _listenBacklog
    & _bindAddrlen & _bindAddr & _acceptRemoteId;

  JSERIALIZE_ASSERT_POINT ( "SocketOptions:" );
  size_t numSockOpts = _sockOptions.size();
  o & numSockOpts;
  if ( o.isWriter() )
  {
    //JTRACE ( "TCP Serialize " ) ( _type ) ( _id.conId() );
    typedef dmtcp::map< int, dmtcp::map< int, jalib::JBuffer > >::iterator levelIterator;
    typedef dmtcp::map< int, jalib::JBuffer >::iterator optionIterator;

    size_t numLvl = _sockOptions.size();
    o & numLvl;

    for ( levelIterator lvl = _sockOptions.begin(); lvl!=_sockOptions.end(); ++lvl )
    {
      int lvlVal = lvl->first;
      size_t numOpts = lvl->second.size();

      JSERIALIZE_ASSERT_POINT ( "Lvl" );

      o & lvlVal & numOpts;

      for ( optionIterator opt = lvl->second.begin(); opt!=lvl->second.end(); ++opt )
      {
        int optType = opt->first;
        jalib::JBuffer& buffer = opt->second;
        int bufLen = buffer.size();

        JSERIALIZE_ASSERT_POINT ( "Opt" );

        o & optType & bufLen;
        o.readOrWrite ( buffer.buffer(), bufLen );
      }
    }
  }
  else
  {
    size_t numLvl = 0;
    o & numLvl;

    while ( numLvl-- > 0 )
    {
      int lvlVal = -1;
      size_t numOpts = 0;

      JSERIALIZE_ASSERT_POINT ( "Lvl" );

      o & lvlVal & numOpts;

      while ( numOpts-- > 0 )
      {
        int optType = -1;
        int bufLen = -1;

        JSERIALIZE_ASSERT_POINT ( "Opt" );

        o & optType & bufLen;

        jalib::JBuffer buffer ( bufLen );
        o.readOrWrite ( buffer.buffer(), bufLen );

        _sockOptions[lvlVal][optType]=buffer;
      }
    }
  }

  JSERIALIZE_ASSERT_POINT ( "EndSockOpts" );

  dmtcp::map< int, dmtcp::map< int, jalib::JBuffer > > _sockOptions;
}

void dmtcp::FileConnection::serializeSubClass ( jalib::JBinarySerializer& o )
{
  JSERIALIZE_ASSERT_POINT ( "dmtcp::FileConnection" );
  o & _path & _rel_path & _ckptFilesDir;
  o & _offset & _stat & _checkpointed & _rmtype;
  JTRACE("Serializing FileConn.") (_path) (_rel_path) (_ckptFilesDir)
    (_checkpointed) (_fcntlFlags) (_restoreInSecondIteration);
}

void dmtcp::FifoConnection::serializeSubClass ( jalib::JBinarySerializer& o )
{
  JSERIALIZE_ASSERT_POINT ( "dmtcp::FifoConnection" );
  o & _path & _rel_path & _savedRelativePath & _stat & _in_data & _has_lock;
  JTRACE("Serializing FifoConn.") (_path) (_rel_path) (_savedRelativePath);
}

void dmtcp::PtyConnection::serializeSubClass ( jalib::JBinarySerializer& o )
{
  JSERIALIZE_ASSERT_POINT ( "dmtcp::PtyConnection" );
  o & _ptsName & _uniquePtsName & _bsdDeviceName & _type & _ptmxIsPacketMode;

  if ( o.isReader() )
  {
    if ( _type == dmtcp::PtyConnection::PTY_MASTER ) {
      dmtcp::UniquePtsNameToPtmxConId::instance().add ( _uniquePtsName, _id );
    }
  }
  JTRACE("Serializing PtyConn.") (_ptsName) (_uniquePtsName);
}

// void dmtcp::PipeConnection::serializeSubClass(jalib::JBinarySerializer& o)
// {
//     JSERIALIZE_ASSERT_POINT("dmtcp::PipeConnection");
//
//     JASSERT(false).Text("Pipes should have been replaced by socketpair() automagically.");
// }

static bool ptmxTestPacketMode(int masterFd) {
  char tmp_buf[100];
  int slave_fd, ioctlArg, rc;
  fd_set readfds;
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
  /* If character already transmitted (usual case for pty), then this flush
     will tell master to flush it. */
  tcflush(masterFd, TCIFLUSH);

  /* B. Now verify that readfds has no more characters to read. */
  ioctlArg = 1;
  ioctl(masterFd, TIOCINQ, &ioctlArg);
  /* Now check if there's a command byte still to read. */
  FD_ZERO(&readfds);
  FD_SET(masterFd, &readfds);
  select(masterFd + 1, &readfds, NULL, NULL, &zeroTimeout);
  if (FD_ISSET(masterFd, &readfds)) {
    // Clean up someone else's command byte from packet mode.
    // FIXME:  We should restore this on resume/restart.
    rc = read(masterFd, tmp_buf, 100);
    JASSERT ( rc == 1 ) (rc) (masterFd);
  }

  /* C. Now we're ready to do the real test.  If in packet mode, we should
        see command byte of TIOCPKT_DATA (0) with data. */
  tmp_buf[0] = 'x'; /* Don't set '\n'.  Could be converted to "\r\n". */
  /* Give the masterFd something to read. */
  JWARNING ((rc = write(slave_fd, tmp_buf, 1)) == 1) (rc) .Text("write failed");
  //tcdrain(slave_fd);
  _real_close(slave_fd);

  /* Read the 'x':  If we also see a command byte, it's packet mode */
  rc = read(masterFd, tmp_buf, 100);

  /* D. Check if command byte packet exists, and chars rec'd is longer by 1. */
  return (rc == 2 && tmp_buf[0] == TIOCPKT_DATA && tmp_buf[1] == 'x');
}
// Also record the count read on each iteration, in case it's packet mode
static bool readyToRead(int fd) {
  fd_set readfds;
  struct timeval zeroTimeout = {0, 0}; /* Zero: will use to poll, not wait.*/
  FD_ZERO(&readfds);
  FD_SET(fd, &readfds);
  select(fd + 1, &readfds, NULL, NULL, &zeroTimeout);
  return FD_ISSET(fd, &readfds);
}
// returns 0 if not ready to read; else returns -1, or size read incl. header
static ssize_t readOnePacket(int fd, const void *buf, size_t maxCount) {
  typedef int hdr;
  ssize_t rc = 0;
  // Read single packet:  rc > 0 will be true for at most one iteration.
  while (readyToRead(fd) && rc <= 0) {
    rc = read(fd, (char *)buf+sizeof(hdr), maxCount-sizeof(hdr));
    *(hdr *)buf = rc; // Record the number read in header
    if (rc >= (ssize_t)(maxCount-sizeof(hdr))) {
      rc = -1; errno = E2BIG; // Invoke new errno for buf size not large enough
    }
    if (rc == -1 && errno != EAGAIN && errno != EINTR)
      break;  /* Give up; bad error */
  }
  return (rc <= 0 ? rc : rc+sizeof(hdr));
}
// rc < 0 => error; rc == sizeof(hdr) => no data to read;
// rc > 0 => saved w/ count hdr
static ssize_t ptmxReadAll(int fd, const void *origBuf, size_t maxCount) {
  typedef int hdr;
  char *buf = (char *)origBuf;
  int rc;
  while ((rc = readOnePacket(fd, buf, maxCount)) > 0) {
    buf += rc;
  }
  *(hdr *)buf = 0; /* Header count of zero means we're done */
  buf += sizeof(hdr);
  JASSERT(rc < 0 || buf - (char *)origBuf > 0)(rc)(origBuf)((void *)buf);
  return (rc < 0 ? rc : buf - (char *)origBuf);
}
// The hdr contains the size of the full buffer ( [hdr, data] ).
// Return size of origBuf written:  includes packets of form:  [hdr, data]
//   with hdr holding size of data.  Last hdr has value zero.
// Also record the count written on each iteration, in case it's packet mode.
static ssize_t writeOnePacket(int fd, const void *origBuf, bool isPacketMode) {
  typedef int hdr;
  int count = *(hdr *)origBuf;
  int cum_count = 0;
  int rc = 0; // Trigger JASSERT if not modified below.
  if (count == 0)
    return sizeof(hdr);  // count of zero means we're done, hdr consumed
  // FIXME:  It would be nice to restore packet mode (flow control, etc.)
  //         For now, we ignore it.
  if (count == 1 && isPacketMode)
    return sizeof(hdr) + 1;
  while (cum_count < count) {
    rc = write(fd, (char *)origBuf+sizeof(hdr)+cum_count, count-cum_count);
    if (rc == -1 && errno != EAGAIN && errno != EINTR)
      break;  /* Give up; bad error */
    if (rc >= 0)
      cum_count += rc;
  }
  JASSERT(rc != 0 && cum_count == count)(JASSERT_ERRNO)(rc)(count)(cum_count);
  return (rc < 0 ? rc : cum_count+sizeof(hdr));
}
static ssize_t ptmxWriteAll(int fd, const void *buf, bool isPacketMode) {
  typedef int hdr;
  ssize_t cum_count = 0;
  ssize_t rc;
  while ((rc = writeOnePacket(fd, (char *)buf+cum_count, isPacketMode))
	 > (ssize_t)sizeof(hdr)) {
    cum_count += rc;
  }
  JASSERT (rc < 0 || rc == sizeof(hdr)) (rc) (cum_count);
  cum_count += sizeof(hdr);  /* Account for last packet: 'done' hdr w/ 0 data */
  return (rc <= 0 ? rc : cum_count);
}


#define MERGE_MISMATCH_TEXT .Text("Mismatch when merging connections from different restore targets")

void dmtcp::Connection::mergeWith ( const Connection& that ){
  JASSERT (_id          == that._id)         MERGE_MISMATCH_TEXT;
  JASSERT (_type        == that._type)       MERGE_MISMATCH_TEXT;
  JWARNING(_fcntlFlags  == that._fcntlFlags) MERGE_MISMATCH_TEXT;
  JWARNING(_fcntlOwner  == that._fcntlOwner) MERGE_MISMATCH_TEXT;
  JWARNING(_fcntlSignal == that._fcntlSignal)MERGE_MISMATCH_TEXT;
}

void dmtcp::TcpConnection::mergeWith ( const Connection& _that ){
  Connection::mergeWith(_that);
  const TcpConnection& that = (const TcpConnection&)_that; //Connection::_type match is checked in Connection::mergeWith
  JWARNING(_sockDomain    == that._sockDomain)   MERGE_MISMATCH_TEXT;
  JWARNING(_sockType      == that._sockType)     MERGE_MISMATCH_TEXT;
  JWARNING(_sockProtocol  == that._sockProtocol) MERGE_MISMATCH_TEXT;
  JWARNING(_listenBacklog == that._listenBacklog)MERGE_MISMATCH_TEXT;
  JWARNING(_bindAddrlen   == that._bindAddrlen)  MERGE_MISMATCH_TEXT;
  //todo: check _bindAddr and _sockOptions

  JTRACE("Merging TcpConnections")(_acceptRemoteId)(that._acceptRemoteId);

  //merge _acceptRemoteId smartly
  if(_acceptRemoteId.isNull())
    _acceptRemoteId = that._acceptRemoteId;

  if(!that._acceptRemoteId.isNull()){
    JASSERT(_acceptRemoteId == that._acceptRemoteId)(id())(_acceptRemoteId)(that._acceptRemoteId)
      .Text("Merging connections disagree on remote host");
  }
}

void dmtcp::PtyConnection::mergeWith ( const Connection& _that ){
  Connection::mergeWith(_that);
  const PtyConnection& that = (const PtyConnection&)_that;
  JWARNING(_ptsName       == that._ptsName)       MERGE_MISMATCH_TEXT;
  JWARNING(_uniquePtsName == that._uniquePtsName) MERGE_MISMATCH_TEXT;
}

void dmtcp::FileConnection::mergeWith ( const Connection& _that ){
  const FileConnection& that = (const FileConnection&)_that;
  JTRACE("Merging file connections") (_path) (_type) (that._path) (that._type);
  Connection::mergeWith(_that);
  JWARNING(_path   == that._path)   MERGE_MISMATCH_TEXT;
  JWARNING(_offset == that._offset) MERGE_MISMATCH_TEXT;
  if (!_checkpointed) {
    _checkpointed = that._checkpointed;
    _rel_path     = that._rel_path;
    _ckptFilesDir = that._ckptFilesDir;
    _restoreInSecondIteration = that._restoreInSecondIteration;
  }

  //JWARNING(false)(id()).Text("We shouldn't be merging file connections, should we?");
}

void dmtcp::FifoConnection::mergeWith ( const Connection& _that ){
  Connection::mergeWith(_that);
  const FifoConnection& that = (const FifoConnection&)_that;
  JWARNING(_path   == that._path)   MERGE_MISMATCH_TEXT;
  //JWARNING(false)(id()).Text("We shouldn't be merging fifo connections, should we?");
}

////////////
///// STDIO CHECKPOINTING

void dmtcp::StdioConnection::preCheckpoint ( const dmtcp::vector<int>& fds, KernelBufferDrainer& drain ){
  //JTRACE ("Checkpointing stdio") (fds[0]) (id());
}
void dmtcp::StdioConnection::postCheckpoint ( const dmtcp::vector<int>& fds , bool isRestart ) {
    restoreOptions ( fds );
  //nothing
}
void dmtcp::StdioConnection::restore ( const dmtcp::vector<int>& fds, ConnectionRewirer& ){
  for(size_t i=0; i<fds.size(); ++i){
    int fd = fds[i];
    if(fd <= 2){
      JTRACE("Skipping restore of STDIO, just inherit from parent")(fd);
      continue;
    }
    int oldFd = -1;
    switch(_type){
      case STDIO_IN:
        JTRACE("Restoring STDIN")(fd);
        oldFd=0;
        break;
      case STDIO_OUT:
        JTRACE("Restoring STDOUT")(fd);
        oldFd=1;
        break;
      case STDIO_ERR:
        JTRACE("Restoring STDERR")(fd);
        oldFd=2;
        break;
      default:
        JASSERT(false);
    }
    errno = 0;
    JWARNING ( _real_dup2 ( oldFd, fd ) == fd ) ( oldFd ) ( fd ) ( JASSERT_ERRNO );
  }
}
void dmtcp::StdioConnection::restoreOptions ( const dmtcp::vector<int>& fds ){
  Connection::restoreOptions ( fds );
  //nothing
}

void dmtcp::StdioConnection::serializeSubClass ( jalib::JBinarySerializer& o ){
  JSERIALIZE_ASSERT_POINT ( "dmtcp::StdioConnection" );
  //JTRACE("Serializing STDIO") (id());
}

void dmtcp::StdioConnection::mergeWith ( const Connection& that ){
  //Connection::mergeWith(that);
}

void dmtcp::StdioConnection::restartDup2(int oldFd, int newFd){
  static ConnectionRewirer ignored;
  restore(dmtcp::vector<int>(1,newFd), ignored);
}
