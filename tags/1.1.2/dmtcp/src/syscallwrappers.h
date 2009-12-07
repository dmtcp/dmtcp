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

#ifndef SYSCALLWRAPPERS_H
#define SYSCALLWRAPPERS_H

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "constants.h"
#include <sys/ptrace.h>
#include <stdarg.h>
#include <asm/ldt.h>
#include <stdio.h>
#include <thread_db.h>
#include <sys/procfs.h>

#ifdef __cplusplus
extern "C"
{
#endif


  int _real_socket ( int domain, int type, int protocol );
  int _real_connect ( int sockfd,  const  struct sockaddr *serv_addr, socklen_t addrlen );
  int _real_bind ( int sockfd,  const struct  sockaddr  *my_addr,  socklen_t addrlen );
  int _real_listen ( int sockfd, int backlog );
  int _real_accept ( int sockfd, struct sockaddr *addr, socklen_t *addrlen );
  int _real_setsockopt ( int s, int  level,  int  optname,  const  void  *optval,
                         socklen_t optlen );


  int _real_fexecve ( int fd, char *const argv[], char *const envp[] );
  int _real_execve ( const char *filename, char *const argv[], char *const envp[] );
  int _real_execv ( const char *path, char *const argv[] );
  int _real_execvp ( const char *file, char *const argv[] );
// int _real_execl(const char *path, const char *arg, ...);
// int _real_execlp(const char *file, const char *arg, ...);
// int _real_execle(const char *path, const char *arg, ..., char * const envp[]);
  int _real_system ( const char * cmd );

  int _real_close ( int fd );

//we no longer wrap dup
#define _real_dup  dup
#define _real_dup2 dup2
//int _real_dup(int oldfd);
//int _real_dup2(int oldfd, int newfd);

  int _real_ptsname_r ( int fd, char * buf, size_t buflen );

  int _real_socketpair ( int d, int type, int protocol, int sv[2] );

  void _real_openlog ( const char *ident, int option, int facility );
  void _real_closelog ( void );

  pid_t _real_fork();

  typedef void (*sighandler_t)(int);

  //set the handler
  sighandler_t _real_signal(int signum, sighandler_t handler);
  int _real_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
  int _real_rt_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
  int _real_sigvec(int sig, const struct sigvec *vec, struct sigvec *ovec);

  //set the mask
  int _real_sigblock(int mask);
  int _real_sigsetmask(int mask);
  int _real_siggetmask(void);
  int _real_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
  int _real_rt_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
  int _real_pthread_sigmask(int how, const sigset_t *newmask, sigset_t *oldmask);

  void _dmtcp_lock();
  void _dmtcp_unlock();

  void _dmtcp_remutex_on_fork();

  int _dmtcp_unsetenv(const char *name);

#ifdef PID_VIRTUALIZATION
  pid_t _real_getpid(void);
  pid_t _real_getppid(void);
  pid_t _real_gettid(void);

  pid_t _real_tcgetpgrp(int fd);
  int   _real_tcsetpgrp(int fd, pid_t pgrp);

  pid_t _real_getpgrp(void);
  pid_t _real_setpgrp(void);
  
  pid_t _real_getpgid(pid_t pid);
  int   _real_setpgid(pid_t pid, pid_t pgid);
  
  pid_t _real_getsid(pid_t pid);
  pid_t _real_setsid(void);
  
  int   _real_kill(pid_t pid, int sig);

  int   _real_tkill(int tid, int sig);
  int   _real_tgkill(int tgid, int tid, int sig);
  
  pid_t _real_wait(__WAIT_STATUS stat_loc);
  pid_t _real_waitpid(pid_t pid, int *stat_loc, int options);
  int   _real_waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options);

  pid_t _real_wait3(__WAIT_STATUS status, int options,      struct rusage *rusage);
  pid_t _real_wait4(pid_t pid, __WAIT_STATUS status, int options,      struct rusage *rusage);

  int _real_open(const char *pathname, int flags, mode_t mode);
  FILE * _real_fopen(const char *path, const char *mode);

#endif /* PID_VIRTUALIZATION */

  long int _real_syscall(long int sys_num, ... );
  
  int _real_clone ( int ( *fn ) ( void *arg ), void *child_stack, int flags, void *arg, int *parent_tidptr, struct user_desc *newtls, int *child_tidptr );

#ifdef __cplusplus
}
#endif

#endif

