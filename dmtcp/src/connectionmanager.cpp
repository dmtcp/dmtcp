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

#include "connectionmanager.h"

#include  "../jalib/jfilesystem.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jassert.h"
#include "protectedfds.h"
#include "syscallwrappers.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>


static std::string _procFDPath ( int fd )
{
  return "/proc/" + jalib::XToString ( getpid() ) + "/fd/" + jalib::XToString ( fd );
}

static bool _isBadFd ( int fd )
{
  std::string device = jalib::Filesystem::ResolveSymlink ( _procFDPath ( fd ) );
  return ( device == "" );
}

dmtcp::ConnectionList& dmtcp::ConnectionList::Instance()
{
  static ConnectionList inst; return inst;
}

dmtcp::KernelDeviceToConnection& dmtcp::KernelDeviceToConnection::Instance()
{
  static KernelDeviceToConnection inst; return inst;
}

dmtcp::ConnectionList::ConnectionList() {}

dmtcp::KernelDeviceToConnection::KernelDeviceToConnection() {}

dmtcp::ConnectionToFds::ConnectionToFds ( KernelDeviceToConnection& source )
{
  std::vector<int> fds = jalib::Filesystem::ListOpenFds();
  JTRACE("Creating Connection->FD mapping")(fds.size());
  KernelDeviceToConnection::Instance().dbgSpamFds();
  _procname = jalib::Filesystem::GetProgramName();
  _hostname = jalib::Filesystem::GetCurrentHostname();
  _inhostname = jalib::Filesystem::GetCurrentHostname();
  _pid = UniquePid::ThisProcess();
  _ppid = UniquePid::ParentProcess();

  for ( size_t i=0; i<fds.size(); ++i )
  {
    if ( _isBadFd ( fds[i] ) ) continue;
    if ( ProtectedFDs::isProtected ( fds[i] ) ) continue;
    Connection* con = &source.retrieve ( fds[i] );
    _table[con->id() ].push_back ( fds[i] );
  }
}

dmtcp::Connection& dmtcp::KernelDeviceToConnection::retrieve ( int fd )
{
  std::string device = fdToDevice ( fd );
  JASSERT ( device.length() > 0 ) ( fd ).Text ( "invalid fd" );
  iterator i = _table.find ( device );
  JASSERT ( i != _table.end() ) ( fd ) ( device ) ( _table.size() ).Text ( "failed to find connection for fd" );
  return ConnectionList::Instance() [i->second];
}

void dmtcp::KernelDeviceToConnection::create ( int fd, Connection* c )
{
  ConnectionList::Instance().add ( c );

  std::string device = fdToDevice ( fd, true );

  JTRACE ( "device created" ) ( fd ) ( device ) ( c->id() );

  JASSERT ( device.length() > 0 ) ( fd ).Text ( "invalid fd" );
  iterator i = _table.find ( device );
  JASSERT ( i == _table.end() ) ( fd ) ( device ).Text ( "connection already exists" );
  _table[device] = c->id();
}



std::string dmtcp::KernelDeviceToConnection::fdToDevice ( int fd, bool noOnDemandPts )
{
  //gather evidence
  errno = 0;
  std::string device = jalib::Filesystem::ResolveSymlink ( _procFDPath ( fd ) );
  bool isBadFd = ( device == "" );

  if ( isBadFd )
  {
    JTRACE ( "bad fd (we expect one of these lines)" ) ( fd );
    JASSERT ( device == "" ) ( fd ) ( _procFDPath ( fd ) ) ( device ) ( JASSERT_ERRNO )
    .Text ( "expected badFd not to have a proc entry..." );

    return "";
  }

  bool isFile  = ( device[0] == '/' );
  bool isPts   = ( strncmp ( device.c_str(), "/dev/pts/", strlen( "/dev/pts/" ) ) ==0 );
  bool isPtmx  = ( strncmp ( device.c_str(), "/dev/ptmx", strlen( "/dev/ptmx" ) ) ==0 );

  if ( isPtmx )
  {
    std::string deviceName = "ptmx["+jalib::XToString ( fd ) +"]:" + device;

    iterator i = _table.find ( deviceName );
    if ( i == _table.end() )
    {
      char slaveDevice[1024];

      errno = 0;
      JASSERT ( _real_ptsname_r ( fd, slaveDevice, sizeof ( slaveDevice ) ) == 0 )
      ( fd ) ( deviceName ) ( JASSERT_ERRNO ).Text( "Unable to find the slave device" );

      std::string symlinkFilename = dmtcp::UniquePid::ptsSymlinkFilename ( slaveDevice );

      JTRACE ( "creating ptmx connection [on-demand]" ) ( deviceName ) ( symlinkFilename );

      int type = dmtcp::PtyConnection::PTY_MASTER;
      Connection * c = new PtyConnection ( device, symlinkFilename, type );
      ConnectionList::Instance().add ( c );
      _table[deviceName] = c->id();
      return deviceName;
    }
    else
    {
      return deviceName;
    }
  }
  else if ( isPts )
  {
    std::string deviceName = "pts["+jalib::XToString ( fd ) +"]:" + device;

    if(noOnDemandPts)
      return deviceName;

    iterator i = _table.find ( deviceName );
    if ( i == _table.end() )
    {
      int type;
      std::string symlinkFilename;
      std::string currentTty = jalib::Filesystem::GetCurrentTty();

      if ( currentTty.compare(device) == 0 ) {
        type = dmtcp::PtyConnection::PTY_TTY;
        symlinkFilename = "";
        JTRACE ( "creating TTY connection [on-demand]" ) 
          ( deviceName ) ( symlinkFilename );
      }
      else {
        type = dmtcp::PtyConnection::PTY_SLAVE;
        symlinkFilename = PtsToSymlink::Instance().getFilename ( device );
        JTRACE ( "creating pts connection [on-demand]" ) 
          ( deviceName ) ( symlinkFilename );
      }

      Connection * c = new PtyConnection ( device, symlinkFilename, type );
      ConnectionList::Instance().add ( c );
      _table[deviceName] = c->id();
      return deviceName;
    }
    else
    {
      return deviceName;
    }
  }
  else if ( isFile )
  {
    std::string deviceName = "file["+jalib::XToString ( fd ) +"]:" + device;
    iterator i = _table.find ( deviceName );
    if ( i == _table.end() )
    {
      JTRACE ( "creating file connection [on-demand]" ) ( deviceName );
      off_t offset = lseek ( fd, 0, SEEK_CUR );
      Connection * c = new FileConnection ( device, offset );
      ConnectionList::Instance().add ( c );
      _table[deviceName] = c->id();
      return deviceName;
    }
    else
    {
      return deviceName;
    }
  }


  return device;

}

void dmtcp::ConnectionList::erase ( iterator i )
{
  Connection * con = i->second;
  JTRACE ( "deleting stale connection..." ) ( con->id() );
  _connections.erase ( i );
  KernelDeviceToConnection::Instance().erase( i->first );
  delete con;
}

void dmtcp::KernelDeviceToConnection::erase( const ConnectionIdentifier& con )
{
  for(iterator i = _table.begin(); i!=_table.end(); ++i){
    if(i->second == con){
      std::string k = i->first;
      JTRACE("removing device->con mapping")(k)(con);
      _table.erase(k);
      return;
    }
  }
  JWARNING(false)(con).Text("failed to find connection in table to erase it");
}

//called when a device name changes
void dmtcp::KernelDeviceToConnection::redirect( int fd, const ConnectionIdentifier& id ){
  //first delete the old one
  erase(id);

  //now add the new fd
  std::string device = fdToDevice ( fd, true );
  JTRACE ( "redirecting device" )(fd)(device) (id);
  JASSERT ( device.length() > 0 ) ( fd ).Text ( "invalid fd" );
  iterator i = _table.find ( device );
  JASSERT ( i == _table.end() ) ( fd ) ( device ).Text ( "connection already exists" );
  _table[device] = id;
}

void dmtcp::KernelDeviceToConnection::dbgSpamFds()
{
#ifdef DEBUG
  JASSERT_STDERR << "Listing FDs...\n";
  std::vector<int> fds = jalib::Filesystem::ListOpenFds();
  for ( size_t i=0; i<fds.size(); ++i )
  {
    if ( _isBadFd ( fds[i] ) ) continue;
    if(ProtectedFDs::isProtected( fds[i] )) continue;
    std::string device = fdToDevice ( fds[i] );
    bool exists = ( _table.find ( device ) != _table.end() );
    JASSERT_STDERR << fds[i]
                   << " -> "  << device
                   << " inTable=" << exists << "\n";
  }
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
    const std::vector<int>& fds = i->second;
    JWARNING(fds.size() > 0)(con);
    if(fds.size()>0){
      std::string device = fdToDevice ( fds[0], true );
      _table[device] = con;
#ifdef DEBUG
      //double check to make sure all fds have same device
      for ( size_t i=1; i<fds.size(); ++i )
      {
        JASSERT ( device == fdToDevice ( fds[i] ) )
        ( device ) ( fdToDevice ( fds[i] ) ) ( fds[i] ) ( fds[0] );
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
      case Connection::PTY:
        con = new PtyConnection();
        break;
      case Connection::STDIO:
        con = new StdioConnection();
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
  std::vector<int> fds = jalib::Filesystem::ListOpenFds();
  for ( size_t i=0; i<fds.size(); ++i )
  {
    if ( _isBadFd ( fds[i] ) ) continue;
    if ( ProtectedFDs::isProtected ( fds[i] ) ) continue;
    KernelDeviceToConnection::Instance().handlePreExistingFd ( fds[i] );
  }
}

void dmtcp::KernelDeviceToConnection::handlePreExistingFd ( int fd )
{
  //this has the side effect of on-demand creating everything except sockets
  std::string device = KernelDeviceToConnection::Instance().fdToDevice ( fd, true );

  JTRACE ( "scanning pre-existing device" ) ( fd ) ( device );

  //so if it doesn't exist it must be a socket
  if ( _table.find ( device ) == _table.end() )
  {
    if ( fd <= 2 )
    {
      create(fd, new StdioConnection(fd));
    }
    else if ( strncmp ( device.c_str(), "/dev/pts/", strlen( "/dev/pts/" ) ) ==0 )
    {
      std::string deviceName = "pts["+jalib::XToString ( fd ) +"]:" + device;
      JNOTE ( "Found pre-existing PTY connection, will be restored as current TTY" )
        ( fd ) ( deviceName );

      std::string symlinkFilename = "?";
      int type = dmtcp::PtyConnection::PTY_TTY;

      PtyConnection *con = new PtyConnection ( device, symlinkFilename, type );
      create ( fd, con );
    }
    else 
    {
      JNOTE ( "found pre-existing socket... will not be restored" ) ( fd ) ( device );
      TcpConnection* con = new TcpConnection ( 0, 0, 0 );
      con->markPreExisting();
      create ( fd, con );
    }
  }
}


void dmtcp::ConnectionToFds::serialize ( jalib::JBinarySerializer& o )
{
  JSERIALIZE_ASSERT_POINT ( "dmtcp-serialized-connection-table!v0.07" );
  ConnectionList::Instance().serialize ( o );
  JSERIALIZE_ASSERT_POINT ( "dmtcp::ConnectionToFds:" );

  // Current process information
  o & _procname & _inhostname & _pid & _ppid;

  size_t numCons = _table.size();
  o & numCons;

  if ( o.isWriter() )
  {
    _hostname = jalib::Filesystem::GetCurrentHostname();
    o & _hostname;
    JTRACE("Writing hostname to checkpoint file")(_hostname)(_inhostname)(_procname)(_ppid);

    // Save connections
    for ( iterator i=_table.begin(); i!=_table.end(); ++i )
    {
      JSERIALIZE_ASSERT_POINT ( "CFdEntry:" );
      ConnectionIdentifier key = i->first;
      std::vector<int>& val = i->second;
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
      std::vector<int> val;
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
  ConnectionList::Instance().serialize ( o );
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
      std::string key = i->first;
      ConnectionIdentifier val = i->second;
      o & key & val;
    }
  }
  else
  {
    while ( numCons-- > 0 )
    {
      JSERIALIZE_ASSERT_POINT ( "KDEntry:" );
      std::string key = "?";
      ConnectionIdentifier val;
      o & key & val;
      _table[key] = val;
    }

  }

  JSERIALIZE_ASSERT_POINT ( "EOF" );
}


dmtcp::Connection& dmtcp::ConnectionList::operator[] ( const ConnectionIdentifier& id )
{
  //  std::cout << "Operator [], conId=" << id << "\n";
  JASSERT ( _connections.find ( id ) != _connections.end() ) ( id )
  .Text ( "Unknown connection" );
  //  std::cout << "Operator [], found: " << (_connections.find ( id ) != _connections.end())
  //            << "\n";
  //  std::cout << "Operator [], Result: conId=" << _connections[id]->id() << "\n";
  return *_connections[id];
}

void dmtcp::ConnectionList::add ( Connection* c )
{
  JWARNING ( _connections.find ( c->id() ) == _connections.end() ) ( c->id() )
  .Text ( "duplicate connection" );
  _connections[c->id() ] = c;
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

  JTRACE ( "allocated fd for connection" ) ( newfd ) ( con );

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
  std::string device = jalib::Filesystem::ResolveSymlink ( _procFDPath ( fd ) );
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
  for ( std::map< ConnectionIdentifier, int >::iterator i=_conToFd.begin()
        ; i!=_conToFd.end()
        ; ++i )
  {
    JWARNING ( _real_close ( i->second ) ==0 ) ( i->second ) ( JASSERT_ERRNO );
  }
  _conToFd.clear();
}

dmtcp::PtsToSymlink::PtsToSymlink() { }

dmtcp::PtsToSymlink& dmtcp::PtsToSymlink::Instance()
{
  static PtsToSymlink inst; return inst;
}

void dmtcp::PtsToSymlink::add ( std::string device, std::string filename )
{
  //    JWARNING(_table.find(device) == _table.end())(device)
  //            .Text("duplicate connection");
  _table[device] = filename;
}

void dmtcp::PtsToSymlink::replace ( std::string oldDevice, std::string newDevice )
{
  iterator i = _table.find ( oldDevice );
  JASSERT ( i != _table.end() )( oldDevice ).Text ( "old device not found" );
  std::string filename = _table[oldDevice];
  _table.erase ( i );
  _table[newDevice] = filename;
}

std::string dmtcp::PtsToSymlink::getFilename ( std::string device )
{
  std::string filename = "?";
  iterator i = _table.find ( device );
  if ( i != _table.end() )
  {
    filename = _table[device];
  }
  return filename;
}

bool dmtcp::PtsToSymlink::isDuplicate( std::string device )
{
  std::string filename = getFilename(device);
  if (filename.compare("?") == 0){
    return false;
  }
  return true;
}

pid_t dmtcp::ConnectionToFds::gzip_child_pid = -1;
static void close_ckpt_to_read(const int fd)
{
    int status;
    int rc;
    while (-1 == (rc = close(fd)) && errno == EINTR) ;
    JASSERT (rc != -1) ("close:") (JASSERT_ERRNO);
    if (dmtcp::ConnectionToFds::gzip_child_pid != -1) {
      while (-1 == (rc = waitpid(dmtcp::ConnectionToFds::gzip_child_pid,
			         &status, 0)) && errno == EINTR) ;
      JASSERT (rc != -1) ("waitpid:") (JASSERT_ERRNO);
      dmtcp::ConnectionToFds::gzip_child_pid = -1;
    }
}

// Define DMTCP_OLD_PCLOSE to get back the old buggy version.
// Remove the old version when satisfied this is better.
#ifndef DMTCP_OLD_PCLOSE
// Copied from mtcp/mtcp_restart.c.
#define DMTCP_MAGIC_FIRST 'D'
#define GZIP_FIRST 037
char *mtcp_executable_path(char *filename);
static char first_char(const char *filename)
{
    int fd, rc;
    char c;

    fd = open(filename, O_RDONLY);
    JASSERT(fd >= 0)(filename).Text("ERROR: Cannot open file %s");

    rc = read(fd, &c, 1);
    JASSERT(rc == 1)(filename).Text("ERROR: Error reading from file %s");

    close(fd);
    return c;
}

// Copied from mtcp/mtcp_restart.c.
// Let's keep this code close to MTCP code to avoid maintenance problems.
// A previous version tried to replace this with popen, causing a regression:
//   (no call to pclose, and possibility of using a wrong fd).
// Returns fd; sets dmtcp::gzip_child_pid::ConnectionToFds, if gzip compression.
static int open_ckpt_to_read(const char *filename)
{
    int fd;
    int fds[2];
    char fc;
    const char *gzip_path = "gzip";
    static const char * gzip_args[] = { "gzip", "-d", "-", NULL };
    pid_t cpid;

    fc = first_char(filename);
    fd = open(filename, O_RDONLY);
    JASSERT(fd>=0)(filename).Text("Failed to open file.");

    if(fc == DMTCP_MAGIC_FIRST) /* no compression */
        return fd;
    else if(fc == GZIP_FIRST) /* gzip */
    {
        JASSERT(pipe(fds) != -1)(filename).Text("Cannote create pipe to execute gunzip to decompress checkpoint file!");

        cpid = fork();

        JASSERT(cpid != -1).Text("ERROR: Cannot fork to execute gunzip to decompress checkpoint file!");
        if(cpid > 0) /* parent process */
        {
           JTRACE ( "created gzip child process to uncompress checkpoint file");
            dmtcp::ConnectionToFds::gzip_child_pid = cpid;
            close(fd);
            close(fds[1]);
            return fds[0];
        }
        else /* child process */
        {
            fd = dup(dup(dup(fd)));
            fds[1] = dup(fds[1]);
            close(fds[0]);
            dup2(fd, STDIN_FILENO);
            close(fd);
            dup2(fds[1], STDOUT_FILENO);
            close(fds[1]);
            unsetenv("LD_PRELOAD");
            execvp(gzip_path, (char **)gzip_args);
            JASSERT(gzip_path!=NULL)(gzip_path).Text("Failed to launch gzip.");
            /* should not get here */
            JASSERT(false)("ERROR: Decompression failed!  No restoration will be performed!  Cancelling now!");
            abort();
        }
    }
    else /* invalid magic number */
        JASSERT(false).Text("ERROR: Invalid magic number in this checkpoint file!");
}

// See comments above for open_ckpt_to_read()
int dmtcp::ConnectionToFds::openDmtcpCheckpointFile(const std::string& path){
  // Function also sets dmtcp::gzip_child_pid::ConnectionToFds
  int fd = open_ckpt_to_read( path.c_str() );
  // The rest of this function is for compatibility with original definition.
  JASSERT(fd>=0)(path).Text("Failed to open file.");
  char buf[512];
  const int len = strlen(DMTCP_FILE_HEADER);
  JASSERT(read(fd, buf, len)==len)(path).Text("read() failed");
  if(strncmp(buf, DMTCP_FILE_HEADER, len)==0){
    JTRACE("opened checkpoint file [uncompressed]")(path);
  }else{
    close_ckpt_to_read(fd);
    fd = open_ckpt_to_read( path.c_str() ); /* Re-open from beginning */
  }
  return fd;
}
#else
int dmtcp::ConnectionToFds::openDmtcpCheckpointFile(const std::string& path){
  int fd = open( path.c_str(), O_RDONLY);
  JASSERT(fd>=0)(path).Text("Failed to open file.");
  char buf[512];
  const int len = strlen(DMTCP_FILE_HEADER);
  JASSERT(read(fd, buf, len)==len)(path).Text("read() failed");
  if(strncmp(buf, DMTCP_FILE_HEADER, len)==0){
    JTRACE("opened checkpoint file [uncompressed]")(path);
    return fd;
  }else{
    close(fd);
    std::string cmd = std::string()+"exec gzip -d - < '"+path+"'";
    FILE* t = popen(cmd.c_str(),"r");
    JASSERT(t!=NULL)(path)(cmd).Text("Failed to launch gzip.");
    JTRACE ( "created gzip child process to uncompress checkpoint file");
    fd = fileno(t);
    JASSERT(read(fd, buf, len)==len)(cmd)(path).Text("Invalid checkpoint file");
    JASSERT(strncmp(buf, DMTCP_FILE_HEADER, len)==0)(path).Text("Invalid checkpoint file");
    JTRACE("opened checkpoint file [compressed]")(path);
    return fd;
  }
}
#endif

int dmtcp::ConnectionToFds::openMtcpCheckpointFile(const std::string& path){
  int fd = openDmtcpCheckpointFile(path);
  jalib::JBinarySerializeReaderRaw rdr(path, fd);
  static ConnectionToFds trash;
  trash.serialize(rdr);
  return fd;
}

int dmtcp::ConnectionToFds::loadFromFile(const std::string& path){
  int fd = openDmtcpCheckpointFile(path);
  jalib::JBinarySerializeReaderRaw rdr(path, fd);
  serialize(rdr);
  close_ckpt_to_read(fd);
  return rdr.bytes() + strlen(DMTCP_FILE_HEADER);
}
