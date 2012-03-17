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
#include "connectionstate.h"
#include "dmtcpmessagetypes.h"
#include "syslogwrappers.h"
#include "connectionrewirer.h"

dmtcp::ConnectionState::ConnectionState ( const ConnectionToFds& ctfd )
    : _conToFds ( ctfd )
{}

void dmtcp::ConnectionState::deleteDupFileConnections()
{
  ConnectionList& connections = ConnectionList::instance();

#ifdef DEBUG
  ostringstream out;
  out << "Duplicate file connections: \n";
#endif
  // i is a list of all File Connections, including those which were dup()'d
  ConnectionList::iterator i;
  for ( i= connections.begin(); i != connections.end(); ++i )
  {
    if ( i->second->conType() != Connection::FILE )
      continue;
    FileConnection* fileConI = (FileConnection*) i->second;

#ifdef DEBUG
    out << "\t" << fileConI->filePath() << ": " <<  (i->first) << ":\n";
#endif

    // Search ahead with j, and erase any dup()'s of i of type Connection::FILE
    ConnectionList::iterator prevJ = i;
    ConnectionList::iterator j = prevJ;
    j++;
    for ( ; j != connections.end() ; prevJ = j, j++ ) {
      FileConnection* fileConJ = (FileConnection*) j->second;
      if ( j->second->conType() != Connection::FILE )
        continue;
      else if ( fileConJ->isDupConnection( *fileConI, _conToFds ) ) {
#ifdef DEBUG
        out << "\t\t" << (j->first) << "\n";
#endif
        for ( size_t st = 0; st < _conToFds[j->first].size(); st++ ) {
          _conToFds[i->first].push_back ( _conToFds[j->first][st] );
        }
        _conToFds.erase( fileConJ->id() );
        connections.erase ( j ); // returns next position after old j pos
        j = prevJ; // old position of j is now undefined, so back up to prev j
      }
    }
  }
#ifdef DEBUG
  JTRACE ("Deleting/Merging duplicate connections") (out.str());
#endif

//  for ( ConnectionToFds::iterator cfIt = _conToFds.begin();
//        cfIt != _conToFds.end();
//        ++cfIt ) {
//    JTRACE("ConToFds")(cfIt->first);
//  }
}

void dmtcp::ConnectionState::deleteStaleConnections()
{
  ConnectionList& connections = ConnectionList::instance();

  //build list of stale connections
  dmtcp::vector<ConnectionIdentifier> staleConnections;
  ConnectionList::iterator i;
  for ( i= connections.begin(); i != connections.end(); ++i )
  {
    if ( _conToFds[i->first].size() == 0 )
      staleConnections.push_back ( i->first );
  }

#ifdef DEBUG
  if (staleConnections.size() > 0) {
    dmtcp::ostringstream out;
    out << "\tDevice \t\t->\t ConnectionId \n";
    out << "==================================================\n";
    for ( size_t i=0; i<staleConnections.size(); ++i ) {
      out << "\t"
          << KernelDeviceToConnection::instance().getDevice(staleConnections[i])
          << "\t->\t" << staleConnections[i] << "\n";
    }
    out << "==================================================\n";
    JTRACE("Deleting Stale Connections") (out.str());
  }
#endif

  //delete all the stale connections
  for ( size_t i=0; i<staleConnections.size(); ++i ) {
    connections.erase ( staleConnections[i] );
  }
}

void dmtcp::ConnectionState::preLockSaveOptions()
{
  SyslogCheckpointer::stopService();

  // build fd table with stale connections included
  _conToFds = ConnectionToFds ( KernelDeviceToConnection::instance() );
  // now delete stale connections
  deleteStaleConnections();

  // Save Options for each Fd (We need to do it here instead of
  // preCheckpointFdLeaderElection because we want to restore the correct owner
  // in postcheckpoint).
  ConnectionList& connections = ConnectionList::instance();
  ConnectionList::iterator i;
  for ( i= connections.begin(); i != connections.end(); ++i ) {
    JASSERT ( _conToFds[i->first].size() != 0 );

    ( i->second )->saveOptions ( _conToFds[i->first] );
  }
}

void dmtcp::ConnectionState::preCheckpointFdLeaderElection()
{
  ConnectionList& connections = ConnectionList::instance();
  ConnectionList::iterator i;
  for ( i= connections.begin(); i != connections.end(); ++i ) {
    JASSERT ( _conToFds[i->first].size() != 0 );

    ( i->second )->doLocking ( _conToFds[i->first] );
  }
}

#ifdef EXTERNAL_SOCKET_HANDLING
void dmtcp::ConnectionState::preCheckpointPeerLookup( dmtcp::vector<TcpConnectionInfo>& conInfoTable )
{
  deleteStaleConnections();
  ConnectionList& connections = ConnectionList::instance();

  ConnectionList::iterator i;
  for ( i= connections.begin(); i != connections.end(); ++i )
  {
    if ( ( i->second )->conType() == Connection::TCP )
    {
      ( (TcpConnection *) (i->second) )->preCheckpointPeerLookup ( _conToFds[i->first], conInfoTable );
    }
  }
}
#endif

void dmtcp::ConnectionState::preCheckpointDrain()
{
  deleteStaleConnections();

  ConnectionList& connections = ConnectionList::instance();

  //initialize the drainer
  ConnectionList::iterator i;
  for ( i= connections.begin(); i != connections.end(); ++i )
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
    KernelDeviceToConnection::instance().redirect(fds[0], id);
  }

  //re build fd table without stale connections and with disconnects
  _conToFds = ConnectionToFds ( KernelDeviceToConnection::instance() );

  //FIXME: Moving the deleteDupFileConnections() near the top of this function
  //breaks make check-tcsh, why?
  deleteDupFileConnections();
}

void dmtcp::ConnectionState::preCheckpointHandshakes(const UniquePid& coordinator)
{
  ConnectionList& connections = ConnectionList::instance();

  //must send first to avoid deadlock
  //we are relying on OS buffers holding our message without blocking
  ConnectionList::iterator i;
  for ( i= connections.begin(); i != connections.end(); ++i )
  {
    const dmtcp::vector<int>& fds = _conToFds[i->first];
    Connection* con =  i->second;
    if ( fds.size() > 0 ){
      con->doSendHandshakes(fds, coordinator);
    }
  }

  //now receive
  for ( i = connections.begin(); i != connections.end(); ++i )
  {
    const dmtcp::vector<int>& fds = _conToFds[i->first];
    Connection* con =  i->second;
    if ( fds.size() > 0 ){
      con->doRecvHandshakes(fds, coordinator);
    }
  }
}

void dmtcp::ConnectionState::outputDmtcpConnectionTable
  (jalib::JBinarySerializer& o)
{
  _conToFds.serialize(o);
}


void dmtcp::ConnectionState::postCheckpoint( bool isRestart )
{
  _drain.refillAllSockets();

  ConnectionList& connections = ConnectionList::instance();
  ConnectionList::iterator i;
  for ( i= connections.begin(); i != connections.end(); ++i )
  {
    if ( _conToFds[i->first].size() <= 0 ) {
      JWARNING(false)  ( i->first.conId() )
        .Text ( "WARNING:: stale connections should be gone by now" );
    }

    if ( _conToFds[i->first].size() == 0 ) continue;

    ( i->second )->postCheckpoint ( _conToFds[i->first], isRestart );
  }

  SyslogCheckpointer::restoreService();
}

void dmtcp::ConnectionState::postRestart()
{
  ConnectionList& connections = ConnectionList::instance();

  // Two part restoreOptions. See the comments in doReconnect()
  // Part 1: Restore options for all but Pseudo-terminal slaves
  ConnectionList::iterator i;
  for ( i= connections.begin(); i != connections.end(); ++i )
  {
    JWARNING ( _conToFds[i->first].size() > 0 ).Text ( "stale connections should be gone by now" );
    if ( _conToFds[i->first].size() == 0 ) continue;

    if ( ( i->second )->conType() == Connection::PTY &&
         ( ( (PtyConnection*) (i->second) )->ptyType() == PtyConnection::PTY_SLAVE ||
           ( (PtyConnection*) (i->second) )->ptyType() == PtyConnection::PTY_BSD_SLAVE ) ) { }
    else {
      ( i->second )->restoreOptions ( _conToFds[i->first] );
    }
  }

  // Part 2: Restore options for all Pseudo-terminal slaves
  for ( i= connections.begin(); i != connections.end(); ++i )
  {
    if ( _conToFds[i->first].size() == 0 ) continue;

    if ( ( i->second )->conType() == Connection::PTY &&
         ( ( (PtyConnection*) (i->second) )->ptyType() == PtyConnection::PTY_SLAVE ||
           ( (PtyConnection*) (i->second) )->ptyType() == PtyConnection::PTY_BSD_SLAVE ) ) {
      ( i->second )->restoreOptions ( _conToFds[i->first] );
    }
  }

  KernelDeviceToConnection::instance().dbgSpamFds();

  //fix our device table to match the new world order
  KernelDeviceToConnection::instance() = KernelDeviceToConnection ( _conToFds );
}

void dmtcp::ConnectionState::doReconnect ( jalib::JSocket& coordinator, jalib::JSocket& restoreListen )
{
  _rewirer.addDataSocket ( new jalib::JChunkReader ( coordinator,sizeof ( DmtcpMessage ) ) );
  _rewirer.addListenSocket ( restoreListen );
  _rewirer.setCoordinatorFd ( coordinator.sockfd() );

  handleDuplicateFilesInSeparateConnections();

  ConnectionList& connections = ConnectionList::instance();

  // Here we modify the restore algorithm by splitting it in two parts. In the
  // first part we restore all the connection except the PTY_SLAVE types and in
  // the second part we restore only PTY_SLAVE connections. This is done to
  // make sure that by the time we are trying to restore a PTY_SLAVE
  // connection, its corresponding PTY_MASTER connection has already been
  // restored.
  // UPDATE: We also restore the files for which the we didn't have the lock in
  //         second iteration along with PTY_SLAVEs
  // Part 1: Restore all but Pseudo-terminal slaves and file connection which
  //         were not checkpointed
  ConnectionList::iterator i;
  for ( i= connections.begin(); i != connections.end(); ++i )
  {
    JASSERT ( _conToFds[i->first].size() > 0 )
      .Text ( "stale connections should be gone by now" );

    if ( (i->second)->restoreInSecondIteration() == false ) {
      ( i->second )->restore ( _conToFds[i->first], _rewirer );
    }
  }

  // Part 2: Restore all Pseudo-terminal slaves and file connections that were
  //         not checkpointed.
  for ( i= connections.begin(); i != connections.end(); ++i )
  {
    JASSERT ( _conToFds[i->first].size() > 0 )
      .Text ( "stale connections should be gone by now" );

    if ( ( i->second )->restoreInSecondIteration() == true ) {
      ( i->second )->restore ( _conToFds[i->first], _rewirer );
    }
  }
  _rewirer.doReconnect();
}

void dmtcp::ConnectionState::handleDuplicateFilesInSeparateConnections()
{
  ostringstream out;
  out << "Duplicate files in different connections: \n";

  ConnectionList& connections = ConnectionList::instance();
  ConnectionList::iterator i;
  for ( i= connections.begin(); i != connections.end(); ++i )
  {
    if ( i->second->conType() != Connection::FILE ) continue;
    if ( i->second->conType() == Connection::EPOLL) 
        out << "found epoll fd\n";

    FileConnection* fileConI = (FileConnection*) i->second;
    if (fileConI->checkpointed() == false) continue;

    out << "\t" << fileConI->filePath() << ": " <<  (i->first) << ":\n";

    ConnectionList::iterator j = i; j++;
    for ( ; j != connections.end() ; j++ ) {
      FileConnection* fileConJ = (FileConnection*) j->second;

      if ( j->second->conType() != Connection::FILE ) continue;

      if (fileConJ->checkpointed() &&
          fileConJ->filePath() == fileConI->filePath()) {
        fileConJ->doNotRestoreCkptCopy();
        out << "\t\t" << (j->first) << "\n";
      }
    }
  }
  JTRACE ("Duplicate files in separate connections") (out.str()); }
