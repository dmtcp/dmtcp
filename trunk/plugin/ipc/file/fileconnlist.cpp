/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, and gene@ccs.neu.edu          *
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

// THESE INCLUDES ARE IN RANDOM ORDER.  LET'S CLEAN IT UP AFTER RELEASE. - Gene
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <mqueue.h>
#include <stdint.h>
#include <signal.h>
#include "util.h"
#include "jfilesystem.h"
#include "jbuffer.h"
#include "jconvert.h"
#include "fileconnection.h"
#include "fileconnlist.h"

using namespace dmtcp;

void dmtcp_FileConnList_ProcessEvent(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  dmtcp::FileConnList::instance().processEvent(event, data);
}

void dmtcp_FileConn_ProcessFdEvent(int event, int arg1, int arg2)
{
  if (event == SYS_close) {
    FileConnList::instance().processClose(arg1);
  } else if (event == SYS_dup) {
    FileConnList::instance().processDup(arg1, arg2);
  } else {
    JASSERT(false);
  }
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

static FileConnList *fileConnList = NULL;
dmtcp::FileConnList& dmtcp::FileConnList::instance()
{
  if (fileConnList == NULL) {
    fileConnList = new FileConnList();
  }
  return *fileConnList;
}

//examine /proc/self/fd for unknown connections
void dmtcp::FileConnList::scanForPreExisting()
{
  // FIXME: Detect stdin/out/err fds to detect duplicates.
  dmtcp::vector<int> fds = jalib::Filesystem::ListOpenFds();
  for (size_t i = 0; i < fds.size(); ++i) {
    int fd = fds[i];
    if (!Util::isValidFd(fd)) continue;
    if (dmtcp_is_protected_fd(fd)) continue;

    dmtcp::string device = _resolveSymlink(_procFDPath(fd));

    JTRACE("scanning pre-existing device") (fd) (device);
    if (device == jalib::Filesystem::GetControllingTerm()) {
      // Search if this is duplicate connection
      iterator conit;
      for (conit = begin(); conit != end(); conit++) {
        Connection *c = conit->second;
        if (c->subType() == PtyConnection::PTY_CTTY &&
            ((PtyConnection*)c)->ptsName() == device) {
          processDup(c->getFds()[0], fd);
          break;
        }
      }
      if (conit == end()) {
        // FIXME: Merge this code with the code in processFileConnection
        Connection *con = new PtyConnection(fd, (const char*) device.c_str(),
                                            -1, -1, PtyConnection::PTY_CTTY);
        add(fd, con);
      }
    } else if(dmtcp_is_bq_file && dmtcp_is_bq_file(device.c_str())) {
      processFileConnection(fd, device.c_str(), -1, -1);
    } else if( fd <= 2 ){
      add(fd, new StdioConnection(fd));
    } else if (Util::strStartsWith(device, "/")) {
      processFileConnection(fd, device.c_str(), -1, -1);
    }
  }
}

Connection *dmtcp::FileConnList::findDuplication(int fd, const char *path)
{
  string npath(path);
  for (iterator i = begin(); i != end(); ++i) {
    Connection *con = i->second;

    if( con->conType() != Connection::FILE )
      continue;

    FileConnection *fcon = (FileConnection*)con;
    // check for duplication
    if( fcon->filePath() == npath && fcon->checkDup(fd) ){
      return con;
    }
  }
  return NULL;
}

void dmtcp::FileConnList::processFileConnection(int fd, const char *path,
                                                int flags, mode_t mode)
{
  Connection *c;
  struct stat statbuf;
  JASSERT(fstat(fd, &statbuf) == 0);

  dmtcp::string device;
  if (path == NULL) {
    device = _resolveSymlink(_procFDPath(fd));
    path = device.c_str();
  }
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
  } else if (S_ISREG(statbuf.st_mode) || S_ISCHR(statbuf.st_mode) ||
             S_ISDIR(statbuf.st_mode) || S_ISBLK(statbuf.st_mode)) {

    c = findDuplication(fd,path);
    if( c == NULL ){
      if (dmtcp_is_bq_file && dmtcp_is_bq_file(path)) {
        // Resource manager related
        c = new FileConnection(path, flags, mode, FileConnection::FILE_BATCH_QUEUE);
      }else{
        // Regular File
        c = new FileConnection(path, flags, mode);
      }
    }
  } else if (S_ISFIFO(statbuf.st_mode)) {
    // FIFO
    c = new FifoConnection(path, flags, mode);
  } else {
    JASSERT(false) (path) .Text("Unimplemented file type.");
  }

  add(fd, c);
}


Connection *dmtcp::FileConnList::createDummyConnection(int type)
{
  switch (type) {
    case Connection::FILE:
      return new FileConnection();
      break;
    case Connection::FIFO:
      return new FifoConnection();
      break;
    case Connection::PTY:
      return new PtyConnection();
      break;
    case Connection::STDIO:
      return new StdioConnection();
      break;
  }
  return NULL;
}
