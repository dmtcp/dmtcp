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

#ifndef DMTCPCONNECTION_H
#define DMTCPCONNECTION_H

#include "dmtcpalloc.h"
#include "connectionidentifier.h"
#include <vector>
#include <sys/types.h>
#include <sys/socket.h>
#include <map>
#include  "../jalib/jbuffer.h"
#include  "../jalib/jserialize.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jconvert.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

namespace jalib { class JSocket; }

namespace dmtcp
{

  class KernelBufferDrainer;
  class ConnectionRewirer;
  class TcpConnection;
  class KernelDeviceToConnection;


  class Connection
  {
    public:
      enum ConnectionType
      {
        INVALID = 0x0000,
        TCP     = 0x1000,
        PIPE    = 0x2000,
        PTY     = 0x3000,
        FILE    = 0x4000,
        STDIO   = 0x5000,

        TYPEMASK = TCP | PIPE | PTY | FILE | STDIO
      };

      virtual ~Connection() {}

      int conType() const { return _type & TYPEMASK; }

      const ConnectionIdentifier& id() const { return _id; }

      virtual void preCheckpoint ( const dmtcp::vector<int>& fds, KernelBufferDrainer& ) = 0;
      virtual void postCheckpoint ( const dmtcp::vector<int>& fds ) = 0;
      virtual void restore ( const dmtcp::vector<int>&, ConnectionRewirer& ) = 0;

      virtual void doLocking ( const dmtcp::vector<int>& fds ) {};
      virtual void saveOptions ( const dmtcp::vector<int>& fds );
      virtual void restoreOptions ( const dmtcp::vector<int>& fds );

      virtual void doSendHandshakes( const dmtcp::vector<int>& fds, const dmtcp::UniquePid& coordinator ) {};
      virtual void doRecvHandshakes( const dmtcp::vector<int>& fds, const dmtcp::UniquePid& coordinator ) {};

      //called on restart when _id collides with another connection
      virtual void mergeWith ( const Connection& that );

      //convert with type checking
      virtual TcpConnection& asTcp();

      virtual void restartDup2(int oldFd, int newFd);


      void serialize ( jalib::JBinarySerializer& o );
    protected:
      virtual void serializeSubClass ( jalib::JBinarySerializer& o ) = 0;
    protected:
      //only child classes can construct us...
      Connection ( int t );
    protected:
      ConnectionIdentifier _id;
      int                  _type;
      int                  _fcntlFlags;
      int                  _fcntlOwner;
      int                  _fcntlSignal;
  };

  class TcpConnection : public Connection
  {
    public:
      enum TcpType
      {
        TCP_INVALID = TCP,
        TCP_ERROR,
        TCP_CREATED,
        TCP_BIND,
        TCP_LISTEN,
        TCP_ACCEPT,
        TCP_CONNECT,
        TCP_PREEXISTING
      };

      int tcpType() const { return _type; }

      //basic commands for updating state as a from wrappers
      /*onSocket*/ TcpConnection ( int domain, int type, int protocol );
      void onBind ( const struct sockaddr* addr, socklen_t len );
      void onListen ( int backlog );
      void onConnect(); // connect side does not know remote host
      /*onAccept*/ TcpConnection ( const TcpConnection& parent, const ConnectionIdentifier& remote );
      void onError();
      void onDisconnect(const dmtcp::vector<int>& fds);
      void addSetsockopt ( int level, int option, const char* value, int len );

      void markPreExisting() { _type = TCP_PREEXISTING; }

      //basic checkpointing commands
      virtual void preCheckpoint ( const dmtcp::vector<int>& fds
                                   , KernelBufferDrainer& drain );
      virtual void postCheckpoint ( const dmtcp::vector<int>& fds );
      virtual void restore ( const dmtcp::vector<int>&, ConnectionRewirer& );

      virtual void doLocking ( const dmtcp::vector<int>& fds );

      virtual void restoreOptions ( const dmtcp::vector<int>& fds );

      virtual void doSendHandshakes( const dmtcp::vector<int>& fds, const dmtcp::UniquePid& coordinator);
      virtual void doRecvHandshakes( const dmtcp::vector<int>& fds, const dmtcp::UniquePid& coordinator);

      void sendHandshake(jalib::JSocket& sock, const dmtcp::UniquePid& coordinator);
      void recvHandshake(jalib::JSocket& sock, const dmtcp::UniquePid& coordinator);

      const ConnectionIdentifier& getRemoteId() const { return _acceptRemoteId; }

      //called on restart when _id collides with another connection
      virtual void mergeWith ( const Connection& that );
    private:
      virtual void serializeSubClass ( jalib::JBinarySerializer& o );
      TcpConnection& asTcp();
    private:
      int                     _sockDomain;
      int                     _sockType;
      int                     _sockProtocol;
      int                     _listenBacklog;
      socklen_t               _bindAddrlen;
      struct sockaddr_storage _bindAddr;
      ConnectionIdentifier    _acceptRemoteId;
      dmtcp::map< int, dmtcp::map< int, jalib::JBuffer > > _sockOptions; // _options[level][option] = value
  };

// class PipeConnection : public Connection
// {
// public:
//     virtual void preCheckpoint(const dmtcp::vector<int>& fds
//                             , KernelBufferDrainer& drain);
//     virtual void postCheckpoint(const dmtcp::vector<int>& fds);
//     virtual void restore(const dmtcp::vector<int>&, ConnectionRewirer&);
//
//     virtual void serializeSubClass(jalib::JBinarySerializer& o);
//
// };

  class PtyConnection : public Connection
  {
    public:
      enum PtyType
      {
        PTY_INVALID   = PTY,
        PTY_TTY,
        PTY_MASTER,
        PTY_SLAVE//,

//        TYPEMASK = PTY_TTY | PTY_Master | PTY_Slave
      };

      PtyConnection ( const dmtcp::string& device, const dmtcp::string& filename, int type )
          : Connection ( PTY )
          , _symlinkFilename ( filename )
          , _device ( device )
      {
        _type = type;
        JTRACE("Creating PtyConnection")(device)(filename)(id());
        if ( type != PTY_TTY &&  filename.compare ( "?" ) == 0 )
        {
          _type = PTY_INVALID;
        }
      }

      PtyConnection()
          : Connection ( PTY )
          , _symlinkFilename ( "?" )
          , _device ( "?" )
      {
        _type = PTY_INVALID;
        JTRACE("Creating null PtyConnection")(id());
      }

      int  ptyType() { return _type;}// & TYPEMASK ); }
      virtual void preCheckpoint ( const dmtcp::vector<int>& fds
                                   , KernelBufferDrainer& drain );
      virtual void postCheckpoint ( const dmtcp::vector<int>& fds );
      virtual void restore ( const dmtcp::vector<int>&, ConnectionRewirer& );
      virtual void restoreOptions ( const dmtcp::vector<int>& fds );

      virtual void serializeSubClass ( jalib::JBinarySerializer& o );

      //called on restart when _id collides with another connection
      virtual void mergeWith ( const Connection& that );
    private:
      //PtyType   _type;
      dmtcp::string _symlinkFilename;
      dmtcp::string _device;

  };

  class StdioConnection : public Connection
  {
    public:
      enum StdioType
      {
        STDIO_IN  = STDIO,
        STDIO_OUT,
        STDIO_ERR,
        STDIO_INVALID
      };

      StdioConnection( int fd ): Connection ( STDIO + fd )
      {
        JTRACE("creating stdio connection")(fd)(id());
        JASSERT( jalib::Between(0, fd, 2) )(fd).Text("invalid fd for StdioConnection");
      }

      StdioConnection(): Connection ( STDIO_INVALID ) {}


      virtual void preCheckpoint ( const dmtcp::vector<int>& fds
                                   , KernelBufferDrainer& drain );
      virtual void postCheckpoint ( const dmtcp::vector<int>& fds );
      virtual void restore ( const dmtcp::vector<int>&, ConnectionRewirer& );
      virtual void restoreOptions ( const dmtcp::vector<int>& fds );

      virtual void serializeSubClass ( jalib::JBinarySerializer& o );

      virtual void mergeWith ( const Connection& that );

      virtual void restartDup2(int oldFd, int newFd);
  };

  class FileConnection : public Connection
  {
    public:
      enum FileType
      {
        FILE_INVALID = FILE,
        FILE_REGULAR,
        FILE_DELETED
      };
      //called on restart when _id collides with another connection
      virtual void mergeWith ( const Connection& that );

      inline FileConnection ( const dmtcp::string& path, off_t offset=-1 )
          : Connection ( FILE )
          , _fileType (FILE_REGULAR)
          , _path ( path )
          , _offset ( offset )
      {}

      virtual void preCheckpoint ( const dmtcp::vector<int>& fds
                                   , KernelBufferDrainer& drain );
      virtual void postCheckpoint ( const dmtcp::vector<int>& fds );
      virtual void restore ( const dmtcp::vector<int>&, ConnectionRewirer& );

      virtual void serializeSubClass ( jalib::JBinarySerializer& o );

    private:
      void saveFile (int fd);
      int  openFile ();
      dmtcp::string getSavedFilePath(const dmtcp::string& path);

      int         _fileType;
      dmtcp::string _path;
      dmtcp::string _savedRelativePath;
      off_t       _offset;
      struct stat _stat;
  };
}

#endif
