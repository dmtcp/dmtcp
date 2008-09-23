/***************************************************************************
 *   Copyright (C) 2006 by Jason Ansel                                     *
 *   jansel@ccs.neu.edu                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "connection.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "jconvert.h"
#include "kernelbufferdrainer.h"
#include "syscallwrappers.h"
#include "connectionrewirer.h"
#include "connectionmanager.h"
#include "dmtcpmessagetypes.h"
#include "dmtcpworker.h"
#include "jsocket.h"
#include <sys/ioctl.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <ios>
#include <fstream>

static bool hasLock ( const std::vector<int>& fds )
{
  JASSERT ( fds.size() > 0 );
  int owner = fcntl ( fds[0], F_GETOWN );
  JASSERT ( owner != 0 );
  int self = getpid();
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


void dmtcp::Connection::saveOptions ( const std::vector<int>& fds )
{
  _fcntlFlags = fcntl ( fds[0],F_GETFL );
  JASSERT ( _fcntlFlags >= 0 ) ( _fcntlFlags ) ( JASSERT_ERRNO );
  _fcntlOwner = fcntl ( fds[0],F_GETOWN );
  JASSERT ( _fcntlOwner != -1 ) ( _fcntlOwner ) ( JASSERT_ERRNO );
  _fcntlSignal = fcntl ( fds[0],F_GETSIG );
  JASSERT ( _fcntlSignal >= 0 ) ( _fcntlSignal ) ( JASSERT_ERRNO );
}
void dmtcp::Connection::restoreOptions ( const std::vector<int>& fds )
{
  //restore F_GETFL flags
  JASSERT ( _fcntlFlags >= 0 ) ( _fcntlFlags );
  JASSERT ( _fcntlOwner != -1 ) ( _fcntlOwner );
  JASSERT ( _fcntlSignal >= 0 ) ( _fcntlSignal );
  JASSERT ( fcntl ( fds[0], F_SETFL, _fcntlFlags ) == 0 ) ( fds[0] ) ( _fcntlFlags ) ( JASSERT_ERRNO );
  JASSERT ( fcntl ( fds[0], F_SETOWN,_fcntlOwner ) == 0 ) ( fds[0] ) ( _fcntlOwner ) ( JASSERT_ERRNO );
  JASSERT ( fcntl ( fds[0], F_SETSIG,_fcntlSignal ) == 0 ) ( fds[0] ) ( _fcntlSignal ) ( JASSERT_ERRNO );
}

////////////
///// TCP CHECKPOINTING

void dmtcp::TcpConnection::preCheckpoint ( const std::vector<int>& fds
    , KernelBufferDrainer& drain )
{
  JASSERT ( fds.size() > 0 ) ( id() );

  if ( ( _fcntlFlags & O_ASYNC ) != 0 )
  {
    JTRACE ( "removing O_ASYNC flag during checkpoint" ) ( fds[0] ) ( id() );
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

  void dmtcp::TcpConnection::doSendHandshakes( const std::vector<int>& fds, const dmtcp::UniquePid& coordinator ){
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
  void dmtcp::TcpConnection::doRecvHandshakes( const std::vector<int>& fds, const dmtcp::UniquePid& coordinator ){
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

void dmtcp::TcpConnection::postCheckpoint ( const std::vector<int>& fds )
{
  if ( ( _fcntlFlags & O_ASYNC ) != 0 )
  {
    JTRACE ( "re-adding O_ASYNC flag" ) ( fds[0] ) ( id() );
    restoreOptions ( fds );
  }
}
void dmtcp::TcpConnection::restore ( const std::vector<int>& fds, ConnectionRewirer& rewirer )
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
      JWARNING (  (_sockDomain == AF_INET || _sockDomain == AF_UNIX ) && _sockType == SOCK_STREAM )
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
      JTRACE ( "binding socket" ) ( id() );
      JWARNING ( sock.bind ( ( sockaddr* ) &_bindAddr,_bindAddrlen ) )
        ( JASSERT_ERRNO ) ( id() )
        .Text ( "bind failed" );
      if ( tcpType() == TCP_BIND ) break;

      JTRACE ( "listening socket" ) ( id() );
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


void dmtcp::TcpConnection::restoreOptions ( const std::vector<int>& fds )
{

  typedef std::map< int, std::map< int, jalib::JBuffer > >::iterator levelIterator;
  typedef std::map< int, jalib::JBuffer >::iterator optionIterator;

  for ( levelIterator lvl = _sockOptions.begin(); lvl!=_sockOptions.end(); ++lvl )
  {
    for ( optionIterator opt = lvl->second.begin(); opt!=lvl->second.end(); ++opt )
    {
      JTRACE ( "restoring socket option" ) ( fds[0] ) ( opt->first ) ( opt->second.size() );
      int ret = _real_setsockopt ( fds[0],lvl->first,opt->first,opt->second.buffer(), opt->second.size() );
      JASSERT ( ret == 0 ) ( fds[0] ) ( opt->first ) ( opt->second.size() )
        .Text ( "restoring setsockopt failed" );
    }
  }


  //call base version (F_GETFL etc)
  Connection::restoreOptions ( fds );

}

void dmtcp::TcpConnection::doLocking ( const std::vector<int>& fds )
{
  JASSERT ( fcntl ( fds[0], F_SETOWN, getpid() ) == 0 ) ( fds[0] ) ( JASSERT_ERRNO );
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


// void dmtcp::PipeConnection::preCheckpoint(const std::vector<int>& fds
//                         , KernelBufferDrainer& drain)
// {
// }
// void dmtcp::PipeConnection::postCheckpoint(const std::vector<int>& fds)
// {
//
// }
// void dmtcp::PipeConnection::restore(const std::vector<int>&, ConnectionRewirer&)
// {
//
// }

////////////
///// PTS CHECKPOINTING

void dmtcp::PtsConnection::preCheckpoint ( const std::vector<int>& fds
    , KernelBufferDrainer& drain )
{

}
void dmtcp::PtsConnection::postCheckpoint ( const std::vector<int>& fds )
{

}
void dmtcp::PtsConnection::restore ( const std::vector<int>& fds, ConnectionRewirer& rewirer )
{
  JASSERT ( fds.size() > 0 );

  int tempfd;
  char pts_name[80];

  switch ( ( int ) type() )
  {
    case INVALID:
      //tempfd = open("/dev/null", O_RDWR);
      JTRACE("restoring invalid PTS")(id());
      return;

    case Pt_Master:
    {
      JTRACE ( "Restoring /dev/ptmx" ) ( fds[0] );

      tempfd = open ( "/dev/ptmx", O_RDWR );

      JASSERT ( tempfd >= 0 ) ( tempfd ) ( JASSERT_ERRNO )
        .Text ( "Error Opening /dev/ptmx" );

      JASSERT ( grantpt ( tempfd ) >= 0 ) ( tempfd ) ( JASSERT_ERRNO );

      JASSERT ( unlockpt ( tempfd ) >= 0 ) ( tempfd ) ( JASSERT_ERRNO );

      JASSERT ( _real_ptsname_r ( tempfd, pts_name, 80 ) == 0 ) ( tempfd ) ( JASSERT_ERRNO );

      /*      if ( jalib::Filesystem::FileExists(_symlinkFilename) )
              {
              JNOTE("File Exists");
              JASSERT(unlink(_symlinkFilename.c_str()) == 0)(pts_name)(_symlinkFilename)(JASSERT_ERRNO)
              .Text("unlink() failed");
              }
       */
      remove ( _symlinkFilename.c_str() );
      JASSERT ( symlink ( pts_name, _symlinkFilename.c_str() ) == 0 ) ( pts_name ) ( _symlinkFilename ) ( JASSERT_ERRNO )
        .Text ( "symlink() failed" );

      JASSERT ( _real_dup2 ( tempfd, fds[0] ) == fds[0] ) ( tempfd ) ( fds[0] )
        .Text ( "dup2() failed" );

      _device = pts_name;

      break;
    }
    case Pt_Slave:
    {
        if ( _device.compare ( "?" ) == 0 )
      {
        JTRACE ( "Restoring PTS ?" ) ( fds[0] );
        return;
      }

      std::string devicename = jalib::Filesystem::ResolveSymlink ( _symlinkFilename );
      JASSERT ( devicename.length() > 0 ) ( _device ) ( _symlinkFilename ) ( JASSERT_ERRNO )
        .Text ( "PTS does not exist" );

      tempfd = open ( devicename.c_str(), O_RDWR );
      JASSERT ( tempfd >= 0 ) ( tempfd ) ( devicename ) ( JASSERT_ERRNO )
        .Text ( "Error Opening PTS" );

      JASSERT ( _real_dup2 ( tempfd, fds[0] ) == fds[0] ) ( tempfd ) ( fds[0] )
        .Text ( "dup2() failed" );

      //std::string oldDeviceName = "pts["+jalib::XToString(fds[0])+"]:" + _device;
      //std::string newDeviceName = "pts["+jalib::XToString(fds[0])+"]:" + devicename;

      JTRACE ( "Restoring PTS real" ) ( devicename ) ( _symlinkFilename ) ( fds[0] );

      _device = devicename;

      break;
    }
    default:
      // should never reach here
      JASSERT ( false ).Text ( "should never reach here" );
  }
}
void dmtcp::PtsConnection::restoreOptions ( const std::vector<int>& fds )
{

}

////////////
///// FILE CHECKPOINTING

void dmtcp::FileConnection::preCheckpoint ( const std::vector<int>& fds
    , KernelBufferDrainer& drain )
{
  if (getenv(ENV_VAR_CKPT_OPEN_FILES) != NULL)
    saveFile();
}
void dmtcp::FileConnection::postCheckpoint ( const std::vector<int>& fds )
{

}
void dmtcp::FileConnection::restore ( const std::vector<int>& fds, ConnectionRewirer& rewirer )
{
  JASSERT ( fds.size() > 0 );

  JTRACE("Restoring File Connection") (_id.conId()) (_path);
  int tempfd = openFile ();

  JASSERT ( tempfd > 0 ) ( tempfd ) ( _path ) ( JASSERT_ERRNO );

  for(size_t i=0; i<fds.size(); ++i)
  {
    JASSERT ( _real_dup2 ( tempfd, fds[0] ) == fds[0] ) ( tempfd ) ( fds[0] )
      .Text ( "dup2() failed" );
  }

  JASSERT ( lseek ( fds[0], _offset, SEEK_SET ) == _offset ) 
    ( _path ) ( _offset ) ( JASSERT_ERRNO );

//     flags = O_RDWR;
//     if (!(statbuf.st_mode & S_IWUSR)) flags = O_RDONLY;
//     else if (!(statbuf.st_mode & S_IRUSR)) flags = O_WRONLY;
//     tempfd = mtcp_sys_open (linkbuf, flags, 0);
//     if (tempfd < 0) {
//       mtcp_printf ("mtcp readfiledescrs: error %d re-opening %s flags %o\n", mtcp_sy_errno, linkbuf, flags);
//       if (mtcp_sy_errno == EACCES)
//         mtcp_printf("  Permission denied.\n");
//       mtcp_abort ();
//     }
//
//     /* Move it to the original fd if it didn't coincidentally open there */
//
//     if (tempfd != fdnum) {
//       if (mtcp_sy_dup2 (tempfd, fdnum) < 0) {
//         mtcp_printf ("mtcp readfiledescrs: error %d duping %s from %d to %d\n", mtcp_sy_errno, linkbuf, tempfd, fdnum);
//         mtcp_abort ();
//       }
//       mtcp_sys_close (tempfd);
//     }
//
//     /* Position the file to its same spot it was at when checkpointed */
//
//     if (S_ISREG (statbuf.st_mode) && (mtcp_sy_lseek (fdnum, offset, SEEK_SET) != offset)) {
//       mtcp_printf ("mtcp readfiledescrs: error %d positioning %s to %ld\n", mtcp_sy_errno, linkbuf, (long)offset);
//       mtcp_abort ();
//     }

}

static void CreateDirectoryStructure(const std::string& path)
{
  size_t index = path.rfind('/');

  if (index == -1)
    return;

  std::string dir = path.substr(0, index);

  index = path.find('/');
  while (index != -1) {
    if (index > 1) {
      std::string dirName = path.substr(0, index);

      JTRACE("dirName ") (dirName);
      int res = mkdir(dirName.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
      JASSERT(res != -1 || errno==EEXIST) (dirName) (path) 
        .Text("Unable to create directory in File Path");
    }
    index = path.find('/', index+1);
  }
}

static void CopyFile(const std::string& src, const std::string& dest)
{
  //std::ifstream in(src.c_str(), std::ios::in | std::ios::binary);
  //std::ofstream out(dest.c_str(), std::ios::in | std::ios::binary);

  std::string command = "cp -f " + src + " " + dest;

  JASSERT(_real_system(command.c_str()) != -1);

  //out << in.rdbuf();
}



int dmtcp::FileConnection::openFile()
{
  int fd;

  if (!jalib::Filesystem::FileExists(_path)) {

    JTRACE("File not present, copying from saved checkpointed file") (_path);

    std::string savedFilePath = GetSavedFilePath(_path);

    JASSERT( jalib::Filesystem::FileExists(savedFilePath) ) 
      (savedFilePath) (_path) .Text("Unable to Find checkpointed copy of File");

    CreateDirectoryStructure(_path);

    JTRACE("Copying saved checkpointed file to original location") (savedFilePath) (_path);
    CopyFile(savedFilePath, _path);
  }

  fd = open(_path.c_str(), _fcntlFlags);

  JASSERT(fd != -1) (_path) (JASSERT_ERRNO);
  return fd;
}

void dmtcp::FileConnection::saveFile()
{
  std::string savedFilePath = GetSavedFilePath(_path);

  CreateDirectoryStructure(savedFilePath);

  JTRACE("Saving checkpointed copy of the file") (_path) (savedFilePath);
  CopyFile(_path, savedFilePath);
}

std::string dmtcp::FileConnection::GetSavedFilePath(const std::string& path)
{
  std::ostringstream os;
  std::string fileName;

  size_t index = path.rfind('/');
  if (index != -1)
    fileName =  path.substr(index+1);
  else
    fileName = path;

  const char* dir = getenv( ENV_VAR_CHECKPOINT_DIR );
  if ( dir == NULL ) {
    dir = get_current_dir_name();
  }

  os << dir << "/" ;

  os << CHECKPOINT_FILES_SUBDIR_PREFIX 
     << jalib::Filesystem::GetProgramName()
     << "_" << _id.pid();

  os << '/' << fileName << '_' << _id.conId();

  return os.str();
}

/////////
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
    typedef std::map< int, std::map< int, jalib::JBuffer > >::iterator levelIterator;
    typedef std::map< int, jalib::JBuffer >::iterator optionIterator;

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

  std::map< int, std::map< int, jalib::JBuffer > > _sockOptions;
}

void dmtcp::FileConnection::serializeSubClass ( jalib::JBinarySerializer& o )
{
  JSERIALIZE_ASSERT_POINT ( "dmtcp::FileConnection" );
  o & _path & _offset;
}

void dmtcp::PtsConnection::serializeSubClass ( jalib::JBinarySerializer& o )
{
  JSERIALIZE_ASSERT_POINT ( "dmtcp::PtsConnection" );
  o & _device & _symlinkFilename & _type;

  if ( o.isReader() )
  {
    dmtcp::PtsToSymlink::Instance().add ( _device,_symlinkFilename );
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

void dmtcp::PtsConnection::mergeWith ( const Connection& _that ){
  Connection::mergeWith(_that);
  const PtsConnection& that = (const PtsConnection&)_that; //Connection::_type match is checked in Connection::mergeWith
  JWARNING(_type            == that._type)           MERGE_MISMATCH_TEXT;
  JWARNING(_symlinkFilename == that._symlinkFilename)MERGE_MISMATCH_TEXT;
  JWARNING(_device          == that._device)         MERGE_MISMATCH_TEXT;
}

void dmtcp::FileConnection::mergeWith ( const Connection& that ){
  Connection::mergeWith(that);
  JWARNING(false)(id()).Text("We shouldn't be merging file connections, should we?");
}

////////////
///// STDIO CHECKPOINTING

void dmtcp::StdioConnection::preCheckpoint ( const std::vector<int>& fds, KernelBufferDrainer& drain ){
  JTRACE("Checkpointing stdio")(fds[0])(id());
}
void dmtcp::StdioConnection::postCheckpoint ( const std::vector<int>& fds ){
  //nothing
}
void dmtcp::StdioConnection::restore ( const std::vector<int>& fds, ConnectionRewirer& ){
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
    JWARNING ( _real_dup2 ( oldFd, fd ) == fd ) ( oldFd ) ( fd ) ( JASSERT_ERRNO );
  }
}
void dmtcp::StdioConnection::restoreOptions ( const std::vector<int>& fds ){
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
  restore(std::vector<int>(1,newFd), ignored);
}

