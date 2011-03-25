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

#include "sockettable.h"
#include  "../jalib/jassert.h"
#include "dmtcpmessagetypes.h"
#include  "../jalib/jsocket.h"
#include "dmtcpworker.h"
#include "syscallwrappers.h"
#include  "../jalib/jfilesystem.h"
#include <netdb.h>
#include <map>
#include <sys/types.h>
#include <sys/socket.h>
#include "connectionidentifier.h"
#include "connectionmanager.h"
#include "connectionmanager.h"


///
///called automatically after a successful user function call
extern "C" int dmtcp_on_socket ( int ret, int domain, int type, int protocol )
{

  JTRACE ( "socket created" ) ( ret ) ( domain ) ( type ) ( protocol );
//     dmtcp::SocketEntry& entry = dmtcp::SocketTable::LookupByFd(ret);
//     entry.setDomain(domain);
//     entry.setType(type);
//     entry.setProtocol(protocol);
//     entry.setState(dmtcp::SocketEntry::T_CREATED);

  dmtcp::KernelDeviceToConnection::instance().create ( ret, new dmtcp::TcpConnection ( domain,type,protocol ) );

  return ret;
}

///
///called automatically after a successful user function call
extern "C" int dmtcp_on_connect ( int ret, int sockfd, const  struct sockaddr *serv_addr, socklen_t addrlen )
{
//     JASSERT(serv_addr != NULL)(serv_addr)(addrlen);
//     dmtcp::SocketEntry& entry = dmtcp::SocketTable::LookupByFd(sockfd);
//     entry.setAddr(serv_addr,addrlen);
//     entry.setState(dmtcp::SocketEntry::T_CONNECT);

  dmtcp::TcpConnection& con = dmtcp::KernelDeviceToConnection::instance().retrieve ( sockfd ).asTcp();
  con.onConnect(sockfd, serv_addr, addrlen);

#if HANDSHAKE_ON_CONNECT == 1
  JTRACE ( "connected, sending 1-way handshake" ) ( sockfd ) ( con.id() );
  jalib::JSocket remote ( sockfd );
  con.sendHandshake(remote, dmtcp::DmtcpWorker::instance().coordinatorId());
  JTRACE ( "1-way handshake sent" );
#else
  JTRACE ( "connected" ) ( sockfd ) ( con.id() );
#endif

  return ret;
}

///
///called automatically after a successful user function call
extern "C" int dmtcp_on_bind ( int ret, int sockfd,  const struct  sockaddr  *my_addr,  socklen_t addrlen )
{
  dmtcp::TcpConnection& con = dmtcp::KernelDeviceToConnection::instance().retrieve ( sockfd ).asTcp();


  con.onBind ( my_addr, addrlen );

  JTRACE ( "bind" ) ( sockfd ) ( con.id() );
//     JASSERT(my_addr != NULL)(my_addr)(addrlen);
//     dmtcp::SocketEntry& entry = dmtcp::SocketTable::LookupByFd(sockfd);
//     entry.setAddr(my_addr,addrlen);
//     entry.setState(dmtcp::SocketEntry::T_BIND);
//
//     theThisProcessPorts[((sockaddr_in*)my_addr)->sin_port] = sockfd;
//
  return ret;
}

///
///called automatically after a successful user function call
extern "C" int dmtcp_on_listen ( int ret, int sockfd, int backlog )
{

  dmtcp::TcpConnection& con = dmtcp::KernelDeviceToConnection::instance().retrieve ( sockfd ).asTcp();

  con.onListen ( backlog );

  JTRACE ( "listen" ) ( sockfd ) ( con.id() ) ( backlog );
  return ret;
}

///
///called automatically after a successful user function call
extern "C" int dmtcp_on_accept ( int ret, int sockfd, struct sockaddr *addr, socklen_t *addrlen )
{
//     dmtcp::SocketEntry& entry = dmtcp::SocketTable::LookupByFd(ret);
//     entry.setAddr(addr,*addrlen);
//     entry.setState(dmtcp::SocketEntry::T_ACCEPT);
//

  dmtcp::TcpConnection& parent = dmtcp::KernelDeviceToConnection::instance().retrieve ( sockfd ).asTcp();

  dmtcp::TcpConnection* con = new dmtcp::TcpConnection ( parent, dmtcp::ConnectionIdentifier::Null() );
  dmtcp::KernelDeviceToConnection::instance().create ( ret, con );

#if HANDSHAKE_ON_CONNECT == 1
  JTRACE ( "accepted, waiting for 1-way handshake" ) ( sockfd ) ( con->id() );
  jalib::JSocket remote ( ret );
  con->recvHandshake(remote, dmtcp::DmtcpWorker::instance().coordinatorId());
  JTRACE ( "1-way handshake received" )(con->getRemoteId());
#else
  JTRACE ( "accepted incoming connection" ) ( sockfd ) ( con->id() );
#endif

//     entry.setRemoteId( hello_remote.from );
//
//     if(hello_remote.from.id == dmtcp::UniquePid::ThisProcess())
//     {
//         if(ret < 10)
//         {
//             JWARNING(false)(ret)(hello_remote.from.conId)
//                     .Text("HACK -- Not marking loopback normally");
//         }
//         else
//         {
//             JTRACE("MARKING LOOPBACK")(ret)(hello_remote.from.conId);
//             entry.setIsLoopback( true );
//             dmtcp::ConnectionIdentifier& fds = dmtcp::ConnectionIdentifiers::Outgoing().lookup( hello_remote.from.conId );
//             for(dmtcp::ConnectionIdentifier::fditerator i = fds.begin()
//                 ; i != fds.end()
//                 ; ++i)
//             {
//                 JTRACE("marking loopback")(*i);
//                 dmtcp::SocketTable::LookupByFd(*i).setIsLoopback( true );
//             }
//         }
//     }
//


  return ret;
}

///
///called automatically after a successful user function call
//#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)) && __GLIBC_PREREQ(2,10)
extern "C" int dmtcp_on_accept4 ( int ret, int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags )
{
  return dmtcp_on_accept(ret, sockfd, addr, addrlen);
}
//#endif

///
///called automatically when a socket error is returned by user function
extern "C" int dmtcp_on_error ( int ret, int sockfd, const char* fname, int savedErrno )
{
  //Ignore EAGAIN errors
  if ( savedErrno == EAGAIN ) return ret;
  if ( savedErrno == EADDRINUSE && strncmp(fname, "bind", 4) == 0 )
    return ret;

  JTRACE ( "socket error" ) ( fname ) ( ret ) ( sockfd ) ( strerror(savedErrno) );

  dmtcp::Connection& con = dmtcp::KernelDeviceToConnection::instance().retrieve ( sockfd );

  if ( con.conType() == dmtcp::Connection::TCP )
  {
    con.asTcp().onError();
  }


//     dmtcp::SocketEntry& entry = dmtcp::SocketTable::LookupByFd(sockfd);
//     entry = dmtcp::SocketEntry();
//     entry.setState(dmtcp::SocketEntry::T_ERROR);
  return ret;
}

extern "C" int dmtcp_on_setsockopt ( int ret, int sockfd, int  level,  int  optname,  const  void  *optval, socklen_t optlen )
{
  JTRACE ( "setsockopt" ) ( ret ) ( sockfd ) ( optname );
//     dmtcp::SocketEntry& entry = dmtcp::SocketTable::LookupByFd(sockfd);
//     entry.addSetsockopt(level,optname,(char*)optval, optlen);

  dmtcp::TcpConnection& con = dmtcp::KernelDeviceToConnection::instance().retrieve ( sockfd ).asTcp();

  con.addSetsockopt ( level, optname, ( char* ) optval, optlen );

  return ret;
}


///
/// autogenerated Set/Get method...
// const struct sockaddr* dmtcp::SocketEntry::addr() const
// {
//     return (sockaddr*)&_addr;
// }


///
/// autogenerated Set/Get method...
// void dmtcp::SocketEntry::setAddr(const struct sockaddr* theValue, socklen_t length)
// {
//     _addrlen = length;
//     memcpy(&_addr, theValue, length);
// }


///
/// autogenerated Set/Get method...
// socklen_t dmtcp::SocketEntry::addrlen() const
// {
//   return _addrlen;
// }

///
/// autogenerated Set/Get method...
// int dmtcp::SocketEntry::backlog() const
// {
//   return _backlog;
// }

///
/// autogenerated Set/Get method...
// void dmtcp::SocketEntry::setBacklog(const int& theValue)
// {
//     _backlog = theValue;
// }


///
/// autogenerated Set/Get method...
// int dmtcp::SocketEntry::domain() const
// {
//   return _domain;
// }


///
/// autogenerated Set/Get method...
// void dmtcp::SocketEntry::setDomain(const int& theValue)
// {
//   _domain = theValue;
// }


///
/// autogenerated Set/Get method...
// int dmtcp::SocketEntry::protocol() const
// {
//   return _protocol;
// }


///
/// autogenerated Set/Get method...
// void dmtcp::SocketEntry::setProtocol(const int& theValue)
// {
//   _protocol = theValue;
// }


///
/// autogenerated Set/Get method...
// dmtcp::SocketEntry::SocketState dmtcp::SocketEntry::state() const
// {
//   return _state;
// }


///
/// autogenerated Set/Get method...
// void dmtcp::SocketEntry::setState(const dmtcp::SocketEntry::SocketState& theValue)
// {
//   _state = theValue;
// }

///
/// autogenerated Set/Get method...
// int dmtcp::SocketEntry::type() const
// {
//   return _type;
// }


///
/// autogenerated Set/Get method...
// void dmtcp::SocketEntry::setType(const int& theValue)
// {
//   _type = theValue;
// }

///
/// autogenerated Set/Get method...
// dmtcp::SocketTable& dmtcp::SocketTable::instance()
// {
//     static SocketTable instance;
//     return instance;
// }
//

///
/// constructor
// dmtcp::SocketEntry::SocketEntry()
//     :_sockfd(-1)
//     ,_state(T_INVALID)
//     ,_domain(-1)
//     ,_type(-1)
//     ,_protocol(-1)
//     ,_backlog(-1)
//     ,_addrlen(0)
//     ,_needRestore(false)
//     ,_isLoopback(false)
// {
//     memset(&_addr, 0, sizeof(_addr));
// }


///
/// Constructor
// dmtcp::SocketTable::SocketTable()
// {
//     _entries.resize(64);
// }


///
/// Lookup SocketEntry by file descriptor
// dmtcp::SocketEntry& dmtcp::SocketTable::operator[] (int sockfd)
// {
//     JASSERT(sockfd >= 0)(sockfd).Text("negative file descriptor");
//     //grow container
//     while(_entries.size()-1 < sockfd) _entries.resize(2*_entries.size());
//     _entries[sockfd].setSockfd(sockfd);
//     return _entries[sockfd];
// }
//

// // const dmtcp::UniquePidConId& dmtcp::SocketEntry::remoteId() const
// // {
// //   return _remoteId;
// // }
// //
// //
// // void dmtcp::SocketEntry::setRemoteId(const UniquePidConId& theValue)
// // {
// //   _remoteId = theValue;
// // }
// //
// // void dmtcp::SocketEntry::changeRemoteId(const UniquePid& theValue)
// // {
// //   _remoteId.id = theValue;
// // }
// //
// //
// // int dmtcp::SocketEntry::sockfd() const
// // {
// //   return _sockfd;
// // }
// //
// //
// // void dmtcp::SocketEntry::setSockfd(const int& theValue)
// // {
// //   _sockfd = theValue;
// // }
// //
// //
// // bool dmtcp::SocketEntry::needRestore() const
// // {
// //   return _needRestore;
// // }
// //
// //
// // void dmtcp::SocketEntry::setNeedRestore(bool theValue)
// // {
// //   _needRestore = theValue;
// // }
// //
// // bool dmtcp::SocketEntry::isLoopback() const
// // {
// //   return _isLoopback;
// // }
// //
// //
// // void dmtcp::SocketEntry::setIsLoopback(bool theValue)
// // {
// //   _isLoopback = theValue;
// // }
// //
// // void dmtcp::SocketEntry::addSetsockopt(int level, int option, const char* value, int len)
// // {
// //     _options[level][option] = jalib::JBuffer( value, len );
// // }
// //
// // void dmtcp::SocketEntry::restoreOptions()
// // {
// //     typedef dmtcp::map< int, dmtcp::map< int, jalib::JBuffer > >::iterator levelIterator;
// //     typedef dmtcp::map< int, jalib::JBuffer >::iterator optionIterator;
// //
// //     for(levelIterator lvl = _options.begin(); lvl!=_options.end(); ++lvl)
// //     {
// //         for(optionIterator opt = lvl->second.begin(); opt!=lvl->second.end(); ++opt)
// //         {
// //             JTRACE("restoring socket option")(_sockfd)(opt->first)(opt->second.size());
// //             int ret = _real_setsockopt(_sockfd,lvl->first,opt->first,opt->second.buffer(), opt->second.size());
// //             JASSERT(ret == 0)(_sockfd)(opt->first)(opt->second.size())
// //                     .Text("restoring setsockopt failed");
// //         }
// //     }
// // }
// //
// // bool dmtcp::SocketEntry::isStillAlive() const
// // {
// //     int sockerr = 0;
// //     socklen_t len = sizeof(sockerr);
// //     int rv = getsockopt(_sockfd, SOL_SOCKET, SO_ERROR, (char*)&sockerr, &len);
// //     return rv == 0 && sockerr == 0;
// // }
// //
// // void dmtcp::SocketTable::onForkUpdate(const dmtcp::UniquePid& parent, const dmtcp::UniquePid& child)
// // {
// //     bool isChild = (child == UniquePid::ThisProcess());
// //     bool isParent = (parent == UniquePid::ThisProcess());
// //     JASSERT((isChild || isParent) && (isChild!=isParent));
// //
// //     const dmtcp::UniquePid& otherProcess = isChild ? parent : child;
// //
// //     for(iterator i = begin(); i!=end(); ++i)
// //     {
// //         switch(i->state())
// //         {
// //             case SocketEntry::T_INVALID:
// //             case SocketEntry::T_ERROR:
// //             case SocketEntry::T_CREATED:
// //                 break;
// //
// //             case SocketEntry::T_BIND:
// //             case SocketEntry::T_LISTEN:
// //                 if(isChild)
// //                 {
// // //                     JTRACE("erroring listen socket on child")(i->sockfd());
// //                     i->setState(SocketEntry::T_ERROR);
// //                 }
// //                 break;
// //
// //             case SocketEntry::T_CONNECT:
// //             case SocketEntry::T_ACCEPT:
// //                 if(i->isLoopback())
// //                 {
// //                     JTRACE("changing remote id for loopback")(i->sockfd())(i->remoteId().id)(otherProcess);
// //                     i->changeRemoteId(otherProcess);
// //                     i->setIsLoopback(false);
// //                 }
// //                 else if(isChild)
// //                 {
// // //                     JTRACE("erroring data socket on child")(i->sockfd());
// //                     i->setState(SocketEntry::T_ERROR);
// //                 }
// //                 break;
// //         }
// //     }
// // }
// //
// // void dmtcp::SocketTable::resetFd(int fd)
// // {
// //     operator[](fd) = SocketEntry();
// //     ConnectionIdentifiers::Outgoing().removeFd( fd );
// //     ConnectionIdentifiers::Incoming().removeFd( fd );
// // }
