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
#include "connectionmanager.h"

#include  "../jalib/jfilesystem.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jassert.h"
#include "protectedfds.h"
#include "util.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

static dmtcp::string _procFDPath ( int fd )
{
  return "/proc/self/fd/" + jalib::XToString ( fd );
}

static bool _isBadFd ( int fd )
{
  dmtcp::string device = jalib::Filesystem::ResolveSymlink ( _procFDPath ( fd ) );
  return ( device == "" );
}

dmtcp::ConnectionList& dmtcp::ConnectionList::instance()
{
  static ConnectionList inst; return inst;
}

dmtcp::KernelDeviceToConnection& dmtcp::KernelDeviceToConnection::instance()
{
  static KernelDeviceToConnection inst; return inst;
}

dmtcp::UniquePtsNameToPtmxConId& dmtcp::UniquePtsNameToPtmxConId::instance()
{
  static UniquePtsNameToPtmxConId inst; return inst;
}

dmtcp::ConnectionList::ConnectionList() {}

dmtcp::KernelDeviceToConnection::KernelDeviceToConnection() {}

dmtcp::ConnectionToFds::ConnectionToFds ( KernelDeviceToConnection& source )
{
  dmtcp::vector<int> fds = jalib::Filesystem::ListOpenFds();
  JTRACE("Creating Connection->FD mapping")(fds.size());
  KernelDeviceToConnection::instance().dbgSpamFds();
  _procname = jalib::Filesystem::GetProgramName();
  _hostname = jalib::Filesystem::GetCurrentHostname();
  _inhostname = jalib::Filesystem::GetCurrentHostname();
  _upid = UniquePid::ThisProcess();
  _uppid = UniquePid::ParentProcess();

  for ( size_t i=0; i<fds.size(); ++i ) {
    if ( _isBadFd ( fds[i] ) ) continue;
    if ( ProtectedFDs::isProtected ( fds[i] ) ) continue;

#ifdef IBV
    dmtcp::string device = dmtcp::KernelDeviceToConnection::instance().fdToDevice(fds[i]);

    if(!Util::strStartsWith(device, "/dev/infiniband/") &&
       !Util::strStartsWith(device, "infinibandevent:"))
#endif
    {
      Connection* con = &source.retrieve ( fds[i] );
      _table[con->id() ].push_back ( fds[i] );
    }
  }
}

void dmtcp::ConnectionToFds::erase ( const ConnectionIdentifier& conId )
{
  // JTRACE("erasing connection from ConnectionToFds") (conId);
  // Find returns iterator 'it' w/ 0 or more elts, with first elt matching key.
  iterator it = _table.find(conId);
  JASSERT( it != _table.end() );
  _table.erase(it);
}

dmtcp::Connection& dmtcp::KernelDeviceToConnection::retrieve ( int fd )
{
  dmtcp::string device = fdToDevice ( fd );
  JASSERT ( device.length() > 0 ) ( fd ).Text ( "invalid fd" );
  iterator i = _table.find ( device );

  if (i == _table.end() && Util::strStartsWith(device, "socket:[")) {
    JWARNING ( false ) ( fd ) ( device ) ( _table.size() )
      .Text ( "failed to find connection for fd. Assuming External Socket." );
    TcpConnection *con = new TcpConnection(-1, -1, -1);
    con->markExternalConnect();
    create(fd, con);
    i = _table.find ( device );
  }

  JASSERT ( i != _table.end() ) ( fd ) ( device ) ( _table.size() ).Text ( "failed to find connection for fd" );
  return ConnectionList::instance() [i->second];
}

void dmtcp::KernelDeviceToConnection::create ( int fd, Connection* c )
{
  ConnectionList::instance().add ( c );

  dmtcp::string device = fdToDevice ( fd, true );

  JTRACE ( "device created" ) ( fd ) ( device ) ( c->id() );

  JASSERT ( device.length() > 0 ) ( fd ).Text ( "invalid fd" );
  iterator i = _table.find ( device );
  JASSERT ( i == _table.end() ) ( fd ) ( device ).Text ( "connection already exists" );
  _table[device] = c->id();
}


void dmtcp::KernelDeviceToConnection::createPtyDevice ( int fd, dmtcp::string device, Connection* c )
{
  ConnectionList::instance().add ( c );

  JTRACE ( "device created" ) ( fd ) ( device ) ( c->id() );

  JASSERT ( device.length() > 0 ) ( fd ).Text ( "invalid fd" );

  /* FIXME: The following JWARNING should be re-enabled */
  //iterator i = _table.find ( device );
  //JWARNING ( i == _table.end() ) ( fd ) ( device ).Text ( "connection already exists" );

  _table[device] = c->id();
}

dmtcp::string dmtcp::KernelDeviceToConnection::fdToDevice ( int fd, bool noOnDemandConnection )
{
  //gather evidence
  errno = 0;
  dmtcp::string device = jalib::Filesystem::ResolveSymlink ( _procFDPath ( fd ) );
  bool isBadFd = ( device == "" );

  if ( isBadFd )
  {
    JTRACE ( "bad fd (we expect one of these lines)" ) ( fd );
    JASSERT ( device == "" ) ( fd ) ( _procFDPath ( fd ) ) ( device ) ( JASSERT_ERRNO )
      .Text ( "expected badFd not to have a proc entry..." );

    return "";
  }
  //while (fd==4);

  bool isFile  = ( device[0] == '/' );

  bool isTty = (device.compare("/dev/tty") == 0);

  bool isPtmx  = (device.compare("/dev/ptmx") == 0);
  bool isPts   = Util::strStartsWith(device, "/dev/pts/");

  bool isBSDMaster  = (Util::strStartsWith(device, "/dev/pty") &&
                       device.compare("/dev/pty") != 0);
  bool isBSDSlave   = (Util::strStartsWith(device, "/dev/tty") &&
                       device.compare("/dev/tty")) != 0;
  bool isEpoll = (device.compare("anon_inode:[eventpoll]")==0);
  bool isEventFd = (device.compare("anon_inode:[eventfd]")==0);
  bool isSignalFd = (device.compare("anon_inode:[signalfd]")==0);
#ifdef IBV
  bool isInfinibandDevice   = Util::strStartsWith(device, "/dev/infiniband/");
  bool isInfinibandConnection   = Util::strStartsWith(device, "infinibandevent:");

  if ( isInfinibandDevice || isInfinibandConnection ) {
    return device;
  } else
#endif
  if ( isTty ) {
    dmtcp::string deviceName = "tty:" + device;

    if(noOnDemandConnection)
      return deviceName;

    iterator i = _table.find ( deviceName );

    if ( i == _table.end() )
    {
      JTRACE("Creating /dev/tty connection [on-demand]");
      int type = PtyConnection::PTY_DEV_TTY;

      Connection * c = new PtyConnection ( device, device, type );
      createPtyDevice ( fd, deviceName, c );
    }

    return deviceName;

  } else if ( isPtmx ) {
    char ptsName[21];
    JASSERT(_real_ptsname_r(fd, ptsName, 21) == 0) (JASSERT_ERRNO);

    string ptsNameStr = ptsName;

    dmtcp::string deviceName = "ptmx[" + ptsNameStr + "]:" + device;

    if(noOnDemandConnection)
      return deviceName;

    iterator i = _table.find ( deviceName );
    JASSERT ( i != _table.end() ) ( fd ) ( device ) ( deviceName ) ( ptsNameStr )
      .Text ("Device not found in connection list");

    return deviceName;

  } else if ( isPts ) {
    dmtcp::string deviceName = "pts:" + device;

    if(noOnDemandConnection)
      return deviceName;

    iterator i = _table.find ( deviceName );

    if ( i == _table.end() )
    {
      JWARNING(false) .Text("PTS Device not found");
      int type;
      dmtcp::string currentTty = jalib::Filesystem::GetControllingTerm();

      JTRACE( "Controlling Terminal") (currentTty);

      if ( currentTty.compare(device) == 0 ) {
        type = dmtcp::PtyConnection::PTY_CTTY;
        JTRACE ( "creating TTY connection [on-demand]" )
          ( deviceName );

        Connection * c = new PtyConnection ( device, device, type );
        createPtyDevice ( fd, deviceName, c );
      } else {
        JASSERT ( false ) ( fd ) ( device )
          .Text ("PTS Device not found in connection list");
      }
    }

    return deviceName;

  } else if ( isBSDMaster ) {
    dmtcp::string deviceName = "BSDMasterPty:" + device;
    dmtcp::string slaveDeviceName = device.replace(0, strlen("/dev/pty"), "/dev/tty");

    if(noOnDemandConnection)
      return deviceName;

    iterator i = _table.find ( deviceName );
    if ( i == _table.end() )
    {
      JTRACE ( "creating BSD Master Pty connection [on-demand]" )
        ( deviceName ) ( slaveDeviceName );

      int type = dmtcp::PtyConnection::PTY_BSD_MASTER;
      Connection * c = new dmtcp::PtyConnection ( device, type );

      ConnectionList::instance().add ( c );
      _table[deviceName] = c->id();
    }

    return deviceName;

  } else if ( isBSDSlave ) {
    dmtcp::string deviceName = "BSDSlave:" + device;
    dmtcp::string masterDeviceName = device.replace(0, strlen("/dev/tty"), "/dev/pty");


    if(noOnDemandConnection)
      return deviceName;

    iterator i = _table.find ( deviceName );
    if ( i == _table.end() )
    {
      JTRACE ( "creating BSD Slave Pty connection [on-demand]" )
        ( deviceName ) ( masterDeviceName );

      int type = dmtcp::PtyConnection::PTY_BSD_SLAVE;
      Connection * c = new dmtcp::PtyConnection ( device, type );

      ConnectionList::instance().add ( c );
      _table[deviceName] = c->id();
    }

    return deviceName;

  } else if ( isFile ) {
    // Can be file or FIFO channel
    struct stat buf;
    stat(device.c_str(),&buf);

    if (!jalib::Filesystem::FileExists(device)) {

      // Make sure _path ends with DELETED_FILE_SUFFIX
      JASSERT(Util::strEndsWith(device, DELETED_FILE_SUFFIX));

      dmtcp::string deviceName = "file["+jalib::XToString ( fd ) +"]:" + device;

      if(noOnDemandConnection)
        return deviceName;

      iterator i = _table.find ( deviceName );
      if ( i == _table.end() )
      {
        JTRACE ( "creating file connection [on-demand]" ) ( deviceName );
        off_t offset = lseek ( fd, 0, SEEK_CUR );
        Connection * c = new FileConnection ( device, offset, FileConnection::FILE_DELETED );
        ConnectionList::instance().add ( c );
        _table[deviceName] = c->id();
      }

      return deviceName;
    } else if (S_ISREG(buf.st_mode) || S_ISCHR(buf.st_mode) ||
               S_ISDIR(buf.st_mode) || S_ISBLK(buf.st_mode)) {
      /* /dev/null is a character special file (non-regular file) */
      dmtcp::string deviceName = "file["+jalib::XToString ( fd ) +"]:" + device;

      if(noOnDemandConnection)
        return deviceName;

      iterator i = _table.find ( deviceName );
      if ( i == _table.end() )
      {
        JTRACE ( "creating file connection [on-demand]" ) ( deviceName );
        off_t offset = lseek ( fd, 0, SEEK_CUR );
        Connection * c = new FileConnection ( device, offset );
        ConnectionList::instance().add ( c );
        _table[deviceName] = c->id();
      }

      return deviceName;

    } else if (S_ISFIFO(buf.st_mode)){
      dmtcp::string deviceName = "fifo["+jalib::XToString ( fd ) +"]:" + device;

      if(noOnDemandConnection)
        return deviceName;

      iterator i = _table.find ( deviceName );
      if (i == _table.end())
      {
        JTRACE ( "creating fifo connection [on-demand]" ) ( deviceName );
        Connection * c = new FifoConnection( device );
        ConnectionList::instance().add( c );
        _table[deviceName] = c->id();
        return deviceName;
      } else {
        return deviceName;
      }
    } else {
      JASSERT(false) (device) .Text("Unimplemented file type.");
    }
  }
  else if (isEventFd) {
      dmtcp::string deviceName = "eventfd["+jalib::XToString(fd)+"]:"+device;
      if (noOnDemandConnection)
          return deviceName;
      iterator i = _table.find(deviceName);
      if (i == _table.end())
      {
          JTRACE("creating eventfd connection [on-demand]") (deviceName);
          Connection *c = new EventFdConnection(0, 0);
          ConnectionList::instance().add(c);
          _table[deviceName] = c->id();
          return deviceName;
      }
      else {
          return deviceName;
      }
  }
  else if (isSignalFd) {
      dmtcp::string deviceName = "signalfd["+jalib::XToString(fd)+"]:"+device;
      if (noOnDemandConnection)
          return deviceName;
      iterator i = _table.find(deviceName);
      if (i == _table.end())
      {
          JTRACE("creating signalfd connection [on-demand]") (deviceName);
          Connection *c = new SignalFdConnection(0, NULL, 0);
          ConnectionList::instance().add(c);
          _table[deviceName] = c->id();
          return deviceName;
      }
      else {
          return deviceName;
      }
  }
  else if (isEpoll) {
      dmtcp::string deviceName = "epoll["+jalib::XToString(fd)+"]:"+device;
      if (noOnDemandConnection)
          return deviceName;
      iterator i = _table.find(deviceName);
      if (i == _table.end())
      {
          JTRACE("creating eventfd connection [on-demand]") (deviceName);
          Connection *c = new EpollConnection(5);
          ConnectionList::instance().add(c);
          _table[deviceName] = c->id();
          return deviceName;
      }
      else {
          return deviceName;
      }
  }
  //JWARNING(false) (device) .Text("Unimplemented Connection Type.");
  return device;
}

// TODO: To properly implement STL erase(), it should return the next iterator.
void dmtcp::ConnectionList::erase ( iterator i )
{
  Connection * con = i->second;
  //JTRACE ( "deleting stale connection..." ) ( con->id() );
  KernelDeviceToConnection::instance().erase( i->first );
  _connections.erase ( i );
  delete con;
}

void dmtcp::ConnectionList::erase ( dmtcp::ConnectionIdentifier& key )
{
  iterator i = _connections.find(key);
  JASSERT(i != _connections.end());
  erase(i);
}

// TODO: To properly implement STL erase(), it should return the next iterator.
void dmtcp::KernelDeviceToConnection::erase( const ConnectionIdentifier& con )
{
  for(iterator i = _table.begin(); i!=_table.end(); ++i){
    if(i->second == con){
      dmtcp::string k = i->first;
      //JTRACE("removing device->con mapping")(k)(con);
      _table.erase(k);
      return;
    }
  }
  JTRACE("WARNING:: failed to find connection in table to erase it")(con);
}

dmtcp::string
dmtcp::KernelDeviceToConnection::getDevice( const ConnectionIdentifier& con )
{
  for(iterator i = _table.begin(); i!=_table.end(); ++i){
    if(i->second == con){
      return i->first;
    }
  }
  return "";
}

//called when a device name changes
void dmtcp::KernelDeviceToConnection::redirect( int fd, const ConnectionIdentifier& id ){
  //first delete the old one
  erase(id);

  //now add the new fd
  dmtcp::string device = fdToDevice ( fd, true );
  JTRACE ( "redirecting device" )(fd)(device) (id);
  JASSERT ( device.length() > 0 ) ( fd ).Text ( "invalid fd" );
  iterator i = _table.find ( device );
  JASSERT ( i == _table.end() ) ( fd ) ( device ).Text ( "connection already exists" );
  _table[device] = id;
}

void dmtcp::KernelDeviceToConnection::dbgSpamFds()
{
#ifdef DEBUG
  dmtcp::ostringstream out;
  out << "\n\tfd -> device \t\t -> inTable\n";
  dmtcp::vector<int> fds = jalib::Filesystem::ListOpenFds();
  for ( size_t i=0; i<fds.size(); ++i )
  {
    if ( _isBadFd ( fds[i] ) ) continue;
    if(ProtectedFDs::isProtected( fds[i] )) continue;
    dmtcp::string device = fdToDevice ( fds[i], true );
    bool exists = ( _table.find ( device ) != _table.end() );
    out << "\t" << fds[i] << " -> "  << device << " inTable=" << exists << "\n";
  }
  JTRACE("Listings FDs...") (out.str());
#endif
}


//fix things up post-restart (all or KernelDevices have changed)
dmtcp::KernelDeviceToConnection::KernelDeviceToConnection ( const ConnectionToFds& source )
{
  JTRACE ( "reconstructing table..." );
  for ( ConnectionToFds::const_iterator i = source.begin()
        ; i!=source.end()
        ; ++i )
  {
    ConnectionIdentifier con = i->first;
    const dmtcp::vector<int>& fds = i->second;
    JWARNING(fds.size() > 0)(con);
    if(fds.size()>0){
      dmtcp::string device = fdToDevice ( fds[0], true );
      _table[device] = con;
#ifdef DEBUG
      //double check to make sure all fds have same device
      Connection *c = &(ConnectionList::instance()[con]);
      for ( size_t i=1; i<fds.size(); ++i ) {
        if ( c->conType() != Connection::FILE ) {
          JASSERT ( device == fdToDevice ( fds[i] ) )
            ( device ) ( fdToDevice ( fds[i] ) ) ( fds[i] ) ( fds[0] );
        } else {
          dmtcp::string filePath =
            jalib::Filesystem::ResolveSymlink ( _procFDPath ( fds[i] ) );
          JASSERT ( filePath == ((FileConnection *)c)->filePath() )
            ( fds[i] ) ( filePath ) ( fds[0] ) ( ((FileConnection *)c)->filePath() );
        }
      }
#endif
    }
  }
#ifdef DEBUG
  JTRACE ( "new fd table..." );
  dbgSpamFds();
#endif

}

void dmtcp::ConnectionList::serialize ( jalib::JBinarySerializer& o )
{
  JSERIALIZE_ASSERT_POINT ( "dmtcp::ConnectionList:" );

  size_t numCons = _connections.size();
  o & numCons;

  if ( o.isWriter() )
  {
    for ( iterator i=_connections.begin(); i!=_connections.end(); ++i )
    {

      ConnectionIdentifier key = i->first;
      Connection& con = *i->second;
      int type = con.conType();

      JSERIALIZE_ASSERT_POINT ( "[StartConnection]" );
      o & key & type;
      con.serialize ( o );
      JSERIALIZE_ASSERT_POINT ( "[EndConnection]" );
    }
  }
  else
  {
    while ( numCons-- > 0 )
    {

      ConnectionIdentifier key;
      int type = -1;
      Connection* con = NULL;

      JSERIALIZE_ASSERT_POINT ( "[StartConnection]" );
      o & key & type;

      switch ( type )
      {
      case Connection::TCP:
        con = new TcpConnection ( -1,-1,-1 );
        break;
      case Connection::FILE:
        con = new FileConnection ( "?", -1 );
        break;
        //             case Connection::PIPE:
        //                 con = new PipeConnection();
        //                 break;
      case Connection::FIFO:
        // sleep(15);
        con = new FifoConnection ( "?" );
        break;
      case Connection::PTY:
        con = new PtyConnection();
        break;
      case Connection::STDIO:
        con = new StdioConnection();
        break;
      case Connection::EPOLL:
        con = new EpollConnection(5); //dummy val
        break;
      case Connection::EVENTFD:
        con = new EventFdConnection(0, 0); //dummy val
        break;
      case Connection::SIGNALFD:
        con = new SignalFdConnection(0, NULL, 0); //dummy val
        break;
      default:
        JASSERT ( false ) ( key ) ( o.filename() ).Text ( "unknown connection type" );
      }

      JASSERT( con != NULL )(key);

      con->serialize ( o );

      ConnectionMapT::const_iterator i = _connections.find(key);
      if(i != _connections.end())
      {
        JTRACE("merging connections from two restore targets")(key)(type);
        con->mergeWith(*(i->second));
      }

      _connections[key] = con;

      JSERIALIZE_ASSERT_POINT ( "[EndConnection]" );
    }

  }

  JSERIALIZE_ASSERT_POINT ( "EndConnectionList" );
}

//examine /proc/self/fd for unknown connections
void dmtcp::ConnectionList::scanForPreExisting()
{
  dmtcp::vector<int> fds = jalib::Filesystem::ListOpenFds();
  for ( size_t i=0; i<fds.size(); ++i )
  {
    if ( _isBadFd ( fds[i] ) ) continue;
    if ( ProtectedFDs::isProtected ( fds[i] ) ) continue;
    KernelDeviceToConnection::instance().handlePreExistingFd ( fds[i] );
  }
}

void dmtcp::KernelDeviceToConnection::handlePreExistingFd ( int fd )
{
  //this has the side effect of on-demand creating everything except sockets
  dmtcp::string device =
    KernelDeviceToConnection::instance().fdToDevice ( fd, true );

  JTRACE ( "scanning pre-existing device" ) ( fd ) ( device );

  //so if it doesn't exist it must be a socket
  if ( _table.find ( device ) == _table.end() )
  {
    if ( Util::strStartsWith(device, "file"))
    {
      device = KernelDeviceToConnection::instance().fdToDevice (fd);
    }
    else if ( device.compare("/dev/tty") == 0 )
    {
      dmtcp::string deviceName = "tty:" + device;
      JTRACE ( "Found pre-existing /dev/tty" )
        ( fd ) ( deviceName );

      int type = dmtcp::PtyConnection::PTY_DEV_TTY;

      PtyConnection *con = new PtyConnection ( device, device, type );
      create ( fd, con );
    }
    else if ( Util::strStartsWith(device, "/dev/pts/"))
    {
      dmtcp::string deviceName = "pts["+jalib::XToString ( fd ) +"]:" + device;
      JNOTE ( "Found pre-existing PTY connection, "
              "will be restored as current TTY" )
        ( fd ) ( deviceName );

      int type = dmtcp::PtyConnection::PTY_CTTY;

      PtyConnection *con = new PtyConnection ( device, device, type );
      create ( fd, con );
    }
    else if (fd <= 2)
    {
      create(fd, new StdioConnection(fd));
    }
    else
    {
      JNOTE ( "found pre-existing socket... will not be restored" )
        ( fd ) ( device );
      TcpConnection* con = new TcpConnection ( 0, 0, 0 );
      con->markPreExisting();
      create ( fd, con );
    }
  }
}

void dmtcp::KernelDeviceToConnection::prepareForFork ( )
{
  dmtcp::vector<int> fds = jalib::Filesystem::ListOpenFds();
  JTRACE("Scanning /proc/self/fd for new connections. New connections will be created");
  for ( size_t i=0; i<fds.size(); ++i )
  {
    if ( _isBadFd ( fds[i] ) ) continue;
    if ( ProtectedFDs::isProtected ( fds[i] ) ) continue;
    dmtcp::string device = fdToDevice ( fds[i] );
  }
}

void dmtcp::ConnectionToFds::serialize ( jalib::JBinarySerializer& o )
{
  JSERIALIZE_ASSERT_POINT ( "dmtcp-serialized-connection-table!v0.07" );
  ConnectionList::instance().serialize ( o );
  JSERIALIZE_ASSERT_POINT ( "dmtcp::ConnectionToFds:" );

  // Current process information
  o & _procname & _inhostname & _upid & _uppid;

  size_t numCons = _table.size();
  o & numCons;

  if ( o.isWriter() )
  {
    _hostname = jalib::Filesystem::GetCurrentHostname();
    o & _hostname;
    JTRACE("Writing hostname to checkpoint file")
      (_hostname) (_inhostname) (_procname) (_uppid);

    // Save connections
    for ( iterator i=_table.begin(); i!=_table.end(); ++i )
    {
      JSERIALIZE_ASSERT_POINT ( "CFdEntry:" );
      ConnectionIdentifier key = i->first;
      dmtcp::vector<int>& val = i->second;
      o & key & val;
      JASSERT ( val.size() >0 ) (key) ( o.filename() ).Text ( "would write empty fd list" );
    }
  }
  else
  {
    o & _hostname;
    // Save connections
    while ( numCons-- > 0 )
    {
      JSERIALIZE_ASSERT_POINT ( "CFdEntry:" );
      ConnectionIdentifier key;
      dmtcp::vector<int> val;
      o & key & val;
      JWARNING ( val.size() >0 ) (key) ( o.filename() ).Text ( "reading empty fd list" );
      _table[key]=val;
    }
  }
  JSERIALIZE_ASSERT_POINT ( "EOF" );
}

void dmtcp::KernelDeviceToConnection::serialize ( jalib::JBinarySerializer& o )
{
  JSERIALIZE_ASSERT_POINT ( "dmtcp-serialized-exec-lifeboat!v0.07" );
  ConnectionIdentifier::serialize(o);
  ConnectionList::instance().serialize ( o );
  JSERIALIZE_ASSERT_POINT ( "dmtcp::KernelDeviceToConnection:" );

  size_t numCons = _table.size();
  o & numCons;

  // Save/Restore parent process UniquePid
  // parentProcess() is for inspection tools
  o & UniquePid::ParentProcess();

  if ( o.isWriter() )
  {
    for ( iterator i=_table.begin(); i!=_table.end(); ++i )
    {
      JSERIALIZE_ASSERT_POINT ( "KDEntry:" );
      dmtcp::string key = i->first;
      ConnectionIdentifier val = i->second;
      o & key & val;
    }
  }
  else
  {
    while ( numCons-- > 0 )
    {
      JSERIALIZE_ASSERT_POINT ( "KDEntry:" );
      dmtcp::string key = "?";
      ConnectionIdentifier val;
      o & key & val;
      _table[key] = val;
      //JTRACE ("Serializing") (key) (val);
    }

  }

  JSERIALIZE_ASSERT_POINT ( "EOF" );
}


dmtcp::Connection& dmtcp::ConnectionList::operator[] ( const ConnectionIdentifier& id )
{
  //  dmtcp::cout << "Operator [], conId=" << id << "\n";
  JASSERT ( _connections.find ( id ) != _connections.end() ) ( id )
  .Text ( "Unknown connection" );
  //  dmtcp::cout << "Operator [], found: " << (_connections.find ( id ) != _connections.end())
  //            << "\n";
  //  dmtcp::cout << "Operator [], Result: conId=" << _connections[id]->id() << "\n";
  return *_connections[id];
}

void dmtcp::ConnectionList::add ( Connection* c )
{
  JWARNING ( _connections.find ( c->id() ) == _connections.end() ) ( c->id() )
  .Text ( "duplicate connection" );
  _connections[c->id()] = c;
}

int dmtcp::SlidingFdTable::getFdFor ( const ConnectionIdentifier& con )
{
  //is our work already done?
  if ( _conToFd.find ( con ) != _conToFd.end() )
    return _conToFd[con];

  //find a free fd
  int newfd;
  while ( isInUse ( newfd = _nextFd++ ) ) {}

  //ad it
  _conToFd[con] = newfd;
  _fdToCon[newfd]  = con;

  //JTRACE ( "allocated fd for connection" ) ( newfd ) ( con );

  return newfd;
}
void dmtcp::SlidingFdTable::freeUpFd ( int fd )
{
  //is that FD in use?
  if ( _fdToCon.find ( fd ) == _fdToCon.end() )
    return;

  ConnectionIdentifier con = _fdToCon[fd];

  if ( con == ConnectionIdentifier::Null() )
    return;

  //find a free fd
  int newfd;
  while ( isInUse ( newfd = _nextFd++ ) ) {}

  JTRACE ( "sliding fd for connection" ) ( fd ) ( newfd ) ( con );

  //do the change
  changeFd ( fd, newfd );

  //update table
  _conToFd[con] = newfd;
  _fdToCon[newfd]  = con;
  _fdToCon[fd] = ConnectionIdentifier::Null();
}
bool dmtcp::SlidingFdTable::isInUse ( int fd ) const
{
  if ( _fdToCon.find ( fd ) != _fdToCon.end() )
    return true;
  //double check with the filesystem
  dmtcp::string device = jalib::Filesystem::ResolveSymlink ( _procFDPath ( fd ) );
  return device != "";
}
void dmtcp::SlidingFdTable::changeFd ( int oldfd, int newfd )
{
  if ( oldfd == newfd ) return;
  JASSERT ( _real_dup2 ( oldfd,newfd ) == newfd ) ( oldfd ) ( newfd ).Text ( "dup2() failed" );
  JASSERT ( _real_close ( oldfd ) == 0 ) ( oldfd ).Text ( "close() failed" );
}

void dmtcp::SlidingFdTable::closeAll()
{
  for ( dmtcp::map< ConnectionIdentifier, int >::iterator i=_conToFd.begin()
        ; i!=_conToFd.end()
        ; ++i )
  {
    Connection& con = ConnectionList::instance() [i->first];
    if (con.conType() == Connection::EPOLL) {
      JTRACE("Delete epoll Connection");
      //continue;
    }
    JWARNING ( _real_close ( i->second ) ==0 ) ( i->second ) ( JASSERT_ERRNO );
  }
  _conToFd.clear();
}

dmtcp::Connection& dmtcp::UniquePtsNameToPtmxConId::retrieve ( dmtcp::string str )
{
  iterator i = _table.find ( str );
  JASSERT ( i != _table.end() ) ( str ) ( _table.size() ).Text ( "failed to find connection for fd" );
  return ConnectionList::instance() [i->second];
}


dmtcp::string dmtcp::UniquePtsNameToPtmxConId::retrieveCurrentPtsDeviceName ( dmtcp::string str )
{
  iterator i = _table.find ( str );
  JASSERT ( i != _table.end() ) ( str ) ( _table.size() ).Text ( "failed to find connection for fd" );
  Connection* c = &(ConnectionList::instance() [i->second]);

  PtyConnection* ptmxConnection = (PtyConnection *)c;

  JASSERT( ptmxConnection->ptyType() == dmtcp::PtyConnection::PTY_MASTER );

  return ptmxConnection->ptsName();
}


/*
dmtcp::PtsToSymlink::PtsToSymlink() { }

dmtcp::PtsToSymlink& dmtcp::PtsToSymlink::instance()
{
  static PtsToSymlink inst; return inst;
}

void dmtcp::PtsToSymlink::add ( dmtcp::string device, dmtcp::string filename )
{
  //    JWARNING(_table.find(device) == _table.end())(device)
  //            .Text("duplicate connection");
  _table[device] = filename;
}

void dmtcp::PtsToSymlink::replace ( dmtcp::string oldDevice, dmtcp::string newDevice )
{
  iterator i = _table.find ( oldDevice );
  JASSERT ( i != _table.end() )( oldDevice ).Text ( "old device not found" );
  dmtcp::string filename = _table[oldDevice];
  _table.erase ( i );
  _table[newDevice] = filename;
}

dmtcp::string dmtcp::PtsToSymlink::getFilename ( dmtcp::string device )
{
  dmtcp::string filename = "?";
  iterator i = _table.find ( device );
  if ( i != _table.end() )
  {
    filename = _table[device];
  }
  return filename;
}

bool dmtcp::PtsToSymlink::exists( dmtcp::string device )
{
  dmtcp::string filename = getFilename(device);
  if (filename.compare("?") == 0){
    return false;
  }
  return true;
}
*/

pid_t dmtcp::ConnectionToFds::ext_decomp_pid = -1;
static void close_ckpt_to_read(const int fd)
{
    int status;
    int rc;
    int pid = dmtcp::ConnectionToFds::ext_decomp_pid;
    if( pid != -1 ) {
      // First close fd to let decompressor know that we want to close it
      while (-1 == (rc = close(fd)) && errno == EINTR) ;
      JASSERT (rc != -1) ("close:") (JASSERT_ERRNO);

      // Kill the decompressor process
      // 1. Send SIGTERM to give the decompressor a chance to clean up
      JASSERT (kill(pid, SIGTERM) != -1)("kill:") (JASSERT_ERRNO);
      // 2. Wait 3 seconds for decompressor termination
      rc = 0;
      for(int i = 0; (i < 3000 && rc != pid); i++){
        struct timespec sleepTime = {0, 1*1000*1000};
        nanosleep(&sleepTime, NULL);
        rc = waitpid(pid, &status, WNOHANG);
      }
      // 3. If the decompressor process still exists
      if( rc != pid ){
        if( (rc = kill(pid, SIGKILL)) == -1 && errno != ESRCH ){
          // process exists but we failed to kill it
          JASSERT (rc != -1)("kill:") (JASSERT_ERRNO);
        }else{
          // process exists and it was SIGKILL'ed. Endless wait for exit.
          while(0 == (rc = waitpid(pid,&status, WNOHANG))){
            struct timespec sleepTime = {0, 1*1000*1000};
            nanosleep(&sleepTime, NULL);
          }
        }
        JASSERT (rc == pid) ("waitpid:") (JASSERT_ERRNO);
      }
      dmtcp::ConnectionToFds::ext_decomp_pid = -1;
    }
}

// Define DMTCP_OLD_PCLOSE to get back the old buggy version.
// Remove the old version when satisfied this is better.
#ifndef DMTCP_OLD_PCLOSE
// Copied from mtcp/mtcp_restart.c.
#define DMTCP_MAGIC_FIRST 'D'
#define GZIP_FIRST 037
#ifdef HBICT_DELTACOMP
#define HBICT_FIRST 'H'
#endif


char *mtcp_executable_path(char *filename);
static char first_char(const char *filename)
{
    int fd, rc;
    char c;

    fd = open(filename, O_RDONLY);
    JASSERT(fd >= 0)(filename).Text("ERROR: Cannot open filename");

    rc = _real_read(fd, &c, 1);
    JASSERT(rc == 1)(filename).Text("ERROR: Error reading from filename");

    close(fd);
    return c;
}

// Copied from mtcp/mtcp_restart.c.
// Let's keep this code close to MTCP code to avoid maintenance problems.
// MTCP code in:  mtcp/mtcp_restart.c:open_ckpt_to_read()
// A previous version tried to replace this with popen, causing a regression:
//   (no call to pclose, and possibility of using a wrong fd).
// Returns fd; sets dmtcp::ext_decomp_pid::ConnectionToFds, if checkpoint was compressed.
static int open_ckpt_to_read(const char *filename)
{
    int fd;
    int fds[2];
    char fc;
    const char *decomp_path;
    const char **decomp_args;
    const char *gzip_path = "gzip";
    static const char * gzip_args[] = { "gzip", "-d", "-", NULL };
#ifdef HBICT_DELTACOMP
    const char *hbict_path = "hbict";
    static const char *hbict_args[] = { "hbict", "-r", NULL };
#endif
    pid_t cpid;

    fc = first_char(filename);
    fd = open(filename, O_RDONLY);
    JASSERT(fd>=0)(filename).Text("Failed to open file.");

    if(fc == DMTCP_MAGIC_FIRST) /* no compression */
        return fd;
#ifdef HBICT_DELTACOMP
    else if (fc == GZIP_FIRST || fc == HBICT_FIRST){ /* External compression */
#else
    else if(fc == GZIP_FIRST){ /* gzip */
#endif
        if( fc == GZIP_FIRST ){
          decomp_path = gzip_path;
          decomp_args = gzip_args;
        }
#ifdef HBICT_DELTACOMP
        else{
          decomp_path = hbict_path;
          decomp_args = hbict_args;
        }
#endif

        JASSERT(pipe(fds) != -1)(filename).Text("Cannot create pipe to execute gunzip to decompress checkpoint file!");

        cpid = _real_fork();

        JASSERT(cpid != -1).Text("ERROR: Cannot fork to execute gunzip to decompress checkpoint file!");
        if(cpid > 0) /* parent process */
        {
           JTRACE ( "created child process to uncompress checkpoint file")(cpid);
            dmtcp::ConnectionToFds::ext_decomp_pid = cpid;
            close(fd);
            close(fds[1]);
            return fds[0];
        }
        else /* child process */
        {
           JTRACE ( "child process, will exec into external de-compressor");
            fd = dup(dup(dup(fd)));
            fds[1] = dup(fds[1]);
            close(fds[0]);
            JASSERT(fd != -1);
            JASSERT(dup2(fd, STDIN_FILENO) == STDIN_FILENO);
            close(fd);
            JASSERT(dup2(fds[1], STDOUT_FILENO) == STDOUT_FILENO);
            close(fds[1]);
            _real_execvp(decomp_path, (char **)decomp_args);
            JASSERT(decomp_path!=NULL)(decomp_path).Text("Failed to launch gzip.");
            /* should not get here */
            JASSERT(false)("ERROR: Decompression failed!  No restoration will be performed!  Cancelling now!");
            abort();
        }
    } else /* invalid magic number */
        JASSERT(false).Text("ERROR: Invalid magic number in this checkpoint file!");
    // NOT_REACHED
    return -1;
}

// See comments above for open_ckpt_to_read()
int dmtcp::ConnectionToFds::openDmtcpCheckpointFile(const dmtcp::string& path){
  // Function also sets dmtcp::ext_decomp_pid::ConnectionToFds
  int fd = open_ckpt_to_read( path.c_str() );
  // The rest of this function is for compatibility with original definition.
  JASSERT(fd>=0)(path).Text("Failed to open file.");
  char buf[512];
  const int len = strlen(DMTCP_FILE_HEADER);
  JASSERT(_real_read(fd, buf, len)==len)(path).Text("_real_read() failed");
  if(strncmp(buf, DMTCP_FILE_HEADER, len)==0){
    JTRACE("opened checkpoint file [uncompressed]")(path);
  }else{
    close_ckpt_to_read(fd);
    fd = open_ckpt_to_read( path.c_str() ); /* Re-open from beginning */
  }
  return fd;
}
#else
int dmtcp::ConnectionToFds::openDmtcpCheckpointFile(const dmtcp::string& path){
  int fd = open( path.c_str(), O_RDONLY);
  JASSERT(fd>=0)(path).Text("Failed to open file.");
  char buf[512];
  const int len = strlen(DMTCP_FILE_HEADER);
  JASSERT(_real_read(fd, buf, len)==len)(path).Text("_real_read() failed");
  if(strncmp(buf, DMTCP_FILE_HEADER, len)==0){
    JTRACE("opened checkpoint file [uncompressed]")(path);
    return fd;
  }else{
    close(fd);
    dmtcp::string cmd = dmtcp::string()+"exec gzip -d - < '"+path+"'";
    FILE* t = popen(cmd.c_str(),"r");
    JASSERT(t!=NULL)(path)(cmd).Text("Failed to launch gzip.");
    JTRACE ( "created gzip child process to uncompress checkpoint file");
    fd = fileno(t);
    JASSERT(_real_read(fd, buf, len)==len)(cmd)(path).Text("Invalid checkpoint file");
    JASSERT(strncmp(buf, DMTCP_FILE_HEADER, len)==0)(path).Text("Invalid checkpoint file");
    JTRACE("opened checkpoint file [compressed]")(path);
    return fd;
  }
}
#endif

int dmtcp::ConnectionToFds::openMtcpCheckpointFile(const dmtcp::string& path){
  int fd = openDmtcpCheckpointFile(path);
  jalib::JBinarySerializeReaderRaw rdr(path, fd);
  static ConnectionToFds trash;
  trash.serialize(rdr);
  return fd;
}

int dmtcp::ConnectionToFds::loadFromFile(const dmtcp::string& path,
                                         dmtcp::SerializedWorkerInfo *info)
{
  int fd = openDmtcpCheckpointFile(path);
  JASSERT(fd != -1);
  jalib::JBinarySerializeReaderRaw rdr(path, fd);
  rdr & info->compGroup;
  rdr & info->numPeers;
  rdr & info->argvSize;
  rdr & info->envSize;
  serialize(rdr);
#ifdef PID_VIRTUALIZATION
  info->virtualPidTable.serialize(rdr);
#endif
  close_ckpt_to_read(fd);
  return rdr.bytes() + strlen(DMTCP_FILE_HEADER);
}
