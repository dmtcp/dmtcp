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
#include <syslog.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef PID_VIRTUALIZATION
# define GLIBC_PID_FAMILY_WRAPPERS(MACRO)   \
  MACRO(getpid)                             \
  MACRO(getppid)                            \
  MACRO(kill)                               \
                                            \
  MACRO(tcgetpgrp)                          \
  MACRO(tcsetpgrp)                          \
  MACRO(getpgrp)                            \
  MACRO(setpgrp)                            \
                                            \
  MACRO(getpgid)                            \
  MACRO(setpgid)                            \
  MACRO(getsid)                             \
  MACRO(setsid)                             \
  MACRO(setgid)                             \
  MACRO(setuid)                             \
                                            \
  MACRO(wait)                               \
  MACRO(waitpid)                            \
  MACRO(waitid)                             \
  MACRO(wait3)                              \
  MACRO(wait4)
#else
# define GLIBC_PID_FUNC_WRAPPERS(MACRO)
#endif /* PID_VIRTUALIZATION */

#ifdef ENABLE_MALLOC_WRAPPER
# define GLIBC_MALLOC_FAMILY_WRAPPERS(MACRO)\
  MACRO(calloc)                             \
  MACRO(malloc)                             \
  MACRO(free)                               \
  MACRO(realloc)
#else
# define GLIBC_MALLOC_FAMILY_WRAPPERS(MACRO)
#endif 

/* First group below is candidates for glibc_base_func_addr in syscallsreal.c
 * We can't tell which ones were already re-defined by the user executable.
 * For example, /bin/dash defines isalnum in Ubuntu 9.10.
 * Note that a system call can't be a base fnc if we are already wrapping it.
 */
#define FOREACH_GLIBC_BASE_FUNC(MACRO)      \
  MACRO(isalnum)			    \
  MACRO(clearerr)			    \
  MACRO(getopt)				    \
  MACRO(perror)				    \
  MACRO(fscanf)

#define GLIBC_SOCKET_WRAPPERS(MACRO)        \
  MACRO(socket)                             \
  MACRO(connect)                            \
  MACRO(bind)                               \
  MACRO(listen)                             \
  MACRO(accept)                             \
  MACRO(accept4)                             \
  MACRO(setsockopt)                         \
  MACRO(socketpair)

#define GLIBC_EXEC_WRAPPERS(MACRO)          \
  MACRO(fexecve)                            \
  MACRO(execve)                             \
  MACRO(execv)                              \
  MACRO(execvp)                             \
  MACRO(execl)                              \
  MACRO(execlp)                             \
  MACRO(execle)                             \
  MACRO(system)

#define GLIBC_SIGNAL_WRAPPERS(MACRO)        \
  MACRO(signal)                             \
  MACRO(sigaction)                          \
  MACRO(sigvec)                             \
                                            \
  MACRO(sigblock)                           \
  MACRO(sigsetmask)                         \
  MACRO(siggetmask)                         \
  MACRO(sigprocmask)                        \
                                            \
  MACRO(sigwait)                            \
  MACRO(sigwaitinfo)                        \
  MACRO(sigtimedwait)

#define GLIBC_MISC_WRAPPERS(MACRO)          \
  MACRO(fork)                               \
  MACRO(__clone)                            \
  MACRO(open)                               \
  MACRO(fopen)                              \
  MACRO(close)                              \
  MACRO(fclose)                             \
  MACRO(exit)                               \
  MACRO(syscall)                            \
  MACRO(unsetenv)                           \
  MACRO(ptsname_r)                          \
  MACRO(getpt)                              \
  MACRO(openlog)                            \
  MACRO(closelog)

#define GLIBC_SYS_V_IPC_WRAPPERS(MACRO)     \
  MACRO(shmget)                             \
  MACRO(shmat)                              \
  MACRO(shmdt)                              \
  MACRO(shmctl)

/* FOREACH_GLIBC_BASE_FUNC (MACRO) must appear first. */
#define FOREACH_GLIBC_FUNC_WRAPPER(MACRO)   \
  FOREACH_GLIBC_BASE_FUNC(MACRO)	    \
                                            \
  GLIBC_SOCKET_WRAPPERS(MACRO)              \
  GLIBC_EXEC_WRAPPERS(MACRO)                \
  GLIBC_SIGNAL_WRAPPERS(MACRO)              \
  GLIBC_MISC_WRAPPERS(MACRO)                \
  GLIBC_SYS_V_IPC_WRAPPERS(MACRO)           \
                                            \
  GLIBC_PID_FAMILY_WRAPPERS(MACRO)          \
  GLIBC_MALLOC_FAMILY_WRAPPERS(MACRO)


# define ENUM(x) enum_ ## x
# define GEN_ENUM(x) ENUM(x),
  typedef enum {
    FOREACH_GLIBC_FUNC_WRAPPER(GEN_ENUM)
    numLibcWrappers
  } LibcWrapperOffset;

  void _dmtcp_lock();
  void _dmtcp_unlock();

  void _dmtcp_remutex_on_fork();

  int _dmtcp_unsetenv(const char *name);

  int _real_socket ( int domain, int type, int protocol );
  int _real_connect ( int sockfd,  const  struct sockaddr *serv_addr, socklen_t addrlen );
  int _real_bind ( int sockfd,  const struct  sockaddr  *my_addr,  socklen_t addrlen );
  int _real_listen ( int sockfd, int backlog );
  int _real_accept ( int sockfd, struct sockaddr *addr, socklen_t *addrlen );
  int _real_accept4 ( int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags );
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
  int _real_fclose ( FILE *fp );
  void _real_exit ( int status );

//we no longer wrap dup
#define _real_dup  dup
#define _real_dup2 dup2
//int _real_dup(int oldfd);
//int _real_dup2(int oldfd, int newfd);

  int _real_ptsname_r ( int fd, char * buf, size_t buflen );
  int _real_getpt ( void );

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

  int _real_sigwait(const sigset_t *set, int *sig);
  int _real_sigwaitinfo(const sigset_t *set, siginfo_t *info);
  int _real_sigtimedwait(const sigset_t *set, siginfo_t *info,
                         const struct timespec *timeout);

#ifdef PID_VIRTUALIZATION
  pid_t _real_getpid(void);
  pid_t _real_getppid(void);

  pid_t _real_tcgetpgrp(int fd);
  int   _real_tcsetpgrp(int fd, pid_t pgrp);

  pid_t _real_getpgrp(void);
  pid_t _real_setpgrp(void);

  pid_t _real_getpgid(pid_t pid);
  int   _real_setpgid(pid_t pid, pid_t pgid);

  pid_t _real_getsid(pid_t pid);
  pid_t _real_setsid(void);

  int   _real_kill(pid_t pid, int sig);

  pid_t _real_wait(__WAIT_STATUS stat_loc);
  pid_t _real_waitpid(pid_t pid, int *stat_loc, int options);
  int   _real_waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options);

  pid_t _real_wait3(__WAIT_STATUS status, int options,      struct rusage *rusage);
  pid_t _real_wait4(pid_t pid, __WAIT_STATUS status, int options,      struct rusage *rusage);

  int _real_setgid(gid_t gid);
  int _real_setuid(uid_t uid);

#endif /* PID_VIRTUALIZATION */

  pid_t _real_gettid(void);
  int   _real_tkill(int tid, int sig);
  int   _real_tgkill(int tgid, int tid, int sig);

  int _real_open(const char *pathname, int flags, mode_t mode);
  FILE * _real_fopen(const char *path, const char *mode);

  long int _real_syscall(long int sys_num, ... );

  int _real_clone ( int ( *fn ) ( void *arg ), void *child_stack, int flags, void *arg, int *parent_tidptr, struct user_desc *newtls, int *child_tidptr );

  int _real_shmget(key_t key, size_t size, int shmflg);
  void* _real_shmat(int shmid, const void *shmaddr, int shmflg);
  int _real_shmdt(const void *shmaddr);
  int _real_shmctl(int shmid, int cmd, struct shmid_ds *buf);

#ifdef ENABLE_MALLOC_WRAPPER
  void *_real_calloc(size_t nmemb, size_t size);
  void *_real_malloc(size_t size);
  void  _real_free(void *ptr);
  void *_real_realloc(void *ptr, size_t size);

  //int _real_vfprintf ( FILE *s, const char *format, va_list ap );
#endif

#ifdef __cplusplus
}
#endif

#endif

