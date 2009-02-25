/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifdef PID_VIRTUALIZATION                                                         

static pid_t originalToCurrentPid( pid_t originalPid )
{
  pid_t currentPid = dmtcp::VirtualPidTable::Instance().originalToCurrentPid( originalPid );
  
  if (currentPid == -1)
    currentPid = originalPid;
  
  return currentPid;
}

static pid_t currentToOriginalPid( pid_t currentPid )
{
  pid_t originalPid = dmtcp::VirtualPidTable::Instance().currentToOriginalPid( currentPid );
  
  if (originalPid == -1)
    originalPid = currentPid;
  
  return originalPid;
}


extern "C" pid_t getpid()
{
  //pid_t pid = _real_getpid();//dmtcp::UniquePid::ThisProcess().pid();
  
  //return currentToOriginalPid ( pid );
  return dmtcp::VirtualPidTable::Instance().pid(); 
}

extern "C" pid_t getppid()
{
  pid_t ppid = _real_getppid();
  if ( _real_getppid() == 1 )
  {
    dmtcp::VirtualPidTable::Instance().setppid( 1 );
  }

  return dmtcp::VirtualPidTable::Instance().ppid( );
}

extern "C" int   tcsetpgrp(int fd, pid_t pgrp)
{
  pid_t currPgrp = originalToCurrentPid( pgrp );
//  JTRACE( "Inside tcsetpgrp wrapper" ) (fd) (pgrp) (currPgrp); 
  int retval = _real_tcsetpgrp(fd, currPgrp);

  JTRACE( "tcsetpgrp return value" ) (fd) (pgrp) (currPgrp) (retval);
  return retval;
}

extern "C" pid_t tcgetpgrp(int fd)
{
  pid_t retval = currentToOriginalPid( _real_tcgetpgrp(fd) );

  JTRACE ( "tcgetpgrp return value" ) (fd) (retval);
  return retval;
}

extern "C" pid_t gettid()
{
  pid_t currentTid = _real_gettid();

  return currentToOriginalPid ( currentTid );
}

extern "C" pid_t getpgrp(void)
{
  pid_t pgrp = _real_getpgrp();
  return currentToOriginalPid( pgrp );
}

extern "C" pid_t setpgrp(void)
{
  pid_t pgrp = _real_setpgrp();
  return currentToOriginalPid( pgrp );
}

extern "C" pid_t getpgid(pid_t pid)
{
  pid_t currentPid = originalToCurrentPid (pid);
  pid_t res = _real_getpgid (currentPid);
  return currentToOriginalPid (res);
}

extern "C" int   setpgid(pid_t pid, pid_t pgid)
{
  pid_t currPid = originalToCurrentPid (pid);
  pid_t currPgid = originalToCurrentPid (pgid);

  return _real_setpgid (currPid, currPgid);
}

extern "C" pid_t getsid(pid_t pid)
{
  pid_t currPid = originalToCurrentPid (pid);
  
  pid_t res = _real_getsid (currPid);

  return currentToOriginalPid (res);
}

extern "C" pid_t setsid(void)
{
  pid_t pid = _real_setsid();

  return currentToOriginalPid (pid);
}

extern "C" int   kill(pid_t pid, int sig)
{
  pid_t currPid = originalToCurrentPid (pid);

  return _real_kill (currPid, sig);
}

extern "C" int   tkill(int tid, int sig)
{
  int currentTid = originalToCurrentPid ( tid );
  
  return _real_tkill ( currentTid, sig );
}

extern "C" int   tgkill(int tgid, int tid, int sig)
{
  int currentTgid = originalToCurrentPid ( tgid );
  int currentTid = originalToCurrentPid ( tid );
  
  return _real_tgkill ( currentTgid, currentTid, sig );
}


//long sys_tgkill (int tgid, int pid, int sig)

// long ptrace(enum __ptrace_request request, pid_t pid, void *addr, void *data)

extern "C" pid_t wait (__WAIT_STATUS stat_loc)
//extern "C" pid_t wait(int *stat_loc)
{
  pid_t retval = _real_wait (stat_loc);

  pid_t pid = currentToOriginalPid (retval);

  if ( pid > 0 )
    dmtcp::VirtualPidTable::Instance().erase(pid);

  return pid;
}

extern "C" pid_t waitpid(pid_t pid, int *stat_loc, int options)
{
  int status;

  if ( stat_loc == NULL )
    stat_loc = &status;
  
  pid_t currPid = originalToCurrentPid (pid);

  pid_t retval = _real_waitpid (currPid, stat_loc, options);

  pid_t originalPid = currentToOriginalPid ( retval );

  if ( retval > 0
       && ( WIFEXITED ( *stat_loc )  || WIFSIGNALED ( *stat_loc ) ) )
    dmtcp::VirtualPidTable::Instance().erase(originalPid);

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
      dmtcp::VirtualPidTable::Instance().erase ( originalPid );
  }

  return retval;
}

extern "C" pid_t wait3(__WAIT_STATUS status, int options, struct rusage *rusage)
{
  pid_t retval = _real_wait3 (status, options, rusage);
  
  pid_t originalPid = currentToOriginalPid ( retval );

  if ( originalPid > 0 )
    dmtcp::VirtualPidTable::Instance().erase(originalPid);

  return originalPid;
}

extern "C" pid_t wait4(pid_t pid, __WAIT_STATUS status, int options, struct rusage *rusage)
{
  int stat;

  if ( status == NULL )
    status = (__WAIT_STATUS) &stat;
  
  pid_t currPid = originalToCurrentPid (pid);

  pid_t retval = _real_wait4 ( currPid, status, options, rusage );;

  pid_t originalPid = currentToOriginalPid ( retval );

  if ( retval > 0
       && ( WIFEXITED ( * (int*) status )  || WIFSIGNALED ( * (int*) status ) ) )
    dmtcp::VirtualPidTable::Instance().erase ( originalPid );

  return originalPid;
}


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




#endif
