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

#include "mtcpinterface.h"
#include <stdarg.h>
#include <vector>
#include <list>
#include <string>
#include "syscallwrappers.h"
#include  "../jalib/jassert.h"
#include "uniquepid.h"
#include "dmtcpworker.h"
#include "sockettable.h"
#include "protectedfds.h"
#include "connectionmanager.h"
#include "connectionidentifier.h"
#include  "../jalib/jconvert.h"
#include "constants.h"
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <thread_db.h>
#include <sys/procfs.h>

#ifdef PID_VIRTUALIZATION

static __thread pid_t _dmtcp_thread_tid = -1;

static pid_t _dmtcp_pid = -1;
static pid_t _dmtcp_ppid = -1;


static pid_t getPidFromEnvVar()
{
  const char *pidstr = getenv(ENV_VAR_VIRTUAL_PID);
  if (pidstr == NULL) {
    fprintf(stderr, "ERROR at %s:%d: env var DMTCP_VIRTUAL_PID not set\n\n",
            __FILE__, __LINE__);
    sleep(5);
    _exit(0);
  }
  return (pid_t) atoi(pidstr);
}


extern "C" LIB_PRIVATE
void dmtcpResetPidPpid(pid_t pid, pid_t ppid)
{
  // Reset __thread_tid on fork. This should be the first thing to do in
  // the child process.
  _dmtcp_pid = pid;
  _dmtcp_ppid = ppid;
}

extern "C" LIB_PRIVATE
void dmtcpResetTid(pid_t tid)
{
  _dmtcp_thread_tid = tid;
}

extern "C" pid_t gettid()
{
  /* mtcpinterface.cpp:thread_start calls gettid() before calling
   * DmtcpWorker::decrementUninitializedThreadCount() and so the value is
   * cached before it is accessed by some other DMTCP code.
   */
  if (_dmtcp_thread_tid == -1) {
    _dmtcp_thread_tid = _real_gettid();
  }
  return _dmtcp_thread_tid;
}

extern "C" pid_t getpid()
{
  if (_dmtcp_pid == -1) {
    _dmtcp_pid = _real_getpid();
  }
  return _dmtcp_pid;
}

extern "C" pid_t getppid()
{
  if (_real_getppid() == 1) {
    _dmtcp_ppid = 1;
  }
  if (_dmtcp_ppid == -1) {
    _dmtcp_ppid = _real_getppid();
  }
  return _dmtcp_ppid;
}

extern "C" pid_t tcsetpgrp(int fd, pid_t pgrp)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  pid_t currPgrp = VIRTUAL_TO_REAL_PID( pgrp );
//  JTRACE( "Inside tcsetpgrp wrapper" ) (fd) (pgrp) (currPgrp);
  pid_t realPid = _real_tcsetpgrp(fd, currPgrp);
  pid_t virtualPid = REAL_TO_VIRTUAL_PID(realPid);

  //JTRACE( "tcsetpgrp return value" ) (fd) (pgrp) (currPgrp) (retval);
  WRAPPER_EXECUTION_ENABLE_CKPT();

  return virtualPid;
}

extern "C" pid_t tcgetpgrp(int fd)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  pid_t retval = REAL_TO_VIRTUAL_PID( _real_tcgetpgrp(fd) );

  JTRACE ( "tcgetpgrp return value" ) (fd) (retval);
  WRAPPER_EXECUTION_ENABLE_CKPT();

  return retval;
}

extern "C" pid_t getpgrp(void)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  pid_t pgrp = _real_getpgrp();
  pid_t origPgrp =  REAL_TO_VIRTUAL_PID( pgrp );

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return origPgrp;
}

extern "C" pid_t setpgrp(void)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  pid_t realPid = _real_setpgrp();
  pid_t virtualPid = REAL_TO_VIRTUAL_PID(realPid);

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return virtualPid;
}

extern "C" pid_t getpgid(pid_t pid)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  pid_t realPid = VIRTUAL_TO_REAL_PID (pid);
  pid_t res = _real_getpgid (realPid);
  pid_t origPgid = REAL_TO_VIRTUAL_PID (res);

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return origPgid;
}

extern "C" int   setpgid(pid_t pid, pid_t pgid)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  pid_t currPid = VIRTUAL_TO_REAL_PID (pid);
  pid_t currPgid = VIRTUAL_TO_REAL_PID (pgid);

  int retVal = _real_setpgid (currPid, currPgid);

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return retVal;
}

extern "C" pid_t getsid(pid_t pid)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  pid_t currPid;

  // If !pid then we ask SID of this process
  if( pid )
    currPid = VIRTUAL_TO_REAL_PID (pid);
  else
    currPid = _real_getpid();

  pid_t res = _real_getsid (currPid);

  pid_t origSid = REAL_TO_VIRTUAL_PID (res);

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return origSid;
}

extern "C" pid_t setsid(void)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  pid_t pid = _real_setsid();
  pid_t origPid = REAL_TO_VIRTUAL_PID (pid);
  dmtcp::ProcessInfo::instance().setsid(origPid);

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return origPid;
}

extern "C" int   kill(pid_t pid, int sig)
{
  /* FIXME: When bash receives a SIGINT signal, the signal handler is
   * called to process the signal. Once the processing is done, bash
   * performs a longjmp to a much higher call frame. As a result, this
   * call frame never gets a chance to return and hence we fail to
   * perform WRAPPER_EXECUTION_ENABLE_CKPT() which results in the lock
   * being held but never released. Thus later on, when the ckpt-thread
   * tries to acquire this lock, it results in a deadlock.
   *
   *  To avoid the deadlock, FOR NOW, we shouldn't call WRAPPER_...()
   *  calls in this function or any kill() family of wrappers.
   *
   * Potential Solution: If the signal sending process is among the
   * potential signal receivers, it should MASK/BLOCK signal delivery
   * right before sending the signal (before calling _real_kill()). Once
   * the system call returns, it should then call
   * WRAPPER_EXECUTION_ENABLE_CKPT() and then restore the signal mask to
   * as it was prior to calling this wrapper. So this function will as
   * follows:
   *     WRAPPER_EXECUTION_DISABLE_CKPT();
   *     // if this process is a potential receiver of this signal
   *     if (pid == getpid() || pid == 0 || pid == -1 ||
   *         getpgrp() == abs(pid)) {
   *       <SAVE_SIGNAL_MASK>;
   *       <BLOCK SIGNAL 'sig'>;
   *       sigmaskAltered = true;
   *     }
   *     pid_t currPid = VIRTUAL_TO_REAL_PID(pid);
   *     int retVal = _real_kill(currPid, sig);
   *     WRAPPER_EXECUTION_ENABLE_CKPT();
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
//  WRAPPER_EXECUTION_DISABLE_CKPT();

  pid_t currPid = VIRTUAL_TO_REAL_PID (pid);

  int retVal = _real_kill (currPid, sig);

//  WRAPPER_EXECUTION_ENABLE_CKPT();

  return retVal;
}

LIB_PRIVATE
int tkill(int tid, int sig)
{
  // FIXME: Check the comments in kill()
//  WRAPPER_EXECUTION_DISABLE_CKPT();

  int realTid = VIRTUAL_TO_REAL_PID ( tid );

  int retVal = _real_tkill ( realTid, sig );

//  WRAPPER_EXECUTION_ENABLE_CKPT();

  return retVal;
}

LIB_PRIVATE
int tgkill(int tgid, int tid, int sig)
{
  // FIXME: Check the comments in kill()
//  WRAPPER_EXECUTION_DISABLE_CKPT();

  int realTgid = VIRTUAL_TO_REAL_PID ( tgid );
  int realTid = VIRTUAL_TO_REAL_PID ( tid );

  int retVal = _real_tgkill ( realTgid, realTid, sig );

//  WRAPPER_EXECUTION_ENABLE_CKPT();

  return retVal;
}


//long sys_tgkill (int tgid, int pid, int sig)

#ifdef PTRACE
typedef td_err_e (*td_thr_get_info_funcptr_t)(const td_thrhandle_t *,
                                              td_thrinfo_t *);
static td_thr_get_info_funcptr_t _td_thr_get_info_funcptr = NULL;
static td_err_e  _dmtcp_td_thr_get_info (const td_thrhandle_t  *th_p,
                                         td_thrinfo_t *ti_p)
{
  td_err_e td_err;
  td_thrinfo_t local_ti_p = *ti_p;

  td_err = (*_td_thr_get_info_funcptr)(th_p, ti_p);

  if (th_p->th_unique != 0) {
    pid_t virtPid =  REAL_TO_VIRTUAL_PID((int)ti_p->ti_lid);
    ti_p->ti_lid  =  (lwpid_t) virtPid;
  }

  //ti_p->ti_lid  =  ( lwpid_t ) REAL_TO_VIRTUAL_PID ( ( int ) ti_p->ti_lid );
  //ti_p->ti_tid =  ( thread_t ) REAL_TO_VIRTUAL_PID ( (int ) ti_p->ti_tid );
  return td_err;
}

/* gdb calls dlsym on td_thr_get_info.  We need to wrap td_thr_get_info for
   tid virtualization. It should be safe to comment this out if you don't
   need to checkpoint gdb.
*/
extern "C" void *dlsym ( void *handle, const char *symbol)
{
  if ( strcmp ( symbol, "td_thr_get_info" ) == 0 ) {
    _td_thr_get_info_funcptr = (td_thr_get_info_funcptr_t) _real_dlsym(handle,
                                                                       symbol);
    if (_td_thr_get_info_funcptr != NULL) {
      return (void *) &_dmtcp_td_thr_get_info;
    } else {
      return NULL;
    }
  }
  else
    return _real_dlsym ( handle, symbol );
}
#endif

/*
 * TODO: Add the wrapper protection for wait() family of system calls.
 *       It wouldn't be a straight forward process, we need to take care of the
 *         _BLOCKING_ property of these system calls.
 *                                                      --KAPIL
 */

extern "C" pid_t wait (__WAIT_STATUS stat_loc)
{
  return waitpid(-1, (int*)stat_loc, 0);
}

extern "C" pid_t waitpid(pid_t pid, int *stat_loc, int options)
{
  return wait4(pid, stat_loc, options, NULL);
}

extern "C" int waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options)
{
  int retval = 0;
  struct timespec sleepTime = {0, 1000};
  siginfo_t siginfop;
  memset(&siginfop, 0, sizeof(siginfop));

  /* waitid returns 0 in case of success as well as when WNOHANG is specified
   * and we need to distinguish those two cases.man page for waitid says:
   *   If WNOHANG was specified in options and there were no children in a
   *   waitable  state, then waitid() returns 0 immediately and the state of
   *   the siginfo_t structure pointed to by infop is unspecified.  To
   *   distinguish this case from that where a child was in a  waitable state,
   *   zero out the si_pid field before the call and check for a nonzero value
   *   in this field after the call returns.
   *
   * See comments above wait4()
   */
  while (retval == 0) {
    WRAPPER_EXECUTION_DISABLE_CKPT();
    pid_t currPid = VIRTUAL_TO_REAL_PID (id);
    retval = _real_waitid (idtype, currPid, &siginfop, options | WNOHANG);

    if (retval != -1) {
      pid_t virtualPid = REAL_TO_VIRTUAL_PID ( siginfop.si_pid );
      siginfop.si_pid = virtualPid;

      if ( siginfop.si_code == CLD_EXITED || siginfop.si_code == CLD_KILLED )
        dmtcp::ProcessInfo::instance().eraseChild ( virtualPid );
    }
    WRAPPER_EXECUTION_ENABLE_CKPT();

    if ((options & WNOHANG) ||
        retval == -1 ||
        siginfop.si_pid != 0) {
      break;
    } else {
      if (sleepTime.tv_sec == 0) {
        sleepTime.tv_nsec *= 2;
        if (sleepTime.tv_nsec >= 1000 * 1000 * 1000) {
          sleepTime.tv_sec++;
          sleepTime.tv_nsec = 0;
        }
      }
      nanosleep(&sleepTime, NULL);
    }
  }

  if (retval == 0 && infop != NULL) {
    *infop = siginfop;
  }

  return retval;
}

extern "C" pid_t wait3(__WAIT_STATUS status, int options, struct rusage *rusage)
{
  return wait4(-1, status, options, rusage);
}

/*
 * wait() family and checkpoint/restart.
 *
 * wait() returns the _real_ pid of the child process. The
 * pid-virtualization layer then converts it to virtualPid pid and return it to
 * the caller.
 *
 * To guarantee the correctness of the real to virtualPid conversion, we need
 * to make sure that there is no ckpt/restart in between (A) returning from
 * wait(), and (B) performing conversion. If a ckpt happens in between, then on
 * restart, the real pid won't be valid anymore and the conversion would be
 * a false one.
 *
 * One way to avoid ckpt/restart in between state A and B is to disable ckpt
 * before A and enable it only after performing B. The problem in doing this is
 * the fact that wait is a blocking system call, unless WNOHANG is specified.
 * Thus we force the WNOHANG flag even though the caller didn't want to.
 *
 * When WNOHANG is specified, a return value of '0' indicates that there is no
 * child process to be waited for, in which case we sleep for a small amount of
 * time and retry.
 *
 * The last bit of logic is the amount of time to sleep for. Too little, and we
 * end up wasting CPU time; too large, and some application (for eg: strace)
 * might not like. We try to avoid this problem by starting with a tiny sleep
 * interval and on every failed wait(), we double the interval until we reach a
 * max. Once it reaches a max time, we don't double it, we just use it as is.
 *
 * The initial sleep interval is set to 10 micro seconds and the max is set to
 * 1 second. We hope that it would cover all the cases.
 *
 */
extern "C"
pid_t wait4(pid_t pid, __WAIT_STATUS status, int options, struct rusage *rusage)
{
  int stat;
  int saved_errno = errno;
  pid_t currPid;
  pid_t virtualPid;
  pid_t retval = 0;
  struct timespec sleepTime = {0, 10*000};

  if (status == NULL)
    status = (__WAIT_STATUS) &stat;

  while (retval == 0) {
    WRAPPER_EXECUTION_DISABLE_CKPT();
    currPid = VIRTUAL_TO_REAL_PID(pid);
    retval = _real_wait4(currPid, status, options | WNOHANG, rusage);
    saved_errno = errno;
    virtualPid = REAL_TO_VIRTUAL_PID(retval);

    if (retval > 0 &&
        (WIFEXITED(*(int*)status) || WIFSIGNALED(*(int*)status))) {
      dmtcp::ProcessInfo::instance().eraseChild ( virtualPid );
    }
    WRAPPER_EXECUTION_ENABLE_CKPT();

    if ((options & WNOHANG) || retval != 0) {
      break;
    } else {
      if (sleepTime.tv_sec == 0) {
        sleepTime.tv_nsec *= 2;
        if (sleepTime.tv_nsec >= 1000 * 1000 * 1000) {
          sleepTime.tv_sec++;
          sleepTime.tv_nsec = 0;
        }
      }
      nanosleep(&sleepTime, NULL);
    }
  }
  errno = saved_errno;
  return virtualPid;
}

extern "C" long ptrace (enum __ptrace_request request, ...)
{
  va_list ap;
  pid_t virtualPid;
  pid_t realPid;
  void *addr;
  void *data;

  va_start(ap, request);
  virtualPid = va_arg(ap, pid_t);
  addr = va_arg(ap, void *);
  data = va_arg(ap, void *);
  va_end(ap);

  realPid = VIRTUAL_TO_REAL_PID(virtualPid);
  long ptrace_ret =  _real_ptrace(request, realPid, addr, data);

  /*
   * PTRACE_GETEVENTMSG (since Linux 2.5.46)
   *          Retrieve  a message (as an unsigned long) about the ptrace event
   *          that just happened, placing it in the location data in the
   *          parent.  For PTRACE_EVENT_EXIT this is the child's exit status.
   *          For PTRACE_EVENT_FORK, PTRACE_EVENT_VFORK and PTRACE_EVENT_CLONE
   *          this is the PID  of the new process.  Since Linux 2.6.18, the PID
   *          of the new process is also available for PTRACE_EVENT_VFORK_DONE.
   *          (addr is ignored.)
   */

  if (ptrace_ret == 0 && request == PTRACE_GETEVENTMSG) {
    unsigned long *ldata = (unsigned long*) data;
    pid_t newRealPid =  (pid_t) *ldata;
    pid_t newVirtualPid = REAL_TO_VIRTUAL_PID(newRealPid);
    *ldata = (unsigned long) newVirtualPid;
  }

  return ptrace_ret;
}

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
#endif

// long sys_set_tid_address(int __user *tidptr);
// extern "C" int   sigqueue(pid_t pid, int signo, const union sigval value)


// long sys_getuid(void);
// long sys_geteuid(void);
// long sys_getgid(void);
// long sys_getegid(void);
// long sys_getresuid(uid_t __user *ruid, uid_t __user *euid, uid_t __user *suid);
// long sys_getresgid(gid_t __user *rgid, gid_t __user *egid, gid_t __user *sgid);
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
//
// long sys_sched_setscheduler(pid_t pid, int policy,
// 					struct sched_param __user *param);
// long sys_sched_setparam(pid_t pid,
// 					struct sched_param __user *param);
// long sys_sched_getscheduler(pid_t pid);
// long sys_sched_getparam(pid_t pid,
// 					struct sched_param __user *param);
// long sys_sched_setaffinity(pid_t pid, unsigned int len,
// 					unsigned long __user *user_mask_ptr);
// long sys_sched_getaffinity(pid_t pid, unsigned int len,
// 					unsigned long __user *user_mask_ptr);
// long sys_sched_yield(void);
// long sys_sched_rr_get_interval(pid_t pid,
//
//
// long sys_rt_sigqueueinfo(int pid, int sig, siginfo_t __user *uinfo);
//
//
//
//
// long sys_chown(const char __user *filename,
// 				uid_t user, gid_t group);
// long sys_lchown(const char __user *filename,
// 				uid_t user, gid_t group);
// long sys_fchown(unsigned int fd, uid_t user, gid_t group);
// #ifdef CONFIG_UID16
// long sys_chown16(const char __user *filename,
// 				old_uid_t user, old_gid_t group);
// long sys_lchown16(const char __user *filename,
// 				old_uid_t user, old_gid_t group);
// long sys_fchown16(unsigned int fd, old_uid_t user, old_gid_t group);
// long sys_setregid16(old_gid_t rgid, old_gid_t egid);
// long sys_setgid16(old_gid_t gid);
// long sys_setreuid16(old_uid_t ruid, old_uid_t euid);
// long sys_setuid16(old_uid_t uid);
// long sys_setresuid16(old_uid_t ruid, old_uid_t euid, old_uid_t suid);
// long sys_getresuid16(old_uid_t __user *ruid,
// 				old_uid_t __user *euid, old_uid_t __user *suid);
// long sys_setresgid16(old_gid_t rgid, old_gid_t egid, old_gid_t sgid);
// long sys_getresgid16(old_gid_t __user *rgid,
// 				old_gid_t __user *egid, old_gid_t __user *sgid);
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
// 			    const char __user *_description,
// 			    const void __user *_payload,
// 			    size_t plen,
// 			    key_serial_t destringid);
//
// long sys_request_key(const char __user *_type,
// 				const char __user *_description,
// 				const char __user *_callout_info,
// 				key_serial_t destringid);
//
//
// long sys_migrate_pages(pid_t pid, unsigned long maxnode,
// 				const unsigned long __user *from,
// 				const unsigned long __user *to);
// long sys_move_pages(pid_t pid, unsigned long nr_pages,
// 				const void __user * __user *pages,
// 				const int __user *nodes,
// 				int __user *status,
// 				int flags);
// long compat_sys_move_pages(pid_t pid, unsigned long nr_page,
// 				__u32 __user *pages,
// 				const int __user *nodes,
// 				int __user *status,
// 				int flags);
//
// long sys_fchownat(int dfd, const char __user *filename, uid_t user,
// 			     gid_t group, int flag);
//
// long sys_get_robust_list(int pid,
// 				    struct robust_list_head __user * __user *head_ptr,
// 				    size_t __user *len_ptr);
//

