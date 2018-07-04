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

#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include "jassert.h"
#include "jfilesystem.h"
#include "dmtcp.h"
#include "shareddata.h"
#include "util.h"

#include "filewrappers.h"
#include "ptyconnection.h"
#include "ptywrappers.h"

using namespace dmtcp;

static bool ptmxTestPacketMode(int masterFd);
static ssize_t ptmxReadAll(int fd, const void *origBuf, size_t maxCount);
static ssize_t ptmxWriteAll(int fd, const void *buf, bool isPacketMode);

static bool
ptmxTestPacketMode(int masterFd)
{
  char tmp_buf[100];
  int slave_fd, ioctlArg, rc;
  struct pollfd pollFd = { 0 };

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
  pollFd.fd = masterFd;
  pollFd.events = POLLIN;
  _real_poll(&pollFd, 1, 0); /* Zero: will use to poll, not wait.*/
  if (pollFd.revents & POLLIN) {
    // Clean up someone else's command byte from packet mode.
    // FIXME:  We should restore this on resume/restart.
    rc = read(masterFd, tmp_buf, 100);
    JASSERT(rc == 1) (rc) (masterFd);
  }

  /* C. Now we're ready to do the real test.  If in packet mode, we should
     see command byte of TIOCPKT_DATA(0) with data. */
  tmp_buf[0] = 'x'; /* Don't set '\n'.  Could be converted to "\r\n". */
  /* Give the masterFd something to read. */
  JWARNING((rc = write(slave_fd, tmp_buf, 1)) == 1) (rc).Text("write failed");

  // tcdrain(slave_fd);
  _real_close(slave_fd);

  /* Read the 'x':  If we also see a command byte, it's packet mode */
  rc = read(masterFd, tmp_buf, 100);

  /* D. Check if command byte packet exists, and chars rec'd is longer by 1. */
  return rc == 2 && tmp_buf[0] == TIOCPKT_DATA && tmp_buf[1] == 'x';
}

// Also record the count read on each iteration, in case it's packet mode
static bool
readyToRead(int fd)
{
  struct pollfd pollFd = { 0 };

  pollFd.fd = fd;
  pollFd.events = POLLIN;
  _real_poll(&pollFd, 1, 0); /* Zero: will use to poll, not wait.*/
  return pollFd.revents & POLLIN;
}

// returns 0 if not ready to read; else returns -1, or size read incl. header
static ssize_t
readOnePacket(int fd, const void *buf, size_t maxCount)
{
  typedef int PtyHdr;
  ssize_t rc = 0;

  // Read single packet:  rc > 0 will be true for at most one iteration.
  while (readyToRead(fd) && rc <= 0) {
    rc = read(fd, (char *)buf + sizeof(PtyHdr), maxCount - sizeof(PtyHdr));
    *(PtyHdr *)buf = rc; // Record the number read in header
    if (rc >= (ssize_t)(maxCount - sizeof(PtyHdr))) {
      rc = -1; errno = E2BIG; // Invoke new errno for buf size not large enough
    }
    if (rc == -1 && errno != EAGAIN && errno != EINTR) {
      break;  /* Give up; bad error */
    }
  }
  return rc <= 0 ? rc : rc + sizeof(PtyHdr);
}

// rc < 0 => error; rc == sizeof(hdr) => no data to read;
// rc > 0 => saved w/ count hdr
static ssize_t
ptmxReadAll(int fd, const void *origBuf, size_t maxCount)
{
  typedef int hdr;
  char *buf = (char *)origBuf;
  int rc;
  while ((rc = readOnePacket(fd, buf, maxCount)) > 0) {
    buf += rc;
  }
  *(hdr *)buf = 0; /* Header count of zero means we're done */
  buf += sizeof(hdr);
  JASSERT(rc < 0 || buf - (char *)origBuf > 0) (rc) (origBuf) ((void *)buf);
  return rc < 0 ? rc : buf - (char *)origBuf;
}

// The hdr contains the size of the full buffer([hdr, data]).
// Return size of origBuf written:  includes packets of form:  [hdr, data]
// with hdr holding size of data.  Last hdr has value zero.
// Also record the count written on each iteration, in case it's packet mode.
static ssize_t
writeOnePacket(int fd, const void *origBuf, bool isPacketMode)
{
  typedef int hdr;
  int count = *(hdr *)origBuf;
  int cum_count = 0;
  int rc = 0; // Trigger JASSERT if not modified below.
  if (count == 0) {
    return sizeof(hdr);  // count of zero means we're done, hdr consumed
  }

  // FIXME:  It would be nice to restore packet mode(flow control, etc.)
  // For now, we ignore it.
  if (count == 1 && isPacketMode) {
    return sizeof(hdr) + 1;
  }
  while (cum_count < count) {
    rc =
      write(fd, (char *)origBuf + sizeof(hdr) + cum_count, count - cum_count);
    if (rc == -1 && errno != EAGAIN && errno != EINTR) {
      break;  /* Give up; bad error */
    }
    if (rc >= 0) {
      cum_count += rc;
    }
  }
  JASSERT(rc != 0 && cum_count == count)
    (JASSERT_ERRNO) (rc) (count) (cum_count);
  return rc < 0 ? rc : cum_count + sizeof(hdr);
}

static ssize_t
ptmxWriteAll(int fd, const void *buf, bool isPacketMode)
{
  typedef int hdr;
  ssize_t cum_count = 0;
  ssize_t rc;
  while ((rc = writeOnePacket(fd, (char *)buf + cum_count, isPacketMode))
         > (ssize_t)sizeof(hdr)) {
    cum_count += rc;
  }
  JASSERT(rc < 0 || rc == sizeof(hdr)) (rc) (cum_count);
  cum_count += sizeof(hdr);  /* Account for last packet: 'done' hdr w/ 0 data */
  return rc <= 0 ? rc : cum_count;
}

PtyConnection::PtyConnection(int fd,
                             const char *path,
                             int flags,
                             mode_t mode,
                             int type)
  : Connection(type)
  , _flags(flags)
  , _mode(mode)
  , _preExistingCTTY(false)
{
  char buf[PTS_PATH_MAX];

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
    // terminal number, if not then we ASSERT here.
    JASSERT((strlen(buf) + strlen("v")) <= 20)
    .Text("string /dev/pts/<n> too long, can not be virtualized."
          "Once possible workaround here is to replace the string"
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

  case PTY_EXTERNAL:
    _ptsName = path;
    break;

  default:
    break;
  }
}

void
PtyConnection::doLocking()
{
  _hasLock = true;
  if ((_type == PTY_MASTER || _type == PTY_BSD_MASTER) &&
      getpgrp() != tcgetpgrp(_fds[0])) {
    _hasLock = false;
  }
}

void
PtyConnection::drain()
{
  JASSERT(_type != PTY_EXTERNAL);
  saveOptions();
  if (_type == PTY_MASTER && getpgrp() == tcgetpgrp(_fds[0])) {
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
  JASSERT((_type == PTY_CTTY || _type == PTY_PARENT_CTTY) || _fcntlFlags != -1);
  if (tcgetpgrp(_fds[0]) != -1) {
    _isControllingTTY = true;
  } else {
    _isControllingTTY = false;
  }
}

void
PtyConnection::refill(bool isRestart)
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
    int extraFlags = 0; // _isControllingTTY ? 0 : O_NOCTTY;
    int tempfd = _real_open(_ptsName.c_str(), _fcntlFlags | extraFlags);
    JASSERT(tempfd >= 0) (_virtPtsName) (_ptsName) (JASSERT_ERRNO)
    .Text("Error Opening PTS");

    JTRACE("Restoring PTS real") (_ptsName) (_virtPtsName) (_fds[0]);
    restoreDupFds(tempfd);
  }

  if (_type == PTY_DEV_TTY) {
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
    restoreDupFds(tempfd);
  }
}

void
PtyConnection::postRestart()
{
  JASSERT(_fds.size() > 0);
  if (_type == PTY_SLAVE || _type == PTY_BSD_SLAVE || _type == PTY_DEV_TTY) {
    return;
  }

  int tempfd = -1;
  int extraFlags = _isControllingTTY ? 0 : O_NOCTTY;

  switch (_type) {
    case PTY_INVALID:

      // tempfd = _real_open("/dev/null", O_RDWR);
      JTRACE("Restoring invalid PTY.") (id());
      return;

    case PTY_CTTY:
    case PTY_PARENT_CTTY:
    {
      string controllingTty;
      string stdinDeviceName;
      if (_type == PTY_CTTY) {
        controllingTty = jalib::Filesystem::GetControllingTerm();
      } else {
        controllingTty = jalib::Filesystem::GetControllingTerm(getppid());
      }
      stdinDeviceName = (jalib::Filesystem::GetDeviceName(STDIN_FILENO));
      if (controllingTty.length() > 0 &&
          _real_access(controllingTty.c_str(), R_OK | W_OK) == 0) {
        tempfd = _real_open(controllingTty.c_str(), _fcntlFlags);
        JASSERT(tempfd >=
                0) (tempfd) (_fcntlFlags) (controllingTty) (JASSERT_ERRNO)
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
        JWARNING(Util::strStartsWith(stdinDeviceName.c_str(), "/dev/pts/") ||
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

      tempfd = _real_open("/dev/ptmx", _fcntlFlags | extraFlags);
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
        ioctl(_fds[0], TIOCPKT, &packetMode);     /* Restore old packet mode */
      }

      JTRACE("Restoring /dev/ptmx") (_fds[0]) (_ptsName) (_virtPtsName);
      break;
    }
    case PTY_BSD_MASTER:
    {
      JTRACE("Restoring BSD Master Pty") (_masterName) (_fds[0]);

      // string slaveDeviceName =
      // _masterName.replace(0, strlen("/dev/pty"), "/dev/tty");

      tempfd = _real_open(_masterName.c_str(), _fcntlFlags | extraFlags);

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
      JASSERT(false).Text("Should never reach here.");
    }
  }

  restoreDupFds(tempfd);
}

void
PtyConnection::serializeSubClass(jalib::JBinarySerializer &o)
{
  JSERIALIZE_ASSERT_POINT("PtyConnection");
  o&_ptsName&_virtPtsName&_masterName &_type;
  o&_flags&_mode &_preExistingCTTY;
  JTRACE("Serializing PtyConn.") (_ptsName) (_virtPtsName);
}
