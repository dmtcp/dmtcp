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
