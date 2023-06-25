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
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "dmtcp.h"
#include "glibc_pthread.h"
#include "pid.h"
#include "pidwrappers.h"
#include "procselfmaps.h"
#include "util.h"
#include "virtualpidtable.h"
#include "wrapperlock.h"

using namespace dmtcp;

#ifdef USE_VIRTUAL_TID_LIBC_STRUCT_PTHREAD

extern "C" int
pthread_getaffinity_np (pthread_t th, size_t cpusetsize, cpu_set_t *cpuset)
{
  return sched_getaffinity(dmtcp_pthread_get_tid(th), cpusetsize, cpuset);
}

extern "C" int
pthread_setaffinity_np(pthread_t th, size_t cpusetsize, const cpu_set_t *cpuset)
{
  return sched_setaffinity(dmtcp_pthread_get_tid(th), cpusetsize, cpuset);
}

extern "C" int
pthread_getschedparam (pthread_t th, int *policy, struct sched_param *param)
{
  /* Make sure the descriptor is valid.  */
  if (INVALID_TD_P(pd)) {
    /* Not a valid thread handle.  */
    return ESRCH;
  }

  struct sched_param sparam;
  int spolicy;
  int result = 0;

  libc_pthread_addr th_addr = dmtcp_pthread_get_addrs(th);
  glibc_lll_lock(th_addr.lock);

  pid_t tid = *th_addr.tid;
  int flags = *th_addr.flags;

  /* The library is responsible for maintaining the values at all
     times.  If the user uses an interface other than
     pthread_setschedparam to modify the scheduler setting it is not
     the library's problem.  In case the descriptor's values have
     not yet been retrieved do it now.  */
  if ((flags & ATTR_FLAG_SCHED_SET) == 0) {
    if (sched_getparam(tid, &sparam) != 0) {
      result = 1;
    } else {
      flags |= ATTR_FLAG_SCHED_SET;
    }
  }

  if ((flags & ATTR_FLAG_POLICY_SET) == 0) {
    spolicy = sched_getscheduler(tid);
    if (spolicy == -1) {
      result = 1;
    } else {
      flags |= ATTR_FLAG_POLICY_SET;
    }
  }

  if (result == 0) {
    *policy = spolicy;
    memcpy(param, &sparam, sizeof(struct sched_param));
  }

  *th_addr.flags = flags;

  dmtcp_pthread_set_schedparam(th, spolicy, &sparam);

  glibc_lll_unlock(th_addr.lock);

  return result;
}

extern "C" int
pthread_setschedparam (pthread_t th,
                       int policy,
                       const struct sched_param *param)
{
  /* Make sure the descriptor is valid.  */
  if (INVALID_TD_P (pd))
    /* Not a valid thread handle.  */
    return ESRCH;

  int result = 0;

  libc_pthread_addr th_addr = dmtcp_pthread_get_addrs(th);
  glibc_lll_lock(th_addr.lock);

  pid_t tid = *th_addr.tid;


  /* Try to set the scheduler information.  */
  if (sched_setscheduler(tid, policy, param) == 0) {
    /* We succeeded changing the kernel information.  Reflect this
       change in the thread descriptor.  */
    dmtcp_pthread_set_schedparam(th, policy, param);

    *th_addr.flags |= ATTR_FLAG_SCHED_SET | ATTR_FLAG_POLICY_SET;
  } else {
    result = errno;
  }

  glibc_lll_unlock(th_addr.lock);

  return result;
}

extern "C" int
pthread_sigqueue (pthread_t th, int signo, const union sigval value)
{
  WrapperLock wrapperLock;

  pid_t tid = dmtcp_pthread_get_tid(th);
  pid_t real_tid = VIRTUAL_TO_REAL_PID(tid);

#ifdef __NR_rt_tgsigqueueinfo
  /* Disallow sending the signal we use for cancellation, timers,
     for the setxid implementation.  */
  if (signo == SIGCANCEL || signo == SIGTIMER || signo == SIGSETXID)
    return EINVAL;

  pid_t real_pid = VIRTUAL_TO_REAL_PID(getpid());

  /* Set up the siginfo_t structure.  */
  siginfo_t info;
  memset (&info, '\0', sizeof (siginfo_t));
  info.si_signo = signo;
  info.si_code = SI_QUEUE;
  info.si_pid = real_pid;
  info.si_uid = getuid ();
  info.si_value = value;

  return _real_syscall(SYS_rt_sigqueueinfo, real_pid, real_tid, signo, &info);
#else
  return ENOSYS;
#endif
}

extern "C" int sigqueue(pid_t pid, int signo, const union sigval value)
{
  WrapperLock wrapperLock;

  pid_t real_pid = 0;
  if (pid != 0) {
    real_pid = VIRTUAL_TO_REAL_PID(pid);
  }
  return NEXT_FNC(sigqueue)(real_pid, signo, value);
}

extern "C" int
pthread_setschedprio (pthread_t th, int prio)
{
  /* Make sure the descriptor is valid.  */
  if (INVALID_TD_P (th))
    /* Not a valid thread handle.  */
    return ESRCH;

  int result = 0;
  struct sched_param param;
  param.sched_priority = prio;

  libc_pthread_addr th_addr = dmtcp_pthread_get_addrs(th);
  glibc_lll_lock(th_addr.lock);

  pid_t tid = *th_addr.tid;

#if 0
  /* If the thread should have higher priority because of some
   * PTHREAD_PRIO_PROTECT mutexes it holds, adjust the priority. */
  if (__builtin_expect (pd->tpp != NULL, 0) && pd->tpp->priomax > prio)
    param.sched_priority = pd->tpp->priomax;
#endif

  /* Try to set the scheduler information.  */
  if (sched_setparam (tid, &param) == -1) {
    result = errno;
  } else {
    /* We succeeded changing the kernel information.  Reflect this change in the
     * thread descriptor. */
    param.sched_priority = prio;

    struct sched_param old_param;
    int old_policy;
    dmtcp_pthread_get_schedparam(th, &old_policy, &old_param);

    dmtcp_pthread_set_schedparam(th, old_policy, &param);

    *th_addr.flags |= ATTR_FLAG_SCHED_SET;
  }

  glibc_lll_unlock(th_addr.lock);

  return result;
}

struct libc_pthread_attr {
  /* Scheduler parameters and priority.  */
  struct sched_param schedparam;
  int schedpolicy;
  /* Various flags like detachstate, scope, etc.  */
  int flags;
  /* Size of guard area.  */
  size_t guardsize;
  /* Stack handling.  */
  void *stackaddr;
  size_t stacksize;

  /* Allocated via a call to __pthread_attr_extension once needed.  */
  void /*struct pthread_attr_extension*/ *extension;
  void *unused;
};

#define _STACK_GROWS_DOWN 1
int
pthread_getattr_np(pthread_t th, pthread_attr_t *attr)
{
  WrapperLock wrapperLock;

  /* Prepare the new thread attribute.  */
  int ret = pthread_attr_init(attr);
  if (ret != 0) {
    return ret;
  }

  struct libc_pthread_attr *iattr = (struct libc_pthread_attr *)attr;

  libc_pthread_addr th_addr = dmtcp_pthread_get_addrs(th);
  glibc_lll_lock(th_addr.lock);

  struct sched_param schedparam;
  int schedpolicy;
  dmtcp_pthread_get_schedparam(th, &schedpolicy, &schedparam);

  /* The thread library is responsible for keeping the values in the
     thread desriptor up-to-date in case the user changes them.  */
  memcpy(&iattr->schedparam, &schedparam, sizeof(struct sched_param));
  iattr->schedpolicy = schedpolicy;

  /* Clear the flags work.  */
  iattr->flags = *th_addr.flags;

  /* The thread might be detached by now.  */
  if (*th_addr.joinid == th) {
    iattr->flags |= ATTR_FLAG_DETACHSTATE;
  }

  /* This is the guardsize after adjusting it.  */
  iattr->guardsize = *th_addr.reported_guardsize;

  /* Stack size limit.  */
  struct rlimit rl;
  /* The sizes are subject to alignment.  */
  if (__glibc_likely(*th_addr.stackblock != NULL)) {
    /* The stack size reported to the user should not include the
       guard size.  */
    iattr->stacksize = *th_addr.stackblock_size - *th_addr.guardsize;
#if _STACK_GROWS_DOWN
    iattr->stackaddr = (char *)*th_addr.stackblock + *th_addr.stackblock_size;
#else
    iattr->stackaddr = (char *)*th_addr.stackblock;
#endif
  } else {
    /* No stack information available.  This must be for the initial thread. Get
     * the info in some magical way. */

    /* Stack size limit.  */
    struct rlimit rl;
    if (getrlimit(RLIMIT_STACK, &rl) != 0) {
      ret = errno;
    } else {
      size_t pagesize = Util::pageSize();
      size_t pagemask = Util::pageMask();

      // This block is to ensure that the object is deleted as soon as we leave
      // this block.
      ProcSelfMaps procSelfMaps;
      Area area;

      // __libc_stack_end comes before argv, envp, etc.
      extern void *__libc_stack_end;
      void *stack_end = (void *)((uintptr_t)__libc_stack_end & pagemask);
#if _STACK_GROWS_DOWN
      stack_end += pagesize;
#endif

      VA lastEndAddr = NULL;
      /* We need the limit of the stack in any case.  */
      while (procSelfMaps.getNextArea(&area)) {
        if (area.addr <= (VA)__libc_stack_end &&
            (VA)__libc_stack_end < area.endAddr) {
          /* Found the entry.  Now we have the info we need.  */
          iattr->stackaddr = stack_end;
          iattr->stacksize =
            rl.rlim_cur - (size_t)(area.endAddr - (VA)stack_end);

          /* Cut it down to align it to page size since otherwise we
             risk going beyond rlimit when the kernel rounds up the
             stack extension request.  */
          iattr->stacksize = (iattr->stacksize & pagemask);
#if _STACK_GROWS_DOWN
          /* The limit might be too high.  */
          if ((size_t)iattr->stacksize >
              (size_t)iattr->stackaddr - (size_t)lastEndAddr)
            iattr->stacksize = (size_t)iattr->stackaddr - (size_t)lastEndAddr;
#else
          /* The limit might be too high.  */
          if ((size_t)iattr->stacksize > to - (size_t)iattr->stackaddr)
            iattr->stacksize = area.addr - (size_t)iattr->stackaddr;
#endif
          /* We succeed and no need to look further.  */
          ret = 0;
          break;
        }

#if _STACK_GROWS_DOWN
        lastEndAddr = area.endAddr;
#endif
      }
    }
  }

  iattr->flags |= ATTR_FLAG_STACKADDR;

  if (ret == 0) {
    size_t size = 16;
    cpu_set_t *cpuset = NULL;

    do {
      size <<= 1;

      void *newp = realloc(cpuset, size);
      if (newp == NULL) {
        ret = ENOMEM;
        break;
      }
      cpuset = (cpu_set_t *)newp;

      ret = pthread_getaffinity_np(th, size, cpuset);
      /* Pick some ridiculous upper limit.  Is 8 million CPUs enough? */
    } while (ret == EINVAL && size < 1024 * 1024);

    if (ret == 0) {
      ret = pthread_attr_setaffinity_np(attr, size, cpuset);
    } else if (ret == ENOSYS) {
      /* There is no such functionality.  */
      ret = 0;
    }

    free(cpuset);
  }

  glibc_lll_unlock(th_addr.lock);

  if (ret != 0) {
    pthread_attr_destroy(attr);
  }

  return ret;
}
#endif // #ifdef USE_VIRTUAL_TID_LIBC_STRUCT_PTHREAD

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

