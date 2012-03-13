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

#include <stdarg.h>
#include <stdlib.h>
#include <vector>
#include <list>
#include <string>
#include <fcntl.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/epoll.h>
#include <linux/version.h>
#include <limits.h>
#include "uniquepid.h"
#include "dmtcpworker.h"
#include "threadsync.h"
#include "dmtcpmessagetypes.h"
#include "protectedfds.h"
#include "constants.h"
#include "connectionmanager.h"
#include "syscallwrappers.h"
#include "util.h"
#include "sysvipc.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jconvert.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,13) && __GLIBC_PREREQ(2,4)
#include <sys/inotify.h>
#endif

extern "C" void exit ( int status )
{
  dmtcp::DmtcpWorker::setExitInProgress();
  _real_exit ( status );
  for (;;); // Without this, gcc emits warning:  `noreturn' fnc does return
}


extern "C" int socketpair ( int d, int type, int protocol, int sv[2] )
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  JASSERT ( sv != NULL );
  int rv = _real_socketpair ( d,type,protocol,sv );
  JTRACE ( "socketpair()" ) ( sv[0] ) ( sv[1] );

  dmtcp::TcpConnection *a, *b;

  a = new dmtcp::TcpConnection ( d, type, protocol );
  a->onConnect();
  b = new dmtcp::TcpConnection ( *a, a->id() );

  dmtcp::KernelDeviceToConnection::instance().create ( sv[0] , a );
  dmtcp::KernelDeviceToConnection::instance().create ( sv[1] , b );

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return rv;
}

extern "C" int pipe ( int fds[2] )
{
  JTRACE ( "promoting pipe() to socketpair()" );
  //just promote pipes to socketpairs
  return socketpair ( AF_UNIX, SOCK_STREAM, 0, fds );
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)) && __GLIBC_PREREQ(2,9)
// pipe2 appeared in Linux 2.6.27
extern "C" int pipe2 ( int fds[2], int flags )
{
  JTRACE ( "promoting pipe2() to socketpair()" );
  //just promote pipes to socketpairs
  int newFlags = 0;
  if ((flags & O_NONBLOCK) != 0) newFlags |= SOCK_NONBLOCK;
  if ((flags & O_CLOEXEC)  != 0) newFlags |= SOCK_CLOEXEC;
  return socketpair ( AF_UNIX, SOCK_STREAM | newFlags, 0, fds );
}
#endif


// extern "C" pid_t getpid()
// {
//   return dmtcp::ProcessInfo::instance().pid();
// }

// extern "C" pid_t getppid()
// {
//   pid_t ppid = _real_getppid();
//   if (ppid == 1 ) {
//     dmtcp::ProcessInfo::instance().setppid( 1 );
//   }
//
//   return origPpid;
// }

// extern "C" pid_t setsid(void)
// {
//   pid_t pid = _real_setsid();
//   dmtcp::ProcessInfo::instance().setsid(origPid);
//   return origPid;
// }

#if 1
extern "C" pid_t wait (__WAIT_STATUS stat_loc)
{
  return waitpid(-1, (int*)stat_loc, 0);
}

extern "C" pid_t waitpid(pid_t pid, int *stat_loc, int options)
{
  return wait4(pid, stat_loc, options, NULL);
}

extern "C" pid_t wait3(__WAIT_STATUS status, int options, struct rusage *rusage)
{
  return wait4(-1, status, options, rusage);
}

extern "C"
pid_t wait4(pid_t pid, __WAIT_STATUS status, int options, struct rusage *rusage)
{
  int stat;
  int saved_errno = errno;
  pid_t retval = 0;

  if (status == NULL) {
    status = (__WAIT_STATUS) &stat;
  }

  retval = _real_wait4(pid, status, options, rusage);
  saved_errno = errno;

  if (retval > 0 &&
      (WIFEXITED(*(int*)status) || WIFSIGNALED(*(int*)status))) {
    dmtcp::ProcessInfo::instance().eraseChild(retval);
  }
  errno = saved_errno;
  return retval;
}

extern "C" int waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options)
{
  siginfo_t siginfop;
  memset(&siginfop, 0, sizeof(siginfop));

  int retval = _real_waitid (idtype, id, &siginfop, options);

  if (retval != -1) {
    if ( siginfop.si_code == CLD_EXITED || siginfop.si_code == CLD_KILLED )
      dmtcp::ProcessInfo::instance().eraseChild ( siginfop.si_pid );
  }

  if (retval == 0 && infop != NULL) {
    *infop = siginfop;
  }

  return retval;
}
#endif


/* Reason for using thread_performing_dlopen_dlsym:
 *
 * dlsym/dlopen/dlclose make a call to calloc() internally. We do not want to
 * checkpoint while we are in the midst of dlopen etc. as it can lead to
 * undesired behavior. To do so, we use WRAPPER_EXECUTION_DISABLE_CKPT() at the
 * beginning of the funtion. However, if a checkpoint request is received right
 * after WRAPPER_EXECUTION_DISABLE_CKPT(), the ckpt-thread is queued for wrlock
 * on the pthread-rwlock and any subsequent request for rdlock by other threads
 * will have to wait until the ckpt-thread releases the lock. However, in this
 * scenario, dlopen calls calloc, which then calls
 * WRAPPER_EXECUTION_DISABLE_CKPT() and hence resulting in a deadlock.
 *
 * We set this variable to true, once we are inside the dlopen/dlsym/dlerror
 * wrapper, so that the calling thread won't try to acquire the lock later on.
 */

// TODO: Integrate NEXT_FNC() with remaining wrappers in future.
#include <dlfcn.h>
#define NEXT_FNC(symbol) \
  (next_fnc ? *next_fnc : \
   *(next_fnc = (__typeof__(next_fnc))dlsym(RTLD_NEXT, #symbol)))
extern "C"
void *dlopen(const char *filename, int flag)
{
  void *ret;
  WRAPPER_EXECUTION_DISABLE_CKPT();
  dmtcp::ThreadSync::setThreadPerformingDlopenDlsym();
  ret = _real_dlopen(filename, flag);
  //ret = NEXT_FNC(dlopen)(filename, flag);
  dmtcp::ThreadSync::unsetThreadPerformingDlopenDlsym();
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return ret;
}
extern "C"
int dlclose(void *handle)
{
  int ret;
  WRAPPER_EXECUTION_DISABLE_CKPT();
  dmtcp::ThreadSync::setThreadPerformingDlopenDlsym();
  ret = _real_dlclose(handle);
  //ret = NEXT_FNC(dlclose)(handle);
  dmtcp::ThreadSync::unsetThreadPerformingDlopenDlsym();
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return ret;
}

extern "C"
int shmget(key_t key, size_t size, int shmflg)
{
  int ret;
  WRAPPER_EXECUTION_DISABLE_CKPT();
  while (true) {
    ret = _real_shmget(key, size, shmflg);
    if (ret != -1 &&
        dmtcp::SysVIPC::instance().isConflictingShmid(ret) == false) {
      dmtcp::SysVIPC::instance().on_shmget(key, size, shmflg, ret);
      break;
    }
    JASSERT(_real_shmctl(ret, IPC_RMID, NULL) != -1);
  };
  JTRACE ("Creating new Shared memory segment" ) (key) (size) (shmflg) (ret);
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return ret;
}

extern "C"
void *shmat(int shmid, const void *shmaddr, int shmflg)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  int currentShmid = dmtcp::SysVIPC::instance().originalToCurrentShmid(shmid);
  JASSERT(currentShmid != -1);
  void *ret = _real_shmat(currentShmid, shmaddr, shmflg);
  if (ret != (void *) -1) {
    dmtcp::SysVIPC::instance().on_shmat(shmid, shmaddr, shmflg, ret);
    JTRACE ("Mapping Shared memory segment" ) (shmid) (shmflg) (ret);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return ret;
}

extern "C"
int shmdt(const void *shmaddr)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  int ret = _real_shmdt(shmaddr);
  if (ret != -1) {
    dmtcp::SysVIPC::instance().on_shmdt(shmaddr);
    JTRACE ("Unmapping Shared memory segment" ) (shmaddr);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return ret;
}

extern "C"
int shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  int currentShmid = dmtcp::SysVIPC::instance().originalToCurrentShmid(shmid);
  JASSERT(currentShmid != -1);
  int ret = _real_shmctl(currentShmid, cmd, buf);
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return ret;
}

extern "C" int __clone ( int ( *fn ) ( void *arg ), void *child_stack, int flags, void *arg, int *parent_tidptr, struct user_desc *newtls, int *child_tidptr );

#define SYSCALL_VA_START()                                              \
  va_list ap;                                                           \
  va_start(ap, sys_num)

#define SYSCALL_VA_END()                                                \
  va_end(ap)

#define SYSCALL_GET_ARG(type,arg) type arg = va_arg(ap, type)

#define SYSCALL_GET_ARGS_2(type1,arg1,type2,arg2)                       \
  SYSCALL_GET_ARG(type1,arg1);                                          \
  SYSCALL_GET_ARG(type2,arg2)

#define SYSCALL_GET_ARGS_3(type1,arg1,type2,arg2,type3,arg3)            \
  SYSCALL_GET_ARGS_2(type1,arg1,type2,arg2);                            \
  SYSCALL_GET_ARG(type3,arg3)

#define SYSCALL_GET_ARGS_4(type1,arg1,type2,arg2,type3,arg3,type4,arg4) \
  SYSCALL_GET_ARGS_3(type1,arg1,type2,arg2,type3,arg3);                 \
  SYSCALL_GET_ARG(type4,arg4)

#define SYSCALL_GET_ARGS_5(type1,arg1,type2,arg2,type3,arg3,type4,arg4, \
                           type5,arg5)                                  \
  SYSCALL_GET_ARGS_4(type1,arg1,type2,arg2,type3,arg3,type4,arg4);      \
  SYSCALL_GET_ARG(type5,arg5)

#define SYSCALL_GET_ARGS_6(type1,arg1,type2,arg2,type3,arg3,type4,arg4, \
                           type5,arg5,type6,arg6)                        \
  SYSCALL_GET_ARGS_5(type1,arg1,type2,arg2,type3,arg3,type4,arg4,       \
                     type5,arg5);                                       \
  SYSCALL_GET_ARG(type6,arg6)

#define SYSCALL_GET_ARGS_7(type1,arg1,type2,arg2,type3,arg3,type4,arg4, \
                           type5,arg5,type6,arg6,type7,arg7)             \
  SYSCALL_GET_ARGS_6(type1,arg1,type2,arg2,type3,arg3,type4,arg4,       \
                     type5,arg5,type6,arg6);                             \
  SYSCALL_GET_ARG(type7,arg7)

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
extern "C" long int syscall(long int sys_num, ... )
{
  long int ret;
  va_list ap;

  va_start(ap, sys_num);

  switch ( sys_num ) {

    case SYS_clone:
    {
      typedef int (*fnc) (void*);
      SYSCALL_GET_ARGS_7(fnc, fn, void*, child_stack, int, flags, void*, arg,
                         pid_t*, pid, struct user_desc*, tls, pid_t*, ctid);
      ret = __clone(fn, child_stack, flags, arg, pid, tls, ctid);
      break;
    }

    case SYS_execve:
    {
      SYSCALL_GET_ARGS_3(const char*,filename,char* const *,argv,char* const *,envp);
      ret = execve(filename,argv,envp);
      break;
    }

    case SYS_fork:
    {
      ret = fork();
      break;
    }
    case SYS_exit:
    {
      SYSCALL_GET_ARG(int,status);
      exit(status);
      break;
    }
    case SYS_open:
    {
      SYSCALL_GET_ARGS_3(const char*,pathname,int,flags,mode_t,mode);
      ret = open(pathname, flags, mode);
      break;
    }
    case SYS_close:
    {
      SYSCALL_GET_ARG(int,fd);
      ret = close(fd);
      break;
    }

    case SYS_rt_sigaction:
    {
      SYSCALL_GET_ARGS_3(int,signum,const struct sigaction*,act,struct sigaction*,oldact);
      ret = sigaction(signum, act, oldact);
      break;
    }
    case SYS_rt_sigprocmask:
    {
      SYSCALL_GET_ARGS_3(int,how,const sigset_t*,set,sigset_t*,oldset);
      ret = sigprocmask(how, set, oldset);
      break;
    }
    case SYS_rt_sigtimedwait:
    {
      SYSCALL_GET_ARGS_3(const sigset_t*,set,siginfo_t*,info,
                        const struct timespec*, timeout);
      ret = sigtimedwait(set, info, timeout);
      break;
    }

#ifdef __i386__
    case SYS_sigaction:
    {
      SYSCALL_GET_ARGS_3(int,signum,const struct sigaction*,act,struct sigaction*,oldact);
      ret = sigaction(signum, act, oldact);
      break;
    }
    case SYS_signal:
    {
      typedef void (*sighandler_t)(int);
      SYSCALL_GET_ARGS_2(int,signum,sighandler_t,handler);
      // Cast needed:  signal returns sighandler_t
      ret = (long int)signal(signum, handler);
      break;
    }
    case SYS_sigprocmask:
    {
      SYSCALL_GET_ARGS_3(int,how,const sigset_t*,set,sigset_t*,oldset);
      ret = sigprocmask(how, set, oldset);
      break;
    }
#endif

#ifdef __x86_64__
// These SYS_xxx are only defined for 64-bit Linux
    case SYS_socket:
    {
      SYSCALL_GET_ARGS_3(int,domain,int,type,int,protocol);
      ret = socket(domain,type,protocol);
      break;
    }
    case SYS_connect:
    {
      SYSCALL_GET_ARGS_3(int,sockfd,const struct sockaddr*,addr,socklen_t,addrlen);
      ret = connect(sockfd, addr, addrlen);
      break;
    }
    case SYS_bind:
    {
      SYSCALL_GET_ARGS_3(int,sockfd,const struct sockaddr*,addr,socklen_t,addrlen);
      ret = bind(sockfd,addr,addrlen);
      break;
    }
    case SYS_listen:
    {
      SYSCALL_GET_ARGS_2(int,sockfd,int,backlog);
      ret = listen(sockfd,backlog);
      break;
    }
    case SYS_accept:
    {
      SYSCALL_GET_ARGS_3(int,sockfd,struct sockaddr*,addr,socklen_t*,addrlen);
      ret = accept(sockfd, addr, addrlen);
      break;
    }
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)) && __GLIBC_PREREQ(2,10)
    case SYS_accept4:
    {
      SYSCALL_GET_ARGS_4(int,sockfd,struct sockaddr*,addr,socklen_t*,addrlen,int,flags);
      ret = accept4(sockfd, addr, addrlen, flags);
      break;
    }
#endif
    case SYS_setsockopt:
    {
      SYSCALL_GET_ARGS_5(int,s,int,level,int,optname,const void*,optval,socklen_t,optlen);
      ret = setsockopt(s, level, optname, optval, optlen);
      break;
    }

    case SYS_socketpair:
    {
      SYSCALL_GET_ARGS_4(int,d,int,type,int,protocol,int*,sv);
      ret = socketpair(d,type,protocol,sv);
      break;
    }
#endif

    case SYS_pipe:
    {
      SYSCALL_GET_ARG(int*,fds);
      ret = pipe(fds);
      break;
    }
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)) && __GLIBC_PREREQ(2,9)
    case SYS_pipe2:
    {
      SYSCALL_GET_ARGS_2(int*,fds,int,flags);
      ret = pipe2(fds, flags);
      break;
    }
#endif

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
      SYSCALL_GET_ARGS_3(key_t,key,size_t,size,int,shmflg);
      ret = shmget(key, size, shmflg);
      break;
    }
    case SYS_shmat:
    {
      SYSCALL_GET_ARGS_3(int,shmid,const void*,shmaddr,int,shmflg);
      ret = (unsigned long) shmat(shmid, shmaddr, shmflg);
      break;
    }
    case SYS_shmdt:
    {
      SYSCALL_GET_ARG(const void*,shmaddr);
      ret = shmdt(shmaddr);
      break;
    }
    case SYS_shmctl:
    {
      SYSCALL_GET_ARGS_3(int,shmid,int,cmd,struct shmid_ds*,buf);
      ret = shmctl(shmid, cmd, buf);
      break;
    }
# endif
#endif
    case SYS_poll:
    {
      SYSCALL_GET_ARGS_3(struct pollfd *,fds,nfds_t,nfds,int,timeout);
      ret = poll(fds, nfds, timeout);
      break;
    }
    case SYS_epoll_create:
    {
      SYSCALL_GET_ARG(int,size);
      ret = epoll_create(size);
      break;
    }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,13) && __GLIBC_PREREQ(2,4)
    case SYS_inotify_init:
    {
      ret = inotify_init();
      break;
    }
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27) && __GLIBC_PREREQ(2,9)
    case SYS_epoll_create1:
    {
      SYSCALL_GET_ARG(int,flags);
      ret = epoll_create(flags);
      break;
    }
    case SYS_inotify_init1:
    {
      SYSCALL_GET_ARG(int,flags);
      ret = inotify_init1(flags);
      break;
    }
#endif

    default:
    {
      SYSCALL_GET_ARGS_7(void*, arg1, void*, arg2, void*, arg3, void*, arg4,
                         void*, arg5, void*, arg6, void*, arg7);
      ret = _real_syscall(sys_num, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
      break;
    }
  }
  va_end(ap);
  return ret;
}

// TODO: Move these to a separate pthreadwrappers.cpp file.
extern "C" int
pthread_cond_broadcast(pthread_cond_t *cond)
{
  return _real_pthread_cond_broadcast(cond);
}

extern "C" int
pthread_cond_destroy(pthread_cond_t *cond)
{
  return _real_pthread_cond_destroy(cond);
}

extern "C" int
pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
  return _real_pthread_cond_init(cond, attr);
}

extern "C" int
pthread_cond_signal(pthread_cond_t *cond)
{
  return _real_pthread_cond_signal(cond);
}

extern "C" int
pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                       const struct timespec *abstime)
{
  return _real_pthread_cond_timedwait(cond, mutex, abstime);
}

extern "C" int
pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
  return _real_pthread_cond_wait(cond, mutex);
}


extern "C" int
pthread_mutex_lock(pthread_mutex_t *mutex)
{
  return _real_pthread_mutex_lock(mutex);
}

extern "C" int
pthread_mutex_unlock(pthread_mutex_t *mutex)
{
  return _real_pthread_mutex_unlock(mutex);
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


extern "C" int vsprintf(char *str, const char *format, va_list ap);
extern "C" int vsnprintf(char *str, size_t size, const char *format, va_list ap);
*/
