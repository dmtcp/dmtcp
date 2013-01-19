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

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "util.h"
#include "dmtcpplugin.h"
#include "shareddata.h"
#include "connectionlist.h"
#include "connwrappers.h"
#include "kernelbufferdrainer.h"
#include "connectionrewirer.h"
#include "../jalib/jfilesystem.h"
#include "../jalib/jconvert.h"
#include "../jalib/jassert.h"
#include "../jalib/jsocket.h"

using namespace dmtcp;

static unsigned _nextVirtualPtyId;
static dmtcp::KernelBufferDrainer *_theDrainer = NULL;
static dmtcp::ConnectionRewirer *_rewirer = NULL;
static size_t _numMissingCons = 0;
static pthread_mutex_t conTblLock = PTHREAD_MUTEX_INITIALIZER;

static void sendFd(int fd, ConnectionIdentifier& id,
                   struct sockaddr_un& addr, socklen_t len);
static int receiveFd(ConnectionIdentifier *id);

static void _lock_tbl()
{
  JASSERT(_real_pthread_mutex_lock(&conTblLock) == 0) (JASSERT_ERRNO);
}

static void _unlock_tbl()
{
  JASSERT(_real_pthread_mutex_unlock(&conTblLock) == 0) (JASSERT_ERRNO);
}

static dmtcp::string _procFDPath(int fd)
{
  return "/proc/self/fd/" + jalib::XToString(fd);
}

static dmtcp::string _resolveSymlink(dmtcp::string path)
{
  dmtcp::string device = jalib::Filesystem::ResolveSymlink(path);
  if (dmtcp_real_to_virtual_pid && path.length() > 0 &&
      dmtcp::Util::strStartsWith(device, "/proc/")) {
    int index = 6;
    char *rest;
    char newpath[128];
    JASSERT(device.length() < sizeof newpath);
    pid_t realPid = strtol(&path[index], &rest, 0);
    if (realPid > 0 && *rest == '/') {
      pid_t virtualPid = dmtcp_real_to_virtual_pid(realPid);
      sprintf(newpath, "/proc/%d%s", virtualPid, rest);
      device = newpath;
    }
  }
  return device;
}

static bool _isBadFd(int fd)
{
  errno = 0;
  return _real_fcntl(fd, F_GETFL, 0) == -1 && errno == EBADF;
}

static ConnectionList *connectionList = NULL;
dmtcp::ConnectionList& dmtcp::ConnectionList::instance()
{
  if (connectionList == NULL) {
    connectionList = new ConnectionList();
  }
  return *connectionList;
}

void dmtcp::ConnectionList::resetOnFork()
{
  pthread_mutex_t newlock = PTHREAD_MUTEX_INITIALIZER;
  conTblLock = newlock;
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
    out << "\tDevice \t\t->\t ConnectionId \n";
    out << "==================================================\n";
    for (size_t i = 0; i < staleFds.size(); ++i) {
      Connection *c = getConnection(staleFds[i]);

      out << "\t[" << jalib::XToString(staleFds[i]) << "]"
          << c->str()
          << "\t->\t" << staleFds[i] << "\n";
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

  size_t numCons = _connections.size();
  o & numCons;

  if (o.isWriter()) {
    for (iterator i=_connections.begin(); i!=_connections.end(); ++i) {
      ConnectionIdentifier key = i->first;
      Connection& con = *i->second;
      int type = con.conType();

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

      switch (type) {
        case Connection::TCP:
          con = new TcpConnection();
          break;
        case Connection::RAW:
          con = new RawSocketConnection();
          break;
        case Connection::FILE:
          con = new FileConnection();
          break;
        case Connection::FIFO:
          con = new FifoConnection();
          break;
        case Connection::PTY:
          con = new PtyConnection();
          break;
        case Connection::STDIO:
          con = new StdioConnection();
          break;
        case Connection::EPOLL:
          con = new EpollConnection(5); //dummy val
          break;
        case Connection::EVENTFD:
          con = new EventFdConnection(0, 0); //dummy val
          break;
        case Connection::SIGNALFD:
          con = new SignalFdConnection(0, NULL, 0); //dummy val
          break;
#ifdef DMTCP_USE_INOTIFY
        case Connection::INOTIFY:
          con = new InotifyConnection(0);
          break;
#endif
        case Connection::POSIXMQ:
          con = new PosixMQConnection("", 0, 0, NULL); //dummy val
          break;
        default:
          JASSERT(false) (key) (o.filename()) .Text("unknown connection type");
      }

      JASSERT(con != NULL) (key);
      con->serialize(o);
      _connections[key] = con;
      const vector<int>& fds = con->getFds();
      for (size_t i = 0; i < fds.size(); i++) {
        _fdToCon[fds[i]] = con;
      }
      JSERIALIZE_ASSERT_POINT("[EndConnection]");
    }
  }
  JSERIALIZE_ASSERT_POINT("EOF");
}

//examine /proc/self/fd for unknown connections
void dmtcp::ConnectionList::scanForPreExisting()
{
  // FIXME: Detect stdin/out/err fds to detect duplicates.
  dmtcp::vector<int> fds = jalib::Filesystem::ListOpenFds();
  for (size_t i = 0; i < fds.size(); ++i) {
    int fd = fds[i];
    if (_isBadFd(fd)) continue;
    if (dmtcp_is_protected_fd(fd)) continue;

    dmtcp::string device = _resolveSymlink(_procFDPath(fd));

    JTRACE("scanning pre-existing device") (fd) (device);
    if (device == jalib::Filesystem::GetControllingTerm()) {
      // FIXME: Merge this code with the code in processFileConnection
      Connection *con = new PtyConnection(fd, (const char*) device.c_str(),
                                          -1, -1, PtyConnection::PTY_CTTY);
      add(fd, con);
    } else if (fd <= 2) {
      add(fd, new StdioConnection(fd));
    } else if (Util::strStartsWith(device, "/")) {
      processFileConnection(fd, device.c_str(), -1, -1);
    } else {
      JNOTE("found pre-existing socket... will not be restored")
        (fd) (device);
      TcpConnection* con = new TcpConnection(0, 0, 0);
      con->markPreExisting();
      add(fd, con);
    }
  }
}

void dmtcp::ConnectionList::list()
{
  ostringstream o;
  for (iterator i = begin(); i != end(); i++) {
    o << i->first << "\n";
  }
  JTRACE("ConnectionList") (UniquePid::ThisProcess()) (o.str());
}

dmtcp::Connection&
dmtcp::ConnectionList::operator[](const ConnectionIdentifier& id)
{
  JASSERT(_connections.find(id) != _connections.end()) (id)
    .Text("Unknown connection");
  return *_connections[id];
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
  JWARNING(_connections.find(c->id()) == _connections.end()) (c->id())
    .Text("duplicate connection");
  _connections[c->id()] = c;
  c->addFd(fd);
  _fdToCon[fd] = c;
  _unlock_tbl();
}

void dmtcp::ConnectionList::processClose(int fd)
{
  JASSERT(_fdToCon.find(fd) != _fdToCon.end());
  _lock_tbl();
  Connection *con = _fdToCon[fd];
  _fdToCon.erase(fd);
  con->removeFd(fd);
  if (con->numFds() == 0) {
    _connections.erase(con->id());
    delete con;
  }
  _unlock_tbl();
}

void dmtcp::ConnectionList::processDup(int oldfd, int newfd)
{
  if (oldfd == newfd) return;
  if (_fdToCon.find(newfd) != _fdToCon.end()) {
    processClose(newfd);
  }
  _lock_tbl();
  Connection *con = _fdToCon[oldfd];
  _fdToCon[newfd] = con;
  con->addFd(newfd);
  _unlock_tbl();
}

void dmtcp::ConnectionList::processFileConnection(int fd, const char *path,
                                                  int flags, mode_t mode)
{
  Connection *c;
  struct stat statbuf;
  JASSERT(fstat(fd, &statbuf) == 0);

  if (strcmp(path, "/dev/tty") == 0) {
    // Controlling terminal
    c = new PtyConnection(fd, path, flags, mode, PtyConnection::PTY_DEV_TTY);
  } else if (strcmp(path, "/dev/pty") == 0) {
    JASSERT(false) .Text("Not Implemented");
  } else if (dmtcp::Util::strStartsWith(path, "/dev/pty")) {
    // BSD Master
    c = new PtyConnection(fd, path, flags, mode, PtyConnection::PTY_BSD_MASTER);
  } else if (dmtcp::Util::strStartsWith(path, "/dev/tty")) {
    // BSD Slave
    c = new PtyConnection(fd, path, flags, mode, PtyConnection::PTY_BSD_SLAVE);
  } else if (strcmp(path, "/dev/ptmx") == 0 ||
             strcmp(path, "/dev/pts/ptmx") == 0) {
    // POSIX Master PTY
    c = new PtyConnection(fd, path, flags, mode, PtyConnection::PTY_MASTER);
  } else if (dmtcp::Util::strStartsWith(path, "/dev/pts/")) {
    // POSIX Slave PTY
    c = new PtyConnection(fd, path, flags, mode, PtyConnection::PTY_SLAVE);
  } else if (dmtcp_is_bq_file && dmtcp_is_bq_file(path)) {
    // Resource manager related
    c = new FileConnection(path, flags, mode, FileConnection::FILE_BATCH_QUEUE);
  } else if (S_ISREG(statbuf.st_mode) || S_ISCHR(statbuf.st_mode) ||
             S_ISDIR(statbuf.st_mode) || S_ISBLK(statbuf.st_mode)) {
    // Regular File
    c = new FileConnection(path, flags, mode);
  } else if (S_ISFIFO(statbuf.st_mode)) {
    // FIFO
    c = new FifoConnection(path, flags, mode);
  } else {
    JASSERT(false) (path) .Text("Unimplemented file type.");
  }

  add(fd, c);
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
  // preCheckpointFdLeaderElection because we want to restore the correct owner
  // in refill).
  for (iterator i = begin(); i != end(); ++i) {
    Connection *con = i->second;
    con->saveOptions();
  }
}

void dmtcp::ConnectionList::preCheckpointFdLeaderElection()
{
  _nextVirtualPtyId = SharedData::getNextVirtualPtyId();
  deleteStaleConnections();
  for (iterator i = begin(); i != end(); ++i) {
    Connection *con = i->second;
    JASSERT(con->numFds() > 0);
    con->doLocking();
  }
}

void dmtcp::ConnectionList::preCheckpointDrain()
{
  //initialize the drainer
  JASSERT(_theDrainer == NULL);
  _theDrainer = new KernelBufferDrainer();
  for (iterator i = begin(); i != end(); ++i) {
    Connection* con =  i->second;
    con->checkLock();
    if (con->hasLock()) {
      con->preCheckpoint(*_theDrainer);
    }
  }

  //this will block until draining is complete
  _theDrainer->monitorSockets(DRAINER_CHECK_FREQ);

  //handle disconnected sockets
  const vector<ConnectionIdentifier>& discn =
    _theDrainer->getDisconnectedSockets();
  for (size_t i = 0; i < discn.size(); ++i) {
    const ConnectionIdentifier& id = discn[i];
    TcpConnection& con = _connections[id]->asTcp();
    JTRACE("recreating disconnected socket") (id);

    //reading from the socket, and taking the error, resulted in an implicit
    //close().
    //we will create a new, broken socket that is not closed
    con.onError();
    con.restore(); //restoring a TCP_ERROR connection makes a dead socket
  }
}

void dmtcp::ConnectionList::preCheckpointHandshakes()
{
  DmtcpUniqueProcessId coordId = dmtcp_get_coord_id();
  //must send first to avoid deadlock
  //we are relying on OS buffers holding our message without blocking
  for (iterator i = begin(); i != end(); ++i) {
    Connection *con = i->second;
    if (con->hasLock()) {
      con->doSendHandshakes(coordId);
    }
  }

  //now receive
  for (iterator i = begin(); i != end(); ++i) {
    Connection *con = i->second;
    if (con->hasLock()) {
      con->doRecvHandshakes(coordId);
    }
  }
}

void dmtcp::ConnectionList::refill(bool isRestart)
{
  _theDrainer->refillAllSockets();
  delete _theDrainer;
  _theDrainer = NULL;
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
  SharedData::restoreNextVirtualPtyId(_nextVirtualPtyId );
  doReconnect();

  registerMissingCons();
}

void dmtcp::ConnectionList::doReconnect()
{
  JASSERT(_rewirer == NULL);
  _rewirer = new ConnectionRewirer();
  _rewirer->openRestoreSocket();

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
    con->restore(_rewirer);
  }
}

void dmtcp::ConnectionList::registerNSData()
{
  _rewirer->registerNSData();
}

void dmtcp::ConnectionList::sendQueries()
{
  _rewirer->sendQueries();
  _rewirer->doReconnect();
  delete _rewirer;
  _rewirer = NULL;
}

void dmtcp::ConnectionList::registerMissingCons()
{
  // Add receive-fd data socket.
  struct sockaddr_un fdReceiveAddr;
  socklen_t         fdReceiveAddrLen;
  memset(&fdReceiveAddr, 0, sizeof(fdReceiveAddr));
  jalib::JSocket sock(_real_socket(AF_UNIX, SOCK_DGRAM, 0));
  JASSERT(sock.isValid());
  sock.changeFd(PROTECTED_FDREWIRER_FD);
  fdReceiveAddr.sun_family = AF_UNIX;
  JASSERT(_real_bind(PROTECTED_FDREWIRER_FD,
                     (struct sockaddr*) &fdReceiveAddr,
                     sizeof(fdReceiveAddr.sun_family)) == 0) (JASSERT_ERRNO);

  fdReceiveAddrLen = sizeof(fdReceiveAddr);
  JASSERT(getsockname(PROTECTED_FDREWIRER_FD,
                      (struct sockaddr *)&fdReceiveAddr,
                      &fdReceiveAddrLen) == 0);

  vector<const char *> missingCons;
  ostringstream in, out;
  for (iterator i = begin(); i != end(); ++i) {
    Connection *con = i->second;
    if (!con->hasLock() && con->subType() != PtyConnection::PTY_CTTY &&
        con->conType() != Connection::STDIO) {
      missingCons.push_back((const char*)&i->first);
      in << "\n\t" << con->str() << i->first;
    } else {
      out << "\n\t" << con->str() << i->first;
    }
  }
  JTRACE("Missing/Outgoing Cons") (in.str()) (out.str());
  _numMissingCons = missingCons.size();
  if (_numMissingCons > 0) {
    SharedData::registerMissingCons(missingCons, fdReceiveAddr,
                                    fdReceiveAddrLen);
  }

}

void dmtcp::ConnectionList::sendReceiveMissingFds()
{
  size_t i;
  vector<int> outgoingCons;
  SharedData::MissingConMap *maps;
  size_t nmaps;
  SharedData::getMissingConMaps(&maps, &nmaps);
  for (i = 0; i < nmaps; i++) {
    ConnectionIdentifier *id = (ConnectionIdentifier*) maps[i].id;
    Connection *con = ConnectionList::instance().getConnection(*id);
    if (con != NULL && con->hasLock()) {
      outgoingCons.push_back(i);
    }
  }

  fd_set rfds;
  fd_set wfds;
  int fd = PROTECTED_FDREWIRER_FD;
  i = 0;
  while (i < outgoingCons.size() || _numMissingCons > 0) {
    FD_ZERO(&wfds);
    if (outgoingCons.size() > 0) {
      FD_SET(fd, &wfds);
    }
    FD_ZERO(&rfds);
    if (_numMissingCons > 0) {
      FD_SET(fd, &rfds);
    }

    int ret = _real_select(PROTECTED_FDREWIRER_FD+1, &rfds, &wfds, NULL, NULL);
    JASSERT(ret != -1) (JASSERT_ERRNO);

    if (i < outgoingCons.size() && FD_ISSET(PROTECTED_FDREWIRER_FD, &wfds)) {
      size_t idx = outgoingCons[i];
      ConnectionIdentifier *id = (ConnectionIdentifier*) maps[idx].id;
      Connection *con = getConnection(*id);
      JTRACE("Sending Missing Con") (*id);
      sendFd(con->getFds()[0], *id, maps[idx].addr, maps[idx].len);
      i++;
    }

    if (_numMissingCons > 0 && FD_ISSET(PROTECTED_FDREWIRER_FD, &rfds)) {
      ConnectionIdentifier id;
      int fd = receiveFd(&id);
      Connection *con = getConnection(id);
      JTRACE("Received Missing Con") (id);
      JASSERT(con != NULL);
      Util::dupFds(fd, con->getFds());
      _numMissingCons--;
    }
  }
  _real_close(PROTECTED_FDREWIRER_FD);
}

static void sendFd(int fd, ConnectionIdentifier& id,
                   struct sockaddr_un& addr, socklen_t len)
{
  struct iovec iov;
  struct msghdr hdr;
  struct cmsghdr *cmsg;
  char cms[CMSG_SPACE(sizeof(int))];

  iov.iov_base = &id;
  iov.iov_len = sizeof(id);

  memset(&hdr, 0, sizeof hdr);
  hdr.msg_name = &addr;
  hdr.msg_namelen = len;
  hdr.msg_iov = &iov;
  hdr.msg_iovlen = 1;
  hdr.msg_control = (caddr_t)cms;
  hdr.msg_controllen = CMSG_LEN(sizeof(int));

  cmsg = CMSG_FIRSTHDR(&hdr);
  cmsg->cmsg_len = CMSG_LEN(sizeof(int));
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  *(int*)CMSG_DATA(cmsg) = fd;

  JASSERT (sendmsg(PROTECTED_FDREWIRER_FD, &hdr, 0) == (ssize_t) iov.iov_len)
    (JASSERT_ERRNO);
}

static int receiveFd(ConnectionIdentifier *id)
{
  int fd;
  struct iovec iov;
  struct msghdr hdr;
  struct cmsghdr *cmsg;
  char cms[CMSG_SPACE(sizeof(int))];

  iov.iov_base = id;
  iov.iov_len = sizeof(*id);

  memset(&hdr, 0, sizeof hdr);
  hdr.msg_name = 0;
  hdr.msg_namelen = 0;
  hdr.msg_iov = &iov;
  hdr.msg_iovlen = 1;

  hdr.msg_control = (caddr_t)cms;
  hdr.msg_controllen = sizeof cms;

  JASSERT(recvmsg(PROTECTED_FDREWIRER_FD, &hdr, 0) != -1)
    (JASSERT_ERRNO);

  cmsg = CMSG_FIRSTHDR(&hdr);
  JASSERT(cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type  == SCM_RIGHTS)
    (cmsg->cmsg_level) (cmsg->cmsg_type);
  fd = *(int *) CMSG_DATA(cmsg);

  return fd;
}
