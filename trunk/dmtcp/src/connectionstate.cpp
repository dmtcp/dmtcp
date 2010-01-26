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

#include "connectionstate.h"
#include "constants.h"
#include "dmtcpmessagetypes.h"
#include "syslogcheckpointer.h"
#include "signalmanager.h"
#include "dmtcpworker.h"
#include "connectionrewirer.h"

dmtcp::ConnectionState::ConnectionState ( const ConnectionToFds& ctfd )
    : _conToFds ( ctfd )
{}


void dmtcp::ConnectionState::preCheckpointLock()
{
  SignalManager::saveSignals();
  SyslogCheckpointer::stopService();

  // build fd table with stale connections included
  _conToFds = ConnectionToFds ( KernelDeviceToConnection::Instance() );

  //lock each fd
  ConnectionList& connections = ConnectionList::Instance();
  for ( ConnectionList::iterator i = connections.begin()
      ; i!= connections.end()
      ; ++i )
  {
    if ( _conToFds[i->first].size() == 0 ) continue;

    ( i->second )->saveOptions ( _conToFds[i->first] );
    ( i->second )->doLocking ( _conToFds[i->first] );
  }


}
void dmtcp::ConnectionState::preCheckpointDrain()
{
  ConnectionList& connections = ConnectionList::Instance();

  //build list of stale connections
  dmtcp::vector<ConnectionList::iterator> staleConnections;
  for ( ConnectionList::iterator i = connections.begin()
        ; i!= connections.end()
        ; ++i )
  {
    if ( _conToFds[i->first].size() == 0 )
      staleConnections.push_back ( i );
  }

  //delete all the stale connections
  for ( size_t i=0; i<staleConnections.size(); ++i )
  {
    JTRACE ( "deleting stale connection" ) ( staleConnections[i]->first );
    connections.erase ( staleConnections[i] );
  }

  //initialize the drainer
  for ( ConnectionList::iterator i = connections.begin()
      ; i!= connections.end()
      ; ++i )
  {
    if ( _conToFds[i->first].size() > 0 )
    {
      ( i->second )->preCheckpoint ( _conToFds[i->first], _drain );
    }
  }

  //this will block until draining is complete
  _drain.monitorSockets ( DRAINER_CHECK_FREQ );

  //handle disconnected sockets
  const dmtcp::vector<ConnectionIdentifier>& discn = _drain.getDisconnectedSockets();
  for(size_t i=0; i<discn.size(); ++i){
    const ConnectionIdentifier& id = discn[i];
    TcpConnection& con = connections[id].asTcp();
    dmtcp::vector<int>& fds = _conToFds[discn[i]];
    JASSERT(fds.size()>0);
    JTRACE("recreating disconnected socket")(fds[0])(id);

    //reading from the socket, and taking the error, resulted in an implicit close().
    //we will create a new, broken socket that is not closed

    con.onError();
    static ConnectionRewirer ignored;
    con.restore(fds, ignored); //restoring a TCP_ERROR connection makes a dead socket
    KernelDeviceToConnection::Instance().redirect(fds[0], id);
  }

  //re build fd table without stale connections and with disconnects
  _conToFds = ConnectionToFds ( KernelDeviceToConnection::Instance() );
}

void dmtcp::ConnectionState::preCheckpointHandshakes(const UniquePid& coordinator)
{
  ConnectionList& connections = ConnectionList::Instance();

  //must send first to avoid deadlock
  //we are relying on OS buffers holding our message without blocking
  for ( ConnectionList::iterator i = connections.begin()
      ; i!= connections.end()
      ; ++i )
  {
    const dmtcp::vector<int>& fds = _conToFds[i->first];
    Connection* con =  i->second;
    if ( fds.size() > 0 ){
      con->doSendHandshakes(fds, coordinator);
    }
  }

  //now receive
  for ( ConnectionList::iterator i = connections.begin()
      ; i!= connections.end()
      ; ++i )
  {
    const dmtcp::vector<int>& fds = _conToFds[i->first];
    Connection* con =  i->second;
    if ( fds.size() > 0 ){
      con->doRecvHandshakes(fds, coordinator);
    }
  }
}

void dmtcp::ConnectionState::outputDmtcpConnectionTable(int fd)
{
    //write out the *.dmtcp file
  //dmtcp::string serialFile = dmtcp::UniquePid::dmtcpCheckpointFilename();
  //JTRACE ( "Writing *.dmtcp checkpoint file" );
  jalib::JBinarySerializeWriterRaw wr ( "mtcp-file-prefix", fd );

  wr & _compGroup;
  wr & _numPeers;
  _conToFds.serialize ( wr );

#ifdef PID_VIRTUALIZATION
  dmtcp::VirtualPidTable::Instance().refresh( );
  dmtcp::VirtualPidTable::Instance().serialize( wr );
#endif
}


void dmtcp::ConnectionState::postCheckpoint()
{
  _drain.refillAllSockets();

  ConnectionList& connections = ConnectionList::Instance();
  for ( ConnectionList::iterator i= connections.begin()
      ; i!= connections.end()
      ; ++i )
  {
    JWARNING ( _conToFds[i->first].size() > 0 ) ( i->first.conId() )
    .Text ( "stale connections should be gone by now" );
    if ( _conToFds[i->first].size() == 0 ) continue;

    ( i->second )->postCheckpoint ( _conToFds[i->first] );
  }

  SyslogCheckpointer::restoreService();
  SignalManager::restoreSignals();
}

void dmtcp::ConnectionState::postRestart()
{
  ConnectionList& connections = ConnectionList::Instance();
  for ( ConnectionList::iterator i= connections.begin()
      ; i!= connections.end()
      ; ++i )
  {
    JWARNING ( _conToFds[i->first].size() > 0 ).Text ( "stale connections should be gone by now" );
    if ( _conToFds[i->first].size() == 0 ) continue;


    ( i->second )->restoreOptions ( _conToFds[i->first] );
  }

  KernelDeviceToConnection::Instance().dbgSpamFds();

  //fix our device table to match the new world order
  KernelDeviceToConnection::Instance() = KernelDeviceToConnection ( _conToFds );
}

void dmtcp::ConnectionState::doReconnect ( jalib::JSocket& coordinator, jalib::JSocket& restoreListen )
{
  _rewirer.addDataSocket ( new jalib::JChunkReader ( coordinator,sizeof ( DmtcpMessage ) ) );
  _rewirer.addListenSocket ( restoreListen );
  _rewirer.setCoordinatorFd ( coordinator.sockfd() );

  ConnectionList& connections = ConnectionList::Instance();

  // Here we modify the restore algorithm by splitting it in two parts. In the
  // first part we restore all the connection except the PTY_SLAVE types and in
  // the second part we restore only PTY_SLAVE connections. This is done to
  // make sure that by the time we are trying to restore a PTY_SLAVE
  // connection, its corresponding PTY_MASTER connection has already been
  // restored.
  for ( ConnectionList::iterator i= connections.begin()
      ; i!= connections.end()
      ; ++i )
  {
    JASSERT ( _conToFds[i->first].size() > 0 ).Text ( "stale connections should be gone by now" );

    if ( ( i->second )->conType() == Connection::PTY &&
         ( (PtyConnection*) (i->second) )->ptyType() == PtyConnection::PTY_SLAVE ) { }
    else {
      ( i->second )->restore ( _conToFds[i->first], _rewirer );
    }
  }

  for ( ConnectionList::iterator i= connections.begin()
      ; i!= connections.end()
      ; ++i )
  {
    JASSERT ( _conToFds[i->first].size() > 0 ).Text ( "stale connections should be gone by now" );

    if ( ( i->second )->conType() == Connection::PTY &&
         ((PtyConnection*) (i->second) )->ptyType() == PtyConnection::PTY_SLAVE ) {
      ( i->second )->restore ( _conToFds[i->first], _rewirer );
    }
  }
  _rewirer.doReconnect();
}
