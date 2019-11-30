/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/poll.h>
#include "../jalib/jassert.h"
#include "../jalib/jconvert.h"
#include "constants.h"
#include "dmtcpworker.h"
#include "processinfo.h"
#include "protectedfds.h"
#include "syscallwrappers.h"
#include "threadsync.h"
#include "util.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 13) && __GLIBC_PREREQ(2, 4)
#include <sys/inotify.h>
#endif  // if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 13) &&
        // __GLIBC_PREREQ(2,
// 4)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22) && __GLIBC_PREREQ(2, 8)
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#endif  // if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22) &&
        // __GLIBC_PREREQ(2,
// 8)

#ifdef __aarch64__

// We must support all deprecated syscalls in case the end user code uses it.
# define __ARCH_WANT_SYSCALL_DEPRECATED
# define __ARCH_WANT_SYSCALL_NO_AT
# define __ARCH_WANT_SYSCALL_NO_FLAGS

// SYS_fork is a deprecated kernel call in aarch64; in favor of SYS_clone?
# include <asm-generic/unistd.h>

// SYS_fork, etc., are undefined in aarch64
// Presumably, libc translates the POSIX syscalls into later kernel calls.
// # define SYS_fork         __NR_fork
// # define SYS_open         __NR_open
// # define SYS_pipe         __NR_pipe
// # define SYS_poll         __NR_poll

// These kernel calls are now often gone on aarch64.  SYS_XXX should not be
//   defined for them.
// # define SYS_epoll_create __NR_epoll_create
// # define SYS_inotify_init __NR_inotify_init
// # define SYS_signalfd     __NR_signalfd
// # define SYS_eventfd      __NR_eventfd
#endif // ifdef __aarch64__


using namespace dmtcp;

EXTERNC int dmtcp_is_popen_fp(FILE *fp) __attribute((weak));

extern "C" int
close(int fd)
{
  if (DMTCP_IS_PROTECTED_FD(fd)) {
    JTRACE("blocked attempt to close protected fd") (fd);
    errno = EBADF;
    return -1;
  }
  return _real_close(fd);
}

extern "C" int
fclose(FILE *fp)
{
  // If this fp was obtained using popen(), we must pclose it
  if (dmtcp_is_popen_fp(fp)) {
    return pclose(fp);
  }
  int fd = fileno(fp);
  if (DMTCP_IS_PROTECTED_FD(fd)) {
    JTRACE("blocked attempt to fclose protected fd") (fd);
    errno = EBADF;
    return -1;
  }
  return _real_fclose(fp);
}

extern "C" int
closedir(DIR *dir)
{
  int fd = dirfd(dir);

  if (DMTCP_IS_PROTECTED_FD(fd)) {
    JTRACE("blocked attempt to closedir protected fd") (fd);
    errno = EBADF;
    return -1;
  }
  return _real_closedir(dir);
}

/*
 * FIXME: Add wrapper for dup2 and dup3 to detect a protected fd.
extern "C" int dup2(int oldfd, int newfd)
{
  if (DMTCP_IS_PROTECTED_FD(newfd)) {
  }
  return _real_dup2(oldfd, newfd);
}
*/

// Linux prlimit() could also be wrapped for protected fd, but it's a rare case.
extern "C" int setrlimit (int resource, const struct rlimit *rlim) {
  if ( resource == RLIMIT_NOFILE &&
       (rlim->rlim_cur < 1024 || rlim->rlim_max < 1024) ) {
    JNOTE("Blocked attempt to lower RLIMIT_NOFILE\n"
                 "  below 1024 (needed for DMTCP protected fd)")
         (rlim->rlim_cur) (rlim->rlim_max);
    struct rlimit rlim2 = {0};
    if (rlim->rlim_cur < 1024) { rlim2.rlim_cur = 1024; }
    if (rlim->rlim_max < 1024) { rlim2.rlim_max = 1024; }
    return _real_setrlimit(resource, &rlim2);
  }
  return _real_setrlimit(resource, rlim);
}

extern "C" int
pipe(int fds[2])
{
  JTRACE("promoting pipe() to socketpair()");

  // just promote pipes to socketpairs
  return socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && __GLIBC_PREREQ(2, 9)

// pipe2 appeared in Linux 2.6.27
extern "C" int
pipe2(int fds[2], int flags)
{
  JTRACE("promoting pipe2() to socketpair()");

  // just promote pipes to socketpairs
  int newFlags = 0;
  if ((flags & O_NONBLOCK) != 0) {
    newFlags |= SOCK_NONBLOCK;
  }
  if ((flags & O_CLOEXEC) != 0) {
    newFlags |= SOCK_CLOEXEC;
  }
  return socketpair(AF_UNIX, SOCK_STREAM | newFlags, 0, fds);
}
#endif // if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) &&
       // __GLIBC_PREREQ(2, 9)


// extern "C" pid_t getpid()
// {
// return ProcessInfo::instance().pid();
// }

// extern "C" pid_t getppid()
// {
// pid_t ppid = _real_getppid();
// if (ppid == 1 ) {
// ProcessInfo::instance().setppid( 1 );
// }
//
// return origPpid;
// }

// extern "C" pid_t setsid(void)
// {
// pid_t pid = _real_setsid();
// ProcessInfo::instance().setsid(origPid);
// return origPid;
// }

#if 1
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

extern "C"
pid_t
wait4(pid_t pid, __WAIT_STATUS status, int options, struct rusage *rusage)
{
  int stat;
  int saved_errno = errno;
  pid_t retval = 0;

  if (status == NULL) {
    status = (__WAIT_STATUS)&stat;
  }

  retval = _real_wait4(pid, status, options, rusage);
  saved_errno = errno;

  if (retval > 0 &&
      (WIFEXITED(*(int *)status) || WIFSIGNALED(*(int *)status))) {
    ProcessInfo::instance().eraseChild(retval);
  }
  errno = saved_errno;
  return retval;
}

extern "C" int
waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options)
{
  siginfo_t siginfop;

  memset(&siginfop, 0, sizeof(siginfop));

  int retval = _real_waitid(idtype, id, &siginfop, options);

  if (retval != -1) {
    if (siginfop.si_code == CLD_EXITED || siginfop.si_code == CLD_KILLED) {
      ProcessInfo::instance().eraseChild(siginfop.si_pid);
    }
  }

  if (retval == 0 && infop != NULL) {
    *infop = siginfop;
  }

  return retval;
}
#endif // if 1

extern "C" int __clone(int (*fn)(void *arg),
                       void *child_stack,
                       int flags,
                       void *arg,
                       int *parent_tidptr,
                       struct user_desc *newtls,
                       int *child_tidptr);

#define SYSCALL_VA_START() \
  va_list ap;              \
  va_start(ap, sys_num)

#define SYSCALL_VA_END() \
  va_end(ap)

#define SYSCALL_GET_ARG(type, arg) type arg = va_arg(ap, type)

#define SYSCALL_GET_ARGS_2(type1, arg1, type2, arg2) \
  SYSCALL_GET_ARG(type1, arg1);                      \
  SYSCALL_GET_ARG(type2, arg2)

#define SYSCALL_GET_ARGS_3(type1, arg1, type2, arg2, type3, arg3) \
  SYSCALL_GET_ARGS_2(type1, arg1, type2, arg2);                   \
  SYSCALL_GET_ARG(type3, arg3)

#define SYSCALL_GET_ARGS_4(type1, arg1, type2, arg2, type3, arg3, type4, arg4) \
  SYSCALL_GET_ARGS_3(type1, arg1, type2, arg2, type3, arg3);                   \
  SYSCALL_GET_ARG(type4, arg4)

#define SYSCALL_GET_ARGS_5(type1, arg1, type2, arg2, type3, arg3, type4, arg4, \
                           type5, arg5)                                        \
  SYSCALL_GET_ARGS_4(type1, arg1, type2, arg2, type3, arg3, type4, arg4);      \
  SYSCALL_GET_ARG(type5, arg5)

#define SYSCALL_GET_ARGS_6(type1, arg1, type2, arg2, type3, arg3, type4, arg4, \
                           type5, arg5, type6, arg6)                           \
  SYSCALL_GET_ARGS_5(type1, arg1, type2, arg2, type3, arg3, type4, arg4,       \
                     type5, arg5);                                             \
  SYSCALL_GET_ARG(type6, arg6)

#define SYSCALL_GET_ARGS_7(type1, arg1, type2, arg2, type3, arg3, type4, arg4, \
                           type5, arg5, type6, arg6, type7, arg7)              \
  SYSCALL_GET_ARGS_6(type1, arg1, type2, arg2, type3, arg3, type4, arg4,       \
                     type5, arg5, type6, arg6);                                \
  SYSCALL_GET_ARG(type7, arg7)

/* Comments by Gene:
 * Here, syscall is the wrapper, and the call to syscall would be _real_syscall
 * We would add a special case for SYS_gettid, while all others default as below
 * It depends on the idea that arguments are stored in registers, whose
 *  natural size is:  sizeof(void*)
 * So, we pass six arguments to syscall, and it will ignore any extra arguments
 * I believe that all Linux system calls have no more than 7 args.
 * clone() is an example of one with 7 arguments.
 * If we discover system calls for which the 7 args strategy doesn't work,
 *  we can special case them.
 *
 * XXX: DO NOT USE JTRACE/JNOTE/JASSERT in this function; even better, do not
 *      use any STL here.  (--Kapil)
 */
extern "C" long
syscall(long sys_num, ...)
{
  long int ret;
  va_list ap;

  va_start(ap, sys_num);

  switch (sys_num) {
  case SYS_clone:
  {
    typedef int (*fnc) (void *);
    SYSCALL_GET_ARGS_7(fnc, fn, void *, child_stack, int, flags, void *, arg,
                       pid_t *, pid, struct user_desc *, tls, pid_t *, ctid);
    ret = __clone(fn, child_stack, flags, arg, pid, tls, ctid);
    break;
  }

  case SYS_execve:
  {
    SYSCALL_GET_ARGS_3(const char *,
                       filename,
                       char *const *,
                       argv,
                       char *const *,
                       envp);
    ret = execve(filename, argv, envp);
    break;
  }

# ifndef __aarch64__
  case SYS_fork:
  {
    ret = fork();
    break;
  }
# endif // ifndef __aarch64__
  case SYS_exit:
  {
    SYSCALL_GET_ARG(int, status);
    exit(status);
    break;
  }
# ifndef __aarch64__
  case SYS_open:
  {
    SYSCALL_GET_ARGS_3(const char *, pathname, int, flags, mode_t, mode);
    ret = open(pathname, flags, mode);
    break;
  }
# endif // ifndef __aarch64__
  case SYS_close:
  {
    SYSCALL_GET_ARG(int, fd);
    ret = close(fd);
    break;
  }

  case SYS_rt_sigaction:
  {
    SYSCALL_GET_ARGS_3(int,
                       signum,
                       const struct sigaction *,
                       act,
                       struct sigaction *,
                       oldact);
    ret = sigaction(signum, act, oldact);
    break;
  }
  case SYS_rt_sigprocmask:
  {
    SYSCALL_GET_ARGS_3(int, how, const sigset_t *, set, sigset_t *, oldset);
    ret = sigprocmask(how, set, oldset);
    break;
  }
  case SYS_rt_sigtimedwait:
  {
    SYSCALL_GET_ARGS_3(const sigset_t *, set, siginfo_t *, info,
                       const struct timespec *, timeout);
    ret = sigtimedwait(set, info, timeout);
    break;
  }

#ifdef __i386__
  case SYS_sigaction:
  {
    SYSCALL_GET_ARGS_3(int,
                       signum,
                       const struct sigaction *,
                       act,
                       struct sigaction *,
                       oldact);
    ret = sigaction(signum, act, oldact);
    break;
  }
  case SYS_signal:
  {
    typedef void (*sighandler_t)(int);
    SYSCALL_GET_ARGS_2(int, signum, sighandler_t, handler);

    // Cast needed:  signal returns sighandler_t
    ret = (long int)signal(signum, handler);
    break;
  }
  case SYS_sigprocmask:
  {
    SYSCALL_GET_ARGS_3(int, how, const sigset_t *, set, sigset_t *, oldset);
    ret = sigprocmask(how, set, oldset);
    break;
  }
#endif // ifdef __i386__

#ifdef __x86_64__

  // These SYS_xxx are only defined for 64-bit Linux
  case SYS_socket:
  {
    SYSCALL_GET_ARGS_3(int, domain, int, type, int, protocol);
    ret = socket(domain, type, protocol);
    break;
  }
  case SYS_connect:
  {
    SYSCALL_GET_ARGS_3(int,
                       sockfd,
                       const struct sockaddr *,
                       addr,
                       socklen_t,
                       addrlen);
    ret = connect(sockfd, addr, addrlen);
    break;
  }
  case SYS_bind:
  {
    SYSCALL_GET_ARGS_3(int,
                       sockfd,
                       const struct sockaddr *,
                       addr,
                       socklen_t,
                       addrlen);
    ret = bind(sockfd, addr, addrlen);
    break;
  }
  case SYS_listen:
  {
    SYSCALL_GET_ARGS_2(int, sockfd, int, backlog);
    ret = listen(sockfd, backlog);
    break;
  }
  case SYS_accept:
  {
    SYSCALL_GET_ARGS_3(int,
                       sockfd,
                       struct sockaddr *,
                       addr,
                       socklen_t *,
                       addrlen);
    ret = accept(sockfd, addr, addrlen);
    break;
  }
# if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28)) && __GLIBC_PREREQ(2, 10)
  case SYS_accept4:
  {
    SYSCALL_GET_ARGS_4(int,
                       sockfd,
                       struct sockaddr *,
                       addr,
                       socklen_t *,
                       addrlen,
                       int,
                       flags);
    ret = accept4(sockfd, addr, addrlen, flags);
    break;
  }
# endif // if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28)) &&
        // __GLIBC_PREREQ(2, 10)
  case SYS_setsockopt:
  {
    SYSCALL_GET_ARGS_5(int,
                       s,
                       int,
                       level,
                       int,
                       optname,
                       const void *,
                       optval,
                       socklen_t,
                       optlen);
    ret = setsockopt(s, level, optname, optval, optlen);
    break;
  }

  case SYS_socketpair:
  {
    SYSCALL_GET_ARGS_4(int, d, int, type, int, protocol, int *, sv);
    ret = socketpair(d, type, protocol, sv);
    break;
  }
#endif // ifdef __x86_64__

# ifndef __aarch64__
  case SYS_pipe:
  {
    SYSCALL_GET_ARG(int *, fds);
    ret = pipe(fds);
    break;
  }
# endif // ifdef __aarch64__
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && __GLIBC_PREREQ(2, 9)
  case SYS_pipe2:
  {
    SYSCALL_GET_ARGS_2(int *, fds, int, flags);
    ret = pipe2(fds, flags);
    break;
  }
#endif // if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) &&
       // __GLIBC_PREREQ(2, 9)

  case SYS_setsid:
  {
    ret = setsid();
    break;
  }

#ifndef DISABLE_SYS_V_IPC
# ifdef __x86_64__

  // These SYS_xxx are only defined for 64-bit Linux
  case SYS_shmget:
  {
    SYSCALL_GET_ARGS_3(key_t, key, size_t, size, int, shmflg);
    ret = shmget(key, size, shmflg);
    break;
  }
  case SYS_shmat:
  {
    SYSCALL_GET_ARGS_3(int, shmid, const void *, shmaddr, int, shmflg);
    ret = (unsigned long)shmat(shmid, shmaddr, shmflg);
    break;
  }
  case SYS_shmdt:
  {
    SYSCALL_GET_ARG(const void *, shmaddr);
    if (dmtcp_svipc_inside_shmdt != NULL &&
        dmtcp_svipc_inside_shmdt()) {
      ret = _real_syscall(SYS_shmdt, shmaddr);
    } else {
      ret = shmdt(shmaddr);
    }
    break;
  }
  case SYS_shmctl:
  {
    SYSCALL_GET_ARGS_3(int, shmid, int, cmd, struct shmid_ds *, buf);
    ret = shmctl(shmid, cmd, buf);
    break;
  }
# endif // ifdef __x86_64__
#endif // ifndef DISABLE_SYS_V_IPC
# ifndef __aarch64__
  case SYS_poll:
  {
    SYSCALL_GET_ARGS_3(struct pollfd *, fds, nfds_t, nfds, int, timeout);
    ret = poll(fds, nfds, timeout);
    break;
  }
# endif // ifdef __aarch64__
# ifndef __aarch64__
  case SYS_epoll_create:
  {
    SYSCALL_GET_ARG(int, size);
    ret = epoll_create(size);
    break;
  }
# endif // ifdef __aarch64__
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 13) && __GLIBC_PREREQ(2, 4)
# ifndef __aarch64__
  case SYS_inotify_init:
  {
    ret = inotify_init();
    break;
  }
# endif // ifdef __aarch64__
#endif // if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 13) && __GLIBC_PREREQ(2,
       // 4)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22) && __GLIBC_PREREQ(2, 8)
# ifndef __aarch64__
  case SYS_signalfd:
  {
    SYSCALL_GET_ARGS_3(int, fd, sigset_t *, mask, int, flags);
    ret = signalfd(fd, mask, flags);
    break;
  }
# endif // ifndef __aarch64__
#endif // if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22) && __GLIBC_PREREQ(2,
       // 8)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27) && __GLIBC_PREREQ(2, 8)
  case SYS_signalfd4:
  {
    SYSCALL_GET_ARGS_3(int, fd, sigset_t *, mask, int, flags);
    ret = signalfd(fd, mask, flags);
    break;
  }
#endif // if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27) && __GLIBC_PREREQ(2,
       // 8)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22) && __GLIBC_PREREQ(2, 8)
# ifndef __aarch64__
  case SYS_eventfd:
  {
    SYSCALL_GET_ARGS_2(unsigned int, initval, int, flags);
    ret = eventfd(initval, flags);
    break;
  }
# endif // ifndef __aarch64__
#endif // if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22) && __GLIBC_PREREQ(2,
       // 8)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27) && __GLIBC_PREREQ(2, 8)
  case SYS_eventfd2:
  {
    SYSCALL_GET_ARGS_2(unsigned int, initval, int, flags);
    ret = eventfd(initval, flags);
    break;
  }
#endif // if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27) && __GLIBC_PREREQ(2,
       // 8)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27) && __GLIBC_PREREQ(2, 9)
  case SYS_epoll_create1:
  {
    SYSCALL_GET_ARG(int, flags);
    ret = epoll_create1(flags);
    break;
  }
  case SYS_inotify_init1:
  {
    SYSCALL_GET_ARG(int, flags);
    ret = inotify_init1(flags);
    break;
  }
#endif // if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27) && __GLIBC_PREREQ(2,
       // 9)

  default:
  {
    SYSCALL_GET_ARGS_7(void *, arg1, void *, arg2, void *, arg3, void *, arg4,
                       void *, arg5, void *, arg6, void *, arg7);
    ret = _real_syscall(sys_num, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
    break;
  }
  }
  va_end(ap);
  return ret;
}

/*
extern "C" int
printf (const char *format, ...)
{
  va_list arg;
  int done;

  va_start (arg, format);
  done = vfprintf (stdout, format, arg);
  va_end (arg);

  return done;
}

extern "C" int
fprintf (FILE *stream, const char *format, ...)
{
  va_list arg;
  int done;

  va_start (arg, format);
  done = vfprintf (stream, format, arg);
  va_end (arg);

  return done;
}

extern "C" int
vprintf (const char *format, __gnuc_va_list arg)
{
  return vfprintf (stdout, format, arg);
}

extern "C" int
vfprintf (FILE *s, const char *format, va_list ap)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  int retVal = _real_vfprintf ( s, format, ap );
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retVal;
}


extern "C" int
sprintf (char *s, const char *format, ...)
{
  va_list arg;
  int done;

  va_start (arg, format);
  done = vsprintf (s, format, arg);
  va_end (arg);

  return done;
}


extern "C" int
snprintf (char *s, size_t maxlen, const char *format, ...)
{
  va_list arg;
  int done;

  va_start (arg, format);
  done = vsnprintf (s, maxlen, format, arg);
  va_end (arg);

  return done;
}


extern "C" int
vsprintf(char *str, const char *format, va_list ap);
extern "C" int
vsnprintf(char *str, size_t size, const char *format, va_list ap);
*/
