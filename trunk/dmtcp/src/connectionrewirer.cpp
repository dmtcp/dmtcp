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
#include "connectionrewirer.h"
#include "dmtcpmessagetypes.h"
#include "syscallwrappers.h"



void dmtcp::ConnectionRewirer::onData ( jalib::JReaderInterface* sock )
{
  JASSERT ( sock->bytesRead() == sizeof ( DmtcpMessage ) ) ( sock->bytesRead() ) ( sizeof ( DmtcpMessage ) );
  DmtcpMessage& msg = * ( DmtcpMessage* ) sock->buffer();
  msg.assertValid();

  if ( msg.type == DMT_FORCE_RESTART )
  {
    JTRACE ( "got DMT_FORCE_RESTART, exiting ConnectionRewirer" ) ( _pendingOutgoing.size() ) ( _pendingIncoming.size() );
    _pendingIncoming.clear();
    _pendingOutgoing.clear();
    finishup();
    return;
  }

  JASSERT ( msg.type==DMT_RESTORE_WAITING ) ( msg.type ).Text ( "unexpected message" );

  iterator i = _pendingOutgoing.find ( msg.restorePid );


  if ( i == _pendingOutgoing.end() )
  {
    JTRACE ( "got RESTORE_WAITING MESSAGE [not used]" ) ( msg.restorePid ) ( _pendingOutgoing.size() ) ( _pendingIncoming.size() );
  }
  else
  {
    JTRACE ( "got RESTORE_WAITING MESSAGE, reconnecting..." )
    ( msg.restorePid ) ( msg.restorePort ) ( msg.restoreAddrlen ) ( _pendingOutgoing.size() ) ( _pendingIncoming.size() );
    const std::vector<int>& fds = i->second;
    JASSERT ( fds.size() > 0 );
    int fd0 = fds[0];

    jalib::JSocket remote = jalib::JSocket::Create();
    remote.changeFd ( fd0 );
    JASSERT ( remote.connect ( ( sockaddr* ) &msg.restoreAddr,msg.restoreAddrlen,msg.restorePort ) )
    ( msg.restorePid ) ( msg.restorePort ) ( JASSERT_ERRNO )
    .Text ( "failed to restore connection" );

    {
      DmtcpMessage peerMsg;
      peerMsg.type = DMT_RESTORE_RECONNECTED;
      peerMsg.restorePid = msg.restorePid;
      addWrite ( new jalib::JChunkWriter ( remote, ( char* ) &peerMsg,sizeof ( peerMsg ) ) );
    }

    for ( size_t n = 1; n<fds.size(); ++n )
    {
      JTRACE ( "restoring extra fd" ) ( fd0 ) ( fds[n] );
      JASSERT ( _real_dup2 ( fd0,fds[n] ) == fds[n] ) ( fd0 ) ( fds[n] ) ( msg.restorePid )
      .Text ( "dup2() failed" );
    }

    _pendingOutgoing.erase ( i );
  }

  if ( pendingCount() ==0 ) finishup();
#ifdef DEBUG
  else debugPrint();
#endif
}

void dmtcp::ConnectionRewirer::onConnect ( const jalib::JSocket& sock,  const struct sockaddr* /*remoteAddr*/,socklen_t /*remoteLen*/ )
{
  jalib::JSocket remote = sock;
  DmtcpMessage msg;
  msg.poison();
  remote >> msg;
  msg.assertValid();
  JASSERT ( msg.type == DMT_RESTORE_RECONNECTED ) ( msg.type ).Text ( "unexpected message" );

  iterator i = _pendingIncoming.find ( msg.restorePid );

  JASSERT ( i != _pendingIncoming.end() ) ( msg.restorePid )
  .Text ( "got unexpected incoming restore request" );

  const std::vector<int>& fds = i->second;
  JASSERT ( fds.size() > 0 );
  int fd0 = fds[0];

  remote.changeFd ( fd0 );

  JTRACE ( "restoring incoming connection" ) ( msg.restorePid ) ( fd0 ) ( fds.size() );

  for ( size_t i = 1; i<fds.size(); ++i )
  {
    JTRACE ( "restoring extra fd" ) ( fd0 ) ( fds[i] );
    JASSERT ( _real_dup2 ( fd0,fds[i] ) == fds[i] ) ( fd0 ) ( fds[i] ) ( msg.restorePid )
    .Text ( "dup2() failed" );
  }

  _pendingIncoming.erase ( i );


  if ( pendingCount() ==0 ) finishup();
#ifdef DEBUG
  else debugPrint();
#endif
}

void dmtcp::ConnectionRewirer::finishup()
{
  JTRACE ( "finishup begin" ) ( _listenSockets.size() ) ( _dataSockets.size() );
  //i expect both sizes above to be 1
  //close the restoreSocket
  for ( size_t i=0; i<_listenSockets.size(); ++i )
    _listenSockets[i].close();
  //poison the coordinator socket listener
  for ( size_t i=0; i<_dataSockets.size(); ++i )
    _dataSockets[i]->socket() = -1;

//     JTRACE("finishup end");
}

void dmtcp::ConnectionRewirer::onDisconnect ( jalib::JReaderInterface* sock )
{
  JASSERT ( sock->socket().sockfd() < 0 )
  .Text ( "dmtcp_coordinator disconnected" );
}

int dmtcp::ConnectionRewirer::coordinatorFd() const
{
  return _coordinatorFd;
}


void dmtcp::ConnectionRewirer::setCoordinatorFd ( const int& theValue )
{
  _coordinatorFd = theValue;
}

void dmtcp::ConnectionRewirer::doReconnect()
{
  if ( pendingCount() > 0 ) monitorSockets();
}

void dmtcp::ConnectionRewirer::registerIncoming ( const ConnectionIdentifier& local
        , const std::vector<int>& fds )
{
  _pendingIncoming[local] = fds;

  JTRACE ( "announcing pending incoming" ) ( local );
  DmtcpMessage msg;
  msg.type = DMT_RESTORE_WAITING;
  msg.restorePid = local;

  JASSERT ( _coordinatorFd > 0 );
  addWrite ( new jalib::JChunkWriter ( _coordinatorFd , ( char* ) &msg, sizeof ( DmtcpMessage ) ) );
}

void dmtcp::ConnectionRewirer::registerOutgoing ( const ConnectionIdentifier& remote
        , const std::vector<int>& fds )
{
  _pendingOutgoing[remote] = fds;
}

void dmtcp::ConnectionRewirer::debugPrint() const
{
  JASSERT_STDERR << "Pending Incoming:\n";
  for ( const_iterator i = _pendingIncoming.begin(); i!=_pendingIncoming.end(); ++i )
  {
    JASSERT_STDERR << i->first << " numFds=" << i->second.size() << " firstFd=" << i->second[0] << '\n';
  }
  JASSERT_STDERR << "Pending Outgoing:\n";
  for ( const_iterator i = _pendingOutgoing.begin(); i!=_pendingOutgoing.end(); ++i )
  {
    JASSERT_STDERR << i->first << " numFds=" << i->second.size() << " firstFd=" << i->second[0] << '\n';
  }
}
