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

#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <termios.h>
#include <iostream>
#include <ios>
#include <fstream>
#include <linux/limits.h>
#include <arpa/inet.h>

#include "dmtcp.h"
#include "shareddata.h"
#include "util.h"
#include "jsocket.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "jconvert.h"

#include "fileconnection.h"
#include "filewrappers.h"

using namespace dmtcp;

static bool ptmxTestPacketMode(int masterFd);
static ssize_t ptmxReadAll(int fd, const void *origBuf, size_t maxCount);
static ssize_t ptmxWriteAll(int fd, const void *buf, bool isPacketMode);
static void CreateDirectoryStructure(const dmtcp::string& path);
static void writeFileFromFd(int fd, int destFd);
static bool areFilesEqual(int fd, int destFd, size_t size);

static bool _isVimApp()
{
  static int isVimApp = -1;

  if (isVimApp == -1) {
    dmtcp::string progName = jalib::Filesystem::GetProgramName();

    if (progName == "vi" || progName == "vim" || progName == "vim-normal" ||
        progName == "vim.basic"  || progName == "vim.tiny" ||
        progName == "vim.gtk" || progName == "vim.gnome") {
      isVimApp = 1;
    } else {
      isVimApp = 0;
    }
  }
  return isVimApp;
}

static bool _isBlacklistedFile(dmtcp::string& path)
{
  if ((dmtcp::Util::strStartsWith(path, "/dev/") &&
       !dmtcp::Util::strStartsWith(path, "/dev/shm/")) ||
      dmtcp::Util::strStartsWith(path, "/proc/") ||
      dmtcp::Util::strStartsWith(path, dmtcp_get_tmpdir())) {
    return true;
  }
  return false;
}

/*****************************************************************************
 * Pseudo-TTY Connection
 *****************************************************************************/

static bool ptmxTestPacketMode(int masterFd)
{
  char tmp_buf[100];
  int slave_fd, ioctlArg, rc;
  fd_set read_fds;
  struct timeval zeroTimeout = {0, 0}; /* Zero: will use to poll, not wait.*/

  _real_ptsname_r(masterFd, tmp_buf, 100);
  /* permissions not used, but _real_open requires third arg */
  slave_fd = _real_open(tmp_buf, O_RDWR, 0666);

  /* A. Drain master before testing.
     Ideally, DMTCP has already drained it and preserved any information
     about exceptional conditions in command byte, but maybe we accidentally
     caused a new command byte in packet mode. */
  /* Note:  if terminal was in packet mode, and the next read would be
     a non-data command byte, then there's no easy way for now to detect and
     restore this. ?? */
  /* Note:  if there was no data to flush, there might be no command byte,
     even in packet mode. */
  tcflush(slave_fd, TCIOFLUSH);
  /* If character already transmitted(usual case for pty), then this flush
     will tell master to flush it. */
  tcflush(masterFd, TCIFLUSH);

  /* B. Now verify that read_fds has no more characters to read. */
  ioctlArg = 1;
  ioctl(masterFd, TIOCINQ, &ioctlArg);
  /* Now check if there's a command byte still to read. */
  FD_ZERO(&read_fds);
  FD_SET(masterFd, &read_fds);
  select(masterFd + 1, &read_fds, NULL, NULL, &zeroTimeout);
  if (FD_ISSET(masterFd, &read_fds)) {
    // Clean up someone else's command byte from packet mode.
    // FIXME:  We should restore this on resume/restart.
    rc = read(masterFd, tmp_buf, 100);
    JASSERT(rc == 1) (rc) (masterFd);
  }

  /* C. Now we're ready to do the real test.  If in packet mode, we should
     see command byte of TIOCPKT_DATA(0) with data. */
  tmp_buf[0] = 'x'; /* Don't set '\n'.  Could be converted to "\r\n". */
  /* Give the masterFd something to read. */
  JWARNING((rc = write(slave_fd, tmp_buf, 1)) == 1) (rc) .Text("write failed");
  //tcdrain(slave_fd);
  _real_close(slave_fd);

  /* Read the 'x':  If we also see a command byte, it's packet mode */
  rc = read(masterFd, tmp_buf, 100);

  /* D. Check if command byte packet exists, and chars rec'd is longer by 1. */
  return(rc == 2 && tmp_buf[0] == TIOCPKT_DATA && tmp_buf[1] == 'x');
}

// Also record the count read on each iteration, in case it's packet mode
static bool readyToRead(int fd)
{
  fd_set read_fds;
  struct timeval zeroTimeout = {0, 0}; /* Zero: will use to poll, not wait.*/
  FD_ZERO(&read_fds);
  FD_SET(fd, &read_fds);
  select(fd + 1, &read_fds, NULL, NULL, &zeroTimeout);
  return FD_ISSET(fd, &read_fds);
}

// returns 0 if not ready to read; else returns -1, or size read incl. header
static ssize_t readOnePacket(int fd, const void *buf, size_t maxCount)
{
  typedef int hdr;
  ssize_t rc = 0;
  // Read single packet:  rc > 0 will be true for at most one iteration.
  while (readyToRead(fd) && rc <= 0) {
    rc = read(fd,(char *)buf+sizeof(hdr), maxCount-sizeof(hdr));
    *(hdr *)buf = rc; // Record the number read in header
    if (rc >=(ssize_t) (maxCount-sizeof(hdr))) {
      rc = -1; errno = E2BIG; // Invoke new errno for buf size not large enough
    }
    if (rc == -1 && errno != EAGAIN && errno != EINTR)
      break;  /* Give up; bad error */
  }
  return(rc <= 0 ? rc : rc+sizeof(hdr));
}

// rc < 0 => error; rc == sizeof(hdr) => no data to read;
// rc > 0 => saved w/ count hdr
static ssize_t ptmxReadAll(int fd, const void *origBuf, size_t maxCount)
{
  typedef int hdr;
  char *buf =(char *)origBuf;
  int rc;
  while ((rc = readOnePacket(fd, buf, maxCount)) > 0) {
    buf += rc;
  }
  *(hdr *)buf = 0; /* Header count of zero means we're done */
  buf += sizeof(hdr);
  JASSERT(rc < 0 || buf -(char *)origBuf > 0) (rc) (origBuf) ((void *)buf);
  return(rc < 0 ? rc : buf -(char *)origBuf);
}

// The hdr contains the size of the full buffer([hdr, data]).
// Return size of origBuf written:  includes packets of form:  [hdr, data]
//   with hdr holding size of data.  Last hdr has value zero.
// Also record the count written on each iteration, in case it's packet mode.
static ssize_t writeOnePacket(int fd, const void *origBuf, bool isPacketMode)
{
  typedef int hdr;
  int count = *(hdr *)origBuf;
  int cum_count = 0;
  int rc = 0; // Trigger JASSERT if not modified below.
  if (count == 0)
    return sizeof(hdr);  // count of zero means we're done, hdr consumed
  // FIXME:  It would be nice to restore packet mode(flow control, etc.)
  //         For now, we ignore it.
  if (count == 1 && isPacketMode)
    return sizeof(hdr) + 1;
  while (cum_count < count) {
    rc = write(fd,(char *)origBuf+sizeof(hdr)+cum_count, count-cum_count);
    if (rc == -1 && errno != EAGAIN && errno != EINTR)
      break;  /* Give up; bad error */
    if (rc >= 0)
      cum_count += rc;
  }
  JASSERT(rc != 0 && cum_count == count)
    (JASSERT_ERRNO) (rc) (count) (cum_count);
  return(rc < 0 ? rc : cum_count+sizeof(hdr));
}

static ssize_t ptmxWriteAll(int fd, const void *buf, bool isPacketMode)
{
  typedef int hdr;
  ssize_t cum_count = 0;
  ssize_t rc;
  while ((rc = writeOnePacket(fd,(char *)buf+cum_count, isPacketMode))
         >(ssize_t)sizeof(hdr)) {
    cum_count += rc;
  }
  JASSERT(rc < 0 || rc == sizeof(hdr)) (rc) (cum_count);
  cum_count += sizeof(hdr);  /* Account for last packet: 'done' hdr w/ 0 data */
  return(rc <= 0 ? rc : cum_count);
}

dmtcp::PtyConnection::PtyConnection(int fd, const char *path,
                                    int flags, mode_t mode, int type)
  : Connection (PTY)
  , _flags(flags)
  , _mode(mode)
  , _preExistingCTTY(false)
{
  char buf[PTS_PATH_MAX];
  _type = type;
  switch (_type) {

    case PTY_DEV_TTY:
      _ptsName = path;
      break;

    case PTY_PARENT_CTTY:
    case PTY_CTTY:
      _ptsName = path;
      SharedData::getVirtPtyName(path, buf, sizeof(buf));
      if (strlen(buf) == 0) {
        SharedData::createVirtualPtyName(path, buf, sizeof(buf));
      }
      _virtPtsName = buf;
      JTRACE("creating CTTY connection") (_ptsName) (_virtPtsName);

      break;

    case PTY_MASTER:
      _masterName = path;
      JASSERT(_real_ptsname_r(fd, buf, sizeof(buf)) == 0) (JASSERT_ERRNO);
      _ptsName = buf;

      // glibc allows only 20 char long buf
      // Check if there is enough room to insert the string "dmtcp_" before the
      //   terminal number, if not then we ASSERT here.
      JASSERT((strlen(buf) + strlen("v")) <= 20)
        .Text("string /dev/pts/<n> too long, can not be virtualized."
              "Once possible workarong here is to replace the string"
              "\"dmtcp_\" with something short like \"d_\" or even "
              "\"d\" and recompile DMTCP");

      // Generate new Unique buf
      SharedData::createVirtualPtyName(_ptsName.c_str(), buf, sizeof(buf));
      _virtPtsName = buf;
      JTRACE("creating ptmx connection") (_ptsName) (_virtPtsName);
      break;

    case PTY_SLAVE:
      _ptsName = path;
      SharedData::getVirtPtyName(path, buf, sizeof(buf));
      _virtPtsName = buf;
      JASSERT(strlen(buf) != 0) (path);
      JTRACE("creating pts connection") (_ptsName) (_virtPtsName);
      break;

    case PTY_BSD_MASTER:
      _masterName = path;
      break;

    case PTY_BSD_SLAVE:
      _ptsName = path;
      break;

    default:
      break;
  }
}

void dmtcp::PtyConnection::drain()
{
  if (_type == PTY_MASTER) {
    const int maxCount = 10000;
    char buf[maxCount];
    int numRead, numWritten;
    // _fds[0] is master fd
    numRead = ptmxReadAll(_fds[0], buf, maxCount);
    _ptmxIsPacketMode = ptmxTestPacketMode(_fds[0]);
    JTRACE("_fds[0] is master(/dev/ptmx)") (_fds[0]) (_ptmxIsPacketMode);
    numWritten = ptmxWriteAll(_fds[0], buf, _ptmxIsPacketMode);
    JASSERT(numRead == numWritten) (numRead) (numWritten);
  }
  JASSERT((_type == PTY_CTTY || _type == PTY_PARENT_CTTY) || _flags != -1);
  if (tcgetpgrp(_fds[0]) != -1) {
    _isControllingTTY = true;
  } else {
    _isControllingTTY = false;
  }
}

void dmtcp::PtyConnection::preRefill(bool isRestart)
{
  if (!isRestart) {
    return;
  }

  if (_type == PTY_SLAVE || _type == PTY_BSD_SLAVE) {
    JASSERT(_ptsName.compare("?") != 0);
    JTRACE("Restoring PTY slave") (_fds[0]) (_ptsName) (_virtPtsName);
    if (_type == PTY_SLAVE) {
      char buf[32];
      SharedData::getRealPtyName(_virtPtsName.c_str(), buf, sizeof(buf));
      JASSERT(strlen(buf) > 0) (_virtPtsName) (_ptsName);
      _ptsName = buf;
    }

    /* If the process has called setsid(), and has no controlling terminal, a
     * call to open() will set this terminal as its controlling. To avoid this,
     * we pass O_NOCTTY flag if this terminal was not the controlling terminal
     * during checkpoint phase.
     */
    int extraFlags = 0;//_isControllingTTY ? 0 : O_NOCTTY;
    int tempfd = _real_open(_ptsName.c_str(), _flags | extraFlags);
    JASSERT(tempfd >= 0) (_virtPtsName) (_ptsName) (JASSERT_ERRNO)
      .Text("Error Opening PTS");

    JTRACE("Restoring PTS real") (_ptsName) (_virtPtsName) (_fds[0]);
    Util::dupFds(tempfd, _fds);
  }
}

void dmtcp::PtyConnection::refill(bool isRestart)
{
  if (!isRestart) {
    return;
  }
  if (_type ==  PTY_DEV_TTY) {
    /* If a process has called setsid(), it is possible that there is no
     * controlling terminal while in the postRestart phase as processes are
     * still creating the pseudo-tty master-slave pairs. By the time we get
     * into the refill mode, we should have all the pseudo-ttys present.
     */
    int tempfd = _real_open("/dev/tty", O_RDWR, 0);
    JASSERT(tempfd >= 0) (tempfd) (JASSERT_ERRNO)
      .Text("Error opening controlling terminal /dev/tty");

    JTRACE("Restoring /dev/tty for the process") (_fds[0]);
    _ptsName = _virtPtsName = "/dev/tty";
    Util::dupFds(tempfd, _fds);
  }
}

void dmtcp::PtyConnection::postRestart()
{
  JASSERT(_fds.size() > 0);
  if (_type == PTY_SLAVE || _type == PTY_BSD_SLAVE || _type == PTY_DEV_TTY) {
    return;
  }

  int tempfd = -1;
  int extraFlags = _isControllingTTY ? 0 : O_NOCTTY;

  switch (_type) {
    case PTY_INVALID:
      //tempfd = _real_open("/dev/null", O_RDWR);
      JTRACE("Restoring invalid PTY.") (id());
      return;

    case PTY_CTTY:
    case PTY_PARENT_CTTY:
      {
        dmtcp::string controllingTty;
        dmtcp::string stdinDeviceName;
        if (_type == PTY_CTTY) {
          controllingTty = jalib::Filesystem::GetControllingTerm();
        } else {
          controllingTty = jalib::Filesystem::GetControllingTerm(getppid());
        }
        stdinDeviceName = (jalib::Filesystem::GetDeviceName(STDIN_FILENO));
        if (controllingTty.length() > 0 &&
            _real_access(controllingTty.c_str(), R_OK | W_OK) == 0) {
          tempfd = _real_open(controllingTty.c_str(), _fcntlFlags);
          JASSERT(tempfd >= 0) (tempfd) (controllingTty) (JASSERT_ERRNO)
            .Text("Error Opening the terminal attached with the process");
        } else {
          if (_type == PTY_CTTY) {
            JTRACE("Unable to restore controlling terminal attached with the "
                   "parent process.\n"
                   "Replacing it with current STDIN")
              (stdinDeviceName);
          } else {
            JWARNING(false) (stdinDeviceName)
              .Text("Unable to restore controlling terminal attached with the "
                    "parent process.\n"
                    "Replacing it with current STDIN");
          }
          JWARNING(Util::strStartsWith(stdinDeviceName, "/dev/pts/") ||
                   stdinDeviceName == "/dev/tty") (stdinDeviceName)
            .Text("Controlling terminal not bound to a terminal device.");

          if (Util::isValidFd(STDIN_FILENO)) {
            tempfd = _real_dup(STDIN_FILENO);
          } else if (Util::isValidFd(STDOUT_FILENO)) {
            tempfd = _real_dup(STDOUT_FILENO);
          } else {
            JASSERT("Controlling terminal and STDIN/OUT not found.");
          }
        }

        JTRACE("Restoring parent CTTY for the process")
          (controllingTty) (_fds[0]);

        _ptsName = controllingTty;
        SharedData::insertPtyNameMap(_virtPtsName.c_str(), _ptsName.c_str());
        break;
      }

    case PTY_MASTER:
      {
        char pts_name[80];

        tempfd = _real_open("/dev/ptmx", _flags | extraFlags);
        JASSERT(tempfd >= 0) (tempfd) (JASSERT_ERRNO)
          .Text("Error Opening /dev/ptmx");

        JASSERT(grantpt(tempfd) >= 0) (tempfd) (JASSERT_ERRNO);
        JASSERT(unlockpt(tempfd) >= 0) (tempfd) (JASSERT_ERRNO);
        JASSERT(_real_ptsname_r(tempfd, pts_name, 80) == 0)
          (tempfd) (JASSERT_ERRNO);

        _ptsName = pts_name;
        SharedData::insertPtyNameMap(_virtPtsName.c_str(), _ptsName.c_str());

        if (_type == PTY_MASTER) {
          int packetMode = _ptmxIsPacketMode;
          ioctl(_fds[0], TIOCPKT, &packetMode); /* Restore old packet mode */
        }

        JTRACE("Restoring /dev/ptmx") (_fds[0]) (_ptsName) (_virtPtsName);
        break;
      }
    case PTY_BSD_MASTER:
      {
        JTRACE("Restoring BSD Master Pty") (_masterName) (_fds[0]);
        //dmtcp::string slaveDeviceName =
          //_masterName.replace(0, strlen("/dev/pty"), "/dev/tty");

        tempfd = _real_open(_masterName.c_str(), _flags | extraFlags);

        // FIXME: If unable to open the original BSD Master Pty, we should try to
        // open another one until we succeed and then open slave device
        // accordingly.
        // This can be done by creating a function openBSDMaster, which will try
        // to open the original master device, but if unable to do so, it would
        // keep on trying all the possible BSD Master devices until one is
        // opened. It should then create a mapping between original Master/Slave
        // device name and current Master/Slave device name.
        JASSERT(tempfd >= 0) (tempfd) (JASSERT_ERRNO)
          .Text("Error Opening BSD Master Pty.(Already in use?)");
        break;
      }
    default:
      {
        // should never reach here
        JASSERT(false) .Text("Should never reach here.");
      }
  }
  Util::dupFds(tempfd, _fds);
}

void dmtcp::PtyConnection::serializeSubClass(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::PtyConnection");
  o & _ptsName & _virtPtsName & _masterName & _type;
  o & _flags & _mode & _preExistingCTTY;
  JTRACE("Serializing PtyConn.") (_ptsName) (_virtPtsName);
}

/*****************************************************************************
 * File Connection
 *****************************************************************************/

// Upper limit on filesize for files that are automatically chosen for ckpt.
// Default 100MB
#define MAX_FILESIZE_TO_AUTOCKPT (100 * 1024 * 1024)

void dmtcp::FileConnection::doLocking()
{
  if (dmtcp::Util::strStartsWith(_path, "/proc/")) {
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
  _checkpointed = false;
}

void dmtcp::FileConnection::handleUnlinkedFile()
{
  if ((!jalib::Filesystem::FileExists(_path) && !_isBlacklistedFile(_path)) ||
      _type == FILE_DELETED ||
      Util::strStartsWith(jalib::Filesystem::BaseName(_path), ".nfs")) {
    /* File not present in Filesystem.
     * /proc/self/fd lists filename of unlink()ed files as:
     *   "<original_file_name>(deleted)"
     */
    string currPath = jalib::Filesystem::GetDeviceName(_fds[0]);

    if (Util::strEndsWith(currPath, DELETED_FILE_SUFFIX)) {
      JTRACE("Deleted file") (_path);
      _type = FILE_DELETED;
    } else if (Util::strStartsWith(jalib::Filesystem::BaseName(currPath),
                                   ".nfs")) {
      JTRACE("Deleted NFS file.") (_path) (currPath);
      _type = FILE_DELETED;
      _path = currPath;
    } else {
      string currPath = jalib::Filesystem::GetDeviceName(_fds[0]);
      if (jalib::Filesystem::FileExists(currPath)) {
        _path = currPath;
        return;
      }
      JASSERT(_type == FILE_DELETED) (_path) (currPath)
        .Text("File not found on disk and yet the filename doesn't "
              "contain the suffix '(deleted)'");
    }
  }
}

void dmtcp::FileConnection::calculateRelativePath()
{
  dmtcp::string cwd = jalib::Filesystem::GetCWD();
  if (_path.compare(0, cwd.length(), cwd) == 0) {
    /* CWD = "/A/B", FileName = "/A/B/C/D" ==> relPath = "C/D" */
    _rel_path = _path.substr(cwd.length() + 1);
  } else {
    _rel_path = "*";
  }
}

void dmtcp::FileConnection::drain()
{
  struct stat statbuf;
  JASSERT(_fds.size() > 0);

  handleUnlinkedFile();

  calculateRelativePath();

  _checkpointed = false;

  // Read the current file descriptor offset
  _offset = lseek(_fds[0], 0, SEEK_CUR);
  fstat(_fds[0], &statbuf);
  _st_dev = statbuf.st_dev;
  _st_ino = statbuf.st_ino;
  _st_size = statbuf.st_size;

  if (_type == FILE_PROCFS) {
    return;
  }

  // If this file is related to supported Resource Management system
  // handle it specially
  if (_type == FILE_BATCH_QUEUE &&
      dmtcp_bq_should_ckpt_file &&
      dmtcp_bq_should_ckpt_file(_path.c_str(), &_rmtype)) {
    JTRACE("Pre-checkpoint Torque files") (_fds.size());
    for (unsigned int i=0; i< _fds.size(); i++)
      JTRACE("_fds[i]=") (i) (_fds[i]);
    _checkpointed = true;
    return;
  }

  if (dmtcp_must_ckpt_file && dmtcp_must_ckpt_file(_path.c_str())) {
    _checkpointed = true;
    return;
  }

  if (_type == FILE_DELETED && (_flags & O_WRONLY)) {
    return;
  }
  if (_isBlacklistedFile(_path)) {
    return;
  }
  if (dmtcp_should_ckpt_open_files() && statbuf.st_uid == getuid()) {
    _checkpointed = true;
  } else if (_type == FILE_DELETED || _type == FILE_SHM) {
    _checkpointed = true;
  } else if (_isVimApp() &&
             (Util::strEndsWith(_path, ".swp") == 0 ||
              Util::strEndsWith(_path, ".swo") == 0)) {
    _checkpointed = true;
  } else if (Util::strStartsWith(jalib::Filesystem::GetProgramName(),
                                 "emacs")) {
    _checkpointed = true;
#if 0
  } else if ((_fcntlFlags &(O_WRONLY|O_RDWR)) != 0 &&
             _offset < _st_size &&
             _st_size < MAX_FILESIZE_TO_AUTOCKPT &&
             statbuf.st_uid == getuid()) {
    // FIXME: Disable the following heuristic until we can comeup with a better
    // one
    _checkpointed = true;
#endif
  } else {
    _checkpointed = false;
  }
}

void dmtcp::FileConnection::preCkpt()
{
  if (_checkpointed) {
    ConnectionIdentifier id;
    JASSERT(_type != FILE_PROCFS && _type != FILE_INVALID);
    JASSERT(SharedData::getCkptLeaderForFile(_st_dev, _st_ino, &id));
    if (id == _id) {
      dmtcp::string savedFilePath = getSavedFilePath(_path);
      CreateDirectoryStructure(savedFilePath);

      int destFd = _real_open(savedFilePath.c_str(), O_CREAT | O_WRONLY | O_TRUNC,
                              S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
      JASSERT(destFd != -1) (JASSERT_ERRNO) (_path) (savedFilePath);

      JTRACE("Saving checkpointed copy of the file") (_path) (savedFilePath);
      if (_flags & O_WRONLY) {
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
      _checkpointed = false;
    }
  }
}

void dmtcp::FileConnection::refill(bool isRestart)
{
  struct stat statbuf;
  if (!isRestart) return;
  if (strstr(_path.c_str(), "infiniband/uverbs") ||
      strstr(_path.c_str(), "uverbs-event")) return;

  if (_checkpointed && _fileAlreadyExists) {
    dmtcp::string savedFilePath = getSavedFilePath(_path);
    int savedFd = _real_open(savedFilePath.c_str(), O_RDONLY, 0);
    JASSERT(savedFd != -1) (JASSERT_ERRNO) (savedFilePath);

    if (!areFilesEqual(_fds[0], savedFd, _st_size)) {
      if (_type == FILE_SHM) {
        JWARNING(false) (_path) (savedFilePath)
          .Text("\n"
                "***Mapping current version of file into memory;\n"
                "   _not_ file as it existed at time of checkpoint.\n"
                "   Change this function and re-compile, if you want "
                "different behavior.");
      } else {
        const char *errMsg =
          "\n**** File already exist! Checkpointed copy can't be restored.\n"
          "       The Contents of checkpointed copy differ from the "
          "contents of the existing copy.\n"
          "****Delete the existing file and try again!";
        JASSERT(false) (_path) (savedFilePath) (errMsg);
      }
    }
    _real_close(savedFd);
  }

  if (!_checkpointed) {
    int tempfd;
    if (_type == FILE_DELETED && (_flags & O_WRONLY)) {
      tempfd = _real_open(_path.c_str(), _fcntlFlags | O_CREAT, 0600);
      JASSERT(tempfd != -1) (_path) (JASSERT_ERRNO) .Text("open() failed");
      JASSERT(truncate(_path.c_str(), _st_size) ==  0)
        (_path.c_str()) (_st_size) (JASSERT_ERRNO);
    } else {

      JASSERT(jalib::Filesystem::FileExists(_path)) (_path)
        .Text("File not found.");

      if (stat(_path.c_str() ,&statbuf) == 0 && S_ISREG(statbuf.st_mode)) {
        if (statbuf.st_size > _st_size &&
            ((_fcntlFlags & O_WRONLY) || (_fcntlFlags & O_RDWR))) {
          errno = 0;
          JASSERT(truncate(_path.c_str(), _st_size) ==  0)
            (_path.c_str()) (_st_size) (JASSERT_ERRNO);
        } else if (statbuf.st_size < _st_size) {
          JWARNING(false) .Text("Size of file smaller than what we expected");
        }
      }
      tempfd = openFile();
    }
    Util::dupFds(tempfd, _fds);
  }

  errno = 0;
  if (jalib::Filesystem::FileExists(_path) &&
      stat(_path.c_str() ,&statbuf) == 0 && S_ISREG(statbuf.st_mode)) {
    if (_offset <= statbuf.st_size && _offset <= _st_size) {
      JASSERT(lseek(_fds[0], _offset, SEEK_SET) == _offset)
        (_path) (_offset) (JASSERT_ERRNO);
      //JTRACE("lseek(_fds[0], _offset, SEEK_SET)") (_fds[0]) (_offset);
    } else if (_offset > statbuf.st_size || _offset > _st_size) {
      JWARNING(false) (_path) (_offset) (_st_size) (statbuf.st_size)
        .Text("No lseek done:  offset is larger than min of old and new size.");
    }
  }
  refreshPath();
}

void dmtcp::FileConnection::resume(bool isRestart)
{
  if (_checkpointed && isRestart && _type == FILE_DELETED) {
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

void dmtcp::FileConnection::refreshPath()
{
  dmtcp::string cwd = jalib::Filesystem::GetCWD();

  if (_type == FILE_BATCH_QUEUE) {
    // get new file name
    dmtcp::string newpath = jalib::Filesystem::GetDeviceName(_fds[0]);
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
    dmtcp::string fullPath = cwd + "/" + _rel_path;
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

void dmtcp::FileConnection::postRestart()
{
  int tempfd;

  JASSERT(_fds.size() > 0);
  if (!_checkpointed) return;
  _fileAlreadyExists = false;

  JTRACE("Restoring File Connection") (id()) (_path);
  dmtcp::string savedFilePath = getSavedFilePath(_path);
  JASSERT(jalib::Filesystem::FileExists(savedFilePath))
    (savedFilePath) (_path) .Text("Unable to find checkpointed copy of file");

  if (_type == FILE_BATCH_QUEUE) {
    JASSERT(dmtcp_bq_restore_file);
    tempfd = dmtcp_bq_restore_file(_path.c_str(), savedFilePath.c_str(),
                                   _fcntlFlags, _rmtype);
    JTRACE("Restore Resource Manager File") (_path);
  } else {
    refreshPath();
    CreateDirectoryStructure(_path);
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
    JASSERT(fd != -1 || errno == EEXIST);

    if (fd == -1) {
      _fileAlreadyExists = true;
    } else {
      int srcFd = _real_open(savedFilePath.c_str(), O_RDONLY, 0);
      JASSERT(srcFd != -1) (_path) (savedFilePath) (JASSERT_ERRNO)
        .Text("Failed to open checkpointed copy of the file.");
      JTRACE("Copying saved checkpointed file to original location")
        (savedFilePath) (_path);
      writeFileFromFd(srcFd, fd);
      _real_close(srcFd);
      _real_close(fd);
    }
    tempfd = openFile();
  }
  Util::dupFds(tempfd, _fds);
}

bool dmtcp::FileConnection::checkDup(int fd)
{
  bool retVal = false;

  int myfd = _fds[0];
  if ( lseek(myfd, 0, SEEK_CUR) == lseek(fd, 0, SEEK_CUR) ) {
    off_t newOffset = lseek (myfd, 1, SEEK_CUR);
    JASSERT (newOffset != -1) (JASSERT_ERRNO) .Text("lseek failed");

    if ( newOffset == lseek (fd, 0, SEEK_CUR) ) {
      retVal = true;
    }
    // Now restore the old offset
    JASSERT (-1 != lseek (myfd, -1, SEEK_CUR)) .Text("lseek failed");
  }
  return retVal;
}

static void CreateDirectoryStructure(const dmtcp::string& path)
{
  size_t index = path.rfind('/');

  if (index == dmtcp::string::npos)
    return;

  dmtcp::string dir = path.substr(0, index);

  index = path.find('/');
  while (index != dmtcp::string::npos) {
    if (index > 1) {
      dmtcp::string dirName = path.substr(0, index);

      int res = mkdir(dirName.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
      JASSERT(res != -1 || errno==EEXIST) (dirName) (path)
        .Text("Unable to create directory in File Path");
    }
    index = path.find('/', index+1);
  }
}

int dmtcp::FileConnection::openFile()
{
  JASSERT(jalib::Filesystem::FileExists(_path)) (_path)
    .Text("File not present");

  int fd = _real_open(_path.c_str(), _fcntlFlags);
  JASSERT(fd != -1) (_path) (JASSERT_ERRNO) .Text("open() failed");

  JTRACE("open(_path.c_str(), _fcntlFlags)") (fd) (_path.c_str()) (_fcntlFlags);
  return fd;
}

static bool areFilesEqual(int fd, int savedFd, size_t size)
{
  long page_size = sysconf(_SC_PAGESIZE);
  const size_t bufSize = 1024 * page_size;
  char *buf1 =(char*)JALLOC_HELPER_MALLOC(bufSize);
  char *buf2 =(char*)JALLOC_HELPER_MALLOC(bufSize);

  off_t offset1 = _real_lseek(fd, 0, SEEK_CUR);
  off_t offset2 = _real_lseek(savedFd, 0, SEEK_CUR);
  JASSERT(_real_lseek(fd, 0, SEEK_SET) == 0) (fd) (JASSERT_ERRNO);
  JASSERT(_real_lseek(savedFd, 0, SEEK_SET) == 0) (savedFd) (JASSERT_ERRNO);

  int readBytes;
  while (size > 0) {
    readBytes = Util::readAll(savedFd, buf1, MIN(bufSize, size));
    JASSERT(readBytes != -1) (JASSERT_ERRNO) .Text("Read Failed");
    if (readBytes == 0) break;
    JASSERT(Util::readAll(fd, buf2, readBytes) == readBytes);
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

static void writeFileFromFd(int fd, int destFd)
{
  long page_size = sysconf(_SC_PAGESIZE);
  const size_t bufSize = 1024 * page_size;
  char *buf =(char*)JALLOC_HELPER_MALLOC(bufSize);

  off_t offset = _real_lseek(fd, 0, SEEK_CUR);
  JASSERT(_real_lseek(fd, 0, SEEK_SET) == 0) (fd) (JASSERT_ERRNO);
  JASSERT(_real_lseek(destFd, 0, SEEK_SET) == 0) (destFd) (JASSERT_ERRNO);

  int readBytes, writtenBytes;
  while (1) {
    readBytes = Util::readAll(fd, buf, bufSize);
    JASSERT(readBytes != -1) (JASSERT_ERRNO) .Text("Read Failed");
    if (readBytes == 0) break;
    writtenBytes = Util::writeAll(destFd, buf, readBytes);
    JASSERT(writtenBytes != -1) (JASSERT_ERRNO) .Text("Write failed.");
  }
  JALLOC_HELPER_FREE(buf);
  JASSERT(_real_lseek(fd, offset, SEEK_SET) != -1);
}

dmtcp::string dmtcp::FileConnection::getSavedFilePath(const dmtcp::string& path)
{
  dmtcp::ostringstream os;
  os << dmtcp_get_ckpt_files_subdir()
    << "/" << jalib::Filesystem::BaseName(_path) << "_" << _id.conId();

  return os.str();
}

void dmtcp::FileConnection::serializeSubClass(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::FileConnection");
  o & _path & _rel_path;
  o & _offset & _st_dev & _st_ino & _st_size & _checkpointed & _rmtype;
  JTRACE("Serializing FileConn.") (_path) (_rel_path)
    (dmtcp_get_ckpt_files_subdir()) (_checkpointed) (_fcntlFlags);
}

/*****************************************************************************
 * FIFO Connection
 *****************************************************************************/

void dmtcp::FifoConnection::drain()
{
  struct stat st;
  JASSERT(_fds.size() > 0);

  stat(_path.c_str(),&st);
  JTRACE("Checkpoint fifo.") (_fds[0]);
  _mode = st.st_mode;

  int new_flags =(_fcntlFlags &(~(O_RDONLY|O_WRONLY))) | O_RDWR | O_NONBLOCK;
  ckptfd = _real_open(_path.c_str(),new_flags);
  JASSERT(ckptfd >= 0) (ckptfd) (JASSERT_ERRNO);

  _in_data.clear();
  size_t bufsize = 256;
  char buf[bufsize];
  int size;

  while (1) { // flush fifo
    size = read(ckptfd,buf,bufsize);
    if (size < 0) {
      break; // nothing to flush
    }
    for (int i=0;i<size;i++) {
      _in_data.push_back(buf[i]);
    }
  }
  close(ckptfd);
  JTRACE("Checkpointing fifo:  end.") (_fds[0]) (_in_data.size());
}

void dmtcp::FifoConnection::refill(bool isRestart)
{
  int new_flags =(_fcntlFlags &(~(O_RDONLY|O_WRONLY))) | O_RDWR | O_NONBLOCK;
  ckptfd = _real_open(_path.c_str(),new_flags);
  JASSERT(ckptfd >= 0) (ckptfd) (JASSERT_ERRNO);

  size_t bufsize = 256;
  char buf[bufsize];
  size_t j;
  ssize_t ret;
  for (size_t i=0;i<(_in_data.size()/bufsize);i++) { // refill fifo
    for (j=0; j<bufsize; j++) {
      buf[j] = _in_data[j+i*bufsize];
    }
    ret = Util::writeAll(ckptfd,buf,j);
    JASSERT(ret ==(ssize_t)j) (JASSERT_ERRNO) (ret) (j) (_fds[0]) (i);
  }
  int start =(_in_data.size()/bufsize)*bufsize;
  for (j=0; j<_in_data.size()%bufsize; j++) {
    buf[j] = _in_data[start+j];
  }
  errno=0;
  buf[j] ='\0';
  JTRACE("Buf internals.") ((const char*)buf);
  ret = Util::writeAll(ckptfd,buf,j);
  JASSERT(ret ==(ssize_t)j) (JASSERT_ERRNO) (ret) (j) (_fds[0]);

  close(ckptfd);
  // unlock fifo
  flock(_fds[0],LOCK_UN);
  JTRACE("End checkpointing fifo.") (_fds[0]);
}

void dmtcp::FifoConnection::refreshPath()
{
  dmtcp::string cwd = jalib::Filesystem::GetCWD();
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

void dmtcp::FifoConnection::postRestart()
{
  JASSERT(_fds.size() > 0);
  JTRACE("Restoring Fifo Connection") (id()) (_path);
  refreshPath();
  int tempfd = openFile();
  Util::dupFds(tempfd, _fds);
  refreshPath();
}

int dmtcp::FifoConnection::openFile()
{
  int fd;

  if (!jalib::Filesystem::FileExists(_path)) {
    JTRACE("Fifo file not present, creating new one") (_path);
    jalib::string dir = jalib::Filesystem::DirName(_path);
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

void dmtcp::FifoConnection::serializeSubClass(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::FifoConnection");
  o & _path & _rel_path & _savedRelativePath & _mode & _in_data;
  JTRACE("Serializing FifoConn.") (_path) (_rel_path) (_savedRelativePath);
}

/*****************************************************************************
 * Stdio Connection
 *****************************************************************************/

void dmtcp::StdioConnection::postRestart()
{
  for (size_t i=0; i<_fds.size(); ++i) {
    int fd = _fds[i];
    if (fd <= 2) {
      JTRACE("Skipping restore of STDIO, just inherit from parent") (fd);
      continue;
    }
    int oldFd = -1;
    switch (_type) {
      case STDIO_IN:
        JTRACE("Restoring STDIN") (fd);
        oldFd=0;
        break;
      case STDIO_OUT:
        JTRACE("Restoring STDOUT") (fd);
        oldFd=1;
        break;
      case STDIO_ERR:
        JTRACE("Restoring STDERR") (fd);
        oldFd=2;
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

void dmtcp::PosixMQConnection::on_mq_close()
{
}

void dmtcp::PosixMQConnection::on_mq_notify(const struct sigevent *sevp)
{
  if (sevp == NULL && _notifyReg) {
    _notifyReg = false;
  } else {
    _notifyReg = true;
    _sevp = *sevp;
  }
}

void dmtcp::PosixMQConnection::drain()
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

  int fd = _real_mq_open(_name.c_str(), O_RDWR, 0, NULL);
  JASSERT(fd != -1);

  _qnum = attr.mq_curmsgs;
  char *buf =(char*) JALLOC_HELPER_MALLOC(attr.mq_msgsize);
  for (long i = 0; i < _qnum; i++) {
    unsigned prio;
    ssize_t numBytes = _real_mq_receive(_fds[0], buf, attr.mq_msgsize, &prio);
    JASSERT(numBytes != -1) (JASSERT_ERRNO);
    _msgInQueue.push_back(jalib::JBuffer((const char*)buf, numBytes));
    _msgInQueuePrio.push_back(prio);
  }
  JALLOC_HELPER_FREE(buf);
  _real_mq_close(fd);
}

void dmtcp::PosixMQConnection::refill(bool isRestart)
{
  for (long i = 0; i < _qnum; i++) {
    JASSERT(_real_mq_send(_fds[0], _msgInQueue[i].buffer(),
                          _msgInQueue[i].size(), _msgInQueuePrio[i]) != -1);
  }
  _msgInQueue.clear();
  _msgInQueuePrio.clear();
}

void dmtcp::PosixMQConnection::postRestart()
{
  JASSERT(_fds.size() > 0);

  errno = 0;
  if (_oflag & O_EXCL) {
    mq_unlink(_name.c_str());
  }

  int tempfd = _real_mq_open(_name.c_str(), _oflag, _mode, &_attr);
  JASSERT(tempfd != -1) (JASSERT_ERRNO);
  Util::dupFds(tempfd, _fds);
}

void dmtcp::PosixMQConnection::serializeSubClass(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::PosixMQConnection");
  o & _name & _oflag & _mode & _attr;
}
