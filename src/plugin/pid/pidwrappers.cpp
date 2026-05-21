/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/procfs.h>
#include <sys/syscall.h>
#include <list>
#include <string>
#include <vector>

#include "jassert.h"
#include "jconvert.h"
#include "dmtcp.h"
#include "pid.h"
#include "pidwrappers.h"
#include "util.h"
#include "wrapperlock.h"
#include "virtualpidtable.h"
#include "glibc_pthread.h"

using namespace dmtcp;

static bool
pidVirt_disabledByEnv()
{
  const char *disableAll = getenv("DMTCP_DISABLE_ALL_PLUGINS");
  return disableAll != NULL && strcmp(disableAll, "1") == 0;
}

extern "C" pid_t
getpid()
{
  if (pidVirt_disabledByEnv()) {
    return pid_real_getpid();
  }

  return VirtualPidTable::getpid();
}

extern "C" pid_t
getppid()
{
  if (pidVirt_disabledByEnv()) {
    return pid_real_getppid();
  }

  return VirtualPidTable::getppid();
}

#ifdef USE_VIRTUAL_TID_LIBC_STRUCT_PTHREAD
extern "C" int
pthread_kill(pthread_t th, int sig)
{
  WrapperLockExcl wrapperLock;

  /* Disallow sending the signal we use for cancellation, timers,
     for the setxid implementation.  */
  if (sig == SIGCANCEL || sig == SIGSETXID) {
    return EINVAL;
  }

  if (th == pthread_self()) {
    /* Use the actual TID from the kernel, so that it refers to the
       current thread even if called after vfork.  There is no
       signal blocking in this case, so that the signal is delivered
       immediately, before __pthread_kill_internal returns. A signal
       sent to the thread itself needs to be delivered
       synchronously.  (It is unclear if Linux guarantees the
       delivery of all pending signals after unblocking in the code
       below.  POSIX only guarantees delivery of a single signal,
       which may not be the right one.)  */
    return pid_real_tgkill(pid_real_getpid(), pid_real_gettid(), sig);
  }

  pid_t virtTid = dmtcp_pthread_get_tid(th);
  pid_t realTid = VIRTUAL_TO_REAL_PID(virtTid);

  return pid_real_tgkill(pid_real_getpid(), realTid, sig);
}

static inline bool
cancel_enabled_and_canceled(int value)
{
  return (value & (CANCELSTATE_BITMASK | CANCELED_BITMASK | EXITING_BITMASK |
                   TERMINATED_BITMASK)) == CANCELED_BITMASK;
}

static inline bool
cancel_enabled_and_canceled_and_async(int value)
{
  return ((value) &
          (CANCELSTATE_BITMASK | CANCELTYPE_BITMASK | CANCELED_BITMASK |
           EXITING_BITMASK | TERMINATED_BITMASK)) ==
         (CANCELTYPE_BITMASK | CANCELED_BITMASK);
}

extern "C" int
pthread_cancel (pthread_t th)
{
  WrapperLock wrapperLock;
  int result = 0;
  pid_t *tidAddr = dmtcp_pthread_get_tid_addr(th);
  if (tidAddr == NULL) {
    return ESRCH;
  }

  pid_t virtTid = *tidAddr;
  if (virtTid == 0) {
    return result;
  }

  // Patch the pthread data structure to use the real tid.
  pid_t realTid = VIRTUAL_TO_REAL_PID(virtTid);
  *tidAddr = realTid;
  result = pid_real_pthread_cancel(th);
  // Restore the pthread data structure to use the virtual tid.
  *tidAddr = virtTid;
  return result;
}
#endif // #ifdef USE_VIRTUAL_TID_LIBC_STRUCT_PTHREAD

extern "C" pid_t
tcsetpgrp(int fd, pid_t pgrp)
{
  DMTCP_PLUGIN_DISABLE_CKPT();

  pid_t currPgrp = VIRTUAL_TO_REAL_PID(pgrp);

  // JTRACE("Inside tcsetpgrp wrapper") (fd) (pgrp) (currPgrp);
  pid_t realPid = pid_real_tcsetpgrp(fd, currPgrp);
  pid_t virtualPid = REAL_TO_VIRTUAL_PID(realPid);

  // JTRACE("tcsetpgrp return value") (fd) (pgrp) (currPgrp) (retval);
  DMTCP_PLUGIN_ENABLE_CKPT();

  return virtualPid;
}

extern "C" pid_t
tcgetpgrp(int fd)
{
  DMTCP_PLUGIN_DISABLE_CKPT();

  pid_t retval = REAL_TO_VIRTUAL_PID(pid_real_tcgetpgrp(fd));

  JTRACE("tcgetpgrp return value") (fd) (retval);
  DMTCP_PLUGIN_ENABLE_CKPT();

  return retval;
}

extern "C" pid_t
tcgetsid(int fd)
{
  DMTCP_PLUGIN_DISABLE_CKPT();

  pid_t retval = REAL_TO_VIRTUAL_PID(pid_real_tcgetsid(fd));

  JTRACE("tcgetsid return value") (fd) (retval);
  DMTCP_PLUGIN_ENABLE_CKPT();

  return retval;
}

extern "C" pid_t
getpgrp(void)
{
  DMTCP_PLUGIN_DISABLE_CKPT();

  pid_t pgrp = pid_real_getpgrp();
  pid_t origPgrp = REAL_TO_VIRTUAL_PID(pgrp);

  DMTCP_PLUGIN_ENABLE_CKPT();

  return origPgrp;
}

extern "C" int
setpgrp(void)
{
  DMTCP_PLUGIN_DISABLE_CKPT();

  pid_t realPid = pid_real_setpgrp();
  pid_t virtualPid = REAL_TO_VIRTUAL_PID(realPid);

  DMTCP_PLUGIN_ENABLE_CKPT();

  return virtualPid;
}

extern "C" pid_t
getpgid(pid_t pid)
{
  DMTCP_PLUGIN_DISABLE_CKPT();

  pid_t realPid = VIRTUAL_TO_REAL_PID(pid);
  pid_t res = pid_real_getpgid(realPid);
  pid_t origPgid = REAL_TO_VIRTUAL_PID(res);

  DMTCP_PLUGIN_ENABLE_CKPT();

  return origPgid;
}

extern "C" int
setpgid(pid_t pid, pid_t pgid)
{
  DMTCP_PLUGIN_DISABLE_CKPT();

  pid_t currPid = VIRTUAL_TO_REAL_PID(pid);
  pid_t currPgid = VIRTUAL_TO_REAL_PID(pgid);

  int retVal = pid_real_setpgid(currPid, currPgid);

  DMTCP_PLUGIN_ENABLE_CKPT();

  return retVal;
}

extern "C" pid_t
getsid(pid_t pid)
{
  DMTCP_PLUGIN_DISABLE_CKPT();

  pid_t currPid;

  // If !pid then we ask SID of this process
  if (pid) {
    currPid = VIRTUAL_TO_REAL_PID(pid);
  } else {
    currPid = pid_real_getpid();
  }

  pid_t res = pid_real_getsid(currPid);

  pid_t origSid = REAL_TO_VIRTUAL_PID(res);

  DMTCP_PLUGIN_ENABLE_CKPT();

  return origSid;
}

extern "C" pid_t
setsid(void)
{
  DMTCP_PLUGIN_DISABLE_CKPT();

  pid_t pid = pid_real_setsid();
  pid_t origPid = REAL_TO_VIRTUAL_PID(pid);

  DMTCP_PLUGIN_ENABLE_CKPT();

  return origPid;
}

extern "C" int
kill(pid_t pid, int sig)
{
  /* FIXME: When bash receives a SIGINT signal, the signal handler is
   * called to process the signal. Once the processing is done, bash
   * performs a longjmp to a much higher call frame. As a result, this
   * call frame never gets a chance to return and hence we fail to
   * perform DMTCP_PLUGIN_ENABLE_CKPT() WHICH RESULTS in the lock
   * being held but never released. Thus later on, when the ckpt-thread
   * tries to acquire this lock, it results in a deadlock.
   *
   *  To avoid the deadlock, FOR NOW, we shouldn't call WRAPPER_...()
   *  calls in this function or any kill() family of wrappers.
   *
   * Potential Solution: If the signal sending process is among the
   * potential signal receivers, it should MASK/BLOCK signal delivery
   * right before sending the signal (before calling pid_real_kill()). Once
   * the system call returns, it should then call
   * DMTCP_PLUGIN_ENABLE_CKPT() AND THEN RESTore the signal mask to
   * as it was prior to calling this wrapper. So this function will as
   * follows:
   *     DMTCP_PLUGIN_DISABLE_CKPT();
   *     // if this process is a potential receiver of this signal
   *     if (pid == getpid() || pid == 0 || pid == -1 ||
   *         getpgrp() == abs(pid)) {
   *       <SAVE_SIGNAL_MASK>;
   *       <BLOCK SIGNAL 'sig'>;
   *       sigmaskAltered = true;
   *     }
   *     pid_t currPid = VIRTUAL_TO_REAL_PID(pid);
   *     int retVal = pid_real_kill(currPid, sig);
   *     DMTCP_PLUGIN_ENABLE_CKPT();
   *     if (sigmaskAltered) {
   *       <RESTORE_SIGNAL_MASK>
   *     }
   *     return retVal;
   *
   *
   * This longjmp trouble can happen with any wrapper, whose execution my
   * end up in a call to user-code i.e. if the call frame looks sth like:
   *        ...
   *        user_func1(...)
   *        ...
   *        DMTCP_WRAPPER(...)
   *        ...
   *        user_func2(...)
   *        ...
   *
   * Another potential way would be to put a wrapper around longjmp() in
   * which the calling thread should release all the DMTCP-locks being
   * held at the moment. This would require us to keep a count of lock()
   * calls without a corresponding unlock() call. After the longjmp()
   * call, one need to make sure that an unlock() call is requested only
   * if there is a corresponding lock, because it might happen that
   * longjmp() was harmless in the sense that, it didn't cause a
   * callframe like the one mentioned above.
   *
   */

  // DMTCP_PLUGIN_DISABLE_CKPT();

  pid_t currPid = VIRTUAL_TO_REAL_PID(pid);

  int retVal = pid_real_kill(currPid, sig);

  // DMTCP_PLUGIN_ENABLE_CKPT();

  return retVal;
}

LIB_PRIVATE
int
dmtcp_tkill(int tid, int sig)
{
  // FIXME: Check the comments in kill()
  // DMTCP_PLUGIN_DISABLE_CKPT();

  int realTid = VIRTUAL_TO_REAL_PID(tid);

  int retVal = pid_real_tkill(realTid, sig);

  // DMTCP_PLUGIN_ENABLE_CKPT();

  return retVal;
}

LIB_PRIVATE
int
dmtcp_tgkill(int tgid, int tid, int sig)
{
  // FIXME: Check the comments in kill()
  // DMTCP_PLUGIN_DISABLE_CKPT();

  int realTgid = VIRTUAL_TO_REAL_PID(tgid);
  int realTid = VIRTUAL_TO_REAL_PID(tid);

  int retVal = pid_real_tgkill(realTgid, realTid, sig);

  // DMTCP_PLUGIN_ENABLE_CKPT();

  return retVal;
}

// wait-family and fcntl wrappers are composed in src/miscwrappers.cpp and
// src/wrappers.cpp so PID virtualization can remain available when the PID
// plugin is linked into libdmtcp.so instead of interposing as a later DSO.

/*
extern "C" int setgid(gid_t gid)
{
  return pid_real_setgid(gid);
}

extern "C" int setuid(uid_t uid)
{
  return pid_real_setuid(uid);
}
*/

// long sys_set_tid_address(int __user *tidptr);
// extern "C" int   sigqueue(pid_t pid, int signo, const union sigval value)
// long sys_rt_sigqueueinfo(int pid, int sig, siginfo_t __user *uinfo);


// long sys_getuid(void);
// long sys_geteuid(void);
// long sys_getgid(void);
// long sys_getegid(void);
// long sys_getresuid(uid_t __user *ruid, uid_t __user *euid, uid_t __user
// *suid);
// long sys_getresgid(gid_t __user *rgid, gid_t __user *egid, gid_t __user
// *sgid);
// long sys_getpgrp(void);
// long sys_getgroups(int gidsetsize, gid_t __user *grouplist);
//
// long sys_setregid(gid_t rgid, gid_t egid);
// long sys_setgid(gid_t gid);
// long sys_setreuid(uid_t ruid, uid_t euid);
// long sys_setuid(uid_t uid);
// long sys_setresuid(uid_t ruid, uid_t euid, uid_t suid);
// long sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid);
// long sys_setfsuid(uid_t uid);
// long sys_setfsgid(gid_t gid);
// long sys_setsid(void);
// long sys_setgroups(int gidsetsize, gid_t __user *grouplist);
//
// long sys_sched_yield(void);
// long sys_sched_rr_get_interval(pid_t pid,
//
//
//
//
//
//
// long sys_chown(const char __user *filename,
// uid_t user, gid_t group);
// long sys_lchown(const char __user *filename,
// uid_t user, gid_t group);
// long sys_fchown(unsigned int fd, uid_t user, gid_t group);
// #ifdef CONFIG_UID16
// long sys_chown16(const char __user *filename,
// old_uid_t user, old_gid_t group);
// long sys_lchown16(const char __user *filename,
// old_uid_t user, old_gid_t group);
// long sys_fchown16(unsigned int fd, old_uid_t user, old_gid_t group);
// long sys_setregid16(old_gid_t rgid, old_gid_t egid);
// long sys_setgid16(old_gid_t gid);
// long sys_setreuid16(old_uid_t ruid, old_uid_t euid);
// long sys_setuid16(old_uid_t uid);
// long sys_setresuid16(old_uid_t ruid, old_uid_t euid, old_uid_t suid);
// long sys_getresuid16(old_uid_t __user *ruid,
// old_uid_t __user *euid, old_uid_t __user *suid);
// long sys_setresgid16(old_gid_t rgid, old_gid_t egid, old_gid_t sgid);
// long sys_getresgid16(old_gid_t __user *rgid,
// old_gid_t __user *egid, old_gid_t __user *sgid);
// long sys_setfsuid16(old_uid_t uid);
// long sys_setfsgid16(old_gid_t gid);
// long sys_getgroups16(int gidsetsize, old_gid_t __user *grouplist);
// long sys_setgroups16(int gidsetsize, old_gid_t __user *grouplist);
// long sys_getuid16(void);
// long sys_geteuid16(void);
// long sys_getgid16(void);
// long sys_getegid16(void);
//
//
//
//
// long sys_add_key(const char __user *_type,
// const char __user *_description,
// const void __user *_payload,
// size_t plen,
// key_serial_t destringid);
//
// long sys_request_key(const char __user *_type,
// const char __user *_description,
// const char __user *_callout_info,
// key_serial_t destringid);
//
//
// long sys_migrate_pages(pid_t pid, unsigned long maxnode,
// const unsigned long __user *from,
// const unsigned long __user *to);
// long sys_move_pages(pid_t pid, unsigned long nr_pages,
// const void __user * __user *pages,
// const int __user *nodes,
// int __user *status,
// int flags);
// long compat_sys_move_pages(pid_t pid, unsigned long nr_page,
// __u32 __user *pages,
// const int __user *nodes,
// int __user *status,
// int flags);
//
// long sys_fchownat(int dfd, const char __user *filename, uid_t user,
// gid_t group, int flag);
//
// long sys_get_robust_list(int pid,
// struct robust_list_head __user * __user *head_ptr,
// size_t __user *len_ptr);
//
