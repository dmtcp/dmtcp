/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "util.h"
#include "dmtcp.h"
#include "shareddata.h"
#include "jfilesystem.h"
#include "jconvert.h"
#include "jassert.h"
#include "jsocket.h"

#include "util_ipc.h"
#include "connection.h"
#include "connectionlist.h"

using namespace dmtcp;

// This is the first program after dmtcp_launch
static bool freshProcess = true;

dmtcp::ConnectionList::~ConnectionList()
{
}

void dmtcp::ConnectionList::eventHook(DmtcpEvent_t event,
                                         DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_INIT:
      // Delete Stale Connections if any.
      deleteStaleConnections();
      if (freshProcess) {
        scanForPreExisting();
      }
      break;

    case DMTCP_EVENT_PRE_EXEC:
      {
        jalib::JBinarySerializeWriterRaw wr("", data->serializerInfo.fd);
        serialize(wr);
      }
      break;

    case DMTCP_EVENT_POST_EXEC:
      {
        freshProcess = false;
        jalib::JBinarySerializeReaderRaw rd("", data->serializerInfo.fd);
        serialize(rd);
        deleteStaleConnections();
      }
      break;

    case DMTCP_EVENT_RESTART:
      postRestart();

      break;

    case DMTCP_EVENT_THREADS_SUSPEND:
      preLockSaveOptions();
      break;

    case DMTCP_EVENT_LEADER_ELECTION:
      JTRACE("locking...");
      preCkptFdLeaderElection();
      JTRACE("locked");
      break;

    case DMTCP_EVENT_DRAIN:
      JTRACE("draining...");
      drain();
      JTRACE("drained");
      break;

    case DMTCP_EVENT_WRITE_CKPT:
      JTRACE("preCKpt...");
      preCkpt();
      JTRACE("done preCkpt");
      break;

    case DMTCP_EVENT_REFILL:
      refill(data->refillInfo.isRestart);
      break;

    case DMTCP_EVENT_THREADS_RESUME:
      resume(data->resumeInfo.isRestart);
      break;

    case DMTCP_EVENT_REGISTER_NAME_SERVICE_DATA:
      registerNSData(data->nameserviceInfo.isRestart);
      break;

    case DMTCP_EVENT_SEND_QUERIES:
      sendQueries(data->nameserviceInfo.isRestart);
      break;

    default:
      break;
  }

  return;
}

static bool _isBadFd(int fd)
{
  errno = 0;
  return _real_fcntl(fd, F_GETFL, 0) == -1 && errno == EBADF;
}

//static ConnectionList *connectionList = NULL;
//dmtcp::ConnectionList& dmtcp::ConnectionList::instance()
//{
//  if (connectionList == NULL) {
//    connectionList = new ConnectionList();
//  }
//  return *connectionList;
//}

void dmtcp::ConnectionList::resetOnFork()
{
  JASSERT(pthread_mutex_destroy(&_lock) == 0) (JASSERT_ERRNO);
  JASSERT(pthread_mutex_init(&_lock, NULL) == 0) (JASSERT_ERRNO);
}


void dmtcp::ConnectionList::deleteStaleConnections()
{
  //build list of stale connections
  vector<int> staleFds;
  for (FdToConMapT::iterator i = _fdToCon.begin(); i != _fdToCon.end(); ++i) {
    if (_isBadFd(i->first)) {
      staleFds.push_back(i->first);
    }
  }

#ifdef DEBUG
  if (staleFds.size() > 0) {
    dmtcp::ostringstream out;
    out << "\tDevice \t\t->\t File Descriptor -> ConnectionId\n";
    out << "==================================================\n";
    for (size_t i = 0; i < staleFds.size(); ++i) {
      Connection *c = getConnection(staleFds[i]);

      out << "\t[" << jalib::XToString(staleFds[i]) << "]"
          << c->str()
          << "\t->\t" << staleFds[i]
          << "\t->\t" << c->id() << "\n";
    }
    out << "==================================================\n";
    JTRACE("Deleting Stale Connections") (out.str());
  }
#endif

  //delete all the stale connections
  for (size_t i = 0; i < staleFds.size(); ++i) {
    processClose(staleFds[i]);
  }
}

void dmtcp::ConnectionList::serialize(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp-serialized-connection-table!v0.07");

  JSERIALIZE_ASSERT_POINT("dmtcp::ConnectionIdentifier:");
  ConnectionIdentifier::serialize(o);

  JSERIALIZE_ASSERT_POINT("dmtcp::ConnectionList:");

  uint32_t numCons = _connections.size();
  o & numCons;

  if (o.isWriter()) {
    for (iterator i=_connections.begin(); i!=_connections.end(); ++i) {
      ConnectionIdentifier key = i->first;
      Connection& con = *i->second;
      uint32_t type = con.conType();

      JSERIALIZE_ASSERT_POINT("[StartConnection]");
      o & key & type;
      con.serialize(o);
      JSERIALIZE_ASSERT_POINT("[EndConnection]");
    }
  } else {
    while (numCons-- > 0) {
      ConnectionIdentifier key;
      int type = -1;
      Connection* con = NULL;

      JSERIALIZE_ASSERT_POINT("[StartConnection]");
      o & key & type;
      con = createDummyConnection(type);
      JASSERT(con != NULL) (key);
      con->serialize(o);
      _connections[key] = con;
      const vector<int32_t>& fds = con->getFds();
      for (size_t i = 0; i < fds.size(); i++) {
        _fdToCon[fds[i]] = con;
      }
      JSERIALIZE_ASSERT_POINT("[EndConnection]");
    }
  }
  JSERIALIZE_ASSERT_POINT("EOF");
}

void dmtcp::ConnectionList::list()
{
  ostringstream o;
  o << "\n";
  for (iterator i = begin(); i != end(); i++) {
    Connection *c = i->second;
    vector<int> fds = c->getFds();
    for (size_t j = 0; j<fds.size(); j++) {
      o << fds[j];
      if (j < fds.size() - 1)
        o << "," ;
    }
    o << "\t" << i->first << "\t" << c->str();
    o << "\n";
  }
  JTRACE("ConnectionList") (dmtcp_get_uniquepid_str()) (o.str());
}

dmtcp::Connection*
dmtcp::ConnectionList::getConnection(const ConnectionIdentifier& id)
{
  if (_connections.find(id) == _connections.end()) {
    return NULL;
  }
  return _connections[id];
}

dmtcp::Connection *dmtcp::ConnectionList::getConnection(int fd)
{
  if (_fdToCon.find(fd) == _fdToCon.end()) {
    return NULL;
  }
  return _fdToCon[fd];
}

void dmtcp::ConnectionList::add(int fd, Connection* c)
{
  _lock_tbl();

  if (_fdToCon.find(fd) != _fdToCon.end()) {
    /* In ordinary situations, we never exercise this path since we already
     * capture close() and remove the connection. However, there is one
     * particular case where this assumption fails -- when gblic opens a socket
     * using socket() but closes it using the internal close_not_cancel() thus
     * bypassing our close wrapper. This behavior is observed when dealing with
     * getaddrinfo().
     */
    processCloseWork(fd);
  }

  if( _connections.find(c->id()) == _connections.end() )
    _connections[c->id()] = c;
  c->addFd(fd);
  _fdToCon[fd] = c;
  _unlock_tbl();
}

void dmtcp::ConnectionList::processCloseWork(int fd)
{
  Connection *con = _fdToCon[fd];
  _fdToCon.erase(fd);
  con->removeFd(fd);
  if (con->numFds() == 0) {
    _connections.erase(con->id());
    delete con;
  }
}

void dmtcp::ConnectionList::processClose(int fd)
{
  if (_fdToCon.find(fd) != _fdToCon.end()) {
    _lock_tbl();
    processCloseWork(fd);
    _unlock_tbl();
  }
}

void dmtcp::ConnectionList::processDup(int oldfd, int newfd)
{
  if (oldfd == newfd) return;
  if (_fdToCon.find(newfd) != _fdToCon.end()) {
    processClose(newfd);
  }

  // Add only if the oldfd was already in the _fdToCon table.
  if (_fdToCon.find(oldfd) != _fdToCon.end()) {
    _lock_tbl();
    Connection *con = _fdToCon[oldfd];
    _fdToCon[newfd] = con;
    con->addFd(newfd);
    _unlock_tbl();
  }
}

/*****************************************************/
/*****************************************************/
/*****************************************************/
/*****************************************************/
/*****************************************************/
/*****************************************************/
/*****************************************************/

void dmtcp::ConnectionList::preLockSaveOptions()
{
  deleteStaleConnections();
  list();
  // Save Options for each Fd(We need to do it here instead of
  // preCkptFdLeaderElection because we want to restore the correct owner
  // in refill).
  for (iterator i = begin(); i != end(); ++i) {
    Connection *con = i->second;
    con->saveOptions();
  }
}

void dmtcp::ConnectionList::preCkptFdLeaderElection()
{
  deleteStaleConnections();
  for (iterator i = begin(); i != end(); ++i) {
    Connection *con = i->second;
    JASSERT(con->numFds() > 0);
    con->doLocking();
  }
}

void dmtcp::ConnectionList::drain()
{
  for (iterator i = begin(); i != end(); ++i) {
    Connection* con =  i->second;
    con->checkLock();
    if (con->hasLock()) {
      con->drain();
    }
  }
}

void dmtcp::ConnectionList::preCkpt()
{
  for (iterator i = begin(); i != end(); ++i) {
    Connection* con =  i->second;
    if (con->hasLock()) {
      con->preCkpt();
    }
  }
}

void dmtcp::ConnectionList::refill(bool isRestart)
{
  for (iterator i = begin(); i != end(); ++i) {
    Connection *con = i->second;
    if (con->hasLock()) {
      con->refill(isRestart);
      con->restoreOptions();
    }
  }
  if (isRestart) {
    JTRACE("Waiting for Missing Cons");
    sendReceiveMissingFds();
    JTRACE("Done waiting for Missing Cons");
  }
}

void dmtcp::ConnectionList::resume(bool isRestart)
{
  for (iterator i = begin(); i != end(); ++i) {
    Connection *con = i->second;
    if (con->hasLock()) {
      con->resume(isRestart);
    }
  }
}

void dmtcp::ConnectionList::postRestart()
{
  // Here we modify the restore algorithm by splitting it in two parts. In the
  // first part we restore all the connection except the PTY_SLAVE types and in
  // the second part we restore only PTY_SLAVE _connections. This is done to
  // make sure that by the time we are trying to restore a PTY_SLAVE
  // connection, its corresponding PTY_MASTER connection has already been
  // restored.
  // UPDATE: We also restore the files for which the we didn't have the lock in
  //         second iteration along with PTY_SLAVEs
  // Part 1: Restore all but Pseudo-terminal slaves and file connection which
  //         were not checkpointed
  for (iterator i = begin(); i != end(); ++i) {
    Connection *con = i->second;
    if (!con->hasLock()) continue;

// TODO: FIXME: Add support for Socketpairs.
//    if (con->conType() == Connection::TCP) {
//      TcpConnection *tcpCon =(TcpConnection *) con;
//      if (tcpCon->peerType() == TcpConnection::PEER_SOCKETPAIR) {
//        ConnectionIdentifier peerId = tcpCon->getSocketpairPeerId();
//        TcpConnection *peerCon = (TcpConnection*) getConnection(peerId);
//        if (peerCon != NULL) {
//          tcpCon->restoreSocketPair(peerCon);
//          continue;
//        }
//      }
//    }
    con->postRestart();
  }

  registerMissingCons();
}


void dmtcp::ConnectionList::registerMissingCons()
{
  int protected_fd = protectedFd();
  // Add receive-fd data socket.
  static struct sockaddr_un fdReceiveAddr;
  static socklen_t         fdReceiveAddrLen;

  memset(&fdReceiveAddr, 0, sizeof(fdReceiveAddr));
  jalib::JSocket sock(_real_socket(AF_UNIX, SOCK_DGRAM, 0));
  JASSERT(sock.isValid());
  sock.changeFd(protected_fd);
  fdReceiveAddr.sun_family = AF_UNIX;
  JASSERT(_real_bind(protected_fd,
                     (struct sockaddr*) &fdReceiveAddr,
                     sizeof(fdReceiveAddr.sun_family)) == 0) (JASSERT_ERRNO);

  fdReceiveAddrLen = sizeof(fdReceiveAddr);
  JASSERT(getsockname(protected_fd,
                      (struct sockaddr *)&fdReceiveAddr,
                      &fdReceiveAddrLen) == 0);


  vector<const char *> missingCons;
  ostringstream in, out;
  for (iterator i = begin(); i != end(); ++i) {
    Connection *con = i->second;
    // Check comments in FileConnList::postRestart() for the explanation
    // about isPreExistingCTTY.
    if (!con->hasLock() && !con->isStdio() && !con->isPreExistingCTTY()) {
      missingCons.push_back((const char*)&i->first);
      in << "\n\t" << con->str() << i->first;
    } else {
      out << "\n\t" << con->str() << i->first;
    }
  }
  JTRACE("Missing/Outgoing Cons") (in.str()) (out.str());
  numMissingCons = missingCons.size();
  if (numMissingCons > 0) {
    SharedData::registerMissingCons(missingCons, fdReceiveAddr,
                                    fdReceiveAddrLen);
  }
}

void dmtcp::ConnectionList::sendReceiveMissingFds()
{
  size_t i;
  vector<int> outgoingCons;
  SharedData::MissingConMap *maps;
  uint32_t nmaps;
  SharedData::getMissingConMaps(&maps, &nmaps);
  for (i = 0; i < nmaps; i++) {
    ConnectionIdentifier *id = (ConnectionIdentifier*) maps[i].id;
    Connection *con = getConnection(*id);
    if (con != NULL && con->hasLock()) {
      outgoingCons.push_back(i);
    }
  }

  fd_set rfds;
  fd_set wfds;
  int restoreFd = protectedFd();
  size_t numOutgoingCons = outgoingCons.size();
  while (numOutgoingCons > 0 || numMissingCons > 0) {
    FD_ZERO(&wfds);
    if (outgoingCons.size() > 0) {
      FD_SET(restoreFd, &wfds);
    }
    FD_ZERO(&rfds);
    if (numMissingCons > 0) {
      FD_SET(restoreFd, &rfds);
    }

    int ret = _real_select(restoreFd+1, &rfds, &wfds, NULL, NULL);
    JASSERT(ret != -1) (JASSERT_ERRNO);

    if (numOutgoingCons > 0 && FD_ISSET(restoreFd, &wfds)) {
      size_t idx = outgoingCons.back();
      outgoingCons.pop_back();
      ConnectionIdentifier *id = (ConnectionIdentifier*) maps[idx].id;
      Connection *con = getConnection(*id);
      JTRACE("Sending Missing Con") (*id);
      JASSERT(sendFd(restoreFd, con->getFds()[0], id, sizeof(*id),
                     maps[idx].addr, maps[idx].len) != -1);
      numOutgoingCons--;
    }

    if (numMissingCons > 0 && FD_ISSET(restoreFd, &rfds)) {
      ConnectionIdentifier id;
      int fd = receiveFd(restoreFd, &id, sizeof(id));
      JASSERT(fd != -1);
      Connection *con = getConnection(id);
      JTRACE("Received Missing Con") (id);
      JASSERT(con != NULL);
      Util::dupFds(fd, con->getFds());
      numMissingCons--;
    }
  }
  dmtcp_close_protected_fd(restoreFd);
}
