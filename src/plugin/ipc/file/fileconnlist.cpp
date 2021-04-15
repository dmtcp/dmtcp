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

/*
 * Ckpt policy for handling files and shared memory segments.
 *
 * TODO(kapil): Fill the holes in this policy.
 *
 * Regular File:
 * - Ckpt file descriptor
 * - Leader election
 * - Ckpt file based on heuristics
 * Unlinked File:
 * - Ckpt file descriptor
 * - Leader election
 * - Ckpt file
 *
 * Shared-memory area with regular file:
 * - TODO(kapil): Any file descriptor with the file? If yes, use that to
 *   ckpt-file.
 * - Open a file descriptor
 * - Ckpt file based on heuristics
 * - recreate file on restart
 * - close fd on restart.
 *
 * Shared-memory area with unlinked file:
 * + Ckpt:
 *   - TODO(kapil): Any file descriptor pointing to the file? If yes, delegate
 *     ckpt to the file descriptor.
 *   - everyone saves the contents of the shared-area.
 * + Restart
 *   - File already exists: verify that the file is at least as large as
 *     (area.offset+area.size).
 *   - File doesn't exist: try to recreate the file; write content
 * - on restart, everyone tries to recreate the file and write their data
 *   (offset, length) to the file.
 * - everyone tries to unlink the file in a subsequent barrier.
 */

// THESE INCLUDES ARE IN RANDOM ORDER.  LET'S CLEAN IT UP AFTER RELEASE. - Gene
#include "fileconnlist.h"
#include <mqueue.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/types.h>
#include <unistd.h>
#include "jbuffer.h"
#include "jconvert.h"
#include "jfilesystem.h"
#include "fileconnection.h"
#include "filewrappers.h"
#include "procselfmaps.h"
#include "ptywrappers.h"
#include "ptyconnlist.h"
#include "shareddata.h"
#include "util.h"

using namespace dmtcp;

static FileConnList *fileConnList = NULL;
static FileConnList *vfork_fileConnList = NULL;

void
dmtcp_FileConnList_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  FileConnList::instance().eventHook(event, data);

  switch (event) {
  case DMTCP_EVENT_OPEN_FD:
    // TODO: Handle the following:
    // if (Util::strStartsWith(path, "/dev/ptmx")) {
    //   // Force O_RDWR flag here.
    //   flags &= ~(O_RDONLY | O_WRONLY);
    //   flags |= O_RDWR;
    // }

    if (Util::isPseudoTty(
            jalib::Filesystem::GetDeviceName(data->openFd.fd).c_str())) {
      PtyConnList::instance().processPtyConnection(data->openFd.fd,
                                                   data->openFd.path,
                                                   data->openFd.flags,
                                                   data->openFd.mode);
    } else {
      FileConnList::instance().processFileConnection(data->openFd.fd,
                                                     data->openFd.path,
                                                     data->openFd.flags,
                                                     data->openFd.mode);
    }
    break;

  case DMTCP_EVENT_CLOSE_FD:
    FileConnList::instance().processClose(data->closeFd.fd);
    break;

  case DMTCP_EVENT_DUP_FD:
    FileConnList::instance().processDup(data->dupFd.oldFd, data->dupFd.newFd);
    break;

  case DMTCP_EVENT_REOPEN_FD:
    FileConnList::instance().processReopen(data->reopenFd.fd,
                                           data->reopenFd.path);

     break;

  case DMTCP_EVENT_VFORK_PREPARE:
    vfork_fileConnList = (FileConnList*) FileConnList::instance().clone();
    break;

  case DMTCP_EVENT_VFORK_PARENT:
  case DMTCP_EVENT_VFORK_FAILED:
    delete fileConnList;
    fileConnList = vfork_fileConnList;
    break;

  case DMTCP_EVENT_PRESUSPEND:
    break;

  case DMTCP_EVENT_PRECHECKPOINT:
    FileConnList::saveOptions();
    dmtcp_local_barrier("File::PRE_CKPT");
    FileConnList::leaderElection();
    dmtcp_local_barrier("File::LEADER_ELECTION");
    FileConnList::drainFd();
    dmtcp_local_barrier("File::DRAIN");
    FileConnList::ckpt();

    break;

  case DMTCP_EVENT_RESUME:
    FileConnList::resumeRefill();
    dmtcp_local_barrier("File::RESUME_REFILL");
    FileConnList::resumeResume();
    break;

  case DMTCP_EVENT_RESTART:
    FileConnList::restart();
    dmtcp_local_barrier("File::RESTART_POST_RESTART");
    FileConnList::restartRefill();
    dmtcp_local_barrier("File::RESTART_REFILL");
    FileConnList::restartResume();
    break;

  default:  // other events are not registered
    break;
  }
}


DmtcpPluginDescriptor_t filePlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "file",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "File plugin",
  dmtcp_FileConnList_EventHook
};

void
ipc_initialize_plugin_file()
{
  dmtcp_register_plugin(filePlugin);
}

static vector<ProcMapsArea>shmAreas;
static vector<ProcMapsArea>unlinkedShmAreas;
static vector<ProcMapsArea>missingUnlinkedShmFiles;
static vector<FileConnection *>shmAreaConn;

void FileConnList::processReopen(int fd, const char *newPath)
{
  FileConnection *con = (FileConnection*) getConnection(fd);
  if (con != NULL) {
    con->updatePath(newPath);
  }
}

bool
FileConnList::createDirectoryTree(const string &path)
{
  size_t index = path.rfind('/');

  if (index == string::npos) {
    return true;
  }

  string dir = path.substr(0, index);

  index = path.find('/');
  while (index != string::npos) {
    if (index > 1) {
      string dirName = path.substr(0, index);

      errno = 0;
      int res = mkdir(dirName.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
#ifdef STAMPEDE_LUSTRE_FIX
      if (res < 0) {
        if (errno == EACCES) {
          struct stat buff;
          int ret = stat(dirName.c_str(), &buff);
          if (ret != 0) {
            return false;
          }
        } else if (errno == EEXIST) {
          /* do nothing */
        } else {
          return false;
        }
      }
#else // ifdef STAMPEDE_LUSTRE_FIX
      if (res == -1 && errno != EEXIST) {
        return false;
      }
#endif // ifdef STAMPEDE_LUSTRE_FIX
    }
    index = path.find('/', index + 1);
  }
  return true;
}


FileConnList&
FileConnList::instance()
{
  if (fileConnList == NULL) {
    fileConnList = new FileConnList();
  }
  return *fileConnList;
}

void
FileConnList::preLockSaveOptions()
{
  // Now create a list of all shared-memory areas.
  prepareShmList();

  ConnectionList::preLockSaveOptions();
}

void
FileConnList::drain()
{
  ConnectionList::drain();

  vector<SharedData::InodeConnIdMap>inodeConnIdMaps;
  for (iterator i = begin(); i != end(); ++i) {
    Connection *con = i->second;
    if (con->hasLock() && con->conType() == Connection::FILE) {
      FileConnection *fileCon = (FileConnection *)con;
      if (fileCon->checkpointed() == true) {
        SharedData::InodeConnIdMap map;
        map.devnum = fileCon->devnum();
        map.inode = fileCon->inode();
        memcpy(map.id, &i->first, sizeof(i->first));
        inodeConnIdMaps.push_back(map);
      }
    }
  }
  if (inodeConnIdMaps.size() > 0) {
    SharedData::insertInodeConnIdMaps(inodeConnIdMaps);
  }
}

/*
 * This function is called after preCkpt() for all the FileConnection
 * objects and writes out information about the open files saved by DMTCP.
 */
void
FileConnList::preCkpt()
{
  ConnectionList::preCkpt();

  string fdInfoFile = dmtcp_get_ckpt_files_subdir();
  fdInfoFile += "/fd-info.txt";
  int tmpfd = _real_open(fdInfoFile.c_str(),
                         O_CREAT | O_WRONLY | O_TRUNC,
                         0644);

  for (iterator i = begin(); i != end(); ++i) {
    Connection* con =  i->second;
    if (con->hasLock() && con->conType() == Connection::FILE) {
      FileConnection *fileCon = (FileConnection*) con;
      if (fileCon->checkpointed() == true) {
        string buf = jalib::Filesystem::BaseName(fileCon->savedFilePath()) +
                     ":" + fileCon->filePath() + "\n";
        JASSERT(Util::writeAll(tmpfd, buf.c_str(),
                               buf.length()) == (ssize_t)buf.length());
      }
    }
  }
  _real_close(tmpfd);
}

void
FileConnList::postRestart()
{
  /* Try to map the file as is, if it already exists on the disk.
   */
  for (size_t i = 0; i < unlinkedShmAreas.size(); i++) {
    if (jalib::Filesystem::FileExists(unlinkedShmAreas[i].name)) {
      // TODO(kapil): Verify the file contents.
      JWARNING(false) (unlinkedShmAreas[i].name)
      .Text("File was unlinked at ckpt but is currently present on disk; "
            "remove it and try again.");
      restoreShmArea(unlinkedShmAreas[i]);
    } else {
      missingUnlinkedShmFiles.push_back(unlinkedShmAreas[i]);
    }
  }

  ConnectionList::postRestart();
}

void
FileConnList::refill(bool isRestart)
{
  if (isRestart) {
    // The backing file will be created as a result of restoreShmArea. We need
    // to unlink all such files in the resume() call below.
    for (size_t i = 0; i < missingUnlinkedShmFiles.size(); i++) {
      recreateShmFileAndMap(missingUnlinkedShmFiles[i]);
    }
  }

  ConnectionList::refill(isRestart);
}

void
FileConnList::resume(bool isRestart)
{
  ConnectionList::resume(isRestart);

  remapShmMaps();

  if (isRestart) {
    // Now unlink the files that we created as a side-effect of restoreShmArea.
    for (size_t i = 0; i < missingUnlinkedShmFiles.size(); i++) {
      JWARNING(unlink(missingUnlinkedShmFiles[i].name) != -1)
        (missingUnlinkedShmFiles[i].name) (JASSERT_ERRNO)
      .Text("The file was unlinked at the time of checkpoint. "
            "Unlinking it after restart failed");
    }
  }
}

void
FileConnList::prepareShmList()
{
  ProcSelfMaps procSelfMaps;
  ProcMapsArea area;

  shmAreas.clear();
  unlinkedShmAreas.clear();
  missingUnlinkedShmFiles.clear();
  shmAreaConn.clear();
  while (procSelfMaps.getNextArea(&area)) {
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

      if (Util::isNscdArea(area) ||
          Util::isIBShmArea(area) ||
          Util::isSysVShmArea(area)) {
        continue;
      }

      /* Invalidate shared memory pages so that the next read to it (when we are
       * writing them to ckpt file) will cause them to be reloaded from the
       * disk.
       */
      JWARNING(msync(area.addr, area.size, MS_INVALIDATE) == 0)
        (area.addr) (area.size) (area.name) (area.offset) (JASSERT_ERRNO);

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
          JASSERT(mmap(area.addr, area.size, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                       -1, 0) != MAP_FAILED) (JASSERT_ERRNO);
        } else {
          JTRACE("Will not checkpoint shared memory area") (area.name);
        }
      } else {
        // TODO: Shared memory areas with unlinked backing files.
        JASSERT(Util::strEndsWith(area.name, DELETED_FILE_SUFFIX)) (area.name);
        if (Util::strStartsWith(area.name, DEV_ZERO_DELETED_STR) ||
            Util::strStartsWith(area.name, DEV_NULL_DELETED_STR)) {
          JWARNING(false) (area.name)
          .Text("Ckpt/Restart of anonymous shared memory not supported.");
        } else {
          JTRACE("Will recreate shm file on restart.") (area.name);

          // Remove the DELETED suffix.
          area.name[strlen(area.name) - strlen(DELETED_FILE_SUFFIX)] = '\0';
          unlinkedShmAreas.push_back(area);
        }
      }
    }
  }
}

static string
removeSuffix(const string &s, const string &suffix)
{
  if (dmtcp::Util::strEndsWith(s.c_str(), suffix.c_str())) {
    string result(s, s.length() - suffix.length());
    return result;
  }
  return s;
}

void
FileConnList::recreateShmFileAndMap(const ProcMapsArea &area)
{
  // TODO(kapil): Handle /dev/zero, /dev/random, etc.
  // Recreate file in dmtcp-tmpdir;
  string filename = removeSuffix(area.name, DELETED_FILE_SUFFIX);

  JASSERT(createDirectoryTree(area.name)) (area.name)
  .Text("Unable to create directory in File Path");

  /* Now try to create the file with O_EXCL. If we fail with EEXIST, there
   * are two possible scenarios:
   * - The file was created by a different restarting process with data from
   *   checkpointed copy. It is possible that the data is "in flight", so we
   *   should wait until the next barrier to compare the data from our copy.
   * - The file existed before restart. After the next barrier, abort if the
   *   contents differ from our checkpointed copy.
   */
  int fd = _real_open(area.name, O_CREAT | O_EXCL | O_RDWR,
                      S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  JASSERT(fd != -1 || errno == EEXIST) (area.name);

  if (fd == -1) {
    fd = _real_open(area.name, O_RDWR);
    JASSERT(fd != -1) (JASSERT_ERRNO);
  }

  // Get to the correct offset.
  JASSERT(lseek(fd, area.offset, SEEK_SET) == area.offset) (JASSERT_ERRNO);

  // Now populate file contents from memory.
  JASSERT(Util::writeAll(fd, area.addr, area.size) == (ssize_t)area.size)
    (JASSERT_ERRNO);
  restoreShmArea(area, fd);
}

void
FileConnList::restoreShmArea(const ProcMapsArea &area, int fd)
{
  if (fd == -1) {
    fd = _real_open(area.name, Util::memProtToOpenFlags(area.prot));
  }

  JASSERT(fd != -1) (area.name) (JASSERT_ERRNO);

  JTRACE("Restoring shared memory area") (area.name) ((void *)area.addr);
  void *addr = mmap(area.addr, area.size, area.prot,
                    MAP_FIXED | area.flags, fd, area.offset);
  JASSERT(addr != MAP_FAILED) (area.flags) (area.prot) (JASSERT_ERRNO)
  .Text("mmap failed");
  _real_close(fd);
}

void
FileConnList::remapShmMaps()
{
  for (size_t i = 0; i < shmAreas.size(); i++) {
    ProcMapsArea *area = &shmAreas[i];
    FileConnection *fileCon = shmAreaConn[i];
    int fd = fileCon->getFds()[0];
    JTRACE("Restoring shared memory area") (area->name) ((void *)area->addr);
    void *addr = mmap(area->addr, area->size, area->prot,
                      MAP_FIXED | area->flags, fd, area->offset);
    JASSERT(addr != MAP_FAILED) (area->flags) (area->prot) (JASSERT_ERRNO).Text(
      "mmap failed");
    _real_close(fd);
    processClose(fd);
  }
  shmAreas.clear();
  shmAreaConn.clear();
}

// examine /proc/self/fd for unknown connections
void
FileConnList::scanForPreExisting()
{
  // FIXME: Detect stdin/out/err fds to detect duplicates.
  vector<int>fds = jalib::Filesystem::ListOpenFds();
  for (size_t i = 0; i < fds.size(); ++i) {
    int fd = fds[i];
    if (!Util::isValidFd(fd)) {
      continue;
    }
    if (dmtcp_is_protected_fd(fd)) {
      continue;
    }
    struct stat statbuf;
    JASSERT(fstat(fd, &statbuf) == 0);
    bool isRegularFile = (S_ISREG(statbuf.st_mode) || S_ISDIR(statbuf.st_mode));

    string device = jalib::Filesystem::GetDeviceName(fd);

    JTRACE("scanning pre-existing device") (fd) (device);

    if (dmtcp_is_bq_file && dmtcp_is_bq_file(device.c_str())) {
      if (isRegularFile) {
        Connection *c = findDuplication(fd, device.c_str());
        if (c != NULL) {
          add(fd, c);
          continue;
        }
      }
      add(fd, new FileConnection(device.c_str(), -1, -1,
                                 FileConnection::FILE_BATCH_QUEUE));
    } else if (fd <= 2) {
      add(fd, new StdioConnection(fd));
    } else if (getenv("PBS_JOBID") &&
               (Util::strStartsWith(device.c_str(), "/proc") &&
                Util::strEndsWith(device.c_str(), "environ"))) {
      /*
       * This is a workaround for an issue seen with PBS at ANU-NCI.
       *
       * Application processes would inherit a "/proc/<pid>/environ"
       * file-descriptor when launched under PBS. As far as I can tell,
       * the file-descriptor is inherited from the PBS launcher process
       * on the compute node. The workaround is to recognize this fd as
       * a pre-existing device and ignore it for checkpoint-restart.
       */
      continue;
    } else if (Util::strStartsWith(device.c_str(), "/") &&
               !Util::isPseudoTty(device.c_str()) &&
               isRegularFile) {
      Connection *c = findDuplication(fd, device.c_str());
      if (c != NULL) {
        add(fd, c);
        continue;
      }
    } else {
      JTRACE("Ignoring pre-existing fd") (device);
    }
  }
}

Connection *
FileConnList::findDuplication(int fd, const char *path)
{
  for (iterator i = begin(); i != end(); ++i) {
    Connection *con = i->second;

    if (con->conType() != Connection::FILE) {
      continue;
    }

    FileConnection *fcon = (FileConnection *)con;

    // check for duplication
    if( fcon->checkDup(fd, path) ){
      return con;
    }
  }
  return NULL;
}

Connection *
FileConnList::createDummyConnection(int type)
{
  switch (type) {
  case Connection::FILE:
    return new FileConnection();

    break;
  case Connection::FIFO:
    return new FifoConnection();

    break;
  case Connection::STDIO:
    return new StdioConnection();

    break;
  }
  return NULL;
}

void
FileConnList::processFileConnection(int fd,
                                    const char *path,
                                    int flags,
                                    mode_t mode)
{
  Connection *c = NULL;

  string device;

  if (path == NULL) {
    device = jalib::Filesystem::GetDeviceName(fd);
  } else {
    device = jalib::Filesystem::ResolveSymlink(path);
    if (device == "") {
      device = path;
    }
  }

  struct stat statbuf;
  JASSERT(fstat(fd, &statbuf) == 0);

  if (strstr(device.c_str(), "infiniband/uverbs") ||
      strstr(device.c_str(), "uverbs-event")) {
    return;
  }

  path = device.c_str();
  if (S_ISREG(statbuf.st_mode) || S_ISCHR(statbuf.st_mode) ||
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
    JASSERT(false) (path).Text("Unimplemented file type.");
  }

  add(fd, c);
}
