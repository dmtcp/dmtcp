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
#include <sys/mman.h>
#include <unistd.h>
#include <mqueue.h>
#include <stdint.h>
#include <signal.h>
#include "util.h"
#include "shareddata.h"
#include "jfilesystem.h"
#include "jbuffer.h"
#include "jconvert.h"
#include "fileconnection.h"
#include "fileconnlist.h"
#include "filewrappers.h"

using namespace dmtcp;

void dmtcp_FileConnList_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  dmtcp::FileConnList::instance().eventHook(event, data);
}

static dmtcp::vector<ProcMapsArea> shmAreas;
static dmtcp::vector<dmtcp::FileConnection*> shmAreaConn;

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

static FileConnList *fileConnList = NULL;
dmtcp::FileConnList& dmtcp::FileConnList::instance()
{
  if (fileConnList == NULL) {
    fileConnList = new FileConnList();
  }
  return *fileConnList;
}

void dmtcp::FileConnList::preLockSaveOptions()
{
  // Now create a list of all shared-memory areas.
  prepareShmList();

  ConnectionList::preLockSaveOptions();
}

void dmtcp::FileConnList::drain()
{
  ConnectionList::drain();

  vector<SharedData::InodeConnIdMap> inodeConnIdMaps;
  for (iterator i = begin(); i != end(); ++i) {
    Connection* con =  i->second;
    if (con->hasLock() && con->conType() == Connection::FILE) {
      FileConnection *fileCon = (FileConnection*) con;
      if (fileCon->checkpointed() == true) {
        SharedData::InodeConnIdMap map;
        map.devnum = fileCon->devnum();
        map.inode = fileCon->inode();
        memcpy(map.id, &i->first, sizeof (i->first));
        inodeConnIdMaps.push_back(map);
      }
    }
  }
  if (inodeConnIdMaps.size() > 0) {
    SharedData::insertInodeConnIdMaps(inodeConnIdMaps);
  }
}

void dmtcp::FileConnList::postRestart()
{
  /* It is possible to have two different connection-ids for a pre-existing
   * CTTY in two or more different process trees. In this case, only one of the
   * several process trees would be able to acquire a lock on the underlying
   * fd.  The send-receive fd logic fails in this case due to different
   * connection-ids.  Therefore, we let every process do a postRestart to
   * reopen the CTTY.
   *
   * TODO: A better fix would be to have a unique connection-id for each
   * pre-existing CTTY that is then used by all process trees.  It can be
   * implemented by using the SharedData area.
   */
  for (iterator i = begin(); i != end(); ++i) {
    Connection* con =  i->second;
    if (!con->hasLock() && con->conType() == Connection::PTY &&
        con->isPreExistingCTTY()) {
      PtyConnection *pcon = (PtyConnection*) con;
      pcon->postRestart();
    }
  }

  ConnectionList::postRestart();
}

void dmtcp::FileConnList::refill(bool isRestart)
{
  // Check comments in PtyConnection::preRefill()/refill()
  for (iterator i = begin(); i != end(); ++i) {
    Connection* con =  i->second;
    if (con->hasLock() && con->conType() == Connection::PTY) {
      PtyConnection *pcon = (PtyConnection*) con;
      pcon->preRefill(isRestart);
    }
  }

  ConnectionList::refill(isRestart);
}

void dmtcp::FileConnList::resume(bool isRestart)
{
  ConnectionList::resume(isRestart);
  remapShmMaps();
}

void dmtcp::FileConnList::prepareShmList()
{
  ProcMapsArea area;
  int mapsfd = _real_open("/proc/self/maps", O_RDONLY, 0);
  JASSERT(mapsfd != -1) (JASSERT_ERRNO);

  shmAreas.clear();
  shmAreaConn.clear();
  while (Util::readProcMapsLine(mapsfd, &area)) {
    if ((area.flags & MAP_SHARED) && area.prot != 0) {
      if (strstr(area.name, "ptraceSharedInfo") != NULL ||
          strstr(area.name, "dmtcpPidMap") != NULL ||
          strstr(area.name, "dmtcpSharedArea") != NULL ||
          strstr(area.name, "dmtcpSharedArea") != NULL ||
          strstr(area.name, "synchronization-log") != NULL ||
          strstr(area.name, "infiniband") != NULL ||
          strstr(area.name, "synchronization-read-log") != NULL) {
        continue;
      }
      if (jalib::Filesystem::FileExists(area.name)) {
        if (_real_access(area.name, W_OK) == 0) {
          JTRACE("Will checkpoint shared memory area") (area.name);
          int flags = Util::memProtToOpenFlags(area.prot);
          int fd = _real_open(area.name, flags, 0);
          JASSERT(fd != -1) (JASSERT_ERRNO) (area.name);
          FileConnection *fileConn =
            new FileConnection(area.name, flags, 0, FileConnection::FILE_SHM);
          add(fd, fileConn);
          shmAreas.push_back(area);
          shmAreaConn.push_back(fileConn);
          /* Instead of unmapping the shared memory area, we make it
           * non-readable. This way mtcp will skip the region while at the same
           * time, we prevent JALLOC arena to grow over it.
           *
           * By munmapping the area, a bug was observed on CCIS linux with
           * 'make check-java'. Once the region was unmapped, the JALLOC arena
           * grew over it. During restart, the JALLOC'd area was reclaimed for
           * remapping the shm file without informing JALLOC. Finally, during
           * the second checkpoint cycle, the area was again unmapped and later
           * JALLOC tried to access it, causing a SIGSEGV.
           */
          JASSERT(_real_mmap(area.addr, area.size, PROT_NONE,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                             -1, 0) != MAP_FAILED) (JASSERT_ERRNO);
        } else {
          JTRACE("Will not checkpoint shared memory area") (area.name);
        }
      } else {
        // TODO: Shared memory areas with unlinked backing files.
#if 0
        JASSERT(Util::strEndsWith(area.name, DELETED_FILE_SUFFIX)) (area.name);
        if (Util::strStartsWith(area.name, DEV_ZERO_DELETED_STR) ||
            Util::strStartsWith(area.name, DEV_NULL_DELETED_STR)) {
          JWARNING(false) (area.name)
            .Text("Ckpt/Restart of Anon Shared memory not supported.");
        } else {
          JTRACE("Will recreate shm file on restart.") (area.name);
          //shmAreas[area] = NULL;
        }
#endif
      }
    }
  }
  _real_close(mapsfd);
}

void dmtcp::FileConnList::remapShmMaps()
{
  for (size_t i = 0; i < shmAreas.size(); i++) {
    ProcMapsArea *area = &shmAreas[i];
    FileConnection *fileCon = shmAreaConn[i];
    int fd = fileCon->getFds()[0];
    JTRACE("Restoring shared memory area") (area->name) ((void*)area->addr);
    void *addr = _real_mmap(area->addr, area->size, area->prot,
                            MAP_FIXED | area->flags,
                            fd, area->offset);
    JASSERT(addr != MAP_FAILED) (area->flags) (area->prot) (JASSERT_ERRNO) .Text("mmap failed");
    _real_close(fd);
    processClose(fd);
  }
  shmAreas.clear();
  shmAreaConn.clear();
}

//examine /proc/self/fd for unknown connections
void dmtcp::FileConnList::scanForPreExisting()
{
  // FIXME: Detect stdin/out/err fds to detect duplicates.
  dmtcp::vector<int> fds = jalib::Filesystem::ListOpenFds();
  dmtcp::string ctty = jalib::Filesystem::GetControllingTerm();
  dmtcp::string parentCtty = jalib::Filesystem::GetControllingTerm(getppid());
  for (size_t i = 0; i < fds.size(); ++i) {
    int fd = fds[i];
    if (!Util::isValidFd(fd)) continue;
    if (dmtcp_is_protected_fd(fd)) continue;
    struct stat statbuf;
    JASSERT(fstat(fd, &statbuf) == 0);
    bool isRegularFile = (S_ISREG(statbuf.st_mode) || S_ISCHR(statbuf.st_mode) ||
                          S_ISDIR(statbuf.st_mode) || S_ISBLK(statbuf.st_mode));

    dmtcp::string device = jalib::Filesystem::GetDeviceName(fd);

    JTRACE("scanning pre-existing device") (fd) (device);
    if (device == ctty || device == parentCtty) {
      // Search if this is duplicate connection
      iterator conit;
      uint32_t cttyType = (device == ctty) ? PtyConnection::PTY_CTTY
                                      : PtyConnection::PTY_PARENT_CTTY;
      for (conit = begin(); conit != end(); conit++) {
        Connection *c = conit->second;
        if (c->subType() == cttyType &&
            ((PtyConnection*)c)->ptsName() == device) {
          processDup(c->getFds()[0], fd);
          break;
        }
      }
      if (conit == end()) {
        // FIXME: Merge this code with the code in processFileConnection
        PtyConnection *con = new PtyConnection(fd, (const char*) device.c_str(),
                                               -1, -1, cttyType);
        // Check comments in FileConnList::postRestart() for the explanation
        // about isPreExistingCTTY.
        con->markPreExistingCTTY();
        add(fd, (Connection*)con);
      }
    } else if(dmtcp_is_bq_file && dmtcp_is_bq_file(device.c_str())) {
      if (isRegularFile) {
        Connection *c = findDuplication(fd, device.c_str());
        if (c != NULL) {
          add(fd,c);
          continue;
        }
      }
      processFileConnection(fd, device.c_str(), -1, -1);
    } else if( fd <= 2 ){
      add(fd, new StdioConnection(fd));
    } else if (Util::strStartsWith(device, "/")) {
      if (isRegularFile) {
        Connection *c = findDuplication(fd, device.c_str());
        if (c != NULL) {
          add(fd,c);
          continue;
        }
      }
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
  Connection *c = NULL;
  struct stat statbuf;
  JASSERT(fstat(fd, &statbuf) == 0);

  dmtcp::string device;
  if (path == NULL) {
    device = jalib::Filesystem::GetDeviceName(fd);
  } else {
    device = jalib::Filesystem::ResolveSymlink(path);
    if (device == "") {
      device = path;
    }
  }

  path = device.c_str();
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
    int type = FileConnection::FILE_REGULAR;
    if (dmtcp_is_bq_file && dmtcp_is_bq_file(path)) {
      // Resource manager related
      type = FileConnection::FILE_BATCH_QUEUE;
    }
    c = new FileConnection(path, flags, mode, type);
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
