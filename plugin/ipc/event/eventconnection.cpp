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
#include "jassert.h"
#include "jfilesystem.h"
#include "jconvert.h"

#include "eventconnection.h"
#include "eventwrappers.h"
#include "util_descriptor.h"
using namespace dmtcp;
/*****************************************************************************
 * Epoll Connection
 *****************************************************************************/

#ifdef HAVE_SYS_EPOLL_H
void dmtcp::EpollConnection::drain()
{
  JASSERT(_fds.size() > 0);
}

void dmtcp::EpollConnection::refill(bool isRestart)
{
  JASSERT(_fds.size() > 0);
  if (isRestart) {
    typedef dmtcp::map< int, struct epoll_event >::iterator fdEventIterator;
    fdEventIterator fevt = _fdToEvent.begin();
    for (; fevt != _fdToEvent.end(); fevt++) {
      JTRACE("restore sfd options") (fevt->first);
      int ret = _real_epoll_ctl(_fds[0], EPOLL_CTL_ADD, fevt->first,
                                &(fevt->second));
      JWARNING(ret == 0) (_fds[0]) (ret) (strerror(errno))
        .Text("Error in restoring options");
    }
  }
}

void dmtcp::EpollConnection::postRestart()
{
  JASSERT(_fds.size()>0);
  JTRACE("Recreating epoll connection") (_fds[0]) (id());
  int tempFd =  _real_epoll_create(_size) ;
  JASSERT(tempFd >= 0);
  Util::dupFds(tempFd, _fds);
}

void dmtcp::EpollConnection::serializeSubClass(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::EpollConnection");
  o & _type & _stat;
  o.serializeMap(_fdToEvent);
}

dmtcp::EpollConnection& dmtcp::EpollConnection::asEpoll()
{
  return *this;
}

void dmtcp::EpollConnection::onCTL(int op, int fd, struct epoll_event *event)
{
  JASSERT(((op==EPOLL_CTL_MOD || op==EPOLL_CTL_ADD) && event != NULL) ||
          op==EPOLL_CTL_DEL) (epollType())
   (id())
    .Text("Passing a NULL event! HUH!");

  struct epoll_event myEvent;
  _type = EPOLL_CTL;
  if (op == EPOLL_CTL_DEL) {
    _fdToEvent.erase(fd);
    return;
  }
  memcpy(&myEvent, event, sizeof myEvent);
  _fdToEvent[fd] = myEvent;
}
#endif

/*****************************************************************************
 * Eventfd Connection
 *****************************************************************************/
#ifdef HAVE_SYS_EVENTFD_H
void dmtcp::EventFdConnection::drain()
{
  JASSERT(_fds.size() > 0);
  JTRACE("Checkpoint eventfd.") (_fds[0]);

  int new_flags =(_fcntlFlags &(~(O_RDONLY|O_WRONLY))) | O_RDWR | O_NONBLOCK;
  int evtfd = _fds[0];
  JASSERT(evtfd >= 0) (evtfd) (JASSERT_ERRNO);
  // set the new flags
  JASSERT(fcntl(evtfd, F_SETFL, new_flags) == 0)
   (evtfd) (new_flags) (JASSERT_ERRNO);
  ssize_t size;
  uint64_t u;
  unsigned int counter = 1;

  // Read whatever is there on top of evtfd
  size = read(evtfd, &u, sizeof(uint64_t));
  if (-1 != size) {
    JTRACE("Read value u: ") (evtfd) (u);
    // EFD_SEMAPHORE flag not specified,
    // the counter value would have been reset to 0 upon read
    // Save the value, so that it can be restored in post-checkpoint
    if (!(_flags & EFD_SEMAPHORE)) {
      _initval = u;
    } else {
      // EFD_SEMAPHORE specified, so can't read the current counter value
      // Keep reading till "semaphore" becomes 0.
      while (-1 != read(evtfd, &u, sizeof(uint64_t)))
        counter++;
      _initval = counter;
    }
  } else {
    JTRACE("Nothing to be read from eventfd.")
      (evtfd) (errno) (strerror(errno));
    _initval = 0;
  }
  JTRACE("Checkpointing eventfd:  end.") (_fds[0]) (_initval);
}

void dmtcp::EventFdConnection::refill(bool isRestart)
{
  JTRACE("Begin refill eventfd.") (_fds[0]);
  JASSERT(_fds.size() > 0);
  evtfd = _fds[0];
  if (!isRestart) {
    uint64_t u =(unsigned long long) _initval;
    JTRACE("Writing") (u);
    JWARNING(write(evtfd, &u, sizeof(uint64_t)) == sizeof(uint64_t))
      (evtfd) (errno) (strerror(errno))
      .Text("Write to eventfd failed during refill");
  }
  JTRACE("End refill eventfd.") (_fds[0]);
}

void dmtcp::EventFdConnection::postRestart()
{
  JASSERT(_fds.size() > 0);

  JTRACE("Restoring EventFd Connection") (id());
  errno = 0;
  int tempfd = _real_eventfd(_initval, _flags);
  JASSERT(tempfd > 0) (tempfd) (JASSERT_ERRNO);
  Util::dupFds(tempfd, _fds);
}

void dmtcp::EventFdConnection::serializeSubClass(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::EventFdConnection");
  o & _initval & _flags;
  JTRACE("Serializing EvenFdConn.") ;
}
#endif

/*****************************************************************************
 * Signalfd Connection
 *****************************************************************************/
#ifdef HAVE_SYS_SIGNALFD_H
void dmtcp::SignalFdConnection::drain()
{
  JASSERT(_fds.size() > 0);

  JTRACE("Checkpoint signalfd.") (_fds[0]);

  int new_flags =(_fcntlFlags &(~(O_RDONLY|O_WRONLY))) | O_RDWR | O_NONBLOCK;
  signlfd = _fds[0];
  JASSERT(signlfd >= 0) (signlfd) (JASSERT_ERRNO);
  // set the new flags
  JASSERT(fcntl(signlfd, F_SETFL, new_flags) == 0)
   (signlfd) (new_flags) (JASSERT_ERRNO);
  ssize_t size;
  struct signalfd_siginfo _fdsi;

  // Read whatever is there on top of signalfd
  size = read(signlfd, &_fdsi, sizeof(struct signalfd_siginfo));
  if (-1 != size) {
    // Save the value, so that it can be restored in post-checkpoint
    memcpy(&_fdsi, &_fdsi, sizeof(struct signalfd_siginfo));

  } else {
    JTRACE("Nothing to be read from signalfd.")
      (signlfd) (errno) (strerror(errno));
  }
  JTRACE("Checkpointing signlfd:  end.") (_fds[0]) ;
}

void dmtcp::SignalFdConnection::refill(bool isRestart)
{
  JTRACE("Begin refill signalfd.") (_fds[0]);
  JASSERT(_fds.size() > 0);
  //raise the signals
  JTRACE("Raising the signal...") (_fdsi.ssi_signo);
  raise(_fdsi.ssi_signo);
  JTRACE("End refill signalfd.") (_fds[0]);
}

void dmtcp::SignalFdConnection::postRestart()
{
  JASSERT(_fds.size() > 0);

  JTRACE("Restoring SignalFd Connection") (id());
  errno = 0;
  int tempfd = _real_signalfd(-1, &_mask, _flags);
  JASSERT(tempfd > 0) (tempfd) (JASSERT_ERRNO);
  Util::dupFds(tempfd, _fds);
}

void dmtcp::SignalFdConnection::serializeSubClass(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::SignalFdConnection");
  o &  _flags & _mask & _fdsi;
  JTRACE("Serializing SignalFdConn.") ;
}
#endif

#ifdef DMTCP_USE_INOTIFY
/*****************************************************************************
 * Inotify Connection
 *****************************************************************************/
void dmtcp::InotifyConnection::drain()
{
  JASSERT(_fds.size() > 0);
}

void dmtcp::InotifyConnection::refill(bool isRestart)
{
  JASSERT(_fds.size() > 0);
  if (isRestart) {
    int num_of_descriptors;
    Util::Descriptor descriptor;
    descriptor_types_u  watch_descriptor;

    //get the number of watch descriptors stored in dmtcp
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

        JWARNING(_real_dup2(new_wd, old_wd) == old_wd)
          (new_wd) (old_wd) (JASSERT_ERRNO);
        JTRACE("restore watch descriptors")
          (old_wd) (new_wd) (watch_descriptor.add_watch.file_descriptor)
          (watch_descriptor.add_watch.pathname)
          (watch_descriptor.add_watch.mask);
      }
    }
  }
}

void dmtcp::InotifyConnection::postRestart()
{
  //create a new inotify instance and clone it as the old one
  int tempfd =  _real_inotify_init1(_flags);
  JASSERT(tempfd >= 0);
  Util::dupFds(tempfd, _fds);
}

void dmtcp::InotifyConnection::serializeSubClass(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::InotifyConnection");
  o & _type & _stat;
  //o.serializeMap(_inotify_fd_to_wd);
  //o.serializeMap(_wd_to_pathname);
  //o.serializeMap(_pathname_to_mask);
}

dmtcp::InotifyConnection& dmtcp::InotifyConnection::asInotify()
{
  JTRACE("Return the connection as Inotify connection");
  return *this;
}

void dmtcp::InotifyConnection::add_watch_descriptors(int wd, int fd,
                                                     const char *pathname,
                                                     uint32_t mask)
{
   int string_len;

   JTRACE("save inotify watch descriptor within dmtcp")
     (wd) (fd) (pathname) (mask);
   JASSERT(pathname != NULL) .Text("pathname is NULL");
   if (NULL != pathname) {
      Util::Descriptor descriptor;
      descriptor_types_u  watch_descriptor;

      //set watch_descriptor to zeros
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

void  dmtcp::InotifyConnection::remove_watch_descriptors(int wd)
{
   Util::Descriptor descriptor;
   descriptor.remove_descriptor(INOTIFY_ADD_WATCH_DESCRIPTOR, (void *)&wd);
}
#endif
