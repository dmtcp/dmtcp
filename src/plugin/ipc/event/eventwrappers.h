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

#pragma once
#ifndef EVENT_WRAPPERS_H
# define EVENT_WRAPPERS_H

# include <features.h>

# include "config.h"
# include "dmtcp.h"

# if __GLIBC_PREREQ(2, 21)
#  define EVENTFD_VAL_TYPE    unsigned int
# else // if __GLIBC_PREREQ(2, 21)
#  define EVENTFD_VAL_TYPE    int
# endif // if __GLIBC_PREREQ(2, 21)

# define _real_poll           NEXT_FNC(poll)
# define _real_poll_chk       NEXT_FNC(__poll_chk)
# define _real_pselect        NEXT_FNC(pselect)

# ifdef HAVE_SYS_EPOLL_H
#  define _real_epoll_create  NEXT_FNC(epoll_create)
#  define _real_epoll_create1 NEXT_FNC(epoll_create1)
#  define _real_epoll_ctl     NEXT_FNC(epoll_ctl)
#  define _real_epoll_wait    NEXT_FNC(epoll_wait)
#  define _real_epoll_pwait   NEXT_FNC(epoll_pwait)
# endif // ifdef HAVE_SYS_EPOLL_H

# ifdef HAVE_SYS_EVENTFD_H
#  define _real_eventfd NEXT_FNC(eventfd)
# endif // ifdef HAVE_SYS_EVENTFD_H

# ifdef HAVE_SYS_SIGNALFD_H
#  define _real_signalfd NEXT_FNC(signalfd)
# endif // ifdef HAVE_SYS_SIGNALFD_H

# ifdef HAVE_SYS_INOTIFY_H
#  define _real_inotify_init      NEXT_FNC(inotify_init)
#  define _real_inotify_init1     NEXT_FNC(inotify_init1)
#  define _real_inotify_add_watch NEXT_FNC(inotify_add_watch)
#  define _real_inotify_rm_watch  NEXT_FNC(inotify_rm_watch)
# endif // ifdef HAVE_SYS_INOTIFY_H
#endif // EVENT_WRAPPERS_H
