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


#include "sockettable.h"
#include "jassert.h"
#include "dmtcpmessagetypes.h"
#include "jsocket.h"
#include "dmtcpworker.h"
#include "syscallwrappers.h"
#include "jfilesystem.h"
#include <netdb.h>
#include <map>
#include <sys/types.h>
#include <sys/socket.h>
#include "connectionidentifier.h"
#include "connectionmanager.h"
#include "connectionmanager.h"


///
///called automatically after a sucessful user function call
extern "C" int dmtcp_on_socket ( int ret, int domain, int type, int protocol )
{

  JTRACE ( "socket created" ) ( ret ) ( domain ) ( type ) ( protocol );
//     dmtcp::SocketEntry& entry = dmtcp::SocketTable::LookupByFd(ret);
//     entry.setDomain(domain);
//     entry.setType(type);
//     entry.setProtocol(protocol);
//     entry.setState(dmtcp::SocketEntry::T_CREATED);

  dmtcp::KernelDeviceToConnection::Instance().create ( ret, new dmtcp::TcpConnection ( domain,type,protocol ) );

  return ret;
}

///
///called automatically after a sucessful user function call
extern "C" int dmtcp_on_connect ( int ret, int sockfd, const  struct sockaddr *serv_addr, socklen_t addrlen )
{

//     JASSERT(serv_addr != NULL)(serv_addr)(addrlen);
//     dmtcp::SocketEntry& entry = dmtcp::SocketTable::LookupByFd(sockfd);
//     entry.setAddr(serv_addr,addrlen);
//     entry.setState(dmtcp::SocketEntry::T_CONNECT);
//
  dmtcp::TcpConnection& con = dmtcp::KernelDeviceToConnection::Instance().retrieve ( sockfd ).asTcp();
  con.onConnect();

  JTRACE ( "connected, doing dmtcp handshake...." ) ( sockfd ) ( con.id() );

  jalib::JSocket remote ( sockfd );
  dmtcp::DmtcpMessage hello_local;
  hello_local.type = dmtcp::DMT_HELLO_PEER;
  hello_local.from = con.id();
  hello_local.coordinator = dmtcp::DmtcpWorker::instance().coordinatorId();
  remote << hello_local;

//     JTRACE("connect complete")(sockfd)(hello_local.from.conId)(hello_local.from.id);

//     entry.setRemoteId( hello_remote.from );

  return ret;
}

///
///called automatically after a sucessful user function call
extern "C" int dmtcp_on_bind ( int ret, int sockfd,  const struct  sockaddr  *my_addr,  socklen_t addrlen )
{
  dmtcp::TcpConnection& con = dmtcp::KernelDeviceToConnection::Instance().retrieve ( sockfd ).asTcp();


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
///called automatically after a sucessful user function call
extern "C" int dmtcp_on_listen ( int ret, int sockfd, int backlog )
{

  dmtcp::TcpConnection& con = dmtcp::KernelDeviceToConnection::Instance().retrieve ( sockfd ).asTcp();

  con.onListen ( backlog );

  JTRACE ( "listen" ) ( sockfd ) ( con.id() ) ( backlog );
  return ret;
}

///
///called automatically after a sucessful user function call
extern "C" int dmtcp_on_accept ( int ret, int sockfd, struct sockaddr *addr, socklen_t *addrlen )
{
//     dmtcp::SocketEntry& entry = dmtcp::SocketTable::LookupByFd(ret);
//     entry.setAddr(addr,*addrlen);
//     entry.setState(dmtcp::SocketEntry::T_ACCEPT);
//

  dmtcp::TcpConnection& parent = dmtcp::KernelDeviceToConnection::Instance().retrieve ( sockfd ).asTcp();

  JTRACE ( "accepted new connection, doing magic cookie handshake..." ) ( sockfd ) ( ret );

  jalib::JSocket remote ( ret );
  dmtcp::DmtcpMessage hello_remote;
  hello_remote.poison();
  remote >> hello_remote;
  hello_remote.assertValid();
  JASSERT ( hello_remote.type == dmtcp::DMT_HELLO_PEER );
  JASSERT ( dmtcp::DmtcpWorker::instance().coordinatorId() == hello_remote.coordinator )
  ( dmtcp::DmtcpWorker::instance().coordinatorId() ) ( hello_remote.coordinator )
  .Text ( "peer has a different dmtcp_coordinator than us! it must be the same." );

  JTRACE ( "accept handshake complete." ) ( hello_remote.from );

  dmtcp::TcpConnection* con = new dmtcp::TcpConnection ( parent, hello_remote.from );
  dmtcp::KernelDeviceToConnection::Instance().create ( ret, con );

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
///called automatically when a socket error is returned by user function
extern "C" int dmtcp_on_error ( int ret, int sockfd, const char* fname )
{
  JTRACE ( "socket error" ) ( fname ) ( ret ) ( sockfd ) ( JASSERT_ERRNO );

  //Ignore EAGAIN errors
  if ( errno == EAGAIN ) return ret;

  dmtcp::Connection& con = dmtcp::KernelDeviceToConnection::Instance().retrieve ( sockfd );

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

  dmtcp::TcpConnection& con = dmtcp::KernelDeviceToConnection::Instance().retrieve ( sockfd ).asTcp();

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
// dmtcp::SocketTable& dmtcp::SocketTable::Instance()
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
// //     typedef std::map< int, std::map< int, jalib::JBuffer > >::iterator levelIterator;
// //     typedef std::map< int, jalib::JBuffer >::iterator optionIterator;
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
