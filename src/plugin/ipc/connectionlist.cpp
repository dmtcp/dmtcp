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

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "jassert.h"
#include "jconvert.h"
#include "jfilesystem.h"
#include "jsocket.h"
#include "dmtcp.h"
#include "shareddata.h"
#include "util.h"

#include "connection.h"
#include "connectionlist.h"
#include "util_ipc.h"

// Each fd may be shared or private.  If a fd is shared, this situation
// must be restored at restart time, and only one process should set
// the properties of that shared fd.  The sequence of events follows.

// At the time of checkpoint, a leader election algorithm is used to decide
// on a unique process as leader, who will be responsible for restoring the
// state of a file descriptor (whether shared or not) at the time of restart.
// It is implemented through doLocking and checkLocking, in which each
// process uses fcntl() with SETOWN, and after a barrier, they check
// if they are still the owner using GETOWN.
// A list of connections (fd's) is found through /proc/*/fd and saved
// in the ConnectionList object.
// At the time of restart, con->hasLock() will tell a process if it is the
// the leader for that connection (con).  However, the process must still
// discover if the file descriptor is private or shared.
// We define a "private fd" as an fd that is shared by exactly one process.
// This criterion is used in the implementation.
// There is a list of outgoing connections (outgoingCons) and
// incoming connections (incomingCons).  These will refer to shared fd's.
// For the various processes on the same host, any shared fd corresponding
// to a connection has a 'con->hasLock()' method.  If it's true, this
// process owns the shared fd, and must send it as an outgoing connection.
// If it's false, this process does not own the shared fd, and it must
// receive it from another process.
// The connections of the ConnectionList object are initially entered into
// a list of outgoing connections.  If a process sees a fd and it does not
// have the lock (if con->hasLock() == false), then the process knows that
// the fd must be shared.  So, the process declares this fd to be "incoming".
// Then registerIncomingCons() can separate the fd's into incoming and outgoing
// connections.  This is done by looking up the shared-area to see which
// connections have been declared as "incoming" by other processes.
// A UNIX domain socket (called 'restoreFd' or 'protected_fd', depending
// on the function, but always deduced from protectedFd()) is used to
// send the outgoing connections, and to receive the incoming connections
// at restart time, so that the corresponding fd's can again be shared.
// The owner of each outgoing connection sends the fd out, and for each
// process, if that connection is on its list of incoming connections
// (missing connections), then the fd is kept as a shared fd.

using namespace dmtcp;

// This is the first program after dmtcp_launch
static bool freshProcess = true;

ConnectionList::~ConnectionList()
{}

void
ConnectionList::eventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_INIT:

    // Delete stale connections if any.
    deleteStaleConnections();
    if (freshProcess) {
      scanForPreExisting();
    }
    break;

  case DMTCP_EVENT_PRE_EXEC:
  {
    jalib::JBinarySerializeWriterRaw wr("", data->serializerInfo.fd);
    serialize(wr);
    break;
  }

  case DMTCP_EVENT_POST_EXEC:
  {
    freshProcess = false;
    jalib::JBinarySerializeReaderRaw rd("", data->serializerInfo.fd);
    serialize(rd);
    deleteStaleConnections();
    break;
  }

  default:
    break;
  }
}

static bool
_isBadFd(int fd)
{
  errno = 0;
  return _real_fcntl(fd, F_GETFL, 0) == -1 && errno == EBADF;
}

// static ConnectionList *connectionList = NULL;
// ConnectionList& ConnectionList::instance()
// {
// if (connectionList == NULL) {
// connectionList = new ConnectionList();
// }
// return *connectionList;
// }

void
ConnectionList::resetOnFork()
{
  DmtcpMutexInit(&_lock, DMTCP_MUTEX_NORMAL);
}

void
ConnectionList::deleteStaleConnections()
{
  // build list of stale connections
  vector<int>staleFds;
  for (FdToConMapT::iterator i = _fdToCon.begin(); i != _fdToCon.end(); ++i) {
    if (_isBadFd(i->first)) {
      staleFds.push_back(i->first);
    }
  }

#ifdef LOGGING
  if (staleFds.size() > 0) {
    ostringstream out;
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
#endif // ifdef LOGGING

  // delete all the stale connections
  for (size_t i = 0; i < staleFds.size(); ++i) {
    processClose(staleFds[i]);
  }
}

void
ConnectionList::serialize(jalib::JBinarySerializer &o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp-serialized-connection-table!v0.07");

  JSERIALIZE_ASSERT_POINT("ConnectionIdentifier:");
  ConnectionIdentifier::serialize(o);

  JSERIALIZE_ASSERT_POINT("ConnectionList:");

  uint32_t numCons = _connections.size();
  o &numCons;

  if (o.isWriter()) {
    for (iterator i = _connections.begin(); i != _connections.end(); ++i) {
      ConnectionIdentifier key = i->first;
      Connection &con = *i->second;
      uint32_t type = con.conType();

      JSERIALIZE_ASSERT_POINT("[StartConnection]");
      o&key &type;
      con.serialize(o);
      JSERIALIZE_ASSERT_POINT("[EndConnection]");
    }
  } else {
    while (numCons-- > 0) {
      ConnectionIdentifier key;
      int type = -1;
      Connection *con = NULL;

      JSERIALIZE_ASSERT_POINT("[StartConnection]");
      o&key &type;
      con = createDummyConnection(type);
      JASSERT(con != NULL) (key);
      con->serialize(o);
      _connections[key] = con;
      const vector<int32_t> &fds = con->getFds();
      for (size_t i = 0; i < fds.size(); i++) {
        _fdToCon[fds[i]] = con;
      }
      JSERIALIZE_ASSERT_POINT("[EndConnection]");
    }
  }
  JSERIALIZE_ASSERT_POINT("EOF");
}

void
ConnectionList::list()
{
  ostringstream o;

  o << "\n";
  for (iterator i = begin(); i != end(); i++) {
    Connection *c = i->second;
    vector<int>fds = c->getFds();
    for (size_t j = 0; j < fds.size(); j++) {
      o << fds[j];
      if (j < fds.size() - 1) {
        o << ",";
      }
    }
    o << "\t" << i->first << "\t" << c->str();
    o << "\n";
  }
  JTRACE("ConnectionList") (dmtcp_get_uniquepid_str()) (o.str());
}

Connection *
ConnectionList::getConnection(const ConnectionIdentifier &id)
{
  if (_connections.find(id) == _connections.end()) {
    return NULL;
  }
  return _connections[id];
}

Connection *
ConnectionList::getConnection(int fd)
{
  if (_fdToCon.find(fd) == _fdToCon.end()) {
    return NULL;
  }
  return _fdToCon[fd];
}

void
ConnectionList::add(int fd, Connection *c)
{
  _lock_tbl();

  if (_fdToCon.find(fd) != _fdToCon.end()) {
    /* In ordinary situations, we never exercise this path since we already
     * capture close() and remove the connection. However, there is one
     * particular case where this assumption fails -- when glibc opens a socket
     * using socket() but closes it using the internal close_not_cancel() thus
     * bypassing our close wrapper. This behavior is observed when dealing with
     * getaddrinfo().
     */
    Connection *con = _fdToCon[fd];
    /*
     * The incoming Connection object pointer, c, and the one
     * present in our existing lists (local variable, con)
     * *must* point to different objects in order for us
     * to be able to execute the code later in the function. So
     * we just return here if the two pointers point to the
     * same object.
     *
     * In particular, the object might get deleted in the call to
     * processCloseWork() in the next step, and trying to use the
     * object after freeing it will result in alloc arena corruption.
     */
    if (con == c) {
      _unlock_tbl();
      return;
    }
    processCloseWork(fd);
  }

  if (_connections.find(c->id()) == _connections.end()) {
    _connections[c->id()] = c;
  }
  c->addFd(fd);
  _fdToCon[fd] = c;
  _unlock_tbl();
}

void
ConnectionList::processCloseWork(int fd)
{
  JASSERT(_fdToCon.find(fd) != _fdToCon.end());
  Connection *con = _fdToCon[fd];

  _fdToCon.erase(fd);
  con->removeFd(fd);
  if (con->numFds() == 0) {
    _connections.erase(con->id());
    delete con;
  }
}

void
ConnectionList::processClose(int fd)
{
  _lock_tbl();
  if (_fdToCon.find(fd) != _fdToCon.end()) {
    processCloseWork(fd);
  }
  _unlock_tbl();
}

void
ConnectionList::processDup(int oldfd, int newfd)
{
  if (oldfd == newfd) {
    return;
  }

  _lock_tbl();
  if (_fdToCon.find(newfd) != _fdToCon.end()) {
    Connection *newFdCon = _fdToCon[newfd];
    Connection *oldFdCon = NULL;
    if (_fdToCon.find(oldfd) != _fdToCon.end()) {
      oldFdCon = _fdToCon[oldfd];
    }
    /*
     * The Connection object pointer corresponding to oldfd,
     * oldFdCon, and the one corresponding to the newfd, newFdCon,
     * *must* point to different objects in order for us to be
     * able to execute the code later in the function. So we just
     * return here if the two pointers point to the same object.
     *
     * In particular, the newfd object might get deleted in the call to
     * processCloseWork() in the next step, and trying to use the
     * object after freeing it will result in alloc arena corruption.
     */
    if (oldFdCon == newFdCon) {
      _unlock_tbl();
      return;
    }

    processCloseWork(newfd);
  }

  // Add only if the oldfd was already in the _fdToCon table.
  if (_fdToCon.find(oldfd) != _fdToCon.end()) {
    Connection *con = _fdToCon[oldfd];
    _fdToCon[newfd] = con;
    con->addFd(newfd);
  }
  _unlock_tbl();
}

/*****************************************************/
/*****************************************************/
/*****************************************************/
/*****************************************************/
/*****************************************************/
/*****************************************************/
/*****************************************************/

void
ConnectionList::preLockSaveOptions()
{
  deleteStaleConnections();
  list();

  // Save Options for each Fd (We need to do it here instead of in
  // preCkptFdLeaderElection because we want to restore the correct owner
  // in refill).
  for (iterator i = begin(); i != end(); ++i) {
    Connection *con = i->second;
    con->saveOptions();
  }
}

void
ConnectionList::preCkptFdLeaderElection()
{
  deleteStaleConnections();
  for (iterator i = begin(); i != end(); ++i) {
    Connection *con = i->second;
    JASSERT(con->numFds() > 0);
    con->doLocking();
  }
}

void
ConnectionList::drain()
{
  for (iterator i = begin(); i != end(); ++i) {
    Connection *con = i->second;
    con->checkLocking();
    if (con->hasLock()) {
      con->drain();
    }
  }
}

void
ConnectionList::preCkpt()
{
  for (iterator i = begin(); i != end(); ++i) {
    Connection *con = i->second;
    if (con->hasLock()) {
      con->preCkpt();
    }
  }
}

void
ConnectionList::refill(bool isRestart)
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

void
ConnectionList::resume(bool isRestart)
{
  for (iterator i = begin(); i != end(); ++i) {
    Connection *con = i->second;
    if (con->hasLock()) {
      con->resume(isRestart);
    }
  }
}

void
ConnectionList::postRestart()
{
  // Here we modify the restore algorithm by splitting it into two parts. In the
  // first part we restore all the connections except the PTY_SLAVE types and
  // in the second part we restore only PTY_SLAVE _connections. This is done to
  // make sure that by the time we are trying to restore a PTY_SLAVE
  // connection, its corresponding PTY_MASTER connection has already been
  // restored.
  // UPDATE: We also restore the files for which the we didn't have the lock in
  // second iteration along with PTY_SLAVEs
  // Part 1: Restore all but Pseudo-terminal slaves and file connection which
  // were not checkpointed
  for (iterator i = begin(); i != end(); ++i) {
    Connection *con = i->second;
    if (!con->hasLock()) {
      continue;
    }

    // TODO: FIXME: Add support for Socketpairs.
    // if (con->conType() == Connection::TCP) {
    // TcpConnection *tcpCon =(TcpConnection *) con;
    // if (tcpCon->peerType() == TcpConnection::PEER_SOCKETPAIR) {
    // ConnectionIdentifier peerId = tcpCon->getSocketpairPeerId();
    // TcpConnection *peerCon = (TcpConnection*) getConnection(peerId);
    // if (peerCon != NULL) {
    // tcpCon->restoreSocketPair(peerCon);
    // continue;
    // }
    // }
    // }
    con->postRestart();
  }

  registerIncomingCons();
}

void
ConnectionList::registerIncomingCons()
{
  int protected_fd = protectedFd();

  // Add receive-fd data socket.
  static struct sockaddr_un fdReceiveAddr;
  static socklen_t fdReceiveAddrLen;

  memset(&fdReceiveAddr, 0, sizeof(fdReceiveAddr));
  jalib::JSocket sock(_real_socket(AF_UNIX, SOCK_DGRAM, 0));
  JASSERT(sock.isValid());
  sock.changeFd(protected_fd);
  fdReceiveAddr.sun_family = AF_UNIX;
  JASSERT(_real_bind(protected_fd,
                     (struct sockaddr *)&fdReceiveAddr,
                     sizeof(fdReceiveAddr.sun_family)) == 0) (JASSERT_ERRNO);

  fdReceiveAddrLen = sizeof(fdReceiveAddr);
  JASSERT(getsockname(protected_fd,
                      (struct sockaddr *)&fdReceiveAddr,
                      &fdReceiveAddrLen) == 0);


  vector<const char *>incomingCons;
  ostringstream in, out;
  for (iterator i = begin(); i != end(); ++i) {
    Connection *con = i->second;

    // Check comments in FileConnList::postRestart() for the explanation
    // about isPreExistingCTTY.
    if (!con->hasLock() && !con->isStdio() && !con->isPreExistingCTTY()) {
      incomingCons.push_back((const char *)&i->first);
      in << "\n\t" << con->str() << i->first;
    } else {
      out << "\n\t" << con->str() << i->first;
    }
  }
  JTRACE("Incoming/Outgoing Cons") (in.str()) (out.str());
  numIncomingCons = incomingCons.size();
  if (numIncomingCons > 0) {
    SharedData::registerIncomingCons(incomingCons, fdReceiveAddr,
                                     fdReceiveAddrLen);
  }
}

void
ConnectionList::sendReceiveMissingFds()
{
  size_t i;

  vector<int>outgoingCons;
  SharedData::IncomingConMap *maps;
  uint32_t nmaps;
  SharedData::getMissingConMaps(&maps, &nmaps);
  for (i = 0; i < nmaps; i++) {
    ConnectionIdentifier *id = (ConnectionIdentifier *)maps[i].id;
    Connection *con = getConnection(*id);
    if (con != NULL && con->hasLock()) {
      outgoingCons.push_back(i);
    }
  }

  int restoreFd = protectedFd();
  size_t numOutgoingCons = outgoingCons.size();
  while (numOutgoingCons > 0 || numIncomingCons > 0) {
    struct pollfd socketFd = { 0 };
    if (outgoingCons.size() > 0) {
      socketFd.fd = restoreFd;
      socketFd.events = POLLOUT;
    }
    if (numIncomingCons > 0) {
      socketFd.fd = restoreFd;
      socketFd.events |= POLLIN;
    }

    int ret = _real_poll(&socketFd, 1, -1);
    JASSERT(ret != -1) (JASSERT_ERRNO);

    if (numOutgoingCons > 0 && (socketFd.revents & POLLOUT)) {
      size_t idx = outgoingCons.back();
      outgoingCons.pop_back();
      ConnectionIdentifier *id = (ConnectionIdentifier *)maps[idx].id;
      Connection *con = getConnection(*id);
      JTRACE("Sending Missing Con") (*id);
      JASSERT(Util::sendFd(restoreFd, con->getFds()[0], id, sizeof(*id),
                           maps[idx].addr, maps[idx].len) != -1);
      numOutgoingCons--;
    }

    if (numIncomingCons > 0 && (socketFd.revents & POLLIN)) {
      ConnectionIdentifier id;
      int fd = Util::receiveFd(restoreFd, &id, sizeof(id));
      JASSERT(fd != -1);
      Connection *con = getConnection(id);
      JTRACE("Received Missing Con") (id);
      JASSERT(con != NULL);
      con->restoreDupFds(fd);
      numIncomingCons--;
    }
  }
  dmtcp_close_protected_fd(restoreFd);
}
