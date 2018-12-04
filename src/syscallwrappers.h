/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
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

// FIXME:  Why are we adding all these includes here, if we're declaring
// only our own _real_XXX() functions?  Some *wrappers.cpp files
// use these includes.  But, then we should split up these includes
// among the individual *wrappers.cpp files that actually need them,
// and not declare every possible include in one giant .h file.
#include <features.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

// FIXME:  The SLES 10 (glibc-2.4) declaration for msgctl differs from
// our wrapper's declaration, which uses the POSIX declaration.
// If for syscallsreal.c, use the local (e.g., SLES 10) decl.
// Otherwise, skip declaration, and use decl. in pidwrappers.h
// or in syscallwrappers.h.  The correct solution should be to fix
// the way that *wrappers.h get their decl. of msgctl().
#ifdef FOR_SYSCALLSREAL_C
# include <sys/msg.h>
#endif // ifdef FOR_SYSCALLSREAL_C
#ifdef __cplusplus
# include <sys/stat.h>
#else // ifdef __cplusplus
# ifndef __USE_LARGEFILE64
#  define __USE_LARGEFILE64_not_defined
#  define __USE_LARGEFILE64
#  include <sys/stat.h>
#  ifdef __USE_LARGEFILE64_not_defined
#   undef __USE_LARGEFILE64_not_defined
#   undef __USE_LARGEFILE64
#  endif // ifdef __USE_LARGEFILE64_not_defined
# endif // ifndef __USE_LARGEFILE64
#endif // ifdef __cplusplus
#include <dirent.h>
#include <mqueue.h>
#include <sys/mman.h>
#include <unistd.h>
#ifdef HAVE_SYS_INOTIFY_H
# include <sys/inotify.h>
#endif // ifdef HAVE_SYS_INOTIFY_H

#include "config.h"
#include "constants.h"
#include "dmtcp.h"
#include "mtcp/ldt.h"

#ifdef HAVE_SYS_EPOLL_H
# include <sys/epoll.h>
#else // ifdef HAVE_SYS_EPOLL_H

/* KEEP THIS IN SYNC WITH connection.h */
# ifndef _SYS_EPOLL_H
#  define _SYS_EPOLL_H 1
struct epoll_event { int dummy; };

/* Valid opcodes ("op" parameter) to issue to epoll_ctl().  */
#  define EPOLL_CTL_ADD 1 /* Add a file descriptor to the interface.  */
#  define EPOLL_CTL_DEL 2 /* Remove a file descriptor from the interface.  */
#  define EPOLL_CTL_MOD 3 /* Change file descriptor epoll_event structure.  */
# endif // ifndef _SYS_EPOLL_H
#endif // ifdef HAVE_SYS_EPOLL_H

void _dmtcp_setup_trampolines();

#ifdef __cplusplus
extern "C"
{
#endif // ifdef __cplusplus

#if defined(__arm__) || defined(__aarch64__)
# define DISABLE_PTHREAD_GETSPECIFIC_TRICK
#endif // if defined(__arm__) || defined(__aarch64__)

LIB_PRIVATE pid_t dmtcp_gettid();
LIB_PRIVATE int dmtcp_tkill(int tid, int sig);
LIB_PRIVATE int dmtcp_tgkill(int tgid, int tid, int sig);

extern int dmtcp_wrappers_initializing;

LIB_PRIVATE extern __thread int thread_performing_dlopen_dlsym;

#define FOREACH_DMTCP_WRAPPER(MACRO)  \
  MACRO(dlopen)                       \
  MACRO(dlclose)                      \
  MACRO(getpid)                       \
  MACRO(getppid)                      \
  MACRO(kill)                         \
                                      \
  MACRO(tcgetpgrp)                    \
  MACRO(tcsetpgrp)                    \
  MACRO(getpgrp)                      \
  MACRO(setpgrp)                      \
                                      \
  MACRO(getpgid)                      \
  MACRO(setpgid)                      \
  MACRO(getsid)                       \
  MACRO(setsid)                       \
  MACRO(setgid)                       \
  MACRO(setuid)                       \
                                      \
  MACRO(wait)                         \
  MACRO(waitpid)                      \
  MACRO(waitid)                       \
  MACRO(wait3)                        \
  MACRO(wait4)                        \
  MACRO(ioctl)                        \
  MACRO(fcntl)                        \
                                      \
  MACRO(socket)                       \
  MACRO(connect)                      \
  MACRO(bind)                         \
  MACRO(listen)                       \
  MACRO(accept)                       \
  MACRO(accept4)                      \
  MACRO(setsockopt)                   \
  MACRO(getsockopt)                   \
  MACRO(socketpair)                   \
                                      \
  MACRO(fexecve)                      \
  MACRO(execve)                       \
  MACRO(execv)                        \
  MACRO(execvp)                       \
  MACRO(execvpe)                      \
  MACRO(execl)                        \
  MACRO(execlp)                       \
  MACRO(execle)                       \
  MACRO(system)                       \
  MACRO(popen)                        \
  MACRO(pclose)                       \
                                      \
  MACRO(signal)                       \
  MACRO(sigaction)                    \
  MACRO(sigvec)                       \
                                      \
  MACRO(sigset)                       \
  MACRO(sigblock)                     \
  MACRO(sigsetmask)                   \
  MACRO(siggetmask)                   \
  MACRO(sigprocmask)                  \
                                      \
  MACRO(sigsuspend)                   \
  MACRO(sighold)                      \
  MACRO(sigignore)                    \
  MACRO(__sigpause)                   \
  MACRO(sigpause)                     \
  MACRO(sigrelse)                     \
                                      \
  MACRO(sigwait)                      \
  MACRO(sigwaitinfo)                  \
  MACRO(sigtimedwait)                 \
                                      \
  MACRO(fork)                         \
  MACRO(__clone)                      \
  MACRO(open)                         \
  MACRO(open64)                       \
  MACRO(fopen)                        \
  MACRO(fopen64)                      \
  MACRO(openat)                       \
  MACRO(openat64)                     \
  MACRO(opendir)                      \
  MACRO(mkstemp)                      \
  MACRO(close)                        \
  MACRO(fclose)                       \
  MACRO(closedir)                     \
  MACRO(setrlimit)                    \
  MACRO(dup)                          \
  MACRO(dup2)                         \
  MACRO(dup3)                         \
  MACRO(__xstat)                      \
  MACRO(__xstat64)                    \
  MACRO(__lxstat)                     \
  MACRO(__lxstat64)                   \
  MACRO(readlink)                     \
  MACRO(exit)                         \
  MACRO(syscall)                      \
  MACRO(unsetenv)                     \
  MACRO(ptsname_r)                    \
  MACRO(ttyname_r)                    \
  MACRO(getpt)                        \
  MACRO(posix_openpt)                 \
  MACRO(openlog)                      \
  MACRO(closelog)                     \
                                      \
  MACRO(shmget)                       \
  MACRO(shmat)                        \
  MACRO(shmdt)                        \
  MACRO(shmctl)                       \
                                      \
  MACRO(semget)                       \
  MACRO(semctl)                       \
  MACRO(semop)                        \
  MACRO(semtimedop)                   \
                                      \
  MACRO(msgget)                       \
  MACRO(msgctl)                       \
  MACRO(msgsnd)                       \
  MACRO(msgrcv)                       \
                                      \
  MACRO(mq_open)                      \
  MACRO(mq_close)                     \
  MACRO(mq_timedsend)                 \
  MACRO(mq_timedreceive)              \
  MACRO(mq_notify)                    \
                                      \
  MACRO(pthread_create)               \
  MACRO(pthread_exit)                 \
  MACRO(pthread_tryjoin_np)           \
  MACRO(pthread_timedjoin_np)         \
  MACRO(pthread_sigmask)              \
  MACRO(pthread_getspecific)

#define ENUM(x)     enum_ ## x
#define GEN_ENUM(x) ENUM(x),
typedef enum {
  FOREACH_DMTCP_WRAPPER(GEN_ENUM)
  numLibcWrappers
} LibcWrapperOffset;

union semun {
  int val;                   /* Value for SETVAL */
  struct semid_ds *buf;      /* Buffer for IPC_STAT, IPC_SET */
  unsigned short *array;     /* Array for GETALL, SETALL */
  struct seminfo *__buf;     /* Buffer for IPC_INFO (Linux-specific) */
};

LIB_PRIVATE void dmtcpResetTid(pid_t tid);
LIB_PRIVATE void dmtcpResetPidPpid();

int _dmtcp_unsetenv(const char *name);
void dmtcp_prepare_wrappers();

int _real_socket(int domain, int type, int protocol);
int _real_connect(int sockfd,
                  const struct sockaddr *serv_addr,
                  socklen_t addrlen);
int _real_bind(int sockfd, const struct  sockaddr *my_addr, socklen_t addrlen);
int _real_listen(int sockfd, int backlog);
int _real_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int _real_accept4(int sockfd,
                  struct sockaddr *addr,
                  socklen_t *addrlen,
                  int flags);
int _real_setsockopt(int s,
                     int level,
                     int optname,
                     const void *optval,
                     socklen_t optlen);
int _real_getsockopt(int s,
                     int level,
                     int optname,
                     void *optval,
                     socklen_t *optlen);

int _real_fexecve(int fd, char *const argv[], char *const envp[]);
int _real_execve(const char *filename, char *const argv[], char *const envp[]);
int _real_execv(const char *path, char *const argv[]);
int _real_execvp(const char *file, char *const argv[]);
int _real_execvpe(const char *file, char *const argv[], char *const envp[]);

// int _real_execl(const char *path, const char *arg, ...);
// int _real_execlp(const char *file, const char *arg, ...);
// int _real_execle(const char *path, const char *arg, ..., char * const
// envp[]);
int _real_system(const char *cmd);
FILE *_real_popen(const char *command, const char *mode);
int _real_pclose(FILE *fp);

pid_t _real_fork();
int _real_clone(int (*fn)(void *arg),
                void *child_stack,
                int flags,
                void *arg,
                int *parent_tidptr,
                struct user_desc *newtls,
                int *child_tidptr);

int _real_open(const char *pathname, int flags, ...);
int _real_open64(const char *pathname, int flags, ...);
FILE *_real_fopen(const char *path, const char *mode);
FILE *_real_fopen64(const char *path, const char *mode);
int _real_openat(int dirfd, const char *pathname, int flags, mode_t mode);
int _real_openat64(int dirfd, const char *pathname, int flags, mode_t mode);
DIR *_real_opendir(const char *name);
int _real_mkstemp(char *ttemplate);
int _real_close(int fd);
int _real_fclose(FILE *fp);
int _real_closedir(DIR *dir);
int _real_setrlimit(int resource, const struct rlimit *rlim);
void _real_exit(int status);
int _real_dup(int oldfd);
int _real_dup2(int oldfd, int newfd);
int _real_dup3(int oldfd, int newfd, int flags);
int _real_fcntl(int fd, int cmd, void *arg);

int _real_ttyname_r(int fd, char *buf, size_t buflen);
int _real_ptsname_r(int fd, char *buf, size_t buflen);
int _real_getpt(void);
int _real_posix_openpt(int flags);

int _real_socketpair(int d, int type, int protocol, int sv[2]);

void _real_openlog(const char *ident, int option, int facility);
void _real_closelog(void);

// Despite what 'man signal' says, signal.h already defines sighandler_t
// But signal.h defines this only because we define GNU_SOURCE (or __USE_GNU_
typedef void (*sighandler_t)(int);    /* POSIX has user define this type */

// set the handler
sighandler_t _real_signal(int signum, sighandler_t handler);
int _real_sigaction(int signum,
                    const struct sigaction *act,
                    struct sigaction *oldact);
int _real_rt_sigaction(int signum,
                       const struct sigaction *act,
                       struct sigaction *oldact);
#if !__GLIBC_PREREQ(2, 21)
int _real_sigvec(int sig, const struct sigvec *vec, struct sigvec *ovec);
#endif // if !__GLIBC_PREREQ(2, 21)

// set the mask
int _real_sigblock(int mask);
int _real_sigsetmask(int mask);
int _real_siggetmask(void);
int _real_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int _real_rt_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int _real_pthread_sigmask(int how, const sigset_t *newmask, sigset_t *oldmask);
void *_real_pthread_getspecific(pthread_key_t key);

int _real_sigsuspend(const sigset_t *mask);
int _real_sighold(int sig);
int _real_sigignore(int sig);
int _real__sigpause(int __sig_or_mask, int __is_sig);
int _real_sigpause(int sig);
int _real_sigrelse(int sig);
sighandler_t _real_sigset(int sig, sighandler_t disp);

int _real_sigwait(const sigset_t *set, int *sig);
int _real_sigwaitinfo(const sigset_t *set, siginfo_t *info);
int _real_sigtimedwait(const sigset_t *set,
                       siginfo_t *info,
                       const struct timespec *timeout);

long _real_syscall(long sys_num, ...);

int _real_pthread_create(pthread_t *thread,
                         const pthread_attr_t *attr,
                         void *(*start_routine)(void *),
                         void *arg);
void _real_pthread_exit(void *retval) __attribute__((__noreturn__));
int _real_pthread_tryjoin_np(pthread_t thread, void **retval);
int _real_pthread_timedjoin_np(pthread_t thread,
                               void **retval,
                               const struct timespec *abstime);

int _real_xstat(int vers, const char *path, struct stat *buf);
int _real_xstat64(int vers, const char *path, struct stat64 *buf);
int _real_lxstat(int vers, const char *path, struct stat *buf);
int _real_lxstat64(int vers, const char *path, struct stat64 *buf);
ssize_t _real_readlink(const char *path, char *buf, size_t bufsiz);
void *_real_dlsym(void *handle, const char *symbol);

void *_real_dlopen(const char *filename, int flag);
int _real_dlclose(void *handle);

off_t _real_lseek(int fd, off_t offset, int whence);
int _real_unlink(const char *pathname);

int _real_waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options);
pid_t _real_wait4(pid_t pid,
                  __WAIT_STATUS status,
                  int options,
                  struct rusage *rusage);

int _real_shmget(int key, size_t size, int shmflg);
void *_real_shmat(int shmid, const void *shmaddr, int shmflg);
int _real_shmdt(const void *shmaddr);
int _real_shmctl(int shmid, int cmd, struct shmid_ds *buf);
int _real_semget(key_t key, int nsems, int semflg);
int _real_semop(int semid, struct sembuf *sops, size_t nsops);
int _real_semtimedop(int semid,
                     struct sembuf *sops,
                     size_t nsops,
                     const struct timespec *timeout);
int _real_semctl(int semid, int semnum, int cmd, ...);

int _real_msgget(key_t key, int msgflg);
int _real_msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg);
ssize_t _real_msgrcv(int msqid,
                     void *msgp,
                     size_t msgsz,
                     long msgtyp,
                     int msgflg);
int _real_msgctl(int msqid, int cmd, struct msqid_ds *buf);


mqd_t _real_mq_open(const char *name,
                    int oflag,
                    mode_t mode,
                    struct mq_attr *attr);
int _real_mq_close(mqd_t mqdes);
int _real_mq_notify(mqd_t mqdes, const struct sigevent *sevp);
ssize_t _real_mq_timedreceive(mqd_t mqdes,
                              char *msg_ptr,
                              size_t msg_len,
                              unsigned int *msg_prio,
                              const struct timespec *abs_timeout);
int _real_mq_timedsend(mqd_t mqdes,
                       const char *msg_ptr,
                       size_t msg_len,
                       unsigned int msg_prio,
                       const struct timespec *abs_timeout);

#ifdef __cplusplus
}
#endif // ifdef __cplusplus
#endif // ifndef SYSCALLWRAPPERS_H
