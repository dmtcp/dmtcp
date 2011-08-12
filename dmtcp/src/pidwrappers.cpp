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

// FIXME:  We need a better way to get MTCP_DEFAULT_SIGNAL
#include "../../mtcp/mtcp.h" //for MTCP_DEFAULT_SIGNAL

#ifdef PID_VIRTUALIZATION

static pid_t originalToCurrentPid( pid_t originalPid )
{
  /* This code is called from MTCP while the checkpoint thread is holding
     the JASSERT log lock. Therefore, don't call JTRACE/JASSERT/JINFO/etc. in
     this function. */
  pid_t currentPid = dmtcp::VirtualPidTable::instance().originalToCurrentPid( originalPid );

  if (currentPid == -1)
    currentPid = originalPid;

  return currentPid;
}

static pid_t currentToOriginalPid( pid_t currentPid )
{
  /* This code is called from MTCP while the checkpoint thread is holding
     the JASSERT log lock. Therefore, don't call JTRACE/JASSERT/JINFO/etc. in
     this function. */
  pid_t originalPid = dmtcp::VirtualPidTable::instance().currentToOriginalPid( currentPid );

  if (originalPid == -1)
    originalPid = currentPid;

  return originalPid;
}

extern "C" pid_t gettid()
{
  /* mtcpinterface.cpp:thread_start calls gettid() before calling
   * DmtcpWorker::decrementUninitializedThreadCount() and so the value is
   * cached before it is accessed by some other DMTCP code.
   */

  if (dmtcp_thread_tid == -1) {
    dmtcp_thread_tid = _real_gettid();
  }
  return dmtcp_thread_tid;
}

extern "C" pid_t getpid()
{
  //pid_t pid = _real_getpid();//dmtcp::UniquePid::ThisProcess().pid();

  //return currentToOriginalPid ( pid );
  return dmtcp::VirtualPidTable::instance().pid();
}

extern "C" pid_t getppid()
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  if ( _real_getppid() == 1 )
  {
    dmtcp::VirtualPidTable::instance().setppid( 1 );
  }

  pid_t origPpid = dmtcp::VirtualPidTable::instance().ppid( );

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return origPpid;
}

extern "C" int   tcsetpgrp(int fd, pid_t pgrp)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  pid_t currPgrp = originalToCurrentPid( pgrp );
//  JTRACE( "Inside tcsetpgrp wrapper" ) (fd) (pgrp) (currPgrp);
  int retVal = _real_tcsetpgrp(fd, currPgrp);

  //JTRACE( "tcsetpgrp return value" ) (fd) (pgrp) (currPgrp) (retval);
  WRAPPER_EXECUTION_ENABLE_CKPT();

  return retVal;
}

extern "C" pid_t tcgetpgrp(int fd)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  pid_t retval = currentToOriginalPid( _real_tcgetpgrp(fd) );

  //JTRACE ( "tcgetpgrp return value" ) (fd) (retval);
  WRAPPER_EXECUTION_ENABLE_CKPT();

  return retval;
}

extern "C" pid_t getpgrp(void)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  pid_t pgrp = _real_getpgrp();
  pid_t origPgrp =  currentToOriginalPid( pgrp );

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return origPgrp;
}

extern "C" pid_t setpgrp(void)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  pid_t pgrp = _real_setpgrp();
  pid_t origPgrp = currentToOriginalPid( pgrp );

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return origPgrp;
}

extern "C" pid_t getpgid(pid_t pid)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  pid_t currentPid = originalToCurrentPid (pid);
  pid_t res = _real_getpgid (currentPid);
  pid_t origPgid = currentToOriginalPid (res);

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return origPgid;
}

extern "C" int   setpgid(pid_t pid, pid_t pgid)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  pid_t currPid = originalToCurrentPid (pid);
  pid_t currPgid = originalToCurrentPid (pgid);

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
    currPid = originalToCurrentPid (pid);
  else
    currPid = _real_getpid();

  pid_t res = _real_getsid (currPid);

  pid_t origSid = currentToOriginalPid (res);

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return origSid;
}

extern "C" pid_t setsid(void)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  pid_t pid = _real_setsid();
  pid_t origPid = currentToOriginalPid (pid);
  dmtcp::VirtualPidTable::instance().setsid(origPid);

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
   *     pid_t currPid = originalToCurrentPid(pid);
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

  pid_t currPid = originalToCurrentPid (pid);

  int retVal = _real_kill (currPid, sig);

//  WRAPPER_EXECUTION_ENABLE_CKPT();

  return retVal;
}

LIB_PRIVATE
int tkill(int tid, int sig)
{
  // FIXME: Check the comments in kill()
//  WRAPPER_EXECUTION_DISABLE_CKPT();

  int currentTid = originalToCurrentPid ( tid );

  int retVal = _real_tkill ( currentTid, sig );

//  WRAPPER_EXECUTION_ENABLE_CKPT();

  return retVal;
}

LIB_PRIVATE
int tgkill(int tgid, int tid, int sig)
{
  // FIXME: Check the comments in kill()
//  WRAPPER_EXECUTION_DISABLE_CKPT();

  int currentTgid = originalToCurrentPid ( tgid );
  int currentTid = originalToCurrentPid ( tid );

  int retVal = _real_tgkill ( currentTgid, currentTid, sig );

//  WRAPPER_EXECUTION_ENABLE_CKPT();

  return retVal;
}


//long sys_tgkill (int tgid, int pid, int sig)

#ifdef PTRACE

#define TRUE 1
#define FALSE 0

typedef td_err_e (*td_thr_get_info_funcptr_t)(const td_thrhandle_t *,
                                              td_thrinfo_t *);
static td_thr_get_info_funcptr_t _td_thr_get_info_funcptr = NULL;
static td_err_e  _dmtcp_td_thr_get_info (const td_thrhandle_t  *th_p,
                                         td_thrinfo_t *ti_p)
{
  td_err_e td_err;

  td_err = (*_td_thr_get_info_funcptr)(th_p, ti_p);

  ti_p->ti_lid  =  ( lwpid_t ) currentToOriginalPid ( ( int ) ti_p->ti_lid );
  ti_p->ti_tid =  ( thread_t ) currentToOriginalPid ( (int ) ti_p->ti_tid );
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

extern "C" void ptrace_info_list_update_info(pid_t superior, pid_t inferior,
                                             int singlestep_waited_on);

typedef struct ptrace_waitpid_info ( *t_mtcp_get_ptrace_waitpid_info) ( );
extern "C" t_mtcp_get_ptrace_waitpid_info mtcp_get_ptrace_waitpid_info;

typedef int ( *fill_in_pthread_t) ();
extern "C" fill_in_pthread_t fill_in_pthread_ptr;

typedef int ( *delete_thread_on_pthread_join_t) ();
extern "C" delete_thread_on_pthread_join_t delete_thread_on_pthread_join_ptr;

extern "C" sigset_t signals_set;
#endif

/*
 * TODO: Add the wrapper protection for wait() family of system calls.
 *       It wouldn't be a straight forward process, we need to take care of the
 *         _BLOCKING_ property of these system calls.
 *                                                      --KAPIL
 */

extern "C" pid_t wait (__WAIT_STATUS stat_loc)
//extern "C" pid_t wait(int *stat_loc)
{
  pid_t retVal = _real_wait (stat_loc);

  if (retVal > 0) {
    pid_t pid = currentToOriginalPid (retVal);

    if ( pid > 0 )
      dmtcp::VirtualPidTable::instance().erase(pid);

    return pid;
  }
  return retVal;
}

/* UNUSED FUNCTION
extern "C" {
static int get_sigckpt() {
  static char *nptr = getenv(ENV_VAR_SIGCKPT);
  static int sigckpt = -1;
  if (sigckpt == -1) {
    char *endptr;
    if (nptr == NULL)
      sigckpt = MTCP_DEFAULT_SIGNAL;
    else {
      sigckpt = strtol(nptr, &endptr, 0);
      if (endptr != '\0')
        sigckpt = MTCP_DEFAULT_SIGNAL;
    }
  }
  return sigckpt;
}
}
*/

LIB_PRIVATE
pid_t safe_real_waitpid(pid_t pid, int *stat_loc, int options) {
  // Note that if the action for SIGCHLD is set to SIG_IGN, then waitpid fails
  //  with errno set to ECHLD (as if the child process was not really our child)
  //  We currently do not specially handle this case.
  while (1) {
    /* The checkpoint signal uses SA_RESTART which causes the system call to
     * restart if it were interrupted by this signal. However, on restart, the
     * currPid will not be correct anymore and hence the restarted waitpid()
     * will fail with ECHILD.
     * TODO: Note that the currPid after restart might be equal to the
     *       current-pid of some other child process resulting in waitpid
     *       waiting un-necessarily for getting status of that other child
     *       process. This could be fixed in the sa_restorer function of
     *       sigaction in mtcp/mtcp_sigaction.c.
     */
    pid_t currPid = originalToCurrentPid(pid);
    pid_t retval = _real_waitpid(currPid, stat_loc, options);

    WRAPPER_EXECUTION_DISABLE_CKPT();

    /* Verify there was no checkpoint/restart between currpid and _real_waitpid.
     * Note:  A checkpoint/restart after _real_waitpid does not need a verify.
     * TODO: There is a possible bug -- if by conincidence the currentPid
     *       becomes equal to curPid, this will cause the if condition to go
     *       false, which is wrong.
     */
    if ( retval == -1 && errno == ECHILD &&
         currPid != originalToCurrentPid(pid) ) {
      struct sigaction oldact;
      if ( sigaction(SIGCHLD, NULL, &oldact) == 0 &&
           oldact.sa_handler != SIG_IGN ) {
        WRAPPER_EXECUTION_ENABLE_CKPT();
        continue;
      }
    }
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retval;
  }
}


extern "C" pid_t waitpid(pid_t pid, int *stat_loc, int options)
{
  int status;
  pid_t originalPid;
  pid_t retval;

  if ( stat_loc == NULL )
    stat_loc = &status;

#ifdef PTRACE
  pid_t superior = syscall(SYS_gettid);
  pid_t inferior = pid;
  struct ptrace_waitpid_info pwi = mtcp_get_ptrace_waitpid_info();

  if (pwi.is_waitpid_local) {
    retval = safe_real_waitpid (pid, stat_loc, options);
  } else {
    /* Where was status and pid saved?  Can we remove this code?  - Gene */
    if (pwi.has_status_and_pid) {
      *stat_loc = pwi.saved_status;
      retval = pwi.saved_pid;
    } else {
// Please remove this comment and all code related to BLOCK_CKPT_ON_WAIT
//  when satisfied waitpid wrapper work.  - Gene
#undef BLOCK_CKPT_ON_WAIT
#if BLOCK_CKPT_ON_WAIT
      if (_real_pthread_sigmask(SIG_BLOCK, &signals_set, NULL) != 0) {
        perror ("waitpid wrapper");
        exit(-1);
      }
#endif
      ptrace_info_list_update_info(superior, inferior, TRUE);
      retval = safe_real_waitpid(pid, stat_loc, options);
#if BLOCK_CKPT_ON_WAIT
      if (_real_pthread_sigmask(SIG_UNBLOCK, &signals_set, NULL) != 0) {
        perror("waitpid wrapper");
        exit(-1);
      }
#endif
    }
  }
#else
  retval = safe_real_waitpid(pid, stat_loc, options);
#endif

  if (retval > 0) {
    originalPid = currentToOriginalPid(retval);
    if ( WIFEXITED(*stat_loc)  || WIFSIGNALED(*stat_loc) )
      dmtcp::VirtualPidTable::instance().erase(originalPid);
    return originalPid;
  } else {
    return retval;
  }
}

extern "C" int   waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options)
{
  siginfo_t status;

  if ( infop == NULL )
    infop = &status;

  pid_t currPd = originalToCurrentPid (id);

  int retval = _real_waitid (idtype, currPd, infop, options);

  if (retval != -1) {
    pid_t originalPid = currentToOriginalPid ( infop->si_pid );
    infop->si_pid = originalPid;

    if ( infop->si_code == CLD_EXITED || infop->si_code == CLD_KILLED )
      dmtcp::VirtualPidTable::instance().erase ( originalPid );
  }

  return retval;
}

extern "C" pid_t wait3(__WAIT_STATUS status, int options, struct rusage *rusage)
{
  pid_t retval = _real_wait3 (status, options, rusage);

  pid_t originalPid = currentToOriginalPid ( retval );

  if ( originalPid > 0 )
    dmtcp::VirtualPidTable::instance().erase(originalPid);

  return originalPid;
}

extern "C" pid_t wait4(pid_t pid, __WAIT_STATUS status, int options, struct rusage *rusage)
{
  int stat;

  if ( status == NULL )
    status = (__WAIT_STATUS) &stat;

  pid_t currPid = originalToCurrentPid (pid);

  pid_t retval = _real_wait4 ( currPid, status, options, rusage );

  pid_t originalPid = currentToOriginalPid ( retval );

  if ( retval > 0
       && ( WIFEXITED ( * (int*) status )  || WIFSIGNALED ( * (int*) status ) ) )
    dmtcp::VirtualPidTable::instance().erase ( originalPid );

  return originalPid;
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

