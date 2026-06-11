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

#include "timerwrappers.h"
#include "pluginmanager.h"
#include "plugin/pid/pidhelpers.h"
#include "timerlist.h"
#include "wrapperlock.h"
#include "util_assert.h"

using namespace dmtcp;

static void
translateSigevThreadIdToReal(struct sigevent *sev)
{
  if (sev != NULL && sev->sigev_notify == SIGEV_THREAD_ID) {
    sev->_sigev_un._tid =
      dmtcp_pid_virtual_to_real(sev->_sigev_un._tid);
  }
}

extern "C" int
timer_create(clockid_t clockid, struct sigevent *sevp, timer_t *timerid)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_TIMER)) {
    struct sigevent sevPid;
    struct sigevent *sevIn = sevp;
    if (sevp != NULL) {
      sevPid = *sevp;
      translateSigevThreadIdToReal(&sevPid);
      sevIn = &sevPid;
    }
    return _real_timer_create(clockid, sevIn, timerid);
  }

  struct sigevent sevPid;
  struct sigevent sevOut;
  struct sigevent *sevIn = sevp;
  timer_t realId;
  timer_t virtId;
  int ret;

  WrapperLock wrapperLock;

  // Note that clockid can be for a system-wide clock with no virtual id.
  // An example is CLOCK_REALTIME.  By luck, VIRTUAL_TO_REAL_CLOCK_ID()
  // return its argument if no virtual id is found.
  // See:  virtualidtable.h: dmtcp::VirtualIdTable::virtualToReal(...)
  clockid_t realClockId = VIRTUAL_TO_REAL_CLOCK_ID(clockid);
  if (sevp != NULL) {
    sevPid = *sevp;
    translateSigevThreadIdToReal(&sevPid);
    sevIn = &sevPid;
  }

  if (sevIn != NULL && sevIn->sigev_notify == SIGEV_THREAD) {
    ret = timer_create_sigev_thread(realClockId, sevIn, &realId, &sevOut);
    sevIn = &sevOut;
  } else {
    ret = _real_timer_create(realClockId, sevIn, &realId);
  }
  if (ret != -1 && timerid != NULL) {
    virtId = TimerList::instance().on_timer_create(realId, clockid, sevIn);
    TRACE("Creating new timer (clockid = {};) (realClockId = {};) "
          "(realId = {};) (virtId = {};)",
          clockid, realClockId, realId, virtId);
    *timerid = virtId;
  }
  return ret;
}

extern "C" int
timer_delete(timer_t timerid)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_TIMER)) {
    return _real_timer_delete(timerid);
  }

  WrapperLock wrapperLock;
  timer_t realId = VIRTUAL_TO_REAL_TIMER_ID(timerid);
  int ret = _real_timer_delete(realId);
  if (ret != -1) {
    TimerList::instance().on_timer_delete(timerid);
    TRACE("Deleted timer (timerid = {};)", timerid);
  }
  return ret;
}

extern "C" int
timer_settime(timer_t timerid,
              int flags,
              const struct itimerspec *new_value,
              struct itimerspec *old_value)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_TIMER)) {
    return _real_timer_settime(timerid, flags, new_value, old_value);
  }

  WrapperLock wrapperLock;
  timer_t realId = VIRTUAL_TO_REAL_TIMER_ID(timerid);
  int ret = _real_timer_settime(realId, flags, new_value, old_value);
  if (ret != -1) {
    TimerList::instance().on_timer_settime(timerid, flags, new_value);
  }
  return ret;
}

extern "C" int
timer_gettime(timer_t timerid, struct itimerspec *curr_value)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_TIMER)) {
    return _real_timer_gettime(timerid, curr_value);
  }

  WrapperLock wrapperLock;
  timer_t realId = VIRTUAL_TO_REAL_TIMER_ID(timerid);
  int ret = _real_timer_gettime(realId, curr_value);
  return ret;
}

extern "C" int
timer_getoverrun(timer_t timerid)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_TIMER)) {
    return _real_timer_getoverrun(timerid);
  }

  WrapperLock wrapperLock;
  timer_t realId = VIRTUAL_TO_REAL_TIMER_ID(timerid);
  int ret = _real_timer_getoverrun(realId);

  // If there was some overrun at checkpoint time, add it to the current value
  ret += TimerList::instance().getoverrun(timerid);
  return ret;
}

extern "C" int
clock_getcpuclockid(pid_t pid, clockid_t *clock_id)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_TIMER)) {
    return _real_clock_getcpuclockid(dmtcp_pid_virtual_to_real(pid),
                                     clock_id);
  }

  clockid_t realId;

  WrapperLock wrapperLock;
  pid_t realPid = dmtcp_pid_virtual_to_real(pid);
  if (clock_id == NULL) {
    return _real_clock_getcpuclockid(realPid, clock_id);
  }
  int ret = _real_clock_getcpuclockid(realPid, &realId);
  if (ret == 0) {
    *clock_id = REAL_TO_VIRTUAL_CLOCK_ID(pid, realId);
  }
  return ret;
}

extern "C" int
pthread_getcpuclockid(pthread_t thread, clockid_t *clock_id)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_TIMER)) {
    return _real_pthread_getcpuclockid(thread, clock_id);
  }

  // We need to acquire an exclusive lock here because the corresponding Pid
  // plugin wrapper requires an exclusive lock.
  WrapperLockExcl wrapperLock;
  if (clock_id == NULL) {
    return _real_pthread_getcpuclockid(thread, clock_id);
  }
  clockid_t realId;
  int ret = _real_pthread_getcpuclockid(thread, &realId);
  if (ret == 0) {
    *clock_id = TimerList::instance().on_pthread_getcpuclockid(thread, realId);
  }
  return ret;
}

extern "C" int
clock_getres(clockid_t clk_id, struct timespec *res)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_TIMER)) {
    return _real_clock_getres(clk_id, res);
  }

  WrapperLock wrapperLock;

  // See comment on VIRTUAL_TO_REAL_CLOCK_ID() in timer_create()
  clockid_t realId = VIRTUAL_TO_REAL_CLOCK_ID(clk_id);
  int ret = _real_clock_getres(realId, res);
  return ret;
}

extern "C" int
clock_gettime(clockid_t clk_id, struct timespec *tp)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_TIMER)) {
    return _real_clock_gettime(clk_id, tp);
  }

  WrapperLock wrapperLock;

  // See comment on VIRTUAL_TO_REAL_CLOCK_ID() in timer_create()
  clockid_t realId = VIRTUAL_TO_REAL_CLOCK_ID(clk_id);
  int ret = _real_clock_gettime(realId, tp);
  return ret;
}

extern "C" int
clock_settime(clockid_t clk_id, const struct timespec *tp)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_TIMER)) {
    return _real_clock_settime(clk_id, tp);
  }

  WrapperLock wrapperLock;

  // See comment on VIRTUAL_TO_REAL_CLOCK_ID() in timer_create()
  clockid_t realId = VIRTUAL_TO_REAL_CLOCK_ID(clk_id);
  int ret = _real_clock_settime(realId, tp);
  return ret;
}

// FIXME: The following wrapper disables ckpt for the entire duration of
// clock_nanosleep. This is dangerous and can lead to a situation where
// checkpointing never happens. Disabling the wrapper for now.
#ifdef ENABLE_CLOCK_NANOSLEEP
extern "C" int
clock_nanosleep(clockid_t clock_id,
                int flags,
                const struct timespec *request,
                struct timespec *remain)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_TIMER)) {
    return _real_clock_nanosleep(clock_id, flags, request, remain);
  }

  WrapperLock wrapperLock;

  // See comment on VIRTUAL_TO_REAL_CLOCK_ID() in timer_create()
  clockid_t realId = VIRTUAL_TO_REAL_CLOCK_ID(clock_id);
  int ret = _real_clock_nanosleep(realId, flags, request, remain);
  return ret;
}
#endif // ifdef ENABLE_CLOCK_NANOSLEEP
