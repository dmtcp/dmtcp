/* FILE: sched_wrappers.cpp
 * AUTHOR: Rohan Garg
 * EMAIL: rohgarg@ccs.neu.edu
 * Copyright (C) 2015 Rohan Garg
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sched.h>

#include "dmtcp.h"
#include "pidwrappers.h"
#include "virtualpidtable.h"

int
sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *mask)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int result = -1;
  pid_t real_pid = 0;
  if (pid != 0) {
    real_pid = VIRTUAL_TO_REAL_PID(pid);
  }
  result = _real_sched_setaffinity(real_pid, cpusetsize, mask);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return result;
}

int
sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int result = -1;
  pid_t real_pid = 0;
  if (pid != 0) {
    real_pid = VIRTUAL_TO_REAL_PID(pid);
  }
  result = _real_sched_getaffinity(real_pid, cpusetsize, mask);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return result;
}

int
sched_setscheduler(pid_t pid, int policy, const struct sched_param *param)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int result = -1;
  pid_t real_pid = 0;
  if (pid != 0) {
    real_pid = VIRTUAL_TO_REAL_PID(pid);
  }
  result = _real_sched_setscheduler(real_pid, policy, param);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return result;
}

int
sched_getscheduler(pid_t pid)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int result = -1;
  pid_t real_pid = 0;
  if (pid != 0) {
    real_pid = VIRTUAL_TO_REAL_PID(pid);
  }
  result = _real_sched_getscheduler(real_pid);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return result;
}

int
sched_setparam(pid_t pid, const struct sched_param *param)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int result = -1;
  pid_t real_pid = 0;
  if (pid != 0) {
    real_pid = VIRTUAL_TO_REAL_PID(pid);
  }
  result = _real_sched_setparam(real_pid, param);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return result;
}

int
sched_getparam(pid_t pid, struct sched_param *param)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int result = -1;
  pid_t real_pid = 0;
  if (pid != 0) {
    real_pid = VIRTUAL_TO_REAL_PID(pid);
  }
  result = _real_sched_getparam(real_pid, param);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return result;
}

#if 0

// TODO: Add check for the below two functions in configure
int
sched_setattr(pid_t pid, const struct sched_attr *attr, unsigned int flags)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int result = -1;
  pid_t real_pid = 0;
  if (pid != 0) {
    real_pid = VIRTUAL_TO_REAL_PID(pid);
  }
  result = _real_sched_setattr(real_pid, attr, flags);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return result;
}

int
sched_getattr(pid_t pid,
              const struct sched_attr *attr,
              unsigned int size,
              unsigned int flags)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int result = -1;
  pid_t real_pid = 0;
  if (pid != 0) {
    real_pid = VIRTUAL_TO_REAL_PID(pid);
  }
  result = _real_sched_getattr(real_pid, attr, size, flags);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return result;
}
#endif // if 0
