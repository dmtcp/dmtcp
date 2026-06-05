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
#include <unistd.h>
#include <fstream>
#include <ios>
#include <iostream>

#include "jassert.h"
#include "jconvert.h"
#include "jfilesystem.h"
#include "dmtcp.h"
#include "shareddata.h"
#include "util.h"

#include "eventconnection.h"
#include "eventwrappers.h"
#include "util_assert.h"
#include "util_descriptor.h"
using namespace dmtcp;

/*****************************************************************************
 * Epoll Connection
 *****************************************************************************/

#ifdef HAVE_SYS_EPOLL_H
void
EpollConnection::drain()
{
  ASSERT(_fds.size() > 0, "epoll connection has no fds during drain");
}

void
EpollConnection::refill(bool isRestart)
{
  ASSERT(_fds.size() > 0, "epoll connection has no fds during refill");
  if (isRestart) {
    typedef map<int, struct epoll_event>::iterator fdEventIterator;
    fdEventIterator fevt = _fdToEvent.begin();
    for (; fevt != _fdToEvent.end(); fevt++) {
      JTRACE("restore sfd options") (fevt->first);
      WARNING_SYSCALL_SUCCESS_MSG(
        _real_epoll_ctl(_fds[0], EPOLL_CTL_ADD, fevt->first,
                        &(fevt->second)),
        "Error in restoring epoll options: epfd={}", _fds[0]);
    }
  }
}

void
EpollConnection::postRestart()
{
  ASSERT(_fds.size() > 0, "epoll connection has no fds during postRestart");
  JTRACE("Recreating epoll connection") (_fds[0]) (id());
  int tempfd;
  if (_size != 0) {
    tempfd = _real_epoll_create(_size);
  } else {
#if HAS_EPOLL_CREATE1
    tempfd = _real_epoll_create1(_flags);
#else
    tempfd = -1;
#endif
  }
  ASSERT_ERRNO(tempfd >= 0,
               "failed to recreate epoll fd: size={} flags={}", _size,
               _flags);
  restoreDupFds(tempfd);
}

void
EpollConnection::serializeSubClass(jalib::JBinarySerializer &o)
{
  JSERIALIZE_ASSERT_POINT("EpollConnection");
  o & _size & _flags & _fdToEvent;
}

EpollConnection&
EpollConnection::asEpoll()
{
  return *this;
}

void
EpollConnection::onCTL(int op, int fd, struct epoll_event *event)
{
  ASSERT(((op == EPOLL_CTL_MOD || op == EPOLL_CTL_ADD) && event != NULL) ||
         op == EPOLL_CTL_DEL,
         "epoll_ctl requires non-null event for ADD/MOD: op={} fd={}",
         op, fd);

  struct epoll_event myEvent;
  if (op == EPOLL_CTL_DEL) {
    _fdToEvent.erase(fd);
    return;
  }
  memcpy(&myEvent, event, sizeof myEvent);
  _fdToEvent[fd] = myEvent;
}
#endif // ifdef HAVE_SYS_EPOLL_H

/*****************************************************************************
 * Eventfd Connection
 *****************************************************************************/
#ifdef HAVE_SYS_EVENTFD_H
void
EventFdConnection::drain()
{
  ASSERT(_fds.size() > 0, "eventfd connection has no fds during drain");
  JTRACE("Checkpoint eventfd.") (_fds[0]);

  int new_flags = (_fcntlFlags & (~(O_RDONLY | O_WRONLY))) | O_RDWR |
    O_NONBLOCK;
  ASSERT(_fds[0] >= 0, "invalid eventfd during drain: fd={}", _fds[0]);

  // set the new flags
  ASSERT_ERRNO(fcntl(_fds[0], F_SETFL, new_flags) == 0,
               "fcntl(F_SETFL) failed for eventfd drain: fd={} flags={}",
               _fds[0], new_flags);
  uint64_t u;

  // Read whatever is there on top of _fds[0]
  ssize_t size = read(_fds[0], &u, sizeof(uint64_t));
  if (-1 != size) {
    JTRACE("Read value u: ") (_fds[0]) (u);

    // EFD_SEMAPHORE flag not specified,
    // the counter value would have been reset to 0 upon read
    // Save the value, so that it can be restored in post-checkpoint
    if (!(_flags & EFD_SEMAPHORE)) {
      _initval = u;
    } else {
      // EFD_SEMAPHORE specified, so can't read the current counter value
      // Keep reading till "semaphore" becomes 0.
      unsigned int counter = 1;
      while (-1 != read(_fds[0], &u, sizeof(uint64_t))) {
        counter++;
      }
      _initval = counter;
    }
  } else {
    JTRACE("Nothing to be read from eventfd.")
      (_fds[0]) (errno) (strerror(errno));
    _initval = 0;
  }
  JTRACE("Checkpointing eventfd:  end.") (_fds[0]) (_initval);
}

void
EventFdConnection::refill(bool isRestart)
{
  JTRACE("Begin refill eventfd.") (_fds[0]);
  ASSERT(_fds.size() > 0, "eventfd connection has no fds during refill");
  if (!isRestart) {
    uint64_t u = (unsigned long long)_initval;
    JTRACE("Writing") (u);
    WARNING_ERRNO(write(_fds[0], &u, sizeof(uint64_t)) == sizeof(uint64_t),
                  "Write to eventfd failed during refill: fd={}", _fds[0]);
  }
  JTRACE("End refill eventfd.") (_fds[0]);
}

void
EventFdConnection::postRestart()
{
  ASSERT(_fds.size() > 0,
         "eventfd connection has no fds during postRestart");

  JTRACE("Restoring EventFd Connection") (id());
  errno = 0;
  int tempfd = _real_eventfd(_initval, _flags);
  ASSERT_ERRNO(tempfd > 0,
               "failed to recreate eventfd: fd={} initval={} flags={}",
               tempfd, _initval, _flags);
  restoreDupFds(tempfd);
}

void
EventFdConnection::serializeSubClass(jalib::JBinarySerializer &o)
{
  JSERIALIZE_ASSERT_POINT("EventFdConnection");
  o & _initval & _flags;
  JTRACE("Serializing EvenFdConn.");
}
#endif // ifdef HAVE_SYS_EVENTFD_H

/*****************************************************************************
 * Signalfd Connection
 *****************************************************************************/
#ifdef HAVE_SYS_SIGNALFD_H
void
SignalFdConnection::drain()
{
  ASSERT(_fds.size() > 0, "signalfd connection has no fds during drain");

  JTRACE("Checkpoint signalfd.") (_fds[0]);

  int new_flags =
    (_fcntlFlags & (~(O_RDONLY | O_WRONLY))) | O_RDWR | O_NONBLOCK;

  // set the new flags
  ASSERT_ERRNO(fcntl(_fds[0], F_SETFL, new_flags) == 0,
               "fcntl(F_SETFL) failed for signalfd drain: fd={} flags={}",
               _fds[0], new_flags);

  // Read whatever is there on top of signalfd
  ssize_t size = read(_fds[0], &_fdsi, sizeof(struct signalfd_siginfo));
  if (size <= 0) {
    JTRACE("Nothing to be read from signalfd.") (_fds[0]) (errno);
    memset(&_fdsi, 0, sizeof(_fdsi));
  }
}

void
SignalFdConnection::refill(bool isRestart)
{
  JTRACE("Begin refill signalfd.") (_fds[0]);
  ASSERT(_fds.size() > 0, "signalfd connection has no fds during refill");

  // raise the signals
  JTRACE("Raising the signal...") (_fdsi.ssi_signo);
  raise(_fdsi.ssi_signo);
  JTRACE("End refill signalfd.") (_fds[0]);
}

void
SignalFdConnection::postRestart()
{
  ASSERT(_fds.size() > 0,
         "signalfd connection has no fds during postRestart");

  JTRACE("Restoring SignalFd Connection") (id());
  errno = 0;
  int tempfd = _real_signalfd(-1, &_mask, _flags);
  ASSERT_ERRNO(tempfd > 0,
               "failed to recreate signalfd: fd={} flags={}", tempfd,
               _flags);
  restoreDupFds(tempfd);
}

void
SignalFdConnection::serializeSubClass(jalib::JBinarySerializer &o)
{
  JSERIALIZE_ASSERT_POINT("SignalFdConnection");
  o & _flags & _mask & _fdsi;
  JTRACE("Serializing SignalFdConn.");
}
#endif // ifdef HAVE_SYS_SIGNALFD_H

#ifdef DMTCP_USE_INOTIFY

/*****************************************************************************
 * Inotify Connection
 *****************************************************************************/
void
InotifyConnection::drain()
{
  ASSERT(_fds.size() > 0, "inotify connection has no fds during drain");
}

void
InotifyConnection::refill(bool isRestart)
{
  ASSERT(_fds.size() > 0, "inotify connection has no fds during refill");
  if (isRestart) {
    int num_of_descriptors;
    Util::Descriptor descriptor;
    descriptor_types_u watch_descriptor;

    // get the number of watch descriptors stored in dmtcp
    num_of_descriptors = descriptor.count_descriptors();

    JTRACE("inotify restoreOptions") (_fds[0]) (id()) (num_of_descriptors);

    for (int i = 0; i < num_of_descriptors; i++) {
      if (true == descriptor.get_descriptor(i, INOTIFY_ADD_WATCH_DESCRIPTOR,
                                            &watch_descriptor)) {
        int old_wd = watch_descriptor.add_watch.watch_descriptor;

        int new_wd =
          _real_inotify_add_watch(watch_descriptor.add_watch.file_descriptor,
                                  watch_descriptor.add_watch.pathname,
                                  watch_descriptor.add_watch.mask);

        WARNING_ERRNO(_real_dup2(new_wd, old_wd) == old_wd,
                      "failed to restore inotify watch descriptor: "
                      "new_wd={} old_wd={}",
                      new_wd, old_wd);
        JTRACE("restore watch descriptors")
          (old_wd) (new_wd) (watch_descriptor.add_watch.file_descriptor)
          (watch_descriptor.add_watch.pathname)
          (watch_descriptor.add_watch.mask);
      }
    }
  }
}

void
InotifyConnection::postRestart()
{
  // create a new inotify instance and clone it as the old one
  int tempfd = _real_inotify_init1(_flags);

  ASSERT_ERRNO(tempfd >= 0, "failed to recreate inotify fd: flags={}",
               _flags);
  restoreDupFds(tempfd);
}

void
InotifyConnection::serializeSubClass(jalib::JBinarySerializer &o)
{
  JSERIALIZE_ASSERT_POINT("InotifyConnection");
  o & _flags & _state;

  // o.serializeMap(_inotify_fd_to_wd);
  // o.serializeMap(_wd_to_pathname);
  // o.serializeMap(_pathname_to_mask);
}

InotifyConnection&
InotifyConnection::asInotify()
{
  JTRACE("Return the connection as Inotify connection");
  return *this;
}

void
InotifyConnection::add_watch_descriptors(int wd,
                                         int fd,
                                         const char *pathname,
                                         uint32_t mask)
{
  int string_len;

  JTRACE("save inotify watch descriptor within dmtcp")
    (wd) (fd) (pathname) (mask);
  ASSERT(pathname != NULL, "pathname is NULL");
  if (NULL != pathname) {
    Util::Descriptor descriptor;
    descriptor_types_u watch_descriptor;

    // set watch_descriptor to zeros
    memset(&watch_descriptor, 0, sizeof(watch_descriptor));

    // get the string length
    string_len = strlen(pathname);

    // fill up the structure
    watch_descriptor.add_watch.file_descriptor = fd;
    watch_descriptor.add_watch.mask = mask;
    watch_descriptor.add_watch.watch_descriptor = wd;
    watch_descriptor.add_watch.type = INOTIFY_ADD_WATCH_DESCRIPTOR;
    strncpy(watch_descriptor.add_watch.pathname, pathname, string_len);

    // save the watch descriptor structure
    descriptor.add_descriptor(&watch_descriptor);
  }
}

void
InotifyConnection::remove_watch_descriptors(int wd)
{
  Util::Descriptor descriptor;

  descriptor.remove_descriptor(INOTIFY_ADD_WATCH_DESCRIPTOR, (void *)&wd);
}
#endif // ifdef DMTCP_USE_INOTIFY
