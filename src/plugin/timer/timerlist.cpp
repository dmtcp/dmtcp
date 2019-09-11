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

#include "timerlist.h"
#include <time.h>
#include "config.h"
#include "dmtcp.h"
#include "timerwrappers.h"

using namespace dmtcp;

static TimerList *_timerlist = NULL;

static DmtcpMutex timerLock = DMTCP_MUTEX_INITIALIZER;
static void
_do_lock_tbl()
{
  JASSERT(DmtcpMutexLock(&timerLock) == 0) (JASSERT_ERRNO);
}

static void
_do_unlock_tbl()
{
  JASSERT(DmtcpMutexUnlock(&timerLock) == 0) (JASSERT_ERRNO);
}

static void
preCheckpoint()
{
  TimerList::instance().preCheckpoint();
}

static void
postRestart()
{
  TimerList::instance().postRestart();
}

static void
timer_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  if (_timerlist != NULL) {
    switch (event) {
    case DMTCP_EVENT_ATFORK_CHILD:
      TimerList::instance().resetOnFork();
      break;

  case DMTCP_EVENT_PRESUSPEND:
    break;

  case DMTCP_EVENT_PRECHECKPOINT:
    preCheckpoint();
    break;

  case DMTCP_EVENT_RESUME:
    break;

  case DMTCP_EVENT_RESTART:
    postRestart();
    break;

    default:
      break;
    }
  }
}

DmtcpPluginDescriptor_t timerPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "timer",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Timer plugin",
  timer_event_hook
};

DMTCP_DECL_PLUGIN(timerPlugin);


/*
 *
 */
TimerList&
TimerList::instance()
{
  if (_timerlist == NULL) {
    _timerlist = new TimerList();
  }
  return *_timerlist;
}

void
TimerList::removeStaleClockIds()
{
  // remove stale clockids
  vector<clockid_t>staleClockIds;
  map<clockid_t, pid_t>::iterator clockPidListIter;
  for (clockPidListIter = _clockPidList.begin();
       clockPidListIter != _clockPidList.end();
       clockPidListIter++) {
    pid_t pid = clockPidListIter->second;
    clockid_t realId = VIRTUAL_TO_REAL_CLOCK_ID(clockPidListIter->first);
    clockid_t clockid;
    if (_real_clock_getcpuclockid(pid, &clockid) != 0 || clockid != realId) {
      staleClockIds.push_back(clockPidListIter->first);
    }
  }
  for (size_t i = 0; i < staleClockIds.size(); i++) {
    JTRACE("Removing stale clock") (staleClockIds[i]);
    _clockPidList.erase(staleClockIds[i]);
  }
  staleClockIds.clear();

  map<clockid_t, pthread_t>::iterator clockPthreadListIter;
  for (clockPthreadListIter = _clockPthreadList.begin();
       clockPthreadListIter != _clockPthreadList.end();
       clockPthreadListIter++) {
    pthread_t pth = clockPthreadListIter->second;
    clockid_t realId = VIRTUAL_TO_REAL_CLOCK_ID(clockPthreadListIter->first);
    clockid_t clockid;
    if (_real_pthread_getcpuclockid(pth, &clockid) != 0 || clockid != realId) {
      staleClockIds.push_back(clockPthreadListIter->first);
    }
  }
  for (size_t i = 0; i < staleClockIds.size(); i++) {
    JNOTE("Removing stale clock") (staleClockIds[i]);
    _clockPthreadList.erase(staleClockIds[i]);
  }
}

void
TimerList::resetOnFork()
{
  _timerInfo.clear();

  // _clockPidList.clear();
  // _clockPthreadList.clear();
  _timerVirtIdTable.clear();
  DmtcpMutexInit(&timerLock, DMTCP_MUTEX_NORMAL);
  _clockVirtIdTable.resetOnFork((clockid_t)(unsigned)getpid());
}

void
TimerList::preCheckpoint()
{
  removeStaleClockIds();
  for (_iter = _timerInfo.begin(); _iter != _timerInfo.end(); _iter++) {
    timer_t virtId = _iter->first;
    timer_t realId = VIRTUAL_TO_REAL_TIMER_ID(virtId);
    TimerInfo &tinfo = _iter->second;
    JASSERT(_real_timer_gettime(realId, &tinfo.curr_timerspec) == 0)
      (virtId) (realId) (JASSERT_ERRNO);
    tinfo.overrun = _real_timer_getoverrun(realId);
  }
}

void
TimerList::postRestart()
{
  // Refresh clockids
  map<clockid_t, pid_t>::iterator it1;
  for (it1 = _clockPidList.begin(); it1 != _clockPidList.end(); it1++) {
    pid_t pid = it1->second;
    clockid_t virtId = it1->first;
    clockid_t realId;
    JASSERT(_real_clock_getcpuclockid(pid, &realId) == 0) (pid) (JASSERT_ERRNO);
    _clockVirtIdTable.updateMapping(virtId, realId);
  }

  map<clockid_t, pthread_t>::iterator it2;
  for (it2 = _clockPthreadList.begin(); it2 != _clockPthreadList.end(); it2++) {
    pthread_t pth = it2->second;
    clockid_t virtId = it2->first;
    clockid_t realId;
    JASSERT(_real_pthread_getcpuclockid(pth, &realId) == 0) (pth)
      (JASSERT_ERRNO);
    _clockVirtIdTable.updateMapping(virtId, realId);
  }

  // Refresh timers
  for (_iter = _timerInfo.begin(); _iter != _timerInfo.end(); _iter++) {
    timer_t realId;
    timer_t virtId = _iter->first;
    struct sigevent *sevp = NULL;
    TimerInfo &tinfo = _iter->second;
    clockid_t clockid = VIRTUAL_TO_REAL_CLOCK_ID(tinfo.clockid);
    if (!tinfo.sevp_null) {
      sevp = &tinfo.sevp;
    }
    JASSERT(_real_timer_create(clockid, sevp, &realId) == 0)
      (virtId) (JASSERT_ERRNO);
    _timerVirtIdTable.updateMapping(virtId, realId);
    if (tinfo.curr_timerspec.it_value.tv_sec != 0 ||
        tinfo.curr_timerspec.it_value.tv_nsec != 0) {
      struct itimerspec tspec;
      if (tinfo.flags & TIMER_ABSTIME) {
        // The timer should expire when the clock time equals the time
        // specified in initial_timerspec.
        // FIXME: For clocks measugin CPU time for processes and threads, such
        // as CLOCK_PROCESS_CPUTIME_ID and CLOCK_THREAD_CPUTIME_ID, we need to
        // adjust the value of initial_timerspec to allow
        // the changes on the time on these clocks after restart.
        tspec = tinfo.initial_timerspec;
      } else {
        tspec = tinfo.curr_timerspec;
      }
      JASSERT(_real_timer_settime(realId, tinfo.flags, &tspec, NULL) == 0)
        (virtId) (JASSERT_ERRNO);
      JTRACE("Restoring timer") (realId) (virtId);
    }
  }
}

int
TimerList::getoverrun(timer_t id)
{
  _do_lock_tbl();
  JASSERT(_timerInfo.find(id) != _timerInfo.end());
  int ret = _timerInfo[id].overrun;
  _timerInfo[id].overrun = 0;
  _do_unlock_tbl();
  return ret;
}

timer_t
TimerList::on_timer_create(timer_t realId,
                           clockid_t clockid,
                           struct sigevent *sevp)
{
  TimerInfo tinfo;
  timer_t virtId;

  _do_lock_tbl();
  JASSERT(!_timerVirtIdTable.realIdExists(realId)) (realId);

  JASSERT(_timerVirtIdTable.getNewVirtualId(&virtId));
  _timerVirtIdTable.updateMapping(virtId, realId);

  memset(&tinfo, 0, sizeof(tinfo));
  tinfo.clockid = clockid;
  if (sevp == NULL) {
    tinfo.sevp_null = true;
  } else {
    tinfo.sevp_null = false;
    tinfo.sevp = *sevp;
  }

  _timerInfo[virtId] = tinfo;
  _do_unlock_tbl();
  return virtId;
}

void
TimerList::on_timer_delete(timer_t timerid)
{
  _do_lock_tbl();
  _timerVirtIdTable.erase(timerid);
  JASSERT(_timerInfo.find(timerid) != _timerInfo.end());
  _timerInfo.erase(timerid);
  _do_unlock_tbl();
}

void
TimerList::on_timer_settime(timer_t timerid,
                            int flags,
                            const struct itimerspec *new_value)
{
  _do_lock_tbl();
  JASSERT(_timerInfo.find(timerid) != _timerInfo.end());
  _timerInfo[timerid].flags = flags;
  _timerInfo[timerid].initial_timerspec = *new_value;
  _do_unlock_tbl();
}

clockid_t
TimerList::on_clock_getcpuclockid(pid_t pid, clockid_t realId)
{
  _do_lock_tbl();
  if (_clockVirtIdTable.size() > 800) {
    removeStaleClockIds();
  }
  clockid_t virtId;
  JASSERT(_clockVirtIdTable.getNewVirtualId(&virtId));
  _clockPidList[virtId] = pid;
  _clockVirtIdTable.updateMapping(virtId, realId);
  _do_unlock_tbl();
  return virtId;
}

clockid_t
TimerList::on_pthread_getcpuclockid(pthread_t thread, clockid_t realId)
{
  _do_lock_tbl();
  _clockPthreadList[realId] = thread;
  if (_clockVirtIdTable.size() > 800) {
    removeStaleClockIds();
  }
  clockid_t virtId = -1;
  JASSERT(_clockVirtIdTable.getNewVirtualId(&virtId));
  _clockVirtIdTable.updateMapping(virtId, realId);
  _do_unlock_tbl();
  return virtId;
}
