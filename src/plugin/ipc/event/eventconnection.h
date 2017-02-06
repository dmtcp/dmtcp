/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, Gene Cooperman,    *
 *                                                           and Rohan Garg *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu, and         *
 *                                                      rohgarg@ccs.neu.edu *
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
#ifndef EVENTCONNECTION_H
#define EVENTCONNECTION_H

// THESE INCLUDES ARE IN RANDOM ORDER.  LET'S CLEAN IT UP AFTER RELEASE. - Gene
#include <signal.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "connection.h"
#include "connectionlist.h"

#ifdef HAVE_SYS_INOTIFY_H
#include <sys/inotify.h>
#endif  // ifdef HAVE_SYS_INOTIFY_H

#ifdef HAVE_SYS_EPOLL_H
#include <sys/epoll.h>
#else  // ifdef HAVE_SYS_EPOLL_H

/* KEEP THIS IN SYNC WITH syscallwrappers.h */
#  ifndef _SYS_EPOLL_H
#   define _SYS_EPOLL_H 1
struct epoll_event { int dummy; };

/* Valid opcodes("op" parameter) to issue to epoll_ctl().  */
#   define EPOLL_CTL_ADD 1 /* Add a file descriptor to the interface.  */
#   define EPOLL_CTL_DEL 2 /* Remove a file descriptor from the interface.  */
#   define EPOLL_CTL_MOD 3 /* Change file descriptor epoll_event structure.  */
#  endif // ifndef _SYS_EPOLL_H
# endif // ifdef HAVE_SYS_EPOLL_H
# ifdef HAVE_SYS_EVENTFD_H
#  include <sys/eventfd.h>
# else // ifdef HAVE_SYS_EVENTFD_H
enum { EFD_SEMAPHORE = 1 };
# endif // ifdef HAVE_SYS_EVENTFD_H
# ifdef HAVE_SYS_SIGNALFD_H
#  include <sys/signalfd.h>
# else // ifdef HAVE_SYS_SIGNALFD_H
#  include <stdint.h>
struct signalfd_siginfo { uint32_t ssi_signo; int dummy; };
# endif // ifdef HAVE_SYS_SIGNALFD_H

namespace dmtcp
{
# ifdef HAVE_SYS_EPOLL_H
class EpollConnection : public Connection
{
  public:
    EpollConnection(int size = 0, int flags = 0)
      : Connection(EPOLL),
      _size(size),
      _flags(flags)
    {
      JTRACE("new epoll connection created");
    }

    virtual void drain();
    virtual void refill(bool isRestart);
    virtual void postRestart();
    virtual void serializeSubClass(jalib::JBinarySerializer &o);

    virtual string str() { return "EPOLL-FD: <Not-a-File>"; }

    void onCTL(int op, int fd, struct epoll_event *event);

  private:
    EpollConnection &asEpoll();
    int64_t _size;       // for epoll_create();
    int64_t _flags;      // for epoll_create1();
    map<int, struct epoll_event>_fdToEvent;
};
# endif // ifdef HAVE_SYS_EPOLL_H

# ifdef HAVE_SYS_EVENTFD_H
class EventFdConnection : public Connection
{
  public:
    inline EventFdConnection(unsigned int initval, int flags)
      : Connection(EVENTFD),
      _initval(initval),
      _flags(flags)
    {
      JTRACE("new eventfd connection created");
    }

    virtual void drain();
    virtual void refill(bool isRestart);
    virtual void postRestart();
    virtual void serializeSubClass(jalib::JBinarySerializer &o);

    virtual string str() { return "EVENT-FD: <Not-a-File>"; }

  private:
    uint64_t _initval;   // initial counter value
    int64_t _flags;   // flags
};
# endif // ifdef HAVE_SYS_EVENTFD_H

# ifdef HAVE_SYS_SIGNALFD_H
class SignalFdConnection : public Connection
{
  public:
    inline SignalFdConnection(int signalfd, const sigset_t *mask, int flags)
      : Connection(SIGNALFD),
      _flags(flags)
    {
      if (mask != NULL) {
        _mask = *mask;
      } else {
        sigemptyset(&_mask);
      }
    }

    virtual void drain();
    virtual void refill(bool isRestart);
    virtual void postRestart();
    virtual void serializeSubClass(jalib::JBinarySerializer &o);

    virtual string str() { return "SIGNAL-FD: <Not-a-File>"; }

  private:
    int64_t _flags;    // flags
    sigset_t _mask;   // mask for signals
    struct signalfd_siginfo _fdsi;
};
# endif // ifdef HAVE_SYS_SIGNALFD_H

# ifdef HAVE_SYS_INOTIFY_H
#  ifdef DMTCP_USE_INOTIFY
class InotifyConnection : public Connection
{
  public:
    enum InotifyState {
      INOTIFY_INVALID = INOTIFY,
      INOTIFY_CREATE,
      INOTIFY_ADD_WAIT
    };

    inline InotifyConnection(int flags)
      : Connection(INOTIFY),
      _flags(flags),
      _state(INOTIFY_CREATE)
    {
      JTRACE("new inotify connection created");
    }

    int inotifyState() const { return (int)_state; }

    InotifyConnection &asInotify();

    virtual void drain();
    virtual void refill(bool isRestart);
    virtual void postRestart();
    virtual void serializeSubClass(jalib::JBinarySerializer &o);

    virtual string str() { return "INOTIFY-FD: <Not-a-File>"; }

    void map_inotify_fd_to_wd(int fd, int wd);
    void add_watch_descriptors(int wd,
                               int fd,
                               const char *pathname,
                               uint32_t mask);
    void remove_watch_descriptors(int wd);

  private:
    int64_t _flags;    // flags
    int64_t _state;    // current state of INOTIFY
};
#  endif // ifdef DMTCP_USE_INOTIFY
# endif // ifdef HAVE_SYS_INOTIFY_H
}
#endif // ifndef EVENTCONNECTION_H
