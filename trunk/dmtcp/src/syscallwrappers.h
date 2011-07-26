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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

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
#include <sys/stat.h>
#include <sys/mman.h>
#include <dirent.h>
#include <unistd.h>

#define LIB_PRIVATE __attribute__ ((visibility ("hidden")))

#ifdef RECORD_REPLAY
#define SET_MMAP_NO_SYNC()   (mmap_no_sync = 1)
#define UNSET_MMAP_NO_SYNC() (mmap_no_sync = 0)
#define MMAP_NO_SYNC         (mmap_no_sync == 1)
// Defined in dmtcpworker.cpp:
LIB_PRIVATE extern __thread int mmap_no_sync;
#endif


void _dmtcp_setup_trampolines();

#ifdef __cplusplus
extern "C"
{
#endif


LIB_PRIVATE extern __thread int thread_performing_dlopen_dlsym;

#define FOREACH_GLIBC_MALLOC_FAMILY_WRAPPERS(MACRO)\
  MACRO(calloc)                             \
  MACRO(malloc)                             \
  MACRO(free)                               \
  MACRO(__libc_memalign)                    \
  MACRO(realloc)                            \
  MACRO(mmap)                               \
  MACRO(mmap64)                             \
  MACRO(mremap)                             \
  MACRO(munmap)

#define FOREACH_GLIBC_WRAPPERS(MACRO)       \
  MACRO(dlopen)                             \
  MACRO(dlclose)                            \
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
  MACRO(wait4)                              \
  MACRO(ioctl)                              \
  MACRO(ptrace)                             \
                                            \
  MACRO(socket)                             \
  MACRO(connect)                            \
  MACRO(bind)                               \
  MACRO(listen)                             \
  MACRO(accept)                             \
  MACRO(accept4)                            \
  MACRO(setsockopt)                         \
  MACRO(getsockopt)                         \
  MACRO(socketpair)                         \
                                            \
  MACRO(fexecve)                            \
  MACRO(execve)                             \
  MACRO(execv)                              \
  MACRO(execvp)                             \
  MACRO(execvpe)                            \
  MACRO(execl)                              \
  MACRO(execlp)                             \
  MACRO(execle)                             \
  MACRO(system)                             \
                                            \
  MACRO(signal)                             \
  MACRO(sigaction)                          \
  MACRO(sigvec)                             \
                                            \
  MACRO(sigset)                             \
  MACRO(sigblock)                           \
  MACRO(sigsetmask)                         \
  MACRO(siggetmask)                         \
  MACRO(sigprocmask)                        \
                                            \
  MACRO(sigsuspend)                         \
  MACRO(sighold)                            \
  MACRO(sigignore)                          \
  MACRO(__sigpause)                         \
  MACRO(sigpause)                           \
  MACRO(sigrelse)                           \
                                            \
  MACRO(sigwait)                            \
  MACRO(sigwaitinfo)                        \
  MACRO(sigtimedwait)                       \
                                            \
  MACRO(fork)                               \
  MACRO(__clone)                            \
  MACRO(open)                               \
  MACRO(open64)                             \
  MACRO(fopen)                              \
  MACRO(fopen64)                            \
  MACRO(close)                              \
  MACRO(fclose)                             \
  MACRO(__xstat)                            \
  MACRO(__xstat64)                          \
  MACRO(__lxstat)                           \
  MACRO(__lxstat64)                         \
  MACRO(readlink)                           \
  MACRO(exit)                               \
  MACRO(syscall)                            \
  MACRO(unsetenv)                           \
  MACRO(ptsname_r)                          \
  MACRO(getpt)                              \
  MACRO(openlog)                            \
  MACRO(closelog)                           \
                                            \
  MACRO(shmget)                             \
  MACRO(shmat)                              \
  MACRO(shmdt)                              \
  MACRO(shmctl)                             \
                                            \
  MACRO(select)                             \
  MACRO(read)                               \
  MACRO(write)                              \
                                            \
  MACRO(pthread_join)                       \
  MACRO(pthread_sigmask)                    \
  MACRO(pthread_mutex_lock)                 \
  MACRO(pthread_mutex_trylock)              \
  MACRO(pthread_mutex_unlock)               \
  MACRO(pthread_rwlock_unlock)              \
  MACRO(pthread_rwlock_rdlock)              \
  MACRO(pthread_rwlock_wrlock)
//  MACRO(creat)
//  MACRO(openat)



#ifdef RECORD_REPLAY
# define FOREACH_RECORD_REPLAY_WRAPPERS(MACRO)\
  MACRO(access)                               \
  MACRO(closedir)                             \
  MACRO(opendir)                              \
  MACRO(fdopendir)			      \
  MACRO(openat)				      \
  MACRO(readdir)                              \
  MACRO(readdir_r)                            \
  MACRO(rand)                                 \
  MACRO(srand)                                \
  MACRO(time)                                 \
  MACRO(getsockname)                          \
  MACRO(getpeername)                          \
  MACRO(fcntl)                                \
  MACRO(dup)                                  \
  MACRO(dup2)                                 \
  MACRO(dup3)                                 \
  MACRO(lseek)                                \
  MACRO(__fxstat)                             \
  MACRO(__fxstat64)                           \
  MACRO(unlink)                               \
  MACRO(pread)                                \
  MACRO(pwrite)                               \
  MACRO(fdopen)                               \
  MACRO(fgets)                                \
  MACRO(fflush)                               \
  MACRO(putc)                                 \
  MACRO(fputc)                                \
  MACRO(fputs)                                \
  MACRO(fdatasync)                            \
  MACRO(fsync)                                \
  MACRO(link)                                 \
  MACRO(getc)                                 \
  MACRO(gettimeofday)                         \
  MACRO(fgetc)                                \
  MACRO(ungetc)                               \
  MACRO(getline)                              \
  MACRO(rename)                               \
  MACRO(rewind)                               \
  MACRO(rmdir)                                \
  MACRO(ftell)                                \
  MACRO(fwrite)                               \
  MACRO(mkdir)                                \
  MACRO(mkstemp)                              \
                                              \
  MACRO(pthread_cond_wait)                    \
  MACRO(pthread_cond_timedwait)               \
  MACRO(pthread_cond_signal)                  \
  MACRO(pthread_cond_broadcast)               \
  MACRO(pthread_create)                       \
  MACRO(pthread_detach)                       \
  MACRO(pthread_exit)                         \
  MACRO(pthread_kill)
#else
# define FOREACH_RECORD_REPLAY_WRAPPERS(MACRO)
#endif


#define FOREACH_DMTCP_WRAPPER(MACRO)            \
  FOREACH_GLIBC_WRAPPERS(MACRO)                 \
  FOREACH_GLIBC_MALLOC_FAMILY_WRAPPERS(MACRO)   \
  FOREACH_RECORD_REPLAY_WRAPPERS(MACRO)

# define ENUM(x) enum_ ## x
# define GEN_ENUM(x) ENUM(x),
  typedef enum {
    FOREACH_DMTCP_WRAPPER(GEN_ENUM)
    numLibcWrappers
  } LibcWrapperOffset;

  void _dmtcp_lock();
  void _dmtcp_unlock();

  void _dmtcp_remutex_on_fork();

  int _dmtcp_unsetenv(const char *name);
  void initialize_wrappers();

  int _real_socket ( int domain, int type, int protocol );
  int _real_connect ( int sockfd,  const  struct sockaddr *serv_addr,
                      socklen_t addrlen );
  int _real_bind ( int sockfd,  const struct  sockaddr  *my_addr,
                   socklen_t addrlen );
  int _real_listen ( int sockfd, int backlog );
  int _real_accept ( int sockfd, struct sockaddr *addr, socklen_t *addrlen );
  int _real_accept4 ( int sockfd, struct sockaddr *addr, socklen_t *addrlen,
                      int flags );
  int _real_setsockopt ( int s, int level, int optname, const void *optval,
                         socklen_t optlen );
  int _real_getsockopt ( int s, int level, int optname, void *optval,
                         socklen_t *optlen );

  int _real_fexecve ( int fd, char *const argv[], char *const envp[] );
  int _real_execve ( const char *filename, char *const argv[], char *const envp[] );
  int _real_execv ( const char *path, char *const argv[] );
  int _real_execvp ( const char *file, char *const argv[] );
  int _real_execvpe(const char *file, char *const argv[], char *const envp[]);
// int _real_execl(const char *path, const char *arg, ...);
// int _real_execlp(const char *file, const char *arg, ...);
// int _real_execle(const char *path, const char *arg, ..., char * const envp[]);
  int _real_system ( const char * cmd );

  pid_t _real_fork();
  int _real_clone ( int ( *fn ) ( void *arg ), void *child_stack, int flags, void *arg, int *parent_tidptr, struct user_desc *newtls, int *child_tidptr );

  int _real_open(const char *pathname, int flags, mode_t mode);
  int _real_open64(const char *pathname, int flags, mode_t mode);
  FILE* _real_fopen(const char *path, const char *mode);
  FILE* _real_fopen64(const char *path, const char *mode);
  int _real_close ( int fd );
  int _real_fclose ( FILE *fp );
  void _real_exit ( int status );

//we no longer wrap dup
//#define _real_dup  dup
//#define _real_dup2 dup2
//int _real_dup(int oldfd);
//int _real_dup2(int oldfd, int newfd);

  int _real_ptsname_r ( int fd, char * buf, size_t buflen );
  int _real_getpt ( void );

  int _real_socketpair ( int d, int type, int protocol, int sv[2] );

  void _real_openlog ( const char *ident, int option, int facility );
  void _real_closelog ( void );

  // Despite what 'man signal' says, signal.h already defines sighandler_t
  // typedef void (*sighandler_t)(int);

  //set the handler
  sighandler_t _real_signal(int signum, sighandler_t handler);
  int _real_sigaction(int signum, const struct sigaction *act,
                      struct sigaction *oldact);
  int _real_rt_sigaction(int signum, const struct sigaction *act,
                         struct sigaction *oldact);
  int _real_sigvec(int sig, const struct sigvec *vec, struct sigvec *ovec);

  //set the mask
  int _real_sigblock(int mask);
  int _real_sigsetmask(int mask);
  int _real_siggetmask(void);
  int _real_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
  int _real_rt_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
  int _real_pthread_sigmask(int how, const sigset_t *newmask,
                            sigset_t *oldmask);

  int _real_sigsuspend(const sigset_t *mask);
  int _real_sighold(int sig);
  int _real_sigignore(int sig);
  int _real__sigpause(int __sig_or_mask, int __is_sig);
  int _real_sigpause(int sig);
  int _real_sigrelse(int sig);
  sighandler_t _real_sigset(int sig, sighandler_t disp);

  int _real_sigwait(const sigset_t *set, int *sig);
  int _real_sigwaitinfo(const sigset_t *set, siginfo_t *info);
  int _real_sigtimedwait(const sigset_t *set, siginfo_t *info,
                         const struct timespec *timeout);

  pid_t _real_gettid(void);
  int   _real_tkill(int tid, int sig);
  int   _real_tgkill(int tgid, int tid, int sig);

  long int _real_syscall(long int sys_num, ... );

  /* System V shared memory */
  int _real_shmget(key_t key, size_t size, int shmflg);
  void* _real_shmat(int shmid, const void *shmaddr, int shmflg);
  int _real_shmdt(const void *shmaddr);
  int _real_shmctl(int shmid, int cmd, struct shmid_ds *buf);

  int _real_pthread_join(pthread_t thread, void **value_ptr);

  int _real_xstat(int vers, const char *path, struct stat *buf);
  int _real_xstat64(int vers, const char *path, struct stat64 *buf);
  int _real_lxstat(int vers, const char *path, struct stat *buf);
  int _real_lxstat64(int vers, const char *path, struct stat64 *buf);
  ssize_t _real_readlink(const char *path, char *buf, size_t bufsiz);
  void * _real_dlsym ( void *handle, const char *symbol );

  void *_real_dlopen(const char *filename, int flag);
  int _real_dlclose(void *handle);

  void *_real_calloc(size_t nmemb, size_t size);
  void *_real_malloc(size_t size);
  void  _real_free(void *ptr);
  void *_real_realloc(void *ptr, size_t size);
  void *_real_libc_memalign(size_t boundary, size_t size);
  void *_real_mmap(void *addr, size_t length, int prot, int flags,
      int fd, off_t offset);
  void *_real_mmap64(void *addr, size_t length, int prot, int flags,
      int fd, off64_t offset);
  void *_real_mremap(void *old_address, size_t old_size, size_t new_size,
      int flags, void *new_address);
  int _real_munmap(void *addr, size_t length);

  ssize_t _real_read(int fd, void *buf, size_t count);
  ssize_t _real_write(int fd, const void *buf, size_t count);
  int _real_select(int nfds, fd_set *readfds, fd_set *writefds,
                   fd_set *exceptfds, struct timeval *timeout);
  int _real_dup(int oldfd);
  int _real_dup2(int oldfd, int newfd);
  int _real_dup3(int oldfd, int newfd, int flags);
  off_t _real_lseek(int fd, off_t offset, int whence);
  int _real_unlink(const char *pathname);

  int _real_pthread_mutex_lock(pthread_mutex_t *mutex);
  int _real_pthread_mutex_trylock(pthread_mutex_t *mutex);
  int _real_pthread_mutex_unlock(pthread_mutex_t *mutex);
  int _real_pthread_rwlock_unlock(pthread_rwlock_t *rwlock);
  int _real_pthread_rwlock_rdlock(pthread_rwlock_t *rwlock);
  int _real_pthread_rwlock_wrlock(pthread_rwlock_t *rwlock);

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

  pid_t _real_wait3(__WAIT_STATUS status, int options, struct rusage *rusage);
  pid_t _real_wait4(pid_t pid, __WAIT_STATUS status, int options,
                    struct rusage *rusage);
  LIB_PRIVATE extern int send_sigwinch;
  int _real_ioctl(int d,  unsigned long int request, ...) __THROW;

  int _real_setgid(gid_t gid);
  int _real_setuid(uid_t uid);

#endif /* PID_VIRTUALIZATION */

#ifdef PTRACE
  long _real_ptrace ( enum __ptrace_request request, pid_t pid, void *addr,
                    void *data);
#endif

#ifdef RECORD_REPLAY
  int _real_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
  int _real_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
  int _real_closedir(DIR *dirp);
  int _real_openat(int dirfd, const char *pathname, int flags);
  DIR * _real_fdopendir(int fd);
  DIR * _real_opendir(const char *name);
  int _real_mkdir(const char *pathname, mode_t mode);
  int _real_mkstemp(char *temp);
  FILE * _real_fdopen(int fd, const char *mode);
  char * _real_fgets(char *s, int size, FILE *stream);
  int _real_fflush(FILE *stream);
  ssize_t _real_getline(char **lineptr, size_t *n, FILE *stream);
  int _real_getc(FILE *stream);
  int _real_gettimeofday(struct timeval *tv, struct timezone *tz);
  int _real_fgetc(FILE *stream);
  int _real_fputc(int, FILE *stream);
  int _real_ungetc(int c, FILE *stream);
  int _real_putc(int c, FILE *stream);
  int _real_fcntl(int fd, int cmd, ...);
  int _real_fdatasync(int fd);
  int _real_fsync(int fd);
  int _real_fputs(const char *s, FILE *stream);
  int _real_fxstat(int vers, int fd, struct stat *buf);
  int _real_fxstat64(int vers, int fd, struct stat64 *buf);
  int _real_link(const char *oldpath, const char *newpath);
  int _real_rename(const char *oldpath, const char *newpath);
  void _real_rewind(FILE *stream);
  int _real_rmdir(const char *pathname);
  long _real_ftell(FILE *stream);
  size_t _real_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

  void * _mmap_no_sync(void *addr, size_t length, int prot, int flags,
      int fd, off_t offset);
  int _munmap_no_sync(void *addr, size_t length);
  //int _real_vfprintf ( FILE *s, const char *format, va_list ap );

  int _real_pthread_cond_signal(pthread_cond_t *cond);
  int _real_pthread_cond_broadcast(pthread_cond_t *cond);
  int _real_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
  int _real_pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
      const struct timespec *abstime);
  int _real_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
      void *(*start_routine)(void*), void *arg);
  void _real_pthread_exit(void *value_ptr);
  int _real_pthread_detach(pthread_t thread);
  int _real_pthread_join(pthread_t thread, void **value_ptr);
  int _real_pthread_kill(pthread_t thread, int sig);
  int _real_access(const char *pathname, int mode);
  struct dirent *_real_readdir(DIR *dirp);
  int _real_readdir_r(DIR *dirp, struct dirent *entry, struct dirent **result);
  int _real_rand(void);
  void _real_srand(unsigned int seed);
  time_t _real_time(time_t *tloc);
  ssize_t _real_pread(int fd, void *buf, size_t count, off_t offset);
  ssize_t _real_pwrite(int fd, const void *buf, size_t count, off_t offset);
#endif

#ifdef __cplusplus
}
#endif

#endif

