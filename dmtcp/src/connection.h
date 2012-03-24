/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, Gene Cooperman,    *
 *                                                           and Rohan Garg *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu, and         *
 *                                                      rohgarg@ccs.neu.edu *
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

// THESE INCLUDES ARE IN RANDOM ORDER.  LET'S CLEAN IT UP AFTER RELEASE. - Gene
#include "constants.h"
#include "dmtcpalloc.h"
#include "connectionidentifier.h"
#include <vector>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <map>
#include "../jalib/jbuffer.h"
#include "../jalib/jserialize.h"
#include "../jalib/jassert.h"
#include "../jalib/jconvert.h"
#include "../jalib/jalloc.h"
#include "../jalib/jfilesystem.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#ifdef HAVE_EPOLL_H
# include <sys/epoll.h>
#else
  /* KEEP THIS IN SYNC WITH syscallwrappers.h */
# ifndef _SYS_EPOLL_H
#  define _SYS_EPOLL_H    1
   struct epoll_event {int dummy;};
   /* Valid opcodes ( "op" parameter ) to issue to epoll_ctl().  */
#  define EPOLL_CTL_ADD 1 /* Add a file decriptor to the interface.  */
#  define EPOLL_CTL_DEL 2 /* Remove a file decriptor from the interface.  */
#  define EPOLL_CTL_MOD 3 /* Change file decriptor epoll_event structure.  */
# endif
#endif
#ifdef HAVE_EVENTFD_H
# include <sys/eventfd.h>
#else
  enum { EFD_SEMAPHORE = 1 };
#endif
#ifdef HAVE_SIGNALFD_H
# include <sys/signalfd.h>
#else
# include <stdint.h>
  struct signalfd_siginfo {uint32_t ssi_signo; int dummy;};
#endif

namespace jalib { class JSocket; }

namespace dmtcp
{

  class KernelBufferDrainer;
  class ConnectionRewirer;
  class TcpConnection;
  class EpollConnection;
  class KernelDeviceToConnection;
  class ConnectionToFds;


  class Connection
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      enum ConnectionType
      {
        INVALID = 0x0000,
        TCP     = 0x1000,
        PIPE    = 0x2000,
        PTY     = 0x3000,
        FILE    = 0x4000,
        STDIO   = 0x5000,
        FIFO    = 0x6000,
        EPOLL   = 0x7000,
        EVENTFD = 0x8000,
        SIGNALFD = 0x9000,
        TYPEMASK = TCP | PIPE | PTY | FILE | STDIO | FIFO | EPOLL | EVENTFD | SIGNALFD
      };

      virtual ~Connection() {}

      int conType() const { return _type & TYPEMASK; }
      int subType() const { return _type; }
      bool restoreInSecondIteration() { return _restoreInSecondIteration; }

      const ConnectionIdentifier& id() const { return _id; }

      virtual void preCheckpoint ( const dmtcp::vector<int>& fds, KernelBufferDrainer& ) = 0;
      virtual void postCheckpoint ( const dmtcp::vector<int>& fds,
                                    bool isRestart = false) = 0;
      virtual void restore(const dmtcp::vector<int>&,
                           ConnectionRewirer *rewirer = NULL) = 0;

      virtual bool isDupConnection ( const Connection& _that,
                                     dmtcp::ConnectionToFds& conToFds ) { return false; };
      virtual void doLocking ( const dmtcp::vector<int>& fds );
      virtual void saveOptions ( const dmtcp::vector<int>& fds );
      virtual void restoreOptions ( const dmtcp::vector<int>& fds );

      virtual void doSendHandshakes( const dmtcp::vector<int>& fds, const dmtcp::UniquePid& coordinator ) {};
      virtual void doRecvHandshakes( const dmtcp::vector<int>& fds, const dmtcp::UniquePid& coordinator ) {};

      //called on restart when _id collides with another connection
      virtual void mergeWith ( const Connection& that );

      //convert with type checking
      virtual TcpConnection& asTcp();
      virtual EpollConnection& asEpoll();

      virtual void restartDup2(int oldFd, int newFd);

      virtual string str() { return "<Not-a-File>"; };

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
      bool                 _restoreInSecondIteration;
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
        TCP_PREEXISTING,
        TCP_EXTERNAL_CONNECT
      };

      int tcpType() const { return _type; }

      enum PeerType
      {
        PEER_UNKNOWN,
        PEER_INTERNAL,
        PEER_EXTERNAL,
        PEER_SOCKETPAIR
      };

      enum PeerType peerType() const { return _peerType; }

#ifdef EXTERNAL_SOCKET_HANDLING
      void markInternal() {
        if (_type == TCP_ACCEPT || _type == TCP_CONNECT)
          _peerType = PEER_INTERNAL;
      }
      void markExternal() {
        if (_type == TCP_ACCEPT || _type == TCP_CONNECT)
          _peerType = PEER_EXTERNAL;
      }
      void preCheckpointPeerLookup ( const dmtcp::vector<int>& fds,
                                     dmtcp::vector<TcpConnectionInfo>& conInfoTable);
#endif
      // This accessor is needed because _type is protected.
      void markExternalConnect() { _type = TCP_EXTERNAL_CONNECT; }

      void setSocketpairPeer(ConnectionIdentifier id) {
        _peerType = PEER_SOCKETPAIR;
        _socketpairPeerId = id;
      }

      //basic commands for updating state from wrappers
      /*onSocket*/ TcpConnection ( int domain, int type, int protocol );
      void onBind ( const struct sockaddr* addr, socklen_t len );
      void onListen ( int backlog );
      void onConnect( int sockfd = -1, const  struct sockaddr *serv_addr = NULL,
                      socklen_t addrlen = 0 );
      /*onAccept*/ TcpConnection ( const TcpConnection& parent, const ConnectionIdentifier& remote );
      void onError();
      void onDisconnect(const dmtcp::vector<int>& fds);
      void addSetsockopt ( int level, int option, const char* value, int len );

      void markPreExisting() { _type = TCP_PREEXISTING; }

      //basic checkpointing commands
      virtual void preCheckpoint ( const dmtcp::vector<int>& fds
                                   , KernelBufferDrainer& drain );
      virtual void postCheckpoint ( const dmtcp::vector<int>& fds,
                                    bool isRestart = false);
      virtual void restore(const dmtcp::vector<int>&,
                           ConnectionRewirer *rewirer = NULL);

      //virtual void doLocking ( const dmtcp::vector<int>& fds );

      virtual void restoreOptions ( const dmtcp::vector<int>& fds );

      virtual void doSendHandshakes( const dmtcp::vector<int>& fds, const dmtcp::UniquePid& coordinator);
      virtual void doRecvHandshakes( const dmtcp::vector<int>& fds, const dmtcp::UniquePid& coordinator);

      void sendHandshake(jalib::JSocket& sock, const dmtcp::UniquePid& coordinator);
      void recvHandshake(jalib::JSocket& sock, const dmtcp::UniquePid& coordinator);

      void restoreSocketPair(const dmtcp::vector<int>& fds,
                             dmtcp::TcpConnection *peer,
                             const dmtcp::vector<int>& peerfds);
      const ConnectionIdentifier& getRemoteId() const { return _acceptRemoteId; }
      const ConnectionIdentifier& getSocketpairPeerId() const { return _socketpairPeerId; }

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
      enum PeerType           _peerType;
      bool                    _socketPairRestored;
      union {
        socklen_t               _bindAddrlen;
        socklen_t               _connectAddrlen;
      };
      union {
        /* See 'man socket.h' or POSIX for 'struct sockaddr_storage' */
        struct sockaddr_storage _bindAddr;
        struct sockaddr_storage _connectAddr;
      };
      ConnectionIdentifier    _acceptRemoteId;
      ConnectionIdentifier    _socketpairPeerId;
      dmtcp::map< int, dmtcp::map< int, jalib::JBuffer > > _sockOptions; // _options[level][option] = value
  };


  class PtyConnection : public Connection
  {
    public:
      enum PtyType
      {
        PTY_INVALID   = PTY,
        PTY_DEV_TTY,
        PTY_CTTY,
        PTY_MASTER,
        PTY_SLAVE,
        PTY_BSD_MASTER,
        PTY_BSD_SLAVE

//        TYPEMASK = PTY_CTTY | PTY_Master | PTY_Slave
      };

      PtyConnection ( const dmtcp::string& ptsName, const dmtcp::string& uniquePtsName, int type )
          : Connection ( PTY )
          , _ptsName ( ptsName )
          , _uniquePtsName ( uniquePtsName )
      {
        _type = type;
        JTRACE("Creating PtyConnection")(ptsName)(uniquePtsName)(id());
        //if ( type != PTY_CTTY &&  filename.compare ( "?" ) == 0 )
        //{
        //  _type = PTY_INVALID;
        //}
      }

      PtyConnection ( const dmtcp::string& device, int type )
          : Connection ( PTY )
          , _bsdDeviceName ( device )
      {
        _type = type;
        JTRACE("Creating BSDPtyConnection")(device)(id());
      }

      PtyConnection()
          : Connection ( PTY )
          , _ptsName ( "?" )
          , _uniquePtsName ( "?" )
      {
        _type = PTY_INVALID;
        //JTRACE("Creating empty PtyConnection")(id());
      }

      int  ptyType() { return _type;}// & TYPEMASK ); }
      dmtcp::string ptsName() { return _ptsName;; }
      dmtcp::string uniquePtsName() { return _uniquePtsName;; }

      //virtual void doLocking ( const dmtcp::vector<int>& fds );
      virtual void preCheckpoint ( const dmtcp::vector<int>& fds
                                   , KernelBufferDrainer& drain );
      virtual void postCheckpoint ( const dmtcp::vector<int>& fds,
                                    bool isRestart = false);
      virtual void restore(const dmtcp::vector<int>&,
                           ConnectionRewirer *rewirer = NULL);
      virtual void restoreOptions ( const dmtcp::vector<int>& fds );

      virtual void serializeSubClass ( jalib::JBinarySerializer& o );

      //called on restart when _id collides with another connection
      virtual void mergeWith ( const Connection& that );
    private:
      //PtyType   _type;
      dmtcp::string _ptsName;
      dmtcp::string _uniquePtsName;
      dmtcp::string _bsdDeviceName;
      bool          _ptmxIsPacketMode;

  };

  class StdioConnection : public Connection
  {
    public:
      enum StdioType
      {
        STDIO_IN = STDIO,
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
      virtual void postCheckpoint ( const dmtcp::vector<int>& fds,
                                    bool isRestart = false);
      virtual void restore(const dmtcp::vector<int>&,
                           ConnectionRewirer *rewirer = NULL);
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
        FILE_PROCFS,
        FILE_DELETED,
        FILE_RESMGR
      };

      enum ResMgrFileType
      {
        TORQUE_IO,
        TORQUE_NODE
      };

      //called on restart when _id collides with another connection
      virtual void mergeWith ( const Connection& that );

      inline FileConnection ( const dmtcp::string& path, off_t offset=-1,
                              int type = FILE_REGULAR )
          : Connection ( FILE )
          , _path ( path )
          , _offset ( offset )
      {
        _type = type;
        if( _type == FILE_RESMGR )
          JTRACE("New Resource Manager File connection created")(_path);
        else if (path != "?") {
          JTRACE("New File connection created")(_path);
        }
      }

      virtual void doLocking ( const dmtcp::vector<int>& fds );
      virtual void preCheckpoint ( const dmtcp::vector<int>& fds
                                   , KernelBufferDrainer& drain );
      virtual void postCheckpoint ( const dmtcp::vector<int>& fds,
                                    bool isRestart = false);

      virtual void restoreOptions ( const dmtcp::vector<int>& fds );
      virtual void restore(const dmtcp::vector<int>&,
                           ConnectionRewirer *rewirer = NULL);

      virtual void serializeSubClass ( jalib::JBinarySerializer& o );

      virtual string str() { return _path; }
      void restoreFile(dmtcp::string newpath = "");
      dmtcp::string filePath() { return _path; }
      bool checkpointed() { return _checkpointed; }
      void doNotRestoreCkptCopy() {
        _checkpointed = false; _restoreInSecondIteration = true;
      }

      bool isDupConnection ( const Connection& _that, dmtcp::ConnectionToFds& conToFds );

      int fileType() { return _type; }

    private:
      void saveFile (int fd);
      int  openFile ();
      void refreshPath( const dmtcp::vector<int>& fds);
      void refreshPath();
      void handleUnlinkedFile();
      void calculateRelativePath();
      dmtcp::string getSavedFilePath(const dmtcp::string& path);
      void preCheckpointResMgrFile(const dmtcp::vector<int>&);
      bool restoreResMgrFile(const dmtcp::vector<int>&);

      dmtcp::string _path;
      dmtcp::string _rel_path;
      dmtcp::string _ckptFilesDir;
      bool        _checkpointed;
      off_t       _offset;
      struct stat _stat;
      ResMgrFileType _rmtype;
  };

  class FifoConnection : public Connection
  {
    public:
      //called on restart when _id collides with another connection
      virtual void mergeWith ( const Connection& that );

      inline FifoConnection ( const dmtcp::string& path )
          : Connection ( FIFO )
          , _path ( path )
      {
        dmtcp::string curDir = jalib::Filesystem::GetCWD();
        int offs = _path.find(curDir);
        if( offs < 0 ){
          _rel_path = "*";
        }else{
          offs += curDir.size();
          offs = _path.find('/',offs);
          offs++;
          _rel_path = _path.substr(offs);
        }
        JTRACE("New Fifo connection created")(_path)(_rel_path);
        _in_data.clear();
      }

      virtual void preCheckpoint ( const dmtcp::vector<int>& fds
                                   , KernelBufferDrainer& drain );
      virtual void postCheckpoint ( const dmtcp::vector<int>& fds,
                                    bool isRestart = false);

      virtual void restoreOptions ( const dmtcp::vector<int>& fds );
      virtual void restore(const dmtcp::vector<int>&,
                           ConnectionRewirer *rewirer = NULL);

      virtual void serializeSubClass ( jalib::JBinarySerializer& o );

      //virtual void doLocking ( const dmtcp::vector<int>& fds );

    private:
      int  openFile();
      void refreshPath();
      dmtcp::string getSavedFilePath(const dmtcp::string& path);
      dmtcp::string _path;
      dmtcp::string _rel_path;
      dmtcp::string _savedRelativePath;
      struct stat _stat;
      bool _has_lock;
      vector<char> _in_data;
      int ckptfd;
  };

  class EpollConnection: public Connection
  {
    public:
      enum EpollType
      {
        EPOLL_INVALID = EPOLL,
        EPOLL_CREATE,
        EPOLL_CTL,
        EPOLL_WAIT
      };

      inline EpollConnection (int size, int type=EPOLL_CREATE)
          :Connection(EPOLL),
           _type(type),
           _size (size)
      {
        JTRACE ("new epoll connection created");
      }

      int epollType () const { return _type; }

      virtual void preCheckpoint ( const dmtcp::vector<int>& fds, KernelBufferDrainer& );
      virtual void postCheckpoint ( const dmtcp::vector<int>& fds,
                                    bool isRestart = false);
      virtual void restore(const dmtcp::vector<int>&,
                           ConnectionRewirer *rewirer = NULL);

      //virtual void doLocking ( const dmtcp::vector<int>& fds );
      //virtual void saveOptions ( const dmtcp::vector<int>& fds );
      virtual void restoreOptions ( const dmtcp::vector<int>& fds );

      //called on restart when _id collides with another connection
      virtual void mergeWith ( const Connection& that );

      //virtual void restartDup2(int oldFd, int newFd);
      virtual void serializeSubClass ( jalib::JBinarySerializer& o );

      virtual string str() { return "EPOLL-FD: <Not-a-File>"; };

      void onCTL (int op, int fd, struct epoll_event *event);

    private:
      EpollConnection& asEpoll();
      int         _type; // current state of EPOLL
      struct stat _stat; // not sure if stat makes sense in case  of epfd
      int         _size; // flags
      dmtcp::map<int, struct epoll_event > _fdToEvent;
  };

  class EventFdConnection: public Connection
  {
    public:
      inline EventFdConnection (unsigned int initval, int flags)
          :Connection(EVENTFD),
           _initval(initval),
           _flags (flags)
      {
        JTRACE ("new eventfd connection created");
      }

      virtual void preCheckpoint ( const dmtcp::vector<int>& fds, KernelBufferDrainer& );
      virtual void postCheckpoint ( const dmtcp::vector<int>& fds,
                                    bool isRestart = false);
      virtual void restore(const dmtcp::vector<int>&,
                           ConnectionRewirer *rewirer = NULL);

      //virtual void doLocking ( const dmtcp::vector<int>& fds );
      //virtual void saveOptions ( const dmtcp::vector<int>& fds );
      virtual void restoreOptions ( const dmtcp::vector<int>& fds );

      //virtual void restartDup2(int oldFd, int newFd);
      virtual void serializeSubClass ( jalib::JBinarySerializer& o );

      virtual string str() { return "EVENT-FD: <Not-a-File>"; };

    private:
      unsigned int   _initval; // initial counter value
      int         _flags; // flags
      bool _has_lock;
      int evtfd;
  };

  class SignalFdConnection: public Connection
  {
    public:
      inline SignalFdConnection (int signalfd, const sigset_t* mask, int flags)
          :Connection(SIGNALFD),
           signlfd (signalfd),
           _flags (flags)
      {
        if (mask!=NULL)
          _mask = *mask;
        else
          sigemptyset(&_mask);
        memset(&_fdsi, 0, sizeof(_fdsi));
        JTRACE ("new signalfd  connection created");
      }

      virtual void preCheckpoint ( const dmtcp::vector<int>& fds, KernelBufferDrainer& );
      virtual void postCheckpoint ( const dmtcp::vector<int>& fds,
                                    bool isRestart = false);
      virtual void restore(const dmtcp::vector<int>&,
                           ConnectionRewirer *rewirer = NULL);

      virtual void restoreOptions ( const dmtcp::vector<int>& fds );

      virtual void serializeSubClass ( jalib::JBinarySerializer& o );

      virtual string str() { return "SIGNAL-FD: <Not-a-File>"; };

    private:
      int signlfd;
      int         _flags; // flags
      sigset_t _mask; // mask for signals
      struct signalfd_siginfo _fdsi;
      bool _has_lock;
  };
}

#endif
