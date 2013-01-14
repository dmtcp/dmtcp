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
#include "connectionlist.h"
#include "syscallwrappers.h"
#include "../jalib/jassert.h"

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
 *   connect(2),  send(2), sendto(2), and sendmsg(2), if a send time‐
 *   out(SO_SNDTIMEO) has been set.
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
 * + Sleep    interfaces:   clock_nanosleep(2),   nanosleep(2),   and
 *   usleep(3).
 *
 * + read(2) from an inotify(7) file descriptor.
 *
 * + io_getevents(2).
 */

/* Poll wrapper forces poll to restart after ckpt/resume or ckpt/restart */
extern "C" int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
  int rc;
  while (1) {
    int orig_generation = dmtcp_get_generation();
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



//////////////////////////////////////
//////// Now we define our wrappers for epoll, eventfd, and signalfd
//////// We als create a wrapper for inotify saying that it is not supported.

/* Prevent recursive calls to dmtcp_on_XXX() */
static int in_dmtcp_on_helper_fnc = 0;

#define PASSTHROUGH_DMTCP_HELPER(func, ...) {       \
    int ret = _real_ ## func(__VA_ARGS__);         \
    int saved_errno;                                \
    saved_errno = errno;                            \
    PASSTHROUGH_DMTCP_HELPER2(func,__VA_ARGS__);    \
    }

#define PASSTHROUGH_DMTCP_HELPER2(func, ...) {                              \
    _dmtcp_lock();                                                          \
    if (in_dmtcp_on_helper_fnc == 0) {                                      \
      in_dmtcp_on_helper_fnc = 1;                                           \
      if (ret < 0) ret = saved_errno/*dmtcp_on_error(ret, epollfd, #func, saved_errno)*/;    \
      else ret = dmtcp_on_ ## func(ret, __VA_ARGS__);                      \
      in_dmtcp_on_helper_fnc = 0;                                           \
    }                                                                       \
    _dmtcp_unlock();                                                        \
    errno =saved_errno;                                                     \
    /* If the wrapper-execution lock was acquired earlier, release it now*/ \
    DMTCP_ENABLE_CKPT();                                                    \
    return ret;                                                             \
  }

#ifndef EXTERNC
# define EXTERNC extern "C"
#endif


// called automatically after a successful user function call
EXTERNC int dmtcp_on_epoll_create(int ret, int size)
{

  JTRACE("epoll fd created") (ret) (size);
  dmtcp::ConnectionList::instance().add(ret, new dmtcp::EpollConnection(size));
  return ret;
}

// called automatically after a successful user function call
EXTERNC int dmtcp_on_epoll_create1(int ret, int flags)
{
  JTRACE("epoll fd created1") (ret) (flags);
  dmtcp_on_epoll_create(ret, flags); //use it as size for now
  return ret;
}

EXTERNC int dmtcp_on_epoll_ctl(int ret, int epfd, int op, int fd,
                               struct epoll_event *event)
{
  JTRACE("epoll fd CTL") (ret) (epfd) (fd) (op);
  dmtcp::EpollConnection& con =
    dmtcp::ConnectionList::instance().getConnection(epfd)->asEpoll();

  con.onCTL(op, fd, event);
  return ret;
}

EXTERNC int dmtcp_on_eventfd(int ret, int initval, int flags)
{

  JTRACE("eventfd created") (ret) (initval) (flags);
  ConnectionList::instance().add(ret, new EventFdConnection(initval, flags));
  return ret;
}

EXTERNC int dmtcp_on_signalfd(int ret, int fd, const sigset_t *mask, int flags)
{
  JTRACE("signalfd created") (fd) (flags);
  ConnectionList::instance().add(ret, new SignalFdConnection(fd, mask, flags));
  return ret;
}

#ifdef DMTCP_USE_INOTIFY
/******************************************************************
 * function name: dmtcp_on_inotify_init()
 *
 * description:   saves the inotify instance within dmtcp. It's called
 *                automatically after a successful inotify_init()
 *
 * para:          ret - inotify instace
 * return:        fd (inotify instance) to the application
 ******************************************************************/
EXTERNC int dmtcp_on_inotify_init(int ret)
{
  JTRACE ( "inotify fd created" ) ( ret );
  //create the inotify object
  dmtcp::Connection *con = new dmtcp::InotifyConnection(0);
  dmtcp::ConnectionList::instance().add(ret, con);
  return ret;
}

/******************************************************************
 * function name: dmtcp_on_inotify_init1()
 *
 * description:   saves the inotify instance within dmtcp. It's called
 *                automatically after a successful inotify_init1()
 *
 * para:          ret   - inotify instance
 * para:          flags
 * return:        fd (inotify instance)
 ******************************************************************/
EXTERNC int dmtcp_on_inotify_init1(int ret, int flags)
{
  JTRACE("inotify1 fd created") (ret) (flags);

  //create the inotify object
  dmtcp::Connection *con = new dmtcp::InotifyConnection(flags);
  dmtcp::ConnectionList::instance().add(ret, flags);
  return ret;
}

/******************************************************************
 * function name: dmtcp_on_inotify_add_watch()
 *
 * description:   saves the pathname and mask within dmtcp. It's
 *                automatically after a successful inotify_add_watch()
 *
 * para:          ret       - watch descriptor
 * para:          fd        - inotify instance
 * para:          pathname  - directory or file
 * para:          mask      - events to be monitored on pathname
 * return:        watch descriptor (wd) on success, -1 on failure
 ******************************************************************/
EXTERNC int dmtcp_on_inotify_add_watch(int ret, int fd,
                                       const char *pathname, uint32_t mask)
{
   JTRACE("calling inotify class methods");

  dmtcp::InotifyConnection& inotify_con =
    dmtcp::ConnectionList::instance().getConnection(fd)->asInotify();

  inotify_con.add_watch_descriptors(ret, fd, pathname, mask);
  /*temp_pathname = pathname;
  inotify_con.map_inotify_fd_to_wd ( fd, ret);
  inotify_con.map_wd_to_pathname(ret, temp_pathname);
  inotify_con.map_pathname_to_mask(temp_pathname, mask);*/
  return ret;
}

/******************************************************************
 * function name: dmtcp_on_inotify_rm_watch()
 *
 * description:   removes the watch descriptor associated with
 *                the inotify instance,from within dmtcp. It's called
 *                automatically after a successful inotify_rm_watch()
 *
 * para:          ret       - return value from actual call
 * para:          fd        - inotify instance
 * para:          wd        - watch descriptor to be removed
 * return:        0 on success, -1 on failure
 ******************************************************************/
EXTERNC int dmtcp_on_inotify_rm_watch(int ret, int fd, int wd)
{
   JTRACE("remove inotify mapping from dmtcp") (ret) (fd) (wd);

   dmtcp::InotifyConnection& inotify_con =
     dmtcp::ConnectionList::instance().getConnection(fd)->asInotify();
   //inotify_con.remove_mappings(fd, wd);
   inotify_con.remove_watch_descriptors(wd);
   return ret;
}
#endif

/****************************************************************************
 ****************************************************************************/

extern "C" int signalfd(int fd, const sigset_t *mask, int flags)
{
  DMTCP_DISABLE_CKPT();
  JTRACE("Creating signalfd");
  PASSTHROUGH_DMTCP_HELPER(signalfd, fd, mask, flags);
}

extern "C" int eventfd(int initval, int flags)
{
  DMTCP_DISABLE_CKPT();
  JTRACE("Creating eventfd");
  PASSTHROUGH_DMTCP_HELPER(eventfd, initval, flags);
}

/* epoll is(apparently!) supported by DMTCP */
extern "C" int epoll_create(int size)
{
  //static int epfd = -1;
  //JWARNING(false) .Text("epoll is currently not supported by DMTCP.");
  DMTCP_DISABLE_CKPT(); // The lock is released inside the macro.
  JTRACE("Starting to create epoll fd.");
  //errno = EPERM;
  PASSTHROUGH_DMTCP_HELPER(epoll_create, size);
  //return -1;
}

/* epoll is(apparently!) supported by DMTCP */
extern "C" int epoll_create1(int flags)
{
  //JWARNING(false) .Text("epoll is currently not supported by DMTCP.");
  DMTCP_DISABLE_CKPT(); // The lock is released inside the macro.
  JTRACE("Starting to create1 epoll fd.");
  //errno = EPERM;
  PASSTHROUGH_DMTCP_HELPER(epoll_create1, flags);
}

extern "C" int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
  DMTCP_DISABLE_CKPT();
  JTRACE("Starting to do stuff with epoll fd");
  PASSTHROUGH_DMTCP_HELPER(epoll_ctl, epfd, op, fd, event);
}

extern "C" int epoll_wait(int epfd, struct epoll_event *events, int maxevents,
                          int timeout)
{
  int readyFds, timeLeft = timeout;
  JTRACE("Starting to do wait on epoll fd");
  int mytime = 1000; // wait time quanta: 1000 ms
  while (1)
  {
    DMTCP_DISABLE_CKPT();
    readyFds = _real_epoll_wait(epfd, events, maxevents, mytime);
    DMTCP_ENABLE_CKPT();
    if (timeLeft > 0)
        timeLeft -= mytime;

    if ((timeout < 0 || timeLeft > 0) &&(0 == readyFds))
    {
      // More time left; didn't get any notification continue to wait...
      continue;
    }
    else
    {
      return readyFds;
    }
  }
}


#ifndef DMTCP_USE_INOTIFY
EXTERNC int inotify_init()
{
  JWARNING(false) .Text("Inotify not yet supported by DMTCP");
  errno = ENOMEM;
  return -1;
}

EXTERNC int inotify_init1(int flags)
{
  JWARNING(false) .Text("Inotify not yet supported by DMTCP");
  errno = ENOMEM;
  return -1;
}
#else
/******************************************************************
 * function name: inotify_init()
 *
 * description:   monitors file system events
 *
 * para:          none
 * return:        fd (inotify instance)
 ******************************************************************/
EXTERNC int inotify_init()
{
  int fd;
  DMTCP_DISABLE_CKPT(); // The lock is released inside the macro.
  JTRACE("Starting to create an inotify fd.");
  fd = _real_inotify_init();
  if (fd > 0) {
    _dmtcp_lock();
    fd = dmtcp_on_inotify_init(fd);
    _dmtcp_unlock();
  }
  DMTCP_ENABLE_CKPT();
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
EXTERNC int inotify_init1(int flags)
{
  DMTCP_DISABLE_CKPT();
  JTRACE("Starting to create an inotify fd.");
  PASSTHROUGH_DMTCP_HELPER(inotify_init1, flags);
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
EXTERNC int inotify_add_watch(int fd, const char *pathname, uint32_t mask)
{
  DMTCP_DISABLE_CKPT();
  JTRACE("Starting to create a watch descriptor.");
  PASSTHROUGH_DMTCP_HELPER (inotify_add_watch, fd, pathname, mask);
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
EXTERNC int inotify_rm_watch(int fd, int wd)
{
  DMTCP_DISABLE_CKPT(); // The lock is released inside the macro.
  JTRACE("Starting to create1 inotify fd.");
  PASSTHROUGH_DMTCP_HELPER (inotify_rm_watch, fd, wd);
}
#endif
