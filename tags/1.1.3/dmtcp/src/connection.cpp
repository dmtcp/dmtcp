/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include "connection.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jfilesystem.h"
#include  "../jalib/jconvert.h"
#include "kernelbufferdrainer.h"
#include "syscallwrappers.h"
#include "connectionrewirer.h"
#include "connectionmanager.h"
#include "dmtcpmessagetypes.h"
#include "dmtcpworker.h"
#include  "../jalib/jsocket.h"
#include <sys/ioctl.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <iostream>
#include <ios>
#include <fstream>

static dmtcp::string _procFDPath ( int fd )
{
  return "/proc/self/fd/" + jalib::XToString ( fd );
}

static bool hasLock ( const dmtcp::vector<int>& fds )
{
  JASSERT ( fds.size() > 0 );
  int owner = fcntl ( fds[0], F_GETOWN );
  JASSERT ( owner != 0 );
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
  JTRACE ( "created dead socket" ) ( sp[0] );
  return sp[0];
}

dmtcp::TcpConnection& dmtcp::Connection::asTcp()
{
  JASSERT ( false ) ( _id ) ( _type ).Text ( "invalid conversion" );
  return * ( ( TcpConnection* ) 0 );
}

dmtcp::TcpConnection& dmtcp::TcpConnection::asTcp()
{
  return *this;
}

  dmtcp::Connection::Connection ( int t )
  : _id ( ConnectionIdentifier::Create() )
  , _type ( ( ConnectionType ) t )
  , _fcntlFlags ( -1 )
  , _fcntlOwner ( -1 )
    , _fcntlSignal ( -1 )
{}

void dmtcp::Connection::restartDup2(int oldFd, int fd){
  errno = 0;
  JWARNING ( _real_dup2 ( oldFd, fd ) == fd ) ( oldFd ) ( fd ) ( JASSERT_ERRNO );
}


/////////////////////////
////// TCP UPDATE COMMANDS

  /*onSocket*/ dmtcp::TcpConnection::TcpConnection ( int domain, int type, int protocol )
  : Connection ( TCP_CREATED )
  , _sockDomain ( domain )
  , _sockType ( type )
  , _sockProtocol ( protocol )
  , _listenBacklog ( -1 )
  , _bindAddrlen ( 0 )
    , _acceptRemoteId ( ConnectionIdentifier::Null() )
{
  JTRACE ( "creating TcpConnection" ) ( id() ) ( domain ) ( type ) ( protocol );
  memset ( &_bindAddr, 0, sizeof _bindAddr );
}
void dmtcp::TcpConnection::onBind ( const struct sockaddr* addr, socklen_t len )
{
  JTRACE ( "binding" ) ( id() ) ( len );

  JASSERT ( tcpType() == TCP_CREATED ) ( tcpType() ) ( id() )
    .Text ( "binding a socket in use????" );
  JASSERT ( len <= sizeof _bindAddr ) ( len ) ( sizeof _bindAddr )
    .Text ( "that is one huge sockaddr buddy" );

  _type = TCP_BIND;
  _bindAddrlen = len;
  memcpy ( &_bindAddr, addr, len );
}
void dmtcp::TcpConnection::onListen ( int backlog )
{
  JTRACE ( "listening" ) ( id() ) ( backlog );

  JASSERT ( tcpType() == TCP_BIND ) ( tcpType() ) ( id() )
    .Text ( "listening on a non-bind()ed socket????" );
  JASSERT ( backlog > 0 ) ( backlog )
    .Text ( "that is an odd backlog????" );

  _type = TCP_LISTEN;
  _listenBacklog = backlog;
}
void dmtcp::TcpConnection::onConnect()
{
  JTRACE ( "connect" ) ( id() );

  JASSERT ( tcpType() == TCP_CREATED ) ( tcpType() ) ( id() )
    .Text ( "connecting with an in-use socket????" );

  _type = TCP_CONNECT;
}
/*onAccept*/
  dmtcp::TcpConnection::TcpConnection ( const TcpConnection& parent, const ConnectionIdentifier& remote )
  : Connection ( TCP_ACCEPT )
  , _sockDomain ( parent._sockDomain )
  , _sockType ( parent._sockType )
  , _sockProtocol ( parent._sockProtocol )
  , _listenBacklog ( -1 )
  , _bindAddrlen ( 0 )
    , _acceptRemoteId ( remote )
{
  JTRACE ( "accepting" ) ( id() ) ( parent.id() ) ( remote );

  //     JASSERT(parent.tcpType() == TCP_LISTEN)(parent.tcpType())(parent.id())
  //             .Text("accepting from a non listening socket????");
  memset ( &_bindAddr, 0, sizeof _bindAddr );
}
void dmtcp::TcpConnection::onError()
{
  JTRACE ( "error" ) ( id() );
  _type = TCP_ERROR;
}

void dmtcp::TcpConnection::addSetsockopt ( int level, int option, const char* value, int len )
{
  _sockOptions[level][option] = jalib::JBuffer ( value, len );
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
  errno = 0;
  JASSERT ( fcntl ( fds[0], F_SETOWN,_fcntlOwner ) == 0 ) ( fds[0] ) ( _fcntlOwner ) ( JASSERT_ERRNO );
  errno = 0;
  JASSERT ( fcntl ( fds[0], F_SETSIG,_fcntlSignal ) == 0 ) ( fds[0] ) ( _fcntlSignal ) ( JASSERT_ERRNO );
}

////////////
///// TCP CHECKPOINTING

void dmtcp::TcpConnection::preCheckpoint ( const dmtcp::vector<int>& fds
    , KernelBufferDrainer& drain )
{
  JASSERT ( fds.size() > 0 ) ( id() );

  if ( ( _fcntlFlags & O_ASYNC ) != 0 )
  {
    JTRACE ( "removing O_ASYNC flag during checkpoint" ) ( fds[0] ) ( id() );
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
        JTRACE ( "will drain socket" ) ( fds[0] ) ( toDrainId ) ( _acceptRemoteId );
        drain.beginDrainOf ( fds[0], toDrainId );
      }
      else
      {
        JTRACE ( "did not get lock.. wont drain" ) ( fds[0] ) ( id() );
      }
      break;
    case TCP_LISTEN:
      drain.addListenSocket ( fds[0] );
      break;
    case TCP_BIND:
      JWARNING ( tcpType() != TCP_BIND ) ( fds[0] )
        .Text ( "if there are pending connections on this socket, they wont be checkpointed because it is not yet in a listen state." );
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
          JTRACE("sending handshake...")(id())(fds[0]);
          jalib::JSocket sock(fds[0]);
          sendHandshake( sock, coordinator );
        }
        else
        {
          JTRACE("skipping handshake send (shared socket, not owner)")(id())(fds[0]);
        }
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
          JTRACE("receiving handshake...")(id())(fds[0]);
          jalib::JSocket sock(fds[0]);
          recvHandshake( sock, coordinator );
          JTRACE("received handshake")(getRemoteId())(fds[0]);
        }
        else
        {
          JTRACE("skipping handshake recv (shared socket, not owner)")(id())(fds[0]);
        }
        break;
    }
  }

void dmtcp::TcpConnection::postCheckpoint ( const dmtcp::vector<int>& fds )
{
  if ( ( _fcntlFlags & O_ASYNC ) != 0 )
  {
    JTRACE ( "re-adding O_ASYNC flag" ) ( fds[0] ) ( id() );
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
    {
      JTRACE("creating dead socket")(fds[0])(fds.size());
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
        .Text ( "socket type not yet [fully] supported" );

      JTRACE ( "restoring socket" ) ( id() ) ( fds[0] );

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
        JTRACE ( "unlinking stale unix domain socket" ) ( un_path );
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
        JTRACE("restoring some socket options before binding");
        typedef dmtcp::map< int, dmtcp::map< int, jalib::JBuffer > >::iterator levelIterator;
        typedef dmtcp::map< int, jalib::JBuffer >::iterator optionIterator;

        for ( levelIterator lvl = _sockOptions.begin(); lvl!=_sockOptions.end(); ++lvl ) {
          if (lvl->first == IPPROTO_IPV6) { 
            for ( optionIterator opt = lvl->second.begin(); opt!=lvl->second.end(); ++opt ) {
              if (opt->first == IPV6_V6ONLY) {
              JTRACE ( "restoring socket option" ) ( fds[0] ) ( opt->first ) ( opt->second.size() );
              int ret = _real_setsockopt ( fds[0],lvl->first,opt->first,opt->second.buffer(), opt->second.size() );
              JASSERT ( ret == 0 ) ( JASSERT_ERRNO ) ( fds[0] ) (lvl->first) ( opt->first ) (opt->second.buffer()) ( opt->second.size() )
                  .Text ( "restoring setsockopt failed" );
              }
            }
          }
        }
      }

      JTRACE ( "binding socket" ) ( id() );
      errno = 0;
      JWARNING ( sock.bind ( ( sockaddr* ) &_bindAddr,_bindAddrlen ) )
        ( JASSERT_ERRNO ) ( id() )
        .Text ( "bind failed" );
      if ( tcpType() == TCP_BIND ) break;

      JTRACE ( "listening socket" ) ( id() );
      errno = 0;
      JWARNING ( sock.listen ( _listenBacklog ) )
        ( JASSERT_ERRNO ) ( id() ) ( _listenBacklog )
        .Text ( "bind failed" );
      if ( tcpType() == TCP_LISTEN ) break;

    }
    break;
    case TCP_ACCEPT:
      JASSERT(!_acceptRemoteId.isNull())( id() ) ( _acceptRemoteId ) ( fds[0] )
        .Text("cant restore a TCP_ACCEPT socket with null acceptRemoteId, perhaps handshake went wrong?");
      JTRACE ( "registerOutgoing" ) ( id() ) ( _acceptRemoteId ) ( fds[0] );
      rewirer.registerOutgoing ( _acceptRemoteId, fds );
      break;
    case TCP_CONNECT:
      JTRACE ( "registerIncoming" ) ( id() ) ( _acceptRemoteId ) ( fds[0] );
      rewirer.registerIncoming ( id(), fds );
      break;
  }
}


void dmtcp::TcpConnection::restoreOptions ( const dmtcp::vector<int>& fds )
{

  typedef dmtcp::map< int, dmtcp::map< int, jalib::JBuffer > >::iterator levelIterator;
  typedef dmtcp::map< int, jalib::JBuffer >::iterator optionIterator;

  if (_sockDomain != AF_INET6) { 
    for ( levelIterator lvl = _sockOptions.begin(); lvl!=_sockOptions.end(); ++lvl ) {
      for ( optionIterator opt = lvl->second.begin(); opt!=lvl->second.end(); ++opt ) {
        JTRACE ( "restoring socket option" ) ( fds[0] ) ( opt->first ) ( opt->second.size() );
        int ret = _real_setsockopt ( fds[0],lvl->first,opt->first,opt->second.buffer(), opt->second.size() );
        JASSERT ( ret == 0 ) ( JASSERT_ERRNO ) ( fds[0] ) (lvl->first) ( opt->first ) ( opt->second.size() )
          .Text ( "restoring setsockopt failed" );
      }
    }
  }


  //call base version (F_GETFL etc)
  Connection::restoreOptions ( fds );

}

void dmtcp::TcpConnection::doLocking ( const dmtcp::vector<int>& fds )
{
  errno = 0;
  JASSERT ( fcntl ( fds[0], F_SETOWN, _real_getpid() ) == 0 ) ( fds[0] ) ( JASSERT_ERRNO );
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
    .Text ( "peer has a different dmtcp_coordinator than us! it must be the same." );

  if(_acceptRemoteId.isNull()){
    //first time
    _acceptRemoteId = hello_remote.from;
    JASSERT(!_acceptRemoteId.isNull()).Text("read handshake with invalid 'from' field");
  }else{
    //next time
    JASSERT(_acceptRemoteId == hello_remote.from)(_acceptRemoteId)(hello_remote.from)
      .Text("read handshake with a different 'from' field than a previous handshake");
  }
}

////////////
///// PIPE CHECKPOINTING


// void dmtcp::PipeConnection::preCheckpoint(const dmtcp::vector<int>& fds
//                         , KernelBufferDrainer& drain)
// {
// }
// void dmtcp::PipeConnection::postCheckpoint(const dmtcp::vector<int>& fds)
// {
//
// }
// void dmtcp::PipeConnection::restore(const dmtcp::vector<int>&, ConnectionRewirer&)
// {
//
// }

////////////
///// PTY CHECKPOINTING

void dmtcp::PtyConnection::preCheckpoint ( const dmtcp::vector<int>& fds
    , KernelBufferDrainer& drain )
{

}
void dmtcp::PtyConnection::postCheckpoint ( const dmtcp::vector<int>& fds )
{

}
void dmtcp::PtyConnection::restore ( const dmtcp::vector<int>& fds, ConnectionRewirer& rewirer )
{
  JASSERT ( fds.size() > 0 );

  int tempfd;
  char pts_name[80];

  switch ( ptyType() )
  {
    case PTY_INVALID:
      //tempfd = open("/dev/null", O_RDWR);
      JTRACE("restoring invalid PTY")(id());
      return;

    case PTY_CTTY:
    {
      dmtcp::string controllingTty = jalib::Filesystem::GetCurrentTty();
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

      //dmtcp::KernelDeviceToConnection::Instance().erase ( id() );
      
      //dmtcp::KernelDeviceToConnection::Instance().createPtyDevice ( fds[0], deviceName, (Connection*) this );

      UniquePtsNameToPtmxConId::Instance().add ( _uniquePtsName, id() );

      break;
    }
    case PTY_SLAVE:
    {
      JASSERT( _ptsName.compare ( "?" ) != 0 );

      _ptsName = dmtcp::UniquePtsNameToPtmxConId::Instance().retrieveCurrentPtsDeviceName ( _uniquePtsName );

      tempfd = open ( _ptsName.c_str(), O_RDWR );
      JASSERT ( tempfd >= 0 ) ( _uniquePtsName ) ( _ptsName ) ( JASSERT_ERRNO )
        .Text ( "Error Opening PTS" );

      JASSERT ( _real_dup2 ( tempfd, fds[0] ) == fds[0] ) ( tempfd ) ( fds[0] )
        .Text ( "dup2() failed" );

      close(tempfd);

      JTRACE ( "Restoring PTS real" ) ( _ptsName ) ( _uniquePtsName ) ( fds[0] );

      //dmtcp::string deviceName = "pts:" + _ptsName;

      //dmtcp::KernelDeviceToConnection::Instance().erase ( id() );
      
      //dmtcp::KernelDeviceToConnection::Instance().createPtyDevice ( fds[0], deviceName, (Connection*) this );
      break;
    }
    default:
    {
      // should never reach here
      JASSERT ( false ).Text ( "should never reach here" );
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

      UniquePtsNameToPtmxConId::Instance().add ( _uniquePtsName, id() );

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
      JASSERT ( false ).Text ( "should never reach here" );
    }
  }
}

////////////
///// FILE CHECKPOINTING

void dmtcp::FileConnection::preCheckpoint ( const dmtcp::vector<int>& fds
    , KernelBufferDrainer& drain )
{
  JASSERT ( fds.size() > 0 );

  // Read the current file descriptor offset
  _offset = lseek(fds[0], 0, SEEK_CUR);
  stat(_path.c_str(),&_stat);

  // Checkpoint Files, if User has requested then OR if File is not present in Filesystem
  if (getenv(ENV_VAR_CKPT_OPEN_FILES) != NULL || !jalib::Filesystem::FileExists(_path)) {
    saveFile(fds[0]);
  }
}
void dmtcp::FileConnection::postCheckpoint ( const dmtcp::vector<int>& fds )
{

}

void dmtcp::FileConnection::refreshPath()
{
  const char* cur_dir = get_current_dir_name();
  dmtcp::string curDir = cur_dir;
  if( _rel_path != "*" ){ // file path is relative to executable current dir
    string oldPath = _path;
    ostringstream fullPath;
    fullPath << curDir << "/" << _rel_path;
    if( jalib::Filesystem::FileExists(fullPath.str()) ){
      _path = fullPath.str();
	  JTRACE("Change _path based on relative path")(oldPath)(_path);
    }
  }
}

void dmtcp::FileConnection::restoreOptions ( const dmtcp::vector<int>& fds )
{
  refreshPath();
  //call base version (F_GETFL etc)
  Connection::restoreOptions ( fds );
}


void dmtcp::FileConnection::restore ( const dmtcp::vector<int>& fds, ConnectionRewirer& rewirer )
{
  struct stat buf;

  JASSERT ( fds.size() > 0 );

  JTRACE("Restoring File Connection") (id()) (_path);
  errno = 0;
  refreshPath();

  if (stat(_path.c_str() ,&buf) == 0 && S_ISREG(buf.st_mode)) {
    if (buf.st_size > _stat.st_size)
    	JASSERT ( truncate ( _path.c_str(), _stat.st_size ) ==  0 )
                ( _path.c_str() ) ( _stat.st_size ) ( JASSERT_ERRNO );
    else if (buf.st_size < _stat.st_size)  
	JWARNING ("Size of file smaller than what we expected");
  }

  int tempfd = openFile ();

  JASSERT ( tempfd > 0 ) ( tempfd ) ( _path ) ( JASSERT_ERRNO );

  for(size_t i=0; i<fds.size(); ++i)
  {
    JASSERT ( _real_dup2 ( tempfd, fds[0] ) == fds[0] ) ( tempfd ) ( fds[0] )
      .Text ( "dup2() failed" );
  }

  errno = 0;
  if (S_ISREG(buf.st_mode)) {
    if (_offset <= buf.st_size && _offset <= _stat.st_size) {
      JASSERT ( lseek ( fds[0], _offset, SEEK_SET ) == _offset )
	      ( _path ) ( _offset ) ( JASSERT_ERRNO );
      JTRACE("lseek ( fds[0], _offset, SEEK_SET )")(fds[0])(_offset);
    } else if (_offset > buf.st_size || _offset > _stat.st_size) {
      JWARNING("no lseek done:  offset is larger than min of old and new size")
	      ( _path ) (_offset ) ( _stat.st_size ) ( buf.st_size );
    }
  }
}

static void CreateDirectoryStructure(const dmtcp::string& path)
{
  size_t index = path.rfind('/');

  if (index == -1)
    return;

  dmtcp::string dir = path.substr(0, index);

  index = path.find('/');
  while (index != -1) {
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
  //dmtcp::ifstream in(src.c_str(), dmtcp::ios::in | dmtcp::ios::binary);
  //dmtcp::ofstream out(dest.c_str(), dmtcp::ios::in | dmtcp::ios::binary);
  //out << in.rdbuf();

  dmtcp::string command = "cp -f " + src + " " + dest;
  JASSERT(_real_system(command.c_str()) != -1);
}

int dmtcp::FileConnection::openFile()
{
  int fd;

  if (!jalib::Filesystem::FileExists(_path)) {

    JTRACE("File not present, copying from saved checkpointed file") (_path);

    dmtcp::string savedFilePath = getSavedFilePath(_path);

    JASSERT( jalib::Filesystem::FileExists(savedFilePath) )
      (savedFilePath) (_path) .Text("Unable to Find checkpointed copy of File");

    CreateDirectoryStructure(_path);

    JTRACE("Copying saved checkpointed file to original location")
      (savedFilePath) (_path);
    CopyFile(savedFilePath, _path);
  }

  fd = open(_path.c_str(), _fcntlFlags);
  JTRACE("open(_path.c_str(), _fcntlFlags)")(fd)(_path.c_str())(_fcntlFlags);

  //HACK: This was deleting our checkpoint files on RHEL5.2,
  //      perhaps we are leaking file descriptors in the restart process.
  //      Deleting files is scary... maybe we want to make a stricter test.
  //
  // // Unlink the File if the File was unlinked at the time of checkpoint
  // if (_fileType == FILE_DELETED) {
  //   JASSERT( unlink(_path.c_str()) != -1 )
  //     .Text("Unlinking of pre-checkpoint-deleted file failed");
  // }

  JASSERT(fd != -1) (_path) (JASSERT_ERRNO);
  return fd;
}

void dmtcp::FileConnection::saveFile(int fd)
{
  if (jalib::Filesystem::FileExists(_path)) {
    dmtcp::string savedFilePath = getSavedFilePath(_path);

    CreateDirectoryStructure(savedFilePath);

    JTRACE("Saving checkpointed copy of the file") (_path) (savedFilePath);

    CopyFile(_path, savedFilePath);
    return;
  }
  /* File not present in File System
   *
   * The name for deleted files in /proc file system is listed as:
   *   "<original_file_name> (deleted)"
   */

  if (_fileType != FILE_DELETED) {
    int index = _path.find(DELETED_FILE_SUFFIX);

    // extract <original_file_name> from _path
    dmtcp::string name = _path.substr(0, index);

    // Make sure _path ends with DELETED_FILE_SUFFIX
    JASSERT( _path.length() == index + strlen(DELETED_FILE_SUFFIX) );

    _path = name;
    _fileType = FILE_DELETED;
  }

  dmtcp::string savedFilePath = getSavedFilePath(_path);

  CreateDirectoryStructure(savedFilePath);

  JTRACE("Saving checkpointed copy of the file") (_path) (savedFilePath);

  {
    char buf[1024];

    int destFd = open(savedFilePath.c_str(), O_CREAT | O_WRONLY | O_TRUNC,
                                            S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    JASSERT(destFd != -1) (_path) (savedFilePath)
      .Text("Unable to create chekpointed copy of file");

    lseek(fd, 0, SEEK_SET);

    int readBytes, writtenBytes;
    while (readBytes = read(fd, buf, 1024)) {
      JASSERT( readBytes != -1 ) ( _path ).Text( "read Failed" );

      writtenBytes = write(destFd, buf, readBytes);
      JASSERT( writtenBytes != -1 ) ( savedFilePath ).Text( "write Failed" );

      while (writtenBytes != readBytes) {
        writtenBytes += write(destFd, buf + writtenBytes, readBytes - writtenBytes);
        JASSERT( writtenBytes != -1 ) ( savedFilePath ).Text( "write Failed" );
      }
    }

    close(destFd);
  }

  JASSERT( lseek(fd, _offset, SEEK_SET) != -1 ) (_path);
}

dmtcp::string dmtcp::FileConnection::getSavedFilePath(const dmtcp::string& path)
{
  dmtcp::ostringstream os;
  dmtcp::ostringstream relPath;
  dmtcp::string fileName;

  size_t index = path.rfind('/');
  if (index != -1)
    fileName =  path.substr(index+1);
  else
    fileName = path;

  const char* curDir = getenv( ENV_VAR_CHECKPOINT_DIR );
  if ( curDir == NULL ) {
    curDir = get_current_dir_name();
  }

  if (_savedRelativePath.empty()) {
    relPath << CHECKPOINT_FILES_SUBDIR_PREFIX
            << jalib::Filesystem::GetProgramName()
            << "_" << _id.pid()
            << "/" << fileName << "_" << _id.conId();
    _savedRelativePath = relPath.str();
  }

  os << curDir << "/" << _savedRelativePath;

  return os.str();
}

////////////
///// FIFO CHECKPOINTING

void dmtcp::FifoConnection::doLocking ( const dmtcp::vector<int>& fds )
{
  int i=0,trials = 4;

  JTRACE("doLocking for FIFO");
  while( i < trials ){ 
    JTRACE("Loop iteration")(i);
    errno = 0;
    int ret = flock(fds[0],LOCK_EX | LOCK_NB);
    JTRACE("flock ret")(ret);
    if( !ret ){
      JTRACE("fd has lock")(ret);
      _has_lock = true;
      return;
    }else if( errno == EWOULDBLOCK ){
      JTRACE("fd has no lock")(ret);
      _has_lock = false;
      return;
    }
  }
  _has_lock=false;
  JTRACE("Error while locking FIFO")(errno);
}


void dmtcp::FifoConnection::preCheckpoint ( const dmtcp::vector<int>& fds
    , KernelBufferDrainer& drain )
{
  JASSERT ( fds.size() > 0 );

  JTRACE("start")(fds[0]);

  stat(_path.c_str(),&_stat);
  
  if( !_has_lock ){
    JTRACE("no lock => don't checkpoint fifo")(fds[0]);
    return;
  }

  JTRACE("checkpoint fifo")(fds[0]);

  int new_flags = (_fcntlFlags & (~(O_RDONLY|O_WRONLY))) | O_RDWR | O_NONBLOCK; 
  ckptfd = open(_path.c_str(),new_flags);
  JASSERT(ckptfd >= 0)(ckptfd)(JASSERT_ERRNO);

  _in_data.clear();
  int bufsize = 256;
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
  JTRACE("checkpoint fifo - end")(fds[0])(_in_data.size());
  
}

void dmtcp::FifoConnection::postCheckpoint ( const dmtcp::vector<int>& fds )
{
  if( !_has_lock )
    return; // nothing to do now

  int new_flags = (_fcntlFlags & (~(O_RDONLY|O_WRONLY))) | O_RDWR | O_NONBLOCK; 
  ckptfd = open(_path.c_str(),new_flags);
  JASSERT(ckptfd >= 0)(ckptfd)(JASSERT_ERRNO);

  int bufsize = 256;
  char buf[bufsize];
  int j;
  int ret;
  for(int i=0;i<(_in_data.size()/bufsize);i++){ // refill fifo
    for(j=0;j<bufsize;j++){
		buf[j] = _in_data[j+i*bufsize];
	}
	JASSERT( (ret=write(ckptfd,buf,j)) == j)(JASSERT_ERRNO)(ret)(j)(fds[0])(i);
  }
  int start = (_in_data.size()/bufsize)*bufsize;
  for(j=0;j<_in_data.size()%bufsize;j++){
    buf[j] = _in_data[start+j];
  }
  errno=0;
  buf[j] ='\0';
  JTRACE("Buf internals")((const char*)buf);
  JASSERT( (ret=write(ckptfd,buf,j)) == j)(JASSERT_ERRNO)(ret)(j)(fds[0]);

  close(ckptfd);  
  // unlock fifo
  flock(fds[0],LOCK_UN);
  JTRACE("end checkpointing fifo")(fds[0]);
}

void dmtcp::FifoConnection::refreshPath()
{
  const char* cur_dir = get_current_dir_name();
  dmtcp::string curDir = cur_dir;
  if( _rel_path != "*" ){ // file path is relative to executable current dir
    string oldPath = _path;
    ostringstream fullPath;
    fullPath << curDir << "/" << _rel_path;
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
  
  int new_flags = (_fcntlFlags & (~(O_RDONLY|O_WRONLY))) | O_RDWR | O_NONBLOCK; 

  for(size_t i=0; i<fds.size(); ++i)
  {
    JASSERT ( _real_dup2 ( tempfd, fds[i] ) == fds[i] ) ( tempfd ) ( fds[i] )
      .Text ( "dup2() failed" );
  }
}

int dmtcp::FifoConnection::openFile()
{
  int fd;

  if (!jalib::Filesystem::FileExists(_path)) {
    JTRACE("Fifo file not present, creating new one") (_path);
	mkfifo(_path.c_str(),_stat.st_mode);
  }

  JTRACE("open(_path.c_str(), _fcntlFlags)")(_path.c_str())(_fcntlFlags);
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
  o & _id & _type & _fcntlFlags;
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
    JTRACE ( "TCP Serialize " ) ( _type ) ( _id.conId() );
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
  o & _path & _rel_path & _savedRelativePath & _offset & _fileType & _stat;
}

void dmtcp::FifoConnection::serializeSubClass ( jalib::JBinarySerializer& o )
{
  JSERIALIZE_ASSERT_POINT ( "dmtcp::FifoConnection" );
  o & _path & _rel_path & _savedRelativePath & _stat & _in_data & _has_lock;
}

void dmtcp::PtyConnection::serializeSubClass ( jalib::JBinarySerializer& o )
{
  JSERIALIZE_ASSERT_POINT ( "dmtcp::PtyConnection" );
  o & _ptsName & _uniquePtsName & _type;

  if ( o.isReader() )
  {
    if ( _type == dmtcp::PtyConnection::PTY_MASTER ) {
      dmtcp::UniquePtsNameToPtmxConId::Instance().add ( _uniquePtsName, _id );
    }
  }
}

// void dmtcp::PipeConnection::serializeSubClass(jalib::JBinarySerializer& o)
// {
//     JSERIALIZE_ASSERT_POINT("dmtcp::PipeConnection");
//
//     JASSERT(false).Text("pipes should have been replaced by socketpair() automagically");
// }

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
  const PtyConnection& that = (const PtyConnection&)_that; //Connection::_type match is checked in Connection::mergeWith
  JWARNING(_type          == that._type)          MERGE_MISMATCH_TEXT;
  JWARNING(_ptsName       == that._ptsName)       MERGE_MISMATCH_TEXT;
  JWARNING(_uniquePtsName == that._uniquePtsName) MERGE_MISMATCH_TEXT;
}

void dmtcp::FileConnection::mergeWith ( const Connection& that ){
  Connection::mergeWith(that);
  JWARNING(false)(id()).Text("We shouldn't be merging file connections, should we?");
}

void dmtcp::FifoConnection::mergeWith ( const Connection& that ){
  Connection::mergeWith(that);
  JWARNING(false)(id()).Text("We shouldn't be merging fifo connections, should we?");
}

////////////
///// STDIO CHECKPOINTING

void dmtcp::StdioConnection::preCheckpoint ( const dmtcp::vector<int>& fds, KernelBufferDrainer& drain ){
  JTRACE("Checkpointing stdio")(fds[0])(id());
}
void dmtcp::StdioConnection::postCheckpoint ( const dmtcp::vector<int>& fds ){
  //nothing
}
void dmtcp::StdioConnection::restore ( const dmtcp::vector<int>& fds, ConnectionRewirer& ){
  for(size_t i=0; i<fds.size(); ++i){
    int fd = fds[i];
    if(fd <= 2){
      JTRACE("Skipping restore of STDIO, just inherit from parent")(fd);
      continue;
    }
    int oldFd;
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
  //nothing
}

void dmtcp::StdioConnection::serializeSubClass ( jalib::JBinarySerializer& o ){
  JSERIALIZE_ASSERT_POINT ( "dmtcp::StdioConnection" );
}

void dmtcp::StdioConnection::mergeWith ( const Connection& that ){
  Connection::mergeWith(that);
}

void dmtcp::StdioConnection::restartDup2(int oldFd, int newFd){
  static ConnectionRewirer ignored;
  restore(dmtcp::vector<int>(1,newFd), ignored);
}
