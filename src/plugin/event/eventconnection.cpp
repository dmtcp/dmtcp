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

#include "jconvert.h"
#include "jfilesystem.h"
#include "dmtcp.h"
#include "shareddata.h"
#include "util.h"

#include "eventconnection.h"
#include "eventwrappers.h"
#include "dmtcp_assert.h"
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
      TRACE("Restoring epoll watch during restart: epfd={} watched_fd={}",
            _fds[0], fevt->first);
      WARN_NE(-1,
        _real_epoll_ctl(_fds[0], EPOLL_CTL_ADD, fevt->first,
                        &(fevt->second)),
        "Failed to restore epoll watch: epfd={} watched_fd={}",
        _fds[0], fevt->first);
    }
  }
}

void
EpollConnection::postRestart()
{
  ASSERT(_fds.size() > 0, "epoll connection has no fds during postRestart");
  TRACE("Recreating epoll connection after restart: fd={} con_id={}",
        _fds[0], id().toString());
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
  ASSERT_NE(-1, tempfd,
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
  TRACE("Checkpointing eventfd: fd={}", _fds[0]);

  int new_flags = (_fcntlFlags & (~(O_RDONLY | O_WRONLY))) | O_RDWR |
    O_NONBLOCK;
  ASSERT_NE(-1, _fds[0], "invalid eventfd during drain");

  // set the new flags
  ASSERT_NE(-1, fcntl(_fds[0], F_SETFL, new_flags),
               "fcntl(F_SETFL) failed for eventfd drain: fd={} flags={}",
               _fds[0], new_flags);
  uint64_t u;

  // Read whatever is there on top of _fds[0]
  ssize_t size = read(_fds[0], &u, sizeof(uint64_t));
  if (-1 != size) {
    TRACE("Drained eventfd counter: fd={} value={}", _fds[0], u);

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
    TRACE("Eventfd had no counter value to drain: fd={} errno={} error={}",
          _fds[0], errno, strerror(errno));
    _initval = 0;
  }
  TRACE("Finished checkpointing eventfd: fd={} restored_initval={}",
        _fds[0], _initval);
}

void
EventFdConnection::refill(bool isRestart)
{
  ASSERT(_fds.size() > 0, "eventfd connection has no fds during refill");
  TRACE("Refilling eventfd state: fd={} is_restart={}", _fds[0], isRestart);
  if (!isRestart) {
    uint64_t u = (unsigned long long)_initval;
    TRACE("Writing eventfd counter during refill: fd={} value={}",
          _fds[0], u);
    WARN_EQ(static_cast<ssize_t>(sizeof(uint64_t)),
                           write(_fds[0], &u, sizeof(uint64_t)),
                           "Write to eventfd failed during refill: fd={}",
                           _fds[0]);
  }
  TRACE("Finished refilling eventfd state: fd={} is_restart={}",
        _fds[0], isRestart);
}

void
EventFdConnection::postRestart()
{
  ASSERT(_fds.size() > 0,
         "eventfd connection has no fds during postRestart");

  TRACE("Recreating eventfd connection after restart: con_id={}",
        id().toString());
  errno = 0;
  int tempfd = _real_eventfd(_initval, _flags);
  ASSERT_NE(-1, tempfd,
                      "failed to recreate eventfd: initval={} flags={}",
                      _initval, _flags);
  restoreDupFds(tempfd);
}

void
EventFdConnection::serializeSubClass(jalib::JBinarySerializer &o)
{
  JSERIALIZE_ASSERT_POINT("EventFdConnection");
  o & _initval & _flags;
  TRACE("Serializing eventfd connection");
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

  TRACE("Checkpointing signalfd: fd={}", _fds[0]);

  int new_flags =
    (_fcntlFlags & (~(O_RDONLY | O_WRONLY))) | O_RDWR | O_NONBLOCK;

  // set the new flags
  ASSERT_NE(-1, fcntl(_fds[0], F_SETFL, new_flags),
               "fcntl(F_SETFL) failed for signalfd drain: fd={} flags={}",
               _fds[0], new_flags);

  // Read whatever is there on top of signalfd
  ssize_t size = read(_fds[0], &_fdsi, sizeof(struct signalfd_siginfo));
  if (size <= 0) {
    TRACE("Signalfd had no pending signal to drain: fd={} errno={}",
          _fds[0], errno);
    memset(&_fdsi, 0, sizeof(_fdsi));
  }
}

void
SignalFdConnection::refill(bool isRestart)
{
  ASSERT(_fds.size() > 0, "signalfd connection has no fds during refill");
  TRACE("Refilling signalfd state: fd={} is_restart={}", _fds[0], isRestart);

  // raise the signals
  TRACE("Re-raising signal for signalfd refill: signo={}", _fdsi.ssi_signo);
  raise(_fdsi.ssi_signo);
  TRACE("Finished refilling signalfd state: fd={} is_restart={}",
        _fds[0], isRestart);
}

void
SignalFdConnection::postRestart()
{
  ASSERT(_fds.size() > 0,
         "signalfd connection has no fds during postRestart");

  TRACE("Recreating signalfd connection after restart: con_id={}",
        id().toString());
  errno = 0;
  int tempfd = _real_signalfd(-1, &_mask, _flags);
  ASSERT_NE(-1, tempfd, "failed to recreate signalfd: flags={}",
                      _flags);
  restoreDupFds(tempfd);
}

void
SignalFdConnection::serializeSubClass(jalib::JBinarySerializer &o)
{
  JSERIALIZE_ASSERT_POINT("SignalFdConnection");
  o & _flags & _mask & _fdsi;
  TRACE("Serializing signalfd connection");
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

    TRACE("Restoring inotify watches: fd={} con_id={} watch_count={}",
          _fds[0], id().toString(), num_of_descriptors);

    for (int i = 0; i < num_of_descriptors; i++) {
      if (true == descriptor.get_descriptor(i, INOTIFY_ADD_WATCH_DESCRIPTOR,
                                            &watch_descriptor)) {
        int old_wd = watch_descriptor.add_watch.watch_descriptor;

        int new_wd =
          _real_inotify_add_watch(watch_descriptor.add_watch.file_descriptor,
                                  watch_descriptor.add_watch.pathname,
                                  watch_descriptor.add_watch.mask);

        WARN_EQ(old_wd,
                               _real_dup2(new_wd, old_wd),
                               "failed to restore inotify watch descriptor: "
                               "new_wd={} old_wd={}",
                               new_wd, old_wd);
        TRACE("Restored inotify watch: old_wd={} new_wd={} fd={} "
              "path={} mask={}",
              old_wd, new_wd,
              watch_descriptor.add_watch.file_descriptor,
              watch_descriptor.add_watch.pathname,
              watch_descriptor.add_watch.mask);
      }
    }
  }
}

void
InotifyConnection::postRestart()
{
  // create a new inotify instance and clone it as the old one
  int tempfd = _real_inotify_init1(_flags);

  ASSERT_NE(-1, tempfd, "failed to recreate inotify fd: flags={}",
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
  TRACE("Using connection as inotify connection");
  return *this;
}

void
InotifyConnection::add_watch_descriptors(int wd,
                                         int fd,
                                         const char *pathname,
                                         uint32_t mask)
{
  ASSERT_NOT_NULL(pathname, "pathname is NULL");
  TRACE("Saving inotify watch descriptor: wd={} fd={} path={} mask={}",
        wd, fd, pathname, mask);
  Util::Descriptor descriptor;
  descriptor_types_u watch_descriptor;

  // set watch_descriptor to zeros
  memset(&watch_descriptor, 0, sizeof(watch_descriptor));

  const size_t string_len = strlen(pathname);
  const size_t pathname_capacity =
    sizeof(watch_descriptor.add_watch.pathname);
  ASSERT(string_len < pathname_capacity,
         "inotify pathname too long: len={} max={} path={}",
         string_len, pathname_capacity - 1, pathname);

  // fill up the structure
  watch_descriptor.add_watch.file_descriptor = fd;
  watch_descriptor.add_watch.mask = mask;
  watch_descriptor.add_watch.watch_descriptor = wd;
  watch_descriptor.add_watch.type = INOTIFY_ADD_WATCH_DESCRIPTOR;
  memcpy(watch_descriptor.add_watch.pathname, pathname, string_len);
  watch_descriptor.add_watch.pathname[string_len] = '\0';

  // save the watch descriptor structure
  descriptor.add_descriptor(&watch_descriptor);
}

void
InotifyConnection::remove_watch_descriptors(int wd)
{
  Util::Descriptor descriptor;

  descriptor.remove_descriptor(INOTIFY_ADD_WATCH_DESCRIPTOR, (void *)&wd);
}
#endif // ifdef DMTCP_USE_INOTIFY
