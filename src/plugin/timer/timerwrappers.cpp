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
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#include "timerlist.h"
#include "wrapperlock.h"
#include "../pid/glibc_pthread.h"
#include "../../constants.h"

using namespace dmtcp;

static bool
timerPluginEnabled()
{
  const char *disableAllPlugins = getenv(ENV_VAR_DISABLE_ALL_PLUGINS);
  return disableAllPlugins == NULL || strcmp(disableAllPlugins, "1") != 0;
}

typedef pid_t (*PidTranslateFn)(pid_t pid);

static PidTranslateFn
timerPidVirtualToRealFn()
{
  static PidTranslateFn nextFn = NULL;
  static bool nextFnResolved = false;

  if (dmtcp_virtual_to_real_pid != NULL) {
    return dmtcp_virtual_to_real_pid;
  }

  if (!nextFnResolved && dmtcp_dlsym != NULL) {
    nextFn = (PidTranslateFn)dmtcp_dlsym(RTLD_NEXT,
                                         "dmtcp_virtual_to_real_pid");
    nextFnResolved = true;
  }

  return nextFn;
}

LIB_PRIVATE pid_t
dmtcp_timer_virtual_to_real_pid(pid_t pid)
{
  if (!timerPluginEnabled()) {
    return pid;
  }

  PidTranslateFn fn = timerPidVirtualToRealFn();
  return fn != NULL ? fn(pid) : pid;
}

LIB_PRIVATE int
dmtcp_timer_pthread_getcpuclockid(pthread_t thread, clockid_t *clock_id)
{
#ifdef USE_VIRTUAL_TID_LIBC_STRUCT_PTHREAD
  pid_t virtualTid = dmtcp_pthread_get_tid(thread);
  pid_t realTid = dmtcp_timer_virtual_to_real_pid(virtualTid);
  *clock_id = make_thread_cpuclock(realTid, CPUCLOCK_SCHED);
  return 0;
#else
  return _real_pthread_getcpuclockid(thread, clock_id);
#endif
}

extern "C" int
timer_create(clockid_t clockid, struct sigevent *sevp, timer_t *timerid)
{
  if (!timerPluginEnabled()) {
    return _real_timer_create(clockid, sevp, timerid);
  }

  struct sigevent sevOut;
  struct sigevent pidTranslatedSev;
  struct sigevent *sevpForCreate = sevp;
  struct sigevent *sevpForSave = sevp;
  timer_t realId;
  timer_t virtId;
  int ret;

  DMTCP_PLUGIN_DISABLE_CKPT();

  // Note that clockid can be for a system-wide clock with no virtual id.
  // An example is CLOCK_REALTIME.  By luck, VIRTUAL_TO_REAL_CLOCK_ID()
  // return its argument if no virtual id is found.
  // See:  virtualidtable.h: dmtcp::VirtualIdTable::virtualToReal(...)
  clockid_t realClockId = VIRTUAL_TO_REAL_CLOCK_ID(clockid);
  if (sevpForCreate != NULL &&
      sevpForCreate->sigev_notify == SIGEV_THREAD_ID) {
    pidTranslatedSev = *sevpForCreate;
    pidTranslatedSev._sigev_un._tid =
      dmtcp_timer_virtual_to_real_pid(pidTranslatedSev._sigev_un._tid);
    sevpForCreate = &pidTranslatedSev;
  }

  if (sevpForCreate != NULL && sevpForCreate->sigev_notify == SIGEV_THREAD) {
    ret = timer_create_sigev_thread(realClockId, sevpForCreate, &realId,
                                    &sevOut);
    sevpForSave = &sevOut;
  } else {
    ret = _real_timer_create(realClockId, sevpForCreate, &realId);
  }
  if (ret != -1 && timerid != NULL) {
    virtId = TimerList::instance().on_timer_create(realId, clockid,
                                                   sevpForSave);
    JTRACE("Creating new timer") (clockid) (realClockId) (realId) (virtId);
    *timerid = virtId;
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

extern "C" int
timer_delete(timer_t timerid)
{
  if (!timerPluginEnabled()) {
    return _real_timer_delete(timerid);
  }

  DMTCP_PLUGIN_DISABLE_CKPT();
  timer_t realId = VIRTUAL_TO_REAL_TIMER_ID(timerid);
  int ret = _real_timer_delete(realId);
  if (ret != -1) {
    TimerList::instance().on_timer_delete(timerid);
    JTRACE("Deleted timer") (timerid);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

extern "C" int
timer_settime(timer_t timerid,
              int flags,
              const struct itimerspec *new_value,
              struct itimerspec *old_value)
{
  if (!timerPluginEnabled()) {
    return _real_timer_settime(timerid, flags, new_value, old_value);
  }

  DMTCP_PLUGIN_DISABLE_CKPT();
  timer_t realId = VIRTUAL_TO_REAL_TIMER_ID(timerid);
  int ret = _real_timer_settime(realId, flags, new_value, old_value);
  if (ret != -1) {
    TimerList::instance().on_timer_settime(timerid, flags, new_value);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

extern "C" int
timer_gettime(timer_t timerid, struct itimerspec *curr_value)
{
  if (!timerPluginEnabled()) {
    return _real_timer_gettime(timerid, curr_value);
  }

  DMTCP_PLUGIN_DISABLE_CKPT();
  timer_t realId = VIRTUAL_TO_REAL_TIMER_ID(timerid);
  int ret = _real_timer_gettime(realId, curr_value);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

extern "C" int
timer_getoverrun(timer_t timerid)
{
  if (!timerPluginEnabled()) {
    return _real_timer_getoverrun(timerid);
  }

  DMTCP_PLUGIN_DISABLE_CKPT();
  timer_t realId = VIRTUAL_TO_REAL_TIMER_ID(timerid);
  int ret = _real_timer_getoverrun(realId);

  // If there was some overrun at checkpoint time, add it to the current value
  ret += TimerList::instance().getoverrun(timerid);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

extern "C" int
clock_getcpuclockid(pid_t pid, clockid_t *clock_id)
{
  if (!timerPluginEnabled()) {
    return _real_clock_getcpuclockid(pid, clock_id);
  }

  clockid_t realId;
  pid_t realPid = dmtcp_timer_virtual_to_real_pid(pid);

  DMTCP_PLUGIN_DISABLE_CKPT();
  int ret = _real_clock_getcpuclockid(realPid, &realId);
  if (ret == 0) {
    *clock_id = REAL_TO_VIRTUAL_CLOCK_ID(pid, realId);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

extern "C" int
pthread_getcpuclockid(pthread_t thread, clockid_t *clock_id)
{
  if (!timerPluginEnabled()) {
    return _real_pthread_getcpuclockid(thread, clock_id);
  }

  // We need to acquire an exclusive lock here because the corresponding Pid
  // plugin translation requires an exclusive lock.
  WrapperLockExcl wrapperLock;
  clockid_t realId;
  int ret = dmtcp_timer_pthread_getcpuclockid(thread, &realId);
  if (ret == 0) {
    *clock_id = TimerList::instance().on_pthread_getcpuclockid(thread, realId);
  }
  return ret;
}

extern "C" int
clock_getres(clockid_t clk_id, struct timespec *res)
{
  if (!timerPluginEnabled()) {
    return _real_clock_getres(clk_id, res);
  }

  DMTCP_PLUGIN_DISABLE_CKPT();

  // See comment on VIRTUAL_TO_REAL_CLOCK_ID() in timer_create()
  clockid_t realId = VIRTUAL_TO_REAL_CLOCK_ID(clk_id);
  int ret = _real_clock_getres(realId, res);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

extern "C" int
clock_gettime(clockid_t clk_id, struct timespec *tp)
{
  if (!timerPluginEnabled()) {
    return _real_clock_gettime(clk_id, tp);
  }

  DMTCP_PLUGIN_DISABLE_CKPT();

  // See comment on VIRTUAL_TO_REAL_CLOCK_ID() in timer_create()
  clockid_t realId = VIRTUAL_TO_REAL_CLOCK_ID(clk_id);
  int ret = _real_clock_gettime(realId, tp);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

extern "C" int
clock_settime(clockid_t clk_id, const struct timespec *tp)
{
  if (!timerPluginEnabled()) {
    return _real_clock_settime(clk_id, tp);
  }

  DMTCP_PLUGIN_DISABLE_CKPT();

  // See comment on VIRTUAL_TO_REAL_CLOCK_ID() in timer_create()
  clockid_t realId = VIRTUAL_TO_REAL_CLOCK_ID(clk_id);
  int ret = _real_clock_settime(realId, tp);
  DMTCP_PLUGIN_ENABLE_CKPT();
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
  if (!timerPluginEnabled()) {
    return _real_clock_nanosleep(clock_id, flags, request, remain);
  }

  DMTCP_PLUGIN_DISABLE_CKPT();

  // See comment on VIRTUAL_TO_REAL_CLOCK_ID() in timer_create()
  clockid_t realId = VIRTUAL_TO_REAL_CLOCK_ID(clock_id);
  int ret = _real_clock_nanosleep(realId, flags, request, remain);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}
#endif // ifdef ENABLE_CLOCK_NANOSLEEP
