/****************************************************************************
 *   Copyright (C) 2012 by Kapil Arya, Gene Cooperman, and Rohan Garg       *
 *   kapil@ccs.neu.edu, gene@ccs.neu.edu, rohgarg@ccs.neu.edu               *
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

#include <poll.h>
#include <sys/select.h>

/* According to POSIX.1-2001 */
#include <sys/select.h>

/* Next three according to earlier standards */
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include "jassert.h"
#include "dmtcpalloc.h"
#include "eventconnection.h"
#include "eventconnlist.h"
#include "eventwrappers.h"

using namespace dmtcp;

/* 'man 7 signal' says the following are not restarted after ckpt signal
 * even though the SA_RESTART option was used.  If we wrap these, we must
 * restart them when they are interrupted by a checkpoint signal.
 * python-2.7{socketmodule.c,signalmodule.c} assume no unknown signal
 *   handlers such as DMTCP checkpoint signal.  So, Python needs this.
 *
 * + Socket interfaces, when a timeout has been  set  on  the  socket
 *   using   setsockopt(2):   accept(2),  recv(2),  recvfrom(2),  and
 *   recvmsg(2), if a receive timeout (SO_RCVTIMEO)  has  been  set;
 *   connect(2),  send(2), sendto(2), and sendmsg(2), if a send timeout
 *   (SO_SNDTIMEO) has been set.
 *
 * + Interfaces used to wait for  signals:  pause(2),  sigsuspend(2),
 *   sigtimedwait(2), and sigwaitinfo(2).
 *
 * + File    descriptor   multiplexing   interfaces:   epoll_wait(2),
 *   epoll_pwait(2), poll(2), ppoll(2), select(2), and pselect(2).
 *
 * + System V IPC interfaces:  msgrcv(2),  msgsnd(2),  semop(2),  and
 *   semtimedop(2).
 *
 * + Sleep interfaces:  clock_nanosleep(2), nanosleep(2), and usleep(3).
 *
 * + read(2) from an inotify(7) file descriptor.
 *
 * + io_getevents(2).
 */

// TODO(kapil): A better way to fix this is to lookup the stack to check if we
// are in the middle of a poll/select/pselect call and set some global variable
// and restart the syscall only if that variable is set.

/* Poll wrapper forces poll to restart after ckpt/resume or ckpt/restart */
extern "C" int
poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
  int rc;

  while (1) {
    uint32_t orig_generation = dmtcp_get_generation();
    rc = _real_poll(fds, nfds, timeout);
    if (rc == -1 && errno == EINTR &&
        dmtcp_get_generation() > orig_generation) {
      continue;  // This was a restart or resume after checkpoint.
    } else {
      break;  // The signal interrupting us was not our checkpoint signal.
    }
  }
  return rc;
}

/* Poll wrapper forces poll to restart after ckpt/resume or ckpt/restart */

// Two separate definitions for poll and __poll_chk allow us to support
// application binaries compiled with "poll" (older libc versions) as well as
// binaries compiled with "__poll_chk" (newer libc versions).
extern "C" int
__poll_chk(struct pollfd *fds, nfds_t nfds, int timeout, size_t fdslen)
{
  JASSERT((fdslen / sizeof(*fds)) >= nfds) (nfds) (fdslen)
  .Text("Buffer Overflow detected!");

  int rc;
  while (1) {
    uint32_t orig_generation = dmtcp_get_generation();
    rc = _real_poll_chk(fds, nfds, timeout, fdslen);
    if (rc == -1 && errno == EINTR &&
        dmtcp_get_generation() > orig_generation) {
      continue;  // This was a restart or resume after checkpoint.
    } else {
      break;  // The signal interrupting us was not our checkpoint signal.
    }
  }
  return rc;
}

// pselect wrapper forces pselect to restart after ckpt/resume or ckpt/restart
extern "C" int
pselect(int nfds,
        fd_set *readfds,
        fd_set *writefds,
        fd_set *exceptfds,
        const struct timespec *timeout,
        const sigset_t *sigmask)
{
  int rc;

  while (1) {
    uint32_t orig_generation = dmtcp_get_generation();
    rc = _real_pselect(nfds, readfds, writefds, exceptfds, timeout, sigmask);
    if (rc == -1 && errno == EINTR &&
        dmtcp_get_generation() > orig_generation) {
      continue;  // This was a restart or resume after checkpoint.
    } else {
      break;  // The signal interrupting us was not our checkpoint signal.
    }
  }
  return rc;
}

extern "C" int
select(int nfds,
       fd_set *readfds,
       fd_set *writefds,
       fd_set *exceptfds,
       struct timeval *timeout)
{
  int rc;

  while (1) {
    uint32_t orig_generation = dmtcp_get_generation();
    rc = _real_select(nfds, readfds, writefds, exceptfds, timeout);
    if (rc == -1 && errno == EINTR &&
        dmtcp_get_generation() > orig_generation) {
      continue;  // This was a restart or resume after checkpoint.
    } else {
      break;  // The signal interrupting us was not our checkpoint signal.
    }
  }
  return rc;
}

/****************************************************************************
 ****************************************************************************/

#ifdef HAVE_SYS_SIGNALFD_H
extern "C" int
signalfd(int fd, const sigset_t *mask, int flags)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int ret = _real_signalfd(fd, mask, flags);
  if (ret != -1) {
    JTRACE("signalfd created") (fd) (flags);
    EventConnList::instance().add(ret, new SignalFdConnection(fd, mask, flags));
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}
#endif // ifdef HAVE_SYS_SIGNALFD_H

#ifdef HAVE_SYS_EVENTFD_H
extern "C" int
eventfd(EVENTFD_VAL_TYPE initval, int flags)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int ret = _real_eventfd(initval, flags);
  if (ret != -1) {
    JTRACE("eventfd created") (ret) (initval) (flags);
    EventConnList::instance().add(ret, new EventFdConnection(initval, flags));
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}
#endif // ifdef HAVE_SYS_EVENTFD_H

#ifdef HAVE_SYS_EPOLL_H
extern "C" int
epoll_create(int size)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int ret = _real_epoll_create(size);
  if (ret != -1) {
    JTRACE("epoll fd created") (ret) (size);
    EventConnList::instance().add(ret, new EpollConnection(size, 0));
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

#if HAS_EPOLL_CREATE1
extern "C" int
epoll_create1(int flags)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int ret = _real_epoll_create1(flags);
  if (ret != -1) {
    JNOTE("epoll fd created1") (ret) (flags);
    EventConnList::instance().add(ret, new EpollConnection(0, flags));
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}
#endif

extern "C" int
epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int ret = _real_epoll_ctl(epfd, op, fd, event);
  if (ret != -1) {
    // JTRACE("epoll fd CTL") (ret) (epfd) (fd) (op);
    EpollConnection *con =
      (EpollConnection *)EventConnList::instance().getConnection(epfd);
    con->onCTL(op, fd, event);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

extern "C" int
epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
  int readyFds = 0;
  int timeLeft = timeout;
  int mytime;

  // JTRACE("Starting to do wait on epoll fd");

  if (timeout >= 0 && timeout < 1000) {
    // Short time intervals
    DMTCP_PLUGIN_DISABLE_CKPT();
    readyFds = _real_epoll_wait(epfd, events, maxevents, timeout);
    DMTCP_PLUGIN_ENABLE_CKPT();
    return readyFds;
  } else if (timeout >= 1000) {
    mytime = 1000; // wait time quanta: 1000 ms
  } else {
    // In case of indefinite timeout, start with 0 and increment by 1ms.
    mytime = 0;
  }

  do {
    DMTCP_PLUGIN_DISABLE_CKPT();
    readyFds = _real_epoll_wait(epfd, events, maxevents, mytime);
    DMTCP_PLUGIN_ENABLE_CKPT();
    if (timeout < 0 && mytime <= 100) {
      // Increase timeout if we are going to wait forever.
      mytime += 1;
    } else {
      timeLeft -= mytime;
    }
  } while ((timeLeft > 0 || timeout < 0) && readyFds == 0);
  return readyFds;
}
#endif // ifdef HAVE_SYS_EPOLL_H


#ifdef HAVE_SYS_INOTIFY_H
# ifndef DMTCP_USE_INOTIFY
EXTERNC int
inotify_init()
{
  JWARNING(false).Text("Inotify not yet supported by DMTCP");
  errno = ENOMEM;
  return -1;
}

EXTERNC int
inotify_init1(int flags)
{
  JWARNING(false).Text("Inotify not yet supported by DMTCP");
  errno = ENOMEM;
  return -1;
}

# else // ifndef DMTCP_USE_INOTIFY

/******************************************************************
 * function name: inotify_init()
 *
 * description:   monitors file system events
 *
 * para:          none
 * return:        fd (inotify instance)
 ******************************************************************/
EXTERNC int
inotify_init()
{
  int fd;

  DMTCP_PLUGIN_DISABLE_CKPT();
  JTRACE("Starting to create an inotify fd.");
  fd = _real_inotify_init();
  if (fd > 0) {
    JTRACE("inotify fd created") (ret);

    // create the inotify object
    Connection *con = new InotifyConnection(0);
    EventConnList::instance().add(ret, con);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return fd;
}

/******************************************************************
 * function name: inotify_init1()
 *
 * description:   monitors file system events
 *
 * para:          flags
 * return:        fd (inotify instance)
 ******************************************************************/
EXTERNC int
inotify_init1(int flags)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int ret = _real_inotify_init1(flags);
  if (ret != -1) {
    JTRACE("inotify1 fd created") (ret) (flags);
    Connection *con = new InotifyConnection(flags);
    EventConnList::instance().add(ret, flags);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

/******************************************************************
 * function name: inotify_add_watch()
 *
 * description:   adds a directory or file to be watched
 *
 * para:          fd        - inotify instance
 * para:          pathname  - directory or file
 * para:          mask      - events to be monitored on pathname
 * return:        watch descriptor (wd) on success, -1 on failure
 ******************************************************************/
EXTERNC int
inotify_add_watch(int fd, const char *pathname, uint32_t mask)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int ret = _real_inotify_add_watch(fd, pathname, mask);
  if (ret != -1) {
    JTRACE("calling inotify class methods");
    InotifyConnection &inotify_con =
      (InotifyConnection *)EventConnList::instance().getConnection(fd);

    inotify_con->add_watch_descriptors(ret, fd, pathname, mask);

    /*temp_pathname = pathname;
      inotify_con.map_inotify_fd_to_wd ( fd, ret);
      inotify_con.map_wd_to_pathname(ret, temp_pathname);
      inotify_con.map_pathname_to_mask(temp_pathname, mask);*/
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

/******************************************************************
 * function name: inotify_rm_watch()
 *
 * description:   removes the watch descriptor associated with
 *                the inotify instance
 *
 * para:          fd        - inotify instance
 * para:          wd        - watch descriptor to be removed
 * return:        0 on success, -1 on failure
 ******************************************************************/
EXTERNC int
inotify_rm_watch(int fd, int wd)
{
  DMTCP_PLUGIN_DISABLE_CKPT(); // The lock is released inside the macro.
  int ret = _real_inotify_rm_watch(fd, wd);
  if (ret != -1) {
    JTRACE("remove inotify mapping from dmtcp") (ret) (fd) (wd);
    InotifyConnection &inotify_con =
      (InotifyConnection *)EventConnList::instance().getConnection(fd);

    // inotify_con.remove_mappings(fd, wd);
    inotify_con->remove_watch_descriptors(wd);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}
# endif // ifndef DMTCP_USE_INOTIFY
#endif // ifdef HAVE_SYS_INOTIFY_H
