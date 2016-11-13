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
#ifndef TIMER_LIST_H
#define TIMER_LIST_H

#include <signal.h>
#include <time.h>
#include "jassert.h"
#include "jconvert.h"
#include "dmtcpalloc.h"
#include "virtualidtable.h"

# define REAL_TO_VIRTUAL_TIMER_ID(id) \
  TimerList::instance().realToVirtualTimerId(id)
# define VIRTUAL_TO_REAL_TIMER_ID(id) \
  TimerList::instance().virtualToRealTimerId(id)

/*
#define REAL_TO_VIRTUAL_CLOCK_ID(id) \
  TimerList::instance().realToVirtualClockId(id)
*/

# define REAL_TO_VIRTUAL_CLOCK_ID(pid, realId) \
  TimerList::instance().on_clock_getcpuclockid(pid, realId)

# define VIRTUAL_TO_REAL_CLOCK_ID(virtId) \
  TimerList::instance().virtualToRealClockId(virtId)

namespace dmtcp
{
typedef struct TimerInfo {
  clockid_t clockid;
  struct sigevent sevp;
  bool sevp_null;
  int flags;
  struct itimerspec initial_timerspec;
  struct itimerspec curr_timerspec;
  int overrun;
} TimerInfo;


class TimerList
{
  public:
# ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
# endif // ifdef JALIB_ALLOCATOR

    TimerList()
      : _timerVirtIdTable("Timer", (timer_t)NULL, 999999)
      , _clockVirtIdTable("Clock", (clockid_t)(unsigned)getpid()) {}

    static TimerList &instance();

    void resetOnFork();
    void preCheckpoint();
    void postRestart();

    timer_t virtualToRealTimerId(timer_t virtId)
    {
      return _timerVirtIdTable.virtualToReal(virtId);
    }

    timer_t realToVirtualTimerId(timer_t realId)
    {
      return _timerVirtIdTable.realToVirtual(realId);
    }

    clockid_t virtualToRealClockId(clockid_t virtId)
    {
      return _clockVirtIdTable.virtualToReal(virtId);
    }

    // timer_t  realToVirtualClockId(timer_t realId) {
    // if (_clockVirtIdTable.realIdExists(realId)) {
    // return _clockVirtIdTable.realToVirtual(realId);
    // } else {
    // return -1;
    // }
    // }

    int getoverrun(timer_t id);
    timer_t on_timer_create(timer_t realId,
                            clockid_t clockid,
                            struct sigevent *sevp);
    void on_timer_delete(timer_t timerid);
    void on_timer_settime(timer_t timerid,
                          int flags,
                          const struct itimerspec *new_value);
    clockid_t on_clock_getcpuclockid(pid_t pid, clockid_t clock_id);
    clockid_t on_pthread_getcpuclockid(pthread_t thread, clockid_t clock_id);

  private:
    void removeStaleClockIds();

    map<timer_t, TimerInfo>_timerInfo;
    map<timer_t, TimerInfo>::iterator _iter;
    map<clockid_t, pid_t>_clockPidList;
    map<clockid_t, pthread_t>_clockPthreadList;

    VirtualIdTable<timer_t>_timerVirtIdTable;
    VirtualIdTable<clockid_t>_clockVirtIdTable;
};
}
#endif // ifndef TIMER_LIST_H
