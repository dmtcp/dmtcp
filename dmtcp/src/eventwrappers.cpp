/****************************************************************************
 *   Copyright (C) 2006-2010 by Kapil Arya, and Gene Cooperman              *
 *   kapil@ccs.neu.edu, gene@ccs.neu.edu                                    *
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
#include "uniquepid.h"
#include "syscallwrappers.h"
#include  "../jalib/jassert.h"

/* 'man 7 signal' says the following are not restarted after ckpt signal
 * even though the SA_RESTART option was used.  If we wrap these, we must
 * restart them when they are interrupted by a checkpoint signal.
 * python-2.7{socketmodule.c,signalmodule.c} assume no unknown signal
 *   handlers such as DMTCP checkpoint signal.  So, Python needs this.
 *
 * + Socket interfaces, when a timeout has been  set  on  the  socket
 *   using   setsockopt(2):   accept(2),  recv(2),  recvfrom(2),  and
 *   recvmsg(2), if a receive timeout  (SO_RCVTIMEO)  has  been  set;
 *   connect(2),  send(2), sendto(2), and sendmsg(2), if a send time‐
 *   out (SO_SNDTIMEO) has been set.
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
    int orig_generation = dmtcp::UniquePid::ComputationId().generation();
    rc = _real_poll(fds, nfds, timeout);
    if ( rc == -1 && errno == EINTR &&
         dmtcp::UniquePid::ComputationId().generation() > orig_generation ) {
      continue;  // This was a restart or resume after checkpoint.
    } else {
      break;  // The signal interrupting us was not our checkpoint signal.
    }
  }
  return rc;
}

/* inotify is currently not supported by DMTCP */
extern "C" int inotify_init()
{
  JWARNING (false) .Text("inotify is currently not supported by DMTCP.");
  errno = EMFILE;
  return -1;
}

/* inotify1 is currently not supported by DMTCP */
extern "C" int inotify_init1(int flags)
{
  JWARNING (false) .Text("inotify is currently not supported by DMTCP.");
  errno = EMFILE;
  return -1;
}

/* epoll is currently not supported by DMTCP */
extern "C" int epoll_create(int size)
{
  /* epoll is currently not supported by DMTCP */
  JWARNING (false) .Text("epoll is currently not supported by DMTCP.");
  errno = EPERM;
  return -1;
}

/* epoll is currently not supported by DMTCP */
extern "C" int epoll_create1(int flags)
{
  JWARNING (false) .Text("epoll is currently not supported by DMTCP.");
  errno = EPERM;
  return -1;
}

