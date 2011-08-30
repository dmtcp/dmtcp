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

// TODO: Better way to do this. I think it was only a problem on dekaksi.
// Remove this, and see the compile error.
#define read _libc_read
#include <stdarg.h>
#include <stdlib.h>
#include <vector>
#include <list>
#include <string>
#include <fcntl.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/version.h>
#include <limits.h>
#include "constants.h"
#include "syscallwrappers.h"
#include "util.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jfilesystem.h"

#ifdef RECORD_REPLAY
#include "fred_wrappers.h"
#include "synchronizationlogging.h"
#include <sys/mman.h>

/* epoll is currently not supported by DMTCP */
extern "C" int epoll_create(int size)
{
  BASIC_SYNC_WRAPPER(int, epoll_create, _almost_real_epoll_create, size);
}

/* epoll is currently not supported by DMTCP */
extern "C" int epoll_create1(int flags)
{
  BASIC_SYNC_WRAPPER(int, epoll_create1, _almost_real_epoll_create1, flags);
}

extern "C" int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
  BASIC_SYNC_WRAPPER(int, epoll_ctl, _real_epoll_ctl, epfd, op, fd, event);
}

extern "C" int epoll_wait(int epfd, struct epoll_event *events,
                          int maxevents, int timeout)
{
  WRAPPER_HEADER(int, epoll_wait, _real_epoll_wait, epfd, events, maxevents,
                 timeout);

  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START_TYPED(int, epoll_wait);
    if (retval > 0) {
      size_t size = retval * sizeof(struct epoll_event);
      WRAPPER_REPLAY_READ_FROM_READ_LOG(epoll_wait, (void*) events, size);
    }
    WRAPPER_REPLAY_END(epoll_wait);
  } else if (SYNC_IS_RECORD) {
    retval = _real_epoll_wait(epfd, events, maxevents, timeout);
    if (retval > 0) {
      size_t size = retval * sizeof(struct epoll_event);
      WRAPPER_LOG_WRITE_INTO_READ_LOG(epoll_wait, (void*) events, size);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" int epoll_pwait(int epfd, struct epoll_event *events,
                           int maxevents, int timeout, const sigset_t *sigmask)
{
  JASSERT(false) .Text("NOT IMPLEMENTED");
  return 0;
}
#endif
