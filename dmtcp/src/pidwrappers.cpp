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
#include "syslogcheckpointer.h"
#include  "../jalib/jconvert.h"
#include "constants.h"
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <thread_db.h>
#include <sys/procfs.h>

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

pid_t gettid()
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  /*
   * We might want to cache the tid of all threads to avoid redundant calls
   *  to _real_gettid() and currentToOriginalPid().
   * To cache, we must make sure that this function is invoked by each thread
   *  at least once prior to checkpoint.
   * __thread can be used along with static storage class to make this cached
   *  value specific to each thread
   */
  pid_t currentTid = _real_gettid();
  pid_t origTid =  currentToOriginalPid ( currentTid );

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return origTid;
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

  pid_t ppid = _real_getppid();
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
  WRAPPER_EXECUTION_DISABLE_CKPT();

  pid_t currPid = originalToCurrentPid (pid);

  int retVal = _real_kill (currPid, sig);

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return retVal;
}

int   tkill(int tid, int sig)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  int currentTid = originalToCurrentPid ( tid );

  int retVal = _real_tkill ( currentTid, sig );

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return retVal;
}

int   tgkill(int tgid, int tid, int sig)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  int currentTgid = originalToCurrentPid ( tgid );
  int currentTid = originalToCurrentPid ( tid );

  int retVal = _real_tgkill ( currentTgid, currentTid, sig );

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return retVal;
}


//long sys_tgkill (int tgid, int pid, int sig)

// long ptrace(enum __ptrace_request request, pid_t pid, void *addr, void *data)

#ifdef PTRACE

#define TRUE 1
#define FALSE 0

extern "C" td_err_e   _dmtcp_td_thr_get_info ( const td_thrhandle_t  *th_p,
                                               td_thrinfo_t *ti_p)
{
  td_err_e td_err;

  td_err = _real_td_thr_get_info ( th_p, ti_p);
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
  if ( strcmp ( symbol, "td_thr_get_info" ) == 0 )
    return (void *) &_dmtcp_td_thr_get_info;
  else
    return _real_dlsym ( handle, symbol );
}

typedef void ( *set_singlestep_waited_on_t ) ( pid_t superior, pid_t inferior, int 
value );
extern "C" set_singlestep_waited_on_t set_singlestep_waited_on_ptr;

typedef int ( *get_is_waitpid_local_t ) ();
extern "C" get_is_waitpid_local_t get_is_waitpid_local_ptr;

typedef void ( *unset_is_waitpid_local_t ) ();
extern "C" unset_is_waitpid_local_t unset_is_waitpid_local_ptr;

typedef pid_t ( *get_saved_pid_t) ( );
extern "C" get_saved_pid_t get_saved_pid_ptr;

typedef int ( *get_saved_status_t) ( );
extern "C" get_saved_status_t get_saved_status_ptr;

typedef int ( *get_has_status_and_pid_t) ( );
extern "C" get_has_status_and_pid_t get_has_status_and_pid_ptr;

typedef void ( *reset_pid_status_t) ( );
extern "C" reset_pid_status_t reset_pid_status_ptr;

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

extern "C" pid_t waitpid(pid_t pid, int *stat_loc, int options)
{
  int status;
#ifdef PTRACE
  pid_t superior;
  pid_t inferior;
  pid_t retval;
  static int i = 0;
  pid_t originalPid;
#endif

  if ( stat_loc == NULL )
    stat_loc = &status;

  pid_t currPid = originalToCurrentPid (pid);

#ifdef PTRACE
  superior = syscall (SYS_gettid);

  inferior = pid;

  if (!get_is_waitpid_local_ptr ()) {
        if (get_has_status_and_pid_ptr ()) {
                *stat_loc = get_saved_status_ptr ();
                retval = get_saved_pid_ptr ();
                reset_pid_status_ptr ();
        }
        else {
                if (_real_pthread_sigmask (SIG_BLOCK, &signals_set, NULL) != 0) {
                        perror ("waitpid wrapper");
                        exit(-1);
                }
                set_singlestep_waited_on_ptr (superior, inferior, TRUE);
                retval = _real_waitpid (currPid, stat_loc, options);
                originalPid = currentToOriginalPid (retval);
                if (_real_pthread_sigmask (SIG_UNBLOCK, &signals_set, NULL) != 0) {
                        perror ("waitpid wrapper");
                        exit(-1);
                }
        }
  }
  else {
        retval = _real_waitpid (currPid, stat_loc, options);
        unset_is_waitpid_local_ptr ();
        originalPid = currentToOriginalPid (retval);
  }
#else 
  pid_t retval = _real_waitpid (currPid, stat_loc, options);

  pid_t originalPid = currentToOriginalPid ( retval );
#endif

  if ( retval > 0
       && ( WIFEXITED ( *stat_loc )  || WIFSIGNALED ( *stat_loc ) ) )
    dmtcp::VirtualPidTable::instance().erase(originalPid);

  return originalPid;
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

// TODO:  ioctl must use virtualized pids for request = TIOCGPGRP / TIOCSPGRP
// These are synonyms for POSIX standard tcgetpgrp / tcsetpgrp
extern "C" {
int send_sigwinch = 0;
}
extern "C" int ioctl(int d,  unsigned long int request, ...)
{ va_list ap;
  int rc;

  if (send_sigwinch && request == TIOCGWINSZ) {
    send_sigwinch = 0;
    va_list local_ap;
    va_copy(local_ap, ap);
    va_start(local_ap, request);
    struct winsize * win = va_arg(local_ap, struct winsize *);
    va_end(local_ap);
    rc = _real_ioctl(d, request, win);  // This fills in win
    win->ws_col--; // Lie to application, and force it to resize window,
		   //  reset any scroll regions, etc.
    kill(getpid(), SIGWINCH); // Tell application to look up true winsize
			      // and resize again.
  } else {
    void * arg;
    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);
    rc = _real_ioctl(d, request, arg);
  }

  return rc;
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

