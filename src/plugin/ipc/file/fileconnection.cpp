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

#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/un.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <fstream>
#include <ios>
#include <iostream>

#include "jassert.h"
#include "jconvert.h"
#include "jfilesystem.h"
#include "jsocket.h"
#include "dmtcp.h"
#include "shareddata.h"
#include "util.h"

#include "fileconnection.h"
#include "fileconnlist.h"
#include "filewrappers.h"

using namespace dmtcp;

static void writeFileFromFd(int fd, int destFd);
static bool areFilesEqual(int fd, int destFd, size_t size);

static bool
_isVimApp()
{
  static int isVimApp = -1;

  if (isVimApp == -1) {
    string progName = jalib::Filesystem::GetProgramName();

    if (progName == "vi" || progName == "vim" || progName == "vim-normal" ||
        progName == "vim.basic" || progName == "vim.tiny" ||
        progName == "vim.gtk" || progName == "vim.gnome") {
      isVimApp = 1;
    } else {
      isVimApp = 0;
    }
  }
  return isVimApp;
}

static bool
_isBlacklistedFile(string &path)
{
  if ((Util::strStartsWith(path.c_str(), "/dev/") &&
       !Util::strStartsWith(path.c_str(), "/dev/shm/")) ||
      Util::strStartsWith(path.c_str(), "/proc/") ||
      Util::strStartsWith(path.c_str(), dmtcp_get_tmpdir())) {
    return true;
  }
  return false;
}

/*****************************************************************************
 * File Connection
 *****************************************************************************/

// Upper limit on filesize for files that are automatically chosen for ckpt.
// Default 100MB
#define MAX_FILESIZE_TO_AUTOCKPT (100 * 1024 * 1024)

void
FileConnection::doLocking()
{
  if (Util::strStartsWith(_path.c_str(), "/proc/")) {
    int index = 6;
    char *rest;
    pid_t proc_pid = strtol(&_path[index], &rest, 0);
    if (proc_pid > 0 && *rest == '/') {
      _type = FILE_PROCFS;
      if (proc_pid != getpid()) {
        return;
      }
    }
  }
  Connection::doLocking();
  _ckpted_file = false;
}

void
FileConnection::calculateRelativePath()
{
  string cwd = jalib::Filesystem::GetCWD();

  if (_path.compare(0, cwd.length(), cwd) == 0 &&
      _path.length() > cwd.length()) {
    /* CWD = "/A/B", FileName = "/A/B/C/D" ==> relPath = "C/D" */
    _rel_path = _path.substr(cwd.length() + 1);
  } else {
    _rel_path = "*";
  }
}

void
FileConnection::drain()
{
  struct stat statbuf;

  JASSERT(_fds.size() > 0);

  _ckpted_file = false;
  _allow_overwrite = false;

  // Read the current file descriptor offset
  _offset = lseek(_fds[0], 0, SEEK_CUR);
  fstat(_fds[0], &statbuf);
  _st_dev = statbuf.st_dev;
  _st_ino = statbuf.st_ino;
  _st_size = statbuf.st_size;

  if (_type == FILE_PROCFS) {
    return;
  }

  if (statbuf.st_nlink == 0) {
    _type = FILE_DELETED;
  } else {
    // Update _path to reflect the current state. The file path might be a new
    // one after restart and if the current process wasn't the leader, it never
    // had a chance to update the _path. Update it now.
    _path = jalib::Filesystem::GetDeviceName(_fds[0]);
    // Files deleted on NFS have the .nfsXXXX format.
    if (Util::strStartsWith(jalib::Filesystem::BaseName(_path).c_str(), ".nfs")
        || !jalib::Filesystem::FileExists(_path)) {
      _type = FILE_DELETED;
    }
  }

  calculateRelativePath();

  // If this file is related to supported Resource Management system
  // handle it specially
  if (_type == FILE_BATCH_QUEUE &&
      dmtcp_bq_should_ckpt_file &&
      dmtcp_bq_should_ckpt_file(_path.c_str(), &_rmtype)) {
    JTRACE("Pre-checkpoint Torque files") (_fds.size());
    for (unsigned int i = 0; i < _fds.size(); i++) {
      JTRACE("_fds[i]=") (i) (_fds[i]);
    }
    _ckpted_file = true;
    return;
  }

  if (_type == FILE_DELETED && (_fcntlFlags & O_WRONLY)) {
    return;
  }

  if (dmtcp_must_ckpt_file && dmtcp_must_ckpt_file(_path.c_str())) {
    _ckpted_file = true;
    return;
  }

  if (_isBlacklistedFile(_path)) {
    return;
  }
  if (dmtcp_should_ckpt_open_files() && statbuf.st_uid == getuid()) {
    _ckpted_file = true;
  } else if (_type == FILE_DELETED || _type == FILE_SHM) {
    _ckpted_file = true;
  } else if (_isVimApp() &&
             (Util::strEndsWith(_path.c_str(), ".swp") == 0 ||
              Util::strEndsWith(_path.c_str(), ".swo") == 0)) {
    _ckpted_file = true;
  } else if (Util::strStartsWith(jalib::Filesystem::GetProgramName().c_str(),
                                 "emacs")) {
    _ckpted_file = true;
#if 0
  } else if ((_fcntlFlags & (O_WRONLY | O_RDWR)) != 0 &&
             _offset < _st_size &&
             _st_size < MAX_FILESIZE_TO_AUTOCKPT &&
             statbuf.st_uid == getuid()) {
    // FIXME: Disable the following heuristic until we can come up with
    // a better one
    _ckpted_file = true;
#endif // if 0
  } else {
    _ckpted_file = false;
  }
}

void
FileConnection::preCkpt()
{
  if (_ckpted_file) {
    ConnectionIdentifier id;
    JASSERT(_type != FILE_PROCFS && _type != FILE_INVALID);
    JASSERT(SharedData::getCkptLeaderForFile(_st_dev, _st_ino, &id));
    if (id == _id) {
      _savedFilePath = getSavedFilePath(_path);
      JASSERT(FileConnList::createDirectoryTree(_savedFilePath))
        (_savedFilePath)
        .Text("Unable to create directory in File Path");

      int destFd = _real_open(
          _savedFilePath.c_str(), O_CREAT | O_WRONLY | O_TRUNC,
          S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
      JASSERT(destFd != -1) (JASSERT_ERRNO) (_path) (_savedFilePath);

      JTRACE("Saving checkpointed copy of the file") (_path) (_savedFilePath);
      if (_fcntlFlags & O_WRONLY) {
        // If the file is opened() in write-only mode. Open it in readonly mode
        // to create the ckpt copy.
        int tmpfd = _real_open(_path.c_str(), O_RDONLY, 0);
        JASSERT(tmpfd != -1);
        writeFileFromFd(tmpfd, destFd);
        _real_close(tmpfd);
      } else {
        writeFileFromFd(_fds[0], destFd);
      }
      _real_close(destFd);
    } else {
      JTRACE("Not checkpointing this file") (_path);
      _ckpted_file = false;
    }

    /* The _allow_overwrite flag is clear by default; we only set
     * it for a regular file that has its _ckpted_file flag set and
     * only for the process that's been chosen as the fd leader.
     */
    if (_ckpted_file &&
        (dmtcp_allow_overwrite_with_ckpted_files() ||
         (dmtcp_must_overwrite_file &&
          dmtcp_must_overwrite_file(_path.c_str())))) {
      _allow_overwrite = true;
    }
  }
}

/* Given an open file-descriptor for a saved file, saves a copy
 * of its existing copy, and replaces the existing copy with the
 * saved file.
 */
void
FileConnection::overwriteFileWithBackup(int savedFd)
{
  char currentTimeBuff[30] = { 0 };
  time_t rawtime;

  time(&rawtime);
  strftime(currentTimeBuff, 30, "-%F-%H-%M-%S.bk", localtime(&rawtime));
  dmtcp::string backupPath = _path + currentTimeBuff;

  // Temporarily close the file to avoid overwriting
  _real_close(_fds[0]);

  // Create a backup of user file
  JWARNING(rename(_path.c_str(), backupPath.c_str()) == 0) (JASSERT_ERRNO)
  .Text("Error creating a backup");

  /* Overwrite the existing file with the contents of the saved
   * file. We need to open it in WRONLY mode here because the file
   * might have been opened originally in read-only mode.
   */
  int destFileFd = _real_open(_path.c_str(), O_CREAT | O_WRONLY, 0640);
  JASSERT(destFileFd > 0)(JASSERT_ERRNO)(_path)
  .Text("Error opening file for overwriting");
  writeFileFromFd(savedFd, destFileFd);
  _real_close(destFileFd);

  // Re-open the (closed) file with the original flags
  int tempfd = openFile();
  restoreDupFds(tempfd);
}

void
FileConnection::refill(bool isRestart)
{
  struct stat statbuf;

  if (!isRestart) {
    return;
  }
  if (strstr(_path.c_str(), "infiniband/uverbs") ||
      strstr(_path.c_str(), "uverbs-event")) {
    return;
  }

  if (_ckpted_file && _fileAlreadyExists) {
    int savedFd = _real_open(_savedFilePath.c_str(), O_RDONLY, 0);
    JASSERT(savedFd != -1) (JASSERT_ERRNO) (_savedFilePath);

    if (_allow_overwrite) {
      JTRACE("Copying checkpointed file to original location")
        (_savedFilePath) (_path);
      this->overwriteFileWithBackup(savedFd);
    } else {
      if (!areFilesEqual(_fds[0], savedFd, _st_size)) {
        if (_type == FILE_SHM) {
          JWARNING(false) (_path) (_savedFilePath)
          .Text("\n"
                "***Mapping current version of file into memory;\n"
                "   _not_ file as it existed at time of checkpoint.\n"
                "   Change this function and re-compile, if you want "
                "different behavior.");
        } else {
          const char *errMsg =
            "\n**** File already exists! Checkpointed copy can't be restored.\n"
            "       The Contents of checkpointed copy differ from the "
            "contents of the existing copy.\n"
            "****Delete the existing file and try again!";
          JASSERT(false) (_path) (_savedFilePath) (errMsg);
        }
      }
    }
    _real_close(savedFd);
  }

  if (!_ckpted_file) {
    int tempfd;
    if (_type == FILE_DELETED &&
        ((_fcntlFlags & O_WRONLY) || (_fcntlFlags & O_RDWR))) {
      tempfd = _real_open(_path.c_str(), _fcntlFlags | O_CREAT, 0600);
      JASSERT(tempfd != -1) (_path) (JASSERT_ERRNO).Text("open() failed");
      JASSERT(truncate(_path.c_str(), _st_size) == 0)
        (_path.c_str()) (_st_size) (JASSERT_ERRNO);
    } else {
      JASSERT(jalib::Filesystem::FileExists(_path)) (_path)
      .Text("File not found.");

      if (stat(_path.c_str(), &statbuf) == 0 && S_ISREG(statbuf.st_mode)) {
        if (statbuf.st_size > _st_size &&
            ((_fcntlFlags & O_WRONLY) || (_fcntlFlags & O_RDWR))) {
          errno = 0;
          JASSERT(truncate(_path.c_str(), _st_size) == 0)
            (_path.c_str()) (_st_size) (JASSERT_ERRNO);
        } else if (statbuf.st_size < _st_size) {
          JWARNING(false).Text("Size of file smaller than what we expected");
        }
      }
      tempfd = openFile();
    }
    restoreDupFds(tempfd);
  }

  errno = 0;
  if (jalib::Filesystem::FileExists(_path) &&
      stat(_path.c_str(), &statbuf) == 0 && S_ISREG(statbuf.st_mode)) {
    if (_offset <= statbuf.st_size && _offset <= _st_size) {
      JASSERT(lseek(_fds[0], _offset, SEEK_SET) == _offset)
        (_path) (_offset) (JASSERT_ERRNO);

      // JTRACE("lseek(_fds[0], _offset, SEEK_SET)") (_fds[0]) (_offset);
    } else if (_offset > statbuf.st_size || _offset > _st_size) {
      JWARNING(false) (_path) (_offset) (_st_size) (statbuf.st_size)
      .Text("No lseek done:  offset is larger than min of old and new size.");
    }
  }
  refreshPath();
}

void
FileConnection::resume(bool isRestart)
{
  if (isRestart && _type == FILE_DELETED) {
    /* Here we want to unlink the file. We want to do it only at the time of
     * restart, but there is no way of finding out if we are restarting or not.
     * That is why we look for the file on disk and if it is present(it was
     * deleted at ckpt time), then we assume that we are restarting and hence
     * we unlink the file.
     */
    if (jalib::Filesystem::FileExists(_path)) {
      JWARNING(unlink(_path.c_str()) != -1) (_path)
      .Text("The file was unlinked at the time of checkpoint. "
            "Unlinking it after restart failed");
    }
  }
}

void
FileConnection::refreshPath()
{
  string cwd = jalib::Filesystem::GetCWD();

  if (_type == FILE_BATCH_QUEUE) {
    // get new file name
    string newpath = jalib::Filesystem::GetDeviceName(_fds[0]);
    JTRACE("This is Resource Manager file!") (_fds[0]) (newpath) (_path) (this);
    if (newpath != _path) {
      JTRACE("File Manager connection _path is changed => _path = newpath!")
        (_path) (newpath);
      _path = newpath;
    }
    return;
  }

  if (dmtcp_get_new_file_path) {
    char newpath[PATH_MAX];
    newpath[0] = '\0';
    dmtcp_get_new_file_path(_path.c_str(), cwd.c_str(), newpath);
    if (newpath[0] != '\0') {
      JASSERT(jalib::Filesystem::FileExists(newpath)) (_path) (newpath)
      .Text("Path returned by plugin does not exist.");
      _path = newpath;
      return;
    }
  }
  if (_rel_path != "*" && !jalib::Filesystem::FileExists(_path)) {
    // If file at absolute path doesn't exist and file path is relative to
    // executable current dir
    string oldPath = _path;
    string fullPath = cwd + "/" + _rel_path;
    if (jalib::Filesystem::FileExists(fullPath)) {
      _path = fullPath;
      JTRACE("Change _path based on relative path")
        (oldPath) (_path) (_rel_path);
    }
  } else if (_type == FILE_PROCFS) {
    int index = 6;
    char *rest;
    char buf[64];
    pid_t proc_pid = strtol(&_path[index], &rest, 0);
    if (proc_pid > 0 && *rest == '/') {
      sprintf(buf, "/proc/%d/%s", getpid(), rest);
      _path = buf;
    }
  }
}

void
FileConnection::postRestart()
{
  int tempfd;

  JASSERT(_fds.size() > 0);

  if (dmtcp_get_new_file_path) {
    refreshPath();
  }

  if (!_ckpted_file) {
    return;
  }
  _fileAlreadyExists = false;

  JTRACE("Restoring File Connection") (id()) (_path);
  JASSERT(jalib::Filesystem::FileExists(_savedFilePath))
    (_savedFilePath) (_path).Text("Unable to find checkpointed copy of file");

  if (_type == FILE_BATCH_QUEUE) {
    JASSERT(dmtcp_bq_restore_file);
    tempfd = dmtcp_bq_restore_file(_path.c_str(), _savedFilePath.c_str(),
                                   _fcntlFlags, _rmtype);
    JTRACE("Restore Resource Manager File") (_path);
  } else {
    refreshPath();
    JASSERT(FileConnList::createDirectoryTree(_path)) (_path)
    .Text("Unable to create directory in File Path");

    /* Now try to create the file with O_EXCL. If we fail with EEXIST, there
     * are two possible scenarios:
     * - The file was created by a different restarting process with data from
     *   checkpointed copy. It is possible that the data is in flight, so we
     *   should wait until the next barrier to compare the data from our copy.
     * - The file existed before restart. After the next barrier, abort if the
     *   contents differ from our checkpointed copy.
     */
    int fd = _real_open(_path.c_str(), O_CREAT | O_EXCL | O_RDWR,
                        S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    JASSERT(fd != -1 || errno == EEXIST) (_path) (JASSERT_ERRNO);

    if (fd == -1) {
      _fileAlreadyExists = true;
    } else {
      int srcFd = _real_open(_savedFilePath.c_str(), O_RDONLY, 0);
      JASSERT(srcFd != -1) (_path) (_savedFilePath) (JASSERT_ERRNO)
      .Text("Failed to open checkpointed copy of the file.");
      JTRACE("Copying saved checkpointed file to original location")
        (_savedFilePath) (_path);
      writeFileFromFd(srcFd, fd);
      _real_close(srcFd);
      _real_close(fd);
    }
    tempfd = openFile();
  }
  restoreDupFds(tempfd);
}

bool
FileConnection::checkDup(int fd, const char *npath)
{
  bool retVal = false;

  int myfd = _fds[0];

  string fullPath = jalib::Filesystem::GetDeviceName(myfd);
  string fpath = string(npath);
  if (fullPath != fpath &&
       lseek(myfd, 0, SEEK_CUR) == lseek(fd, 0, SEEK_CUR)) {
    off_t newOffset = lseek(myfd, 1, SEEK_CUR);
    JASSERT(newOffset != -1) (JASSERT_ERRNO).Text("lseek failed");

    if (newOffset == lseek(fd, 0, SEEK_CUR)) {
      retVal = true;
    }

    // Now restore the old offset
    JASSERT(-1 != lseek(myfd, -1, SEEK_CUR)).Text("lseek failed");
  }
  return retVal;
}

int
FileConnection::openFile()
{
  JASSERT(jalib::Filesystem::FileExists(_path)) (_path)
  .Text("File not present");

  int fd = _real_open(_path.c_str(), _fcntlFlags);
  JASSERT(fd != -1) (_path) (JASSERT_ERRNO).Text("open() failed");

  JTRACE("open(_path.c_str(), _fcntlFlags)") (fd) (_path.c_str()) (_fcntlFlags);
  return fd;
}

static bool
areFilesEqual(int fd, int savedFd, size_t size)
{
  long page_size = sysconf(_SC_PAGESIZE);
  const size_t bufSize = 1024 * page_size;
  char *buf1 = (char *)JALLOC_HELPER_MALLOC(bufSize);
  char *buf2 = (char *)JALLOC_HELPER_MALLOC(bufSize);

  off_t offset1 = _real_lseek(fd, 0, SEEK_CUR);
  off_t offset2 = _real_lseek(savedFd, 0, SEEK_CUR);

  JASSERT(_real_lseek(fd, 0, SEEK_SET) == 0) (fd) (JASSERT_ERRNO);
  JASSERT(_real_lseek(savedFd, 0, SEEK_SET) == 0) (savedFd) (JASSERT_ERRNO);

  int readBytes;
  while (size > 0) {
    readBytes = Util::readAll(savedFd, buf1, MIN(bufSize, size));
    JASSERT(readBytes != -1) (JASSERT_ERRNO).Text("Read Failed");
    if (readBytes == 0) {
      break;
    }
    if (Util::readAll(fd, buf2, readBytes) != readBytes) {
      break;
    }
    if (memcmp(buf1, buf2, readBytes) != 0) {
      break;
    }
    size -= readBytes;
  }
  JALLOC_HELPER_FREE(buf1);
  JALLOC_HELPER_FREE(buf2);
  JASSERT(_real_lseek(fd, offset1, SEEK_SET) != -1);
  JASSERT(_real_lseek(savedFd, offset2, SEEK_SET) != -1);
  return size == 0;
}

static void
writeFileFromFd(int fd, int destFd)
{
  long page_size = sysconf(_SC_PAGESIZE);
  const size_t bufSize = 1024 * page_size;
  char *buf = (char *)JALLOC_HELPER_MALLOC(bufSize);

  // Synchronize memory buffer with data in filesystem
  // On some Linux kernels, the shared-memory test will fail without this.
  fsync(fd);

  off_t offset = _real_lseek(fd, 0, SEEK_CUR);
  JASSERT(_real_lseek(fd, 0, SEEK_SET) == 0) (fd) (JASSERT_ERRNO);
  JASSERT(_real_lseek(destFd, 0, SEEK_SET) == 0) (destFd) (JASSERT_ERRNO);

  int readBytes, writtenBytes;
  while (1) {
    readBytes = Util::readAll(fd, buf, bufSize);
    JASSERT(readBytes != -1) (JASSERT_ERRNO).Text("Read Failed");
    if (readBytes == 0) {
      break;
    }
    writtenBytes = Util::writeAll(destFd, buf, readBytes);
    JASSERT(writtenBytes != -1) (JASSERT_ERRNO).Text("Write failed.");
  }
  JALLOC_HELPER_FREE(buf);
  JASSERT(_real_lseek(fd, offset, SEEK_SET) != -1);
}

string
FileConnection::getSavedFilePath(const string &path)
{
  ostringstream os;

  os << dmtcp_get_ckpt_files_subdir()
     << "/" << jalib::Filesystem::BaseName(_path) << "_" << _id.conId();

  return os.str();
}

void
FileConnection::serializeSubClass(jalib::JBinarySerializer &o)
{
  JSERIALIZE_ASSERT_POINT("FileConnection");
  o&_path &_rel_path;
  o&_offset&_st_dev&_st_ino&_st_size&_ckpted_file &_rmtype;
  JTRACE("Serializing FileConn.") (_path) (_rel_path)
    (dmtcp_get_ckpt_files_subdir()) (_ckpted_file) (_allow_overwrite) (
    _fcntlFlags);
}

/*****************************************************************************
 * FIFO Connection
 *****************************************************************************/
void
FifoConnection::drain()
{
  struct stat st;

  JASSERT(_fds.size() > 0);

  stat(_path.c_str(), &st);
  JTRACE("Checkpoint fifo.") (_fds[0]);
  _mode = st.st_mode;

  int new_flags = (_fcntlFlags & (~(O_RDONLY | O_WRONLY))) | O_RDWR |
    O_NONBLOCK;
  ckptfd = _real_open(_path.c_str(), new_flags);
  JASSERT(ckptfd >= 0) (ckptfd) (JASSERT_ERRNO);

  _in_data.clear();
  size_t bufsize = 256;
  char buf[bufsize];
  int size;

  while (1) { // flush fifo
    size = read(ckptfd, buf, bufsize);
    if (size < 0) {
      break; // nothing to flush
    }
    for (int i = 0; i < size; i++) {
      _in_data.push_back(buf[i]);
    }
  }
  close(ckptfd);
  JTRACE("Checkpointing fifo:  end.") (_fds[0]) (_in_data.size());
}

void
FifoConnection::refill(bool isRestart)
{
  int new_flags = (_fcntlFlags & (~(O_RDONLY | O_WRONLY))) | O_RDWR |
    O_NONBLOCK;

  ckptfd = _real_open(_path.c_str(), new_flags);
  JASSERT(ckptfd >= 0) (ckptfd) (JASSERT_ERRNO);

  size_t bufsize = 256;
  char buf[bufsize];
  size_t j;
  ssize_t ret;
  for (size_t i = 0; i < (_in_data.size() / bufsize); i++) { // refill fifo
    for (j = 0; j < bufsize; j++) {
      buf[j] = _in_data[j + i * bufsize];
    }
    ret = Util::writeAll(ckptfd, buf, j);
    JASSERT(ret == (ssize_t)j) (JASSERT_ERRNO) (ret) (j) (_fds[0]) (i);
  }
  int start = (_in_data.size() / bufsize) * bufsize;
  for (j = 0; j < _in_data.size() % bufsize; j++) {
    buf[j] = _in_data[start + j];
  }
  errno = 0;
  buf[j] = '\0';
  JTRACE("Buf internals.") ((const char *)buf);
  ret = Util::writeAll(ckptfd, buf, j);
  JASSERT(ret == (ssize_t)j) (JASSERT_ERRNO) (ret) (j) (_fds[0]);

  close(ckptfd);

  // unlock fifo
  flock(_fds[0], LOCK_UN);
  JTRACE("End checkpointing fifo.") (_fds[0]);
}

void
FifoConnection::refreshPath()
{
  string cwd = jalib::Filesystem::GetCWD();

  if (_rel_path != "*") { // file path is relative to executable current dir
    string oldPath = _path;
    ostringstream fullPath;
    fullPath << cwd << "/" << _rel_path;
    if (jalib::Filesystem::FileExists(fullPath.str())) {
      _path = fullPath.str();
      JTRACE("Change _path based on relative path") (oldPath) (_path);
    }
  }
}

void
FifoConnection::postRestart()
{
  JASSERT(_fds.size() > 0);
  JTRACE("Restoring Fifo Connection") (id()) (_path);
  refreshPath();
  int tempfd = openFile();
  restoreDupFds(tempfd);
  refreshPath();
}

int
FifoConnection::openFile()
{
  int fd;

  if (!jalib::Filesystem::FileExists(_path)) {
    JTRACE("Fifo file not present, creating new one") (_path);
    dmtcp::string dir = jalib::Filesystem::DirName(_path);
    JTRACE("fifo dir:")(dir);
    jalib::Filesystem::mkdir_r(dir, 0755);
    mkfifo(_path.c_str(), _mode);
    JTRACE("mkfifo") (_path.c_str()) (errno);
  }

  fd = _real_open(_path.c_str(), O_RDWR | O_NONBLOCK);
  JTRACE("Is opened") (_path.c_str()) (fd);

  JASSERT(fd != -1) (_path) (JASSERT_ERRNO);
  return fd;
}

void
FifoConnection::serializeSubClass(jalib::JBinarySerializer &o)
{
  JSERIALIZE_ASSERT_POINT("FifoConnection");
  o&_path&_rel_path&_savedRelativePath&_mode &_in_data;
  JTRACE("Serializing FifoConn.") (_path) (_rel_path) (_savedRelativePath);
}

/*****************************************************************************
 * Stdio Connection
 *****************************************************************************/
void
StdioConnection::postRestart()
{
  for (size_t i = 0; i < _fds.size(); ++i) {
    int fd = _fds[i];
    if (fd <= 2) {
      JTRACE("Skipping restore of STDIO, just inherit from parent") (fd);
      continue;
    }
    int oldFd = -1;
    switch (_type) {
    case STDIO_IN:
      JTRACE("Restoring STDIN") (fd);
      oldFd = 0;
      break;
    case STDIO_OUT:
      JTRACE("Restoring STDOUT") (fd);
      oldFd = 1;
      break;
    case STDIO_ERR:
      JTRACE("Restoring STDERR") (fd);
      oldFd = 2;
      break;
    default:
      JASSERT(false);
    }
    errno = 0;
    JWARNING(_real_dup2(oldFd, fd) == fd) (oldFd) (fd) (JASSERT_ERRNO);
  }
}

/*****************************************************************************
 * POSIX Message Queue Connection
 *****************************************************************************/
void
PosixMQConnection::on_mq_close()
{}

void
PosixMQConnection::on_mq_notify(const struct sigevent *sevp)
{
  if (sevp == NULL && _notifyReg) {
    _notifyReg = false;
  } else {
    _notifyReg = true;
    if (sevp) {
      _sevp = *sevp;
    }
  }
}

void
PosixMQConnection::doLocking()
{
  if (!(_oflag & O_RDWR) && !(_oflag & O_RDONLY)) {
    return;
  }
  Connection::doLocking();
}

void
PosixMQConnection::drain()
{
  JASSERT(_fds.size() > 0);

  JTRACE("Checkpoint Posix Message Queue.") (_fds[0]);

  struct stat statbuf;
  JASSERT(fstat(_fds[0], &statbuf) != -1) (JASSERT_ERRNO);
  if (_mode == 0) {
    _mode = statbuf.st_mode;
  }

  struct mq_attr attr;
  JASSERT(mq_getattr(_fds[0], &attr) != -1) (JASSERT_ERRNO);
  _attr = attr;
  if (attr.mq_curmsgs < 0) {
    return;
  }

  _qnum = attr.mq_curmsgs;
  char *buf = (char *)JALLOC_HELPER_MALLOC(attr.mq_msgsize);
  for (long i = 0; i < _qnum; i++) {
    unsigned prio;
    ssize_t numBytes = _real_mq_receive(_fds[0], buf, attr.mq_msgsize, &prio);
    JASSERT(numBytes != -1) (JASSERT_ERRNO);
    _msgInQueue.push_back(jalib::JBuffer((const char *)buf, numBytes));
    _msgInQueuePrio.push_back(prio);
  }
  JALLOC_HELPER_FREE(buf);
}

void
PosixMQConnection::refill(bool isRestart)
{
  for (long i = 0; i < _qnum; i++) {
    JASSERT(_real_mq_send(_fds[0], _msgInQueue[i].buffer(),
                          _msgInQueue[i].size(), _msgInQueuePrio[i]) != -1);
  }
  _msgInQueue.clear();
  _msgInQueuePrio.clear();
}

void
PosixMQConnection::postRestart()
{
  JASSERT(_fds.size() > 0);

  errno = 0;
  if (_oflag & O_EXCL) {
    mq_unlink(_name.c_str());
  }

  int tempfd = _real_mq_open(_name.c_str(), _oflag, _mode, &_attr);
  JASSERT(tempfd != -1) (JASSERT_ERRNO);
  restoreDupFds(tempfd);
}

void
PosixMQConnection::serializeSubClass(jalib::JBinarySerializer &o)
{
  JSERIALIZE_ASSERT_POINT("PosixMQConnection");
  o&_name&_oflag&_mode &_attr;
}
