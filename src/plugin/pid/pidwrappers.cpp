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
#include <sys/ioctl.h>
#include <sys/procfs.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
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

static pid_t
realPidSelector(pid_t pid)
{
  if (pid < -1) {
    return -dmtcp_pid_virtual_to_real((pid_t)-pid);
  }
  if (pid > 0) {
    return dmtcp_pid_virtual_to_real(pid);
  }
  return pid;
}

extern "C" pid_t
getpid()
{
  if (!dmtcp_pid_is_enabled()) {
    return _real_getpid();
  }
  return VirtualPidTable::getpid();
}

extern "C" pid_t
getppid()
{
  if (!dmtcp_pid_is_enabled()) {
    return _real_getppid();
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
    return _real_tgkill(_real_getpid(), _real_gettid(), sig);
  }

  pid_t virtTid = dmtcp_pthread_get_tid(th);
  pid_t realTid = dmtcp_pid_virtual_to_real(virtTid);

  return _real_tgkill(_real_getpid(), realTid, sig);
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
  pid_t realTid = dmtcp_pid_virtual_to_real(virtTid);
  *tidAddr = realTid;
  result = _real_pthread_cancel(th);
  // Restore the pthread data structure to use the virtual tid.
  *tidAddr = virtTid;
  return result;
}
#endif // #ifdef USE_VIRTUAL_TID_LIBC_STRUCT_PTHREAD

extern "C" int
tcsetpgrp(int fd, pid_t pgrp)
{
  if (!dmtcp_pid_is_enabled()) {
    return _real_tcsetpgrp(fd, pgrp);
  }

  WrapperLock wrapperLock;
  pid_t currPgrp = dmtcp_pid_virtual_to_real(pgrp);

  // JTRACE("Inside tcsetpgrp wrapper") (fd) (pgrp) (currPgrp);
  int ret = _real_tcsetpgrp(fd, currPgrp);

  // JTRACE("tcsetpgrp return value") (fd) (pgrp) (currPgrp) (retval);
  return ret;
}

extern "C" pid_t
tcgetpgrp(int fd)
{
  if (!dmtcp_pid_is_enabled()) {
    return _real_tcgetpgrp(fd);
  }

  WrapperLock wrapperLock;
  pid_t retval = dmtcp_pid_real_to_virtual(_real_tcgetpgrp(fd));

  JTRACE("tcgetpgrp return value") (fd) (retval);

  return retval;
}

extern "C" pid_t
tcgetsid(int fd)
{
  if (!dmtcp_pid_is_enabled()) {
    return _real_tcgetsid(fd);
  }

  WrapperLock wrapperLock;
  pid_t retval = dmtcp_pid_real_to_virtual(_real_tcgetsid(fd));

  JTRACE("tcgetsid return value") (fd) (retval);

  return retval;
}

extern "C" pid_t
getpgrp(void)
{
  if (!dmtcp_pid_is_enabled()) {
    return _real_getpgrp();
  }

  WrapperLock wrapperLock;
  pid_t pgrp = _real_getpgrp();
  pid_t origPgrp = dmtcp_pid_real_to_virtual(pgrp);

  return origPgrp;
}

extern "C" int
setpgrp(void)
{
  if (!dmtcp_pid_is_enabled()) {
    return _real_setpgrp();
  }

  WrapperLock wrapperLock;
  return _real_setpgrp();
}

extern "C" pid_t
getpgid(pid_t pid)
{
  if (!dmtcp_pid_is_enabled()) {
    return _real_getpgid(pid);
  }

  WrapperLock wrapperLock;
  pid_t realPid = dmtcp_pid_virtual_to_real(pid);
  pid_t res = _real_getpgid(realPid);
  pid_t origPgid = dmtcp_pid_real_to_virtual(res);

  return origPgid;
}

extern "C" int
setpgid(pid_t pid, pid_t pgid)
{
  if (!dmtcp_pid_is_enabled()) {
    return _real_setpgid(pid, pgid);
  }

  WrapperLock wrapperLock;
  pid_t currPid = dmtcp_pid_virtual_to_real(pid);
  pid_t currPgid = dmtcp_pid_virtual_to_real(pgid);

  int retVal = _real_setpgid(currPid, currPgid);

  return retVal;
}

extern "C" pid_t
getsid(pid_t pid)
{
  if (!dmtcp_pid_is_enabled()) {
    return _real_getsid(pid);
  }

  WrapperLock wrapperLock;
  pid_t currPid;

  // If !pid then we ask SID of this process
  if (pid) {
    currPid = dmtcp_pid_virtual_to_real(pid);
  } else {
    currPid = _real_getpid();
  }

  pid_t res = _real_getsid(currPid);

  pid_t origSid = dmtcp_pid_real_to_virtual(res);

  return origSid;
}

extern "C" pid_t
setsid(void)
{
  if (!dmtcp_pid_is_enabled()) {
    return _real_setsid();
  }

  WrapperLock wrapperLock;
  pid_t pid = _real_setsid();
  pid_t origPid = dmtcp_pid_real_to_virtual(pid);

  return origPid;
}

extern "C" int
kill(pid_t pid, int sig)
{
  /*
   * Intentionally do not take WrapperLock in kill-family wrappers.
   *
   * When bash receives a SIGINT signal, the signal handler is
   * called to process the signal. Once the processing is done, bash
   * performs a longjmp to a much higher call frame. As a result, this
   * call frame never gets a chance to return and a held wrapper lock
   * would never be released. Thus later on, when the ckpt-thread tries
   * to acquire this lock, it results in a deadlock.
   *
   * To avoid the deadlock, FOR NOW, we shouldn't take wrapper locks in
   * this function or any kill() family of wrappers.
   *
   * Potential Solution: If the signal sending process is among the
   * potential signal receivers, it should MASK/BLOCK signal delivery
   * right before sending the signal (before calling _real_kill()). Once
   * the system call returns, it should then release the wrapper lock
   * and restore the signal mask to as it was prior to calling this
   * wrapper. So this function will look like:
   *     int retVal;
   *     {
   *       WrapperLock wrapperLock;
   *       // if this process is a potential receiver of this signal
   *       if (pid == getpid() || pid == 0 || pid == -1 ||
   *           getpgrp() == abs(pid)) {
   *         <SAVE_SIGNAL_MASK>;
   *         <BLOCK SIGNAL 'sig'>;
   *         sigmaskAltered = true;
   *       }
   *       pid_t currPid = dmtcp_pid_virtual_to_real(pid);
   *       retVal = _real_kill(currPid, sig);
   *     }
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

  pid_t currPid = realPidSelector(pid);

  int retVal = _real_kill(currPid, sig);

  return retVal;
}

static pid_t
virtualizeWait4Result(pid_t realPid, int *status)
{
  if (realPid <= 0) {
    return realPid;
  }

  pid_t virtualPid = dmtcp_pid_real_to_virtual(realPid);
  if (status != NULL && (WIFEXITED(*status) || WIFSIGNALED(*status))) {
    VirtualPidTable::instance().erase(virtualPid);
  }
  return virtualPid;
}

static void
virtualizeWaitidResult(siginfo_t *infop)
{
  if (infop == NULL || infop->si_pid <= 0) {
    return;
  }

  pid_t virtualPid = dmtcp_pid_real_to_virtual(infop->si_pid);
  infop->si_pid = virtualPid;
  if (infop->si_code == CLD_EXITED ||
      infop->si_code == CLD_KILLED ||
      infop->si_code == CLD_DUMPED) {
    VirtualPidTable::instance().erase(virtualPid);
  }
}

static id_t
realWaitidId(idtype_t idtype, id_t id)
{
  switch (idtype) {
  case P_PID:
    return dmtcp_pid_virtual_to_real(id);

  case P_PGID:
    return id == 0 ? id : dmtcp_pid_virtual_to_real(id);

  case P_ALL:
    return id;

#ifdef P_PIDFD
  case P_PIDFD:
    return id;
#endif

  default:
    return id;
  }
}

extern "C" pid_t
wait(__WAIT_STATUS stat_loc)
{
  return waitpid(-1, (int *)stat_loc, 0);
}

extern "C" pid_t
waitpid(pid_t pid, int *stat_loc, int options)
{
  return wait4(pid, stat_loc, options, NULL);
}

extern "C" pid_t
wait3(__WAIT_STATUS status, int options, struct rusage *rusage)
{
  return wait4(-1, status, options, rusage);
}

extern "C" pid_t
wait4(pid_t pid, __WAIT_STATUS status, int options, struct rusage *rusage)
{
  int stat;
  int saved_errno = errno;
  pid_t retval = 0;
  struct timespec ts = { 0, 1000 };
  const struct timespec maxts = { 1, 0 };

  if (status == NULL) {
    status = (__WAIT_STATUS)&stat;
  }

  if (!dmtcp_pid_is_enabled()) {
    return _real_wait4(pid, status, options, rusage);
  }

  while (retval == 0) {
    {
      WrapperLock wrapperLock;
      pid_t realPid = realPidSelector(pid);
      pid_t realResult =
        _real_wait4(realPid, status, options | WNOHANG, rusage);
      if (realResult == -1) {
        saved_errno = errno;
      }
      retval = virtualizeWait4Result(realResult, (int *)status);
    }

    if ((options & WNOHANG) || retval != 0) {
      break;
    }

    nanosleep(&ts, NULL);
    if (TIMESPEC_CMP(&ts, &maxts, <)) {
      TIMESPEC_ADD(&ts, &ts, &ts);
    }
  }

  errno = saved_errno;
  return retval;
}

static int
waitidImpl(idtype_t idtype,
           id_t id,
           siginfo_t *infop,
           int options,
           struct rusage *rusage,
           bool useRawSyscall)
{
  if (!dmtcp_pid_is_enabled()) {
    siginfo_t siginfop;
    memset(&siginfop, 0, sizeof(siginfop));
    int retval = useRawSyscall ?
                 (int)_real_syscall(SYS_waitid, idtype, id, (long)&siginfop,
                                    options, (long)rusage, 0, 0) :
                 _real_waitid(idtype, id, &siginfop, options);
    if (retval == 0 && infop != NULL) {
      *infop = siginfop;
    }
    return retval;
  }

  int retval = 0;
  int saved_errno = errno;
  struct timespec ts = { 0, 1000 };
  const struct timespec maxts = { 1, 0 };
  siginfo_t siginfop;
  memset(&siginfop, 0, sizeof(siginfop));

  while (retval == 0) {
    {
      WrapperLock wrapperLock;
      id_t realId = realWaitidId(idtype, id);
      retval = useRawSyscall ?
               (int)_real_syscall(SYS_waitid, idtype, realId,
                                  (long)&siginfop, options | WNOHANG,
                                  (long)rusage, 0, 0) :
               _real_waitid(idtype, realId, &siginfop, options | WNOHANG);
      if (retval == -1) {
        saved_errno = errno;
      } else {
        virtualizeWaitidResult(&siginfop);
      }
    }

    if ((options & WNOHANG) || retval == -1 || siginfop.si_pid != 0) {
      break;
    }

    nanosleep(&ts, NULL);
    if (TIMESPEC_CMP(&ts, &maxts, <)) {
      TIMESPEC_ADD(&ts, &ts, &ts);
    }
  }

  if (retval == 0 && infop != NULL) {
    *infop = siginfop;
  }
  errno = saved_errno;
  return retval;
}

extern "C" int
waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options)
{
  return waitidImpl(idtype, id, infop, options, NULL, false);
}

LIB_PRIVATE
int
dmtcp_pid_on_waitid_syscall(idtype_t idtype,
                            id_t id,
                            siginfo_t *infop,
                            int options,
                            struct rusage *rusage)
{
  return waitidImpl(idtype, id, infop, options, rusage, true);
}

#ifdef HAS_CMA
EXTERNC
ssize_t
process_vm_readv(pid_t pid,
                 const struct iovec *local_iov,
                 unsigned long liovcnt,
                 const struct iovec *remote_iov,
                 unsigned long riovcnt,
                 unsigned long flags)
{
  if (!dmtcp_pid_is_enabled()) {
    return _real_process_vm_readv(pid, local_iov, liovcnt,
                                  remote_iov, riovcnt, flags);
  }

  WrapperLock wrapperLock;
  pid_t realPid = dmtcp_pid_virtual_to_real(pid);
  return _real_process_vm_readv(realPid, local_iov, liovcnt,
                                remote_iov, riovcnt, flags);
}

EXTERNC
ssize_t
process_vm_writev(pid_t pid,
                  const struct iovec *local_iov,
                  unsigned long liovcnt,
                  const struct iovec *remote_iov,
                  unsigned long riovcnt,
                  unsigned long flags)
{
  if (!dmtcp_pid_is_enabled()) {
    return _real_process_vm_writev(pid, local_iov, liovcnt,
                                   remote_iov, riovcnt, flags);
  }

  WrapperLock wrapperLock;
  pid_t realPid = dmtcp_pid_virtual_to_real(pid);
  return _real_process_vm_writev(realPid, local_iov, liovcnt,
                                 remote_iov, riovcnt, flags);
}
#endif // ifdef HAS_CMA

LIB_PRIVATE
int
dmtcp_tkill(int tid, int sig)
{
  // Intentionally lock-free; see kill() for the self-signal longjmp hazard.

  int realTid = dmtcp_pid_virtual_to_real(tid);

  int retVal = _real_tkill(realTid, sig);

  return retVal;
}

LIB_PRIVATE
int
dmtcp_tgkill(int tgid, int tid, int sig)
{
  // Intentionally lock-free; see kill() for the self-signal longjmp hazard.

  int realTgid = dmtcp_pid_virtual_to_real(tgid);
  int realTid = dmtcp_pid_virtual_to_real(tid);

  int retVal = _real_syscall(SYS_tgkill, realTgid, realTid, sig,
                             0, 0, 0, 0);

  return retVal;
}

// long sys_tgkill (int tgid, int pid, int sig)

/*
extern "C" int setgid(gid_t gid)
{
  return _real_setgid(gid);
}

extern "C" int setuid(uid_t uid)
{
  return _real_setuid(uid);
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
