/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include <semaphore.h>
#include <sys/syscall.h>
#include <linux/version.h>

#include "jassert.h"
#include "jconvert.h"
#include "jfilesystem.h"
#include "config.h"  // for HAS_CMA
#include "dmtcp.h"
#include "pid.h"
#include "pidwrappers.h"
#include "shareddata.h"
#include "util.h"
#include "virtualpidtable.h"

using namespace dmtcp;

LIB_PRIVATE pid_t getPidFromEnvVar();

void
pidVirt_pthread_atfork_child()
{
  dmtcpResetPidPpid();
  dmtcpResetTid(getpid());
  VirtualPidTable::instance().resetOnFork();
}

extern "C" int
__register_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(
                    void), void *dso_handle)
{
  /* dmtcp_initialize() must be called before __register_atfork().
   * NEXT_FNC() guarantees that dmtcp_initialize() is called if
   * it was not called earlier. */
  return NEXT_FNC(__register_atfork)(prepare, parent, child, dso_handle);
}

extern "C" pid_t
fork()
{
  pid_t retval = 0;
  pid_t virtualPid = getPidFromEnvVar();

  VirtualPidTable::instance().writeVirtualTidToFileForPtrace(virtualPid);

  pid_t realPid = _real_fork();

  if (realPid > 0) { /* Parent Process */
    retval = virtualPid;
    VirtualPidTable::instance().updateMapping(virtualPid, realPid);
    SharedData::setPidMap(virtualPid, realPid);
  } else {
    retval = realPid;
    VirtualPidTable::instance().readVirtualTidFromFileForPtrace();
  }

  return retval;
}

struct ThreadArg {
  int (*fn) (void *arg);  // clone() calls fn that returns int
  void *arg;
  pid_t virtualTid;
  sem_t sem;
};

// Invoked via __clone
LIB_PRIVATE
int
clone_start(void *arg)
{
  struct ThreadArg *threadArg = (struct ThreadArg *)arg;

  int (*fn) (void *) = threadArg->fn;
  void *thread_arg = threadArg->arg;
  pid_t virtualTid = threadArg->virtualTid;

  if (dmtcp_is_running_state()) {
    dmtcpResetTid(virtualTid);
  }

  VirtualPidTable::instance().updateMapping(virtualTid, _real_gettid());
  sem_post(&threadArg->sem);

  JTRACE("Calling user function") (virtualTid);
  return (*fn)(thread_arg);
}

typedef int (*clone_fptr_t)(int (*fn)(void *arg), void *child_stack, int
                            flags, void *arg, int *parent_tidptr,
                            struct user_desc *newtls, int *child_tidptr);

// need to forward user clone
extern "C" int
__clone(int (*fn)(void *arg),
        void *child_stack,
        int flags,
        void *arg,
        int *parent_tidptr,
        struct user_desc *newtls,
        int *child_tidptr)
{
  pid_t virtualTid = -1;
  struct MtcpRestartThreadArg *mtcpRestartThreadArg;

  if (!dmtcp_is_running_state()) {
    mtcpRestartThreadArg = (struct MtcpRestartThreadArg *)arg;
    arg = mtcpRestartThreadArg->arg;
    virtualTid = mtcpRestartThreadArg->virtualTid;
    if (virtualTid != VIRTUAL_TO_REAL_PID(virtualTid)) {
      VirtualPidTable::instance().postRestart();
    }
  } else {
    virtualTid = VirtualPidTable::instance().getNewVirtualTid();
    VirtualPidTable::instance().writeVirtualTidToFileForPtrace(virtualTid);
  }

  // We have to use DMTCP-specific memory allocator because using glibc:malloc
  // can interfere with user threads.
  // We use JALLOC_HELPER_FREE to free this memory in two places:
  // 1.  later in this function in case of failure on call to __clone; and
  // 2.  near the beginnging of clone_start (wrapper for start_routine).
  struct ThreadArg *threadArg =
    (struct ThreadArg *)JALLOC_HELPER_MALLOC(sizeof(struct ThreadArg));
  threadArg->fn = fn;
  threadArg->arg = arg;
  threadArg->virtualTid = virtualTid;
  sem_init(&threadArg->sem, 0, 0);

  JTRACE("Calling libc:__clone");
  pid_t tid = _real_clone(clone_start, child_stack, flags, threadArg,
                          parent_tidptr, newtls, child_tidptr);

  if (dmtcp_is_running_state()) {
    VirtualPidTable::instance().readVirtualTidFromFileForPtrace();
  }

  if (tid > 0) {
    JTRACE("New thread created") (tid);

    /* Wait for child thread to finish initializing.
     * We must let the child thread insert original->current tid in the
     * virtualpidtable. If we don't wait for the child thread and update the
     * pidtable ourselves, there is a possible race if the child thread is
     * short lived. In that case, the parent thread might insert
     * original->current mapping well after the child thread has exited causing
     * stale entries in the virtualpidtable.
     */
    sem_wait(&threadArg->sem);
    sem_destroy(&threadArg->sem);
  } else {
    virtualTid = tid;
  }

  // Free memory previously allocated by calling JALLOC_HELPER_MALLOC
  JALLOC_HELPER_FREE(threadArg);
  return virtualTid;
}

extern "C"
int
shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int ret = _real_shmctl(shmid, cmd, buf);
  if (buf != NULL) {
    buf->shm_cpid = REAL_TO_VIRTUAL_PID(buf->shm_cpid);
    buf->shm_lpid = REAL_TO_VIRTUAL_PID(buf->shm_lpid);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

extern "C"
int
semctl(int semid, int semnum, int cmd, ...)
{
  union semun uarg;
  va_list arg;

  va_start(arg, cmd);
  uarg = va_arg(arg, union semun);
  va_end(arg);

  DMTCP_PLUGIN_DISABLE_CKPT();
  int ret = _real_semctl(semid, semnum, cmd, uarg);
  if (ret != -1 && (cmd & GETPID)) {
    ret = REAL_TO_VIRTUAL_PID(ret);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

extern "C"
int
msgctl(int msqid, int cmd, struct msqid_ds *buf)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int ret = _real_msgctl(msqid, cmd, buf);
  if (ret != -1 && buf != NULL && ((cmd & IPC_STAT) || (cmd & MSG_STAT))) {
    buf->msg_lspid = REAL_TO_VIRTUAL_PID(buf->msg_lspid);
    buf->msg_lrpid = REAL_TO_VIRTUAL_PID(buf->msg_lrpid);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

extern "C"
int
clock_getcpuclockid(pid_t pid, clockid_t *clock_id)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  pid_t realPid = VIRTUAL_TO_REAL_PID(pid);
  int ret = _real_clock_getcpuclockid(realPid, clock_id);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

extern "C" int
timer_create(clockid_t clockid, struct sigevent *sevp, timer_t *timerid)
{
  if (sevp != NULL && (sevp->sigev_notify == SIGEV_THREAD_ID)) {
    DMTCP_PLUGIN_DISABLE_CKPT();
    pid_t virtPid = sevp->_sigev_un._tid;
    sevp->_sigev_un._tid = VIRTUAL_TO_REAL_PID(virtPid);
    int ret = _real_timer_create(clockid, sevp, timerid);
    sevp->_sigev_un._tid = virtPid;
    DMTCP_PLUGIN_ENABLE_CKPT();
    return ret;
  }
  return _real_timer_create(clockid, sevp, timerid);
}

#if 0
extern "C"
int
mq_notify(mqd_t mqdes, const struct sigevent *sevp)
{
  struct sigevent n;
  int ret;

  DMTCP_PLUGIN_DISABLE_CKPT();
  if (sevp != NULL) {
    n = *sevp;
    n.sigev_notify_thread_id = VIRTUAL_TO_REAL_PID(n.sigev_notify_thread_id);
    ret = _real_mq_notify(mqdes, &n);
  } else {
    ret = _real_mq_notify(mqdes, sevp);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}
#endif // if 0

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
  long ret;
  va_list ap;

  va_start(ap, sys_num);

  switch (sys_num) {
  case SYS_gettid:
  {
    ret = dmtcp_gettid();
    break;
  }
  case SYS_tkill:
  {
    SYSCALL_GET_ARGS_2(int, tid, int, sig);
    ret = dmtcp_tkill(tid, sig);
    break;
  }
  case SYS_tgkill:
  {
    SYSCALL_GET_ARGS_3(int, tgid, int, tid, int, sig);
    ret = dmtcp_tgkill(tgid, tid, sig);
    break;
  }

  case SYS_getpid:
  {
    ret = getpid();
    break;
  }
  case SYS_getppid:
  {
    ret = getppid();
    break;
  }

// SYS_getpgrp undefined in aarch64.
// Presumably, it's handled by libc, and is not a kernel call
//   in AARCH64 (e.g., v5.01).
#ifndef __aarch64__
  case SYS_getpgrp:
  {
    ret = getpgrp();
    break;
  }
#endif

  case SYS_getpgid:
  {
    SYSCALL_GET_ARG(pid_t, pid);
    ret = getpgid(pid);
    break;
  }
  case SYS_setpgid:
  {
    SYSCALL_GET_ARGS_2(pid_t, pid, pid_t, pgid);
    ret = setpgid(pid, pgid);
    break;
  }

  case SYS_getsid:
  {
    SYSCALL_GET_ARG(pid_t, pid);
    ret = getsid(pid);
    break;
  }
  case SYS_setsid:
  {
    ret = setsid();
    break;
  }

  case SYS_kill:
  {
    SYSCALL_GET_ARGS_2(pid_t, pid, int, sig);
    ret = kill(pid, sig);
    break;
  }

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 9))
  case SYS_waitid:
  {
    // SYSCALL_GET_ARGS_4(idtype_t,idtype,id_t,id,siginfo_t*,infop,int,options);
    SYSCALL_GET_ARGS_4(int, idtype, id_t, id, siginfo_t *, infop, int, options);
    ret = waitid((idtype_t)idtype, id, infop, options);
    break;
  }
#endif // if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 9))
  case SYS_wait4:
  {
    SYSCALL_GET_ARGS_4(pid_t, pid, __WAIT_STATUS, status, int, options,
                       struct rusage *, rusage);
    ret = wait4(pid, status, options, rusage);
    break;
  }
#ifdef __i386__
  case SYS_waitpid:
  {
    SYSCALL_GET_ARGS_3(pid_t, pid, int *, status, int, options);
    ret = waitpid(pid, status, options);
    break;
  }
#endif // ifdef __i386__

  case SYS_setgid:
  {
    SYSCALL_GET_ARG(gid_t, gid);
    ret = setgid(gid);
    break;
  }
  case SYS_setuid:
  {
    SYSCALL_GET_ARG(uid_t, uid);
    ret = setuid(uid);
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

#ifdef HAS_CMA
  case SYS_process_vm_readv:
  {
    pid_t realPid;
    DMTCP_PLUGIN_DISABLE_CKPT();
    SYSCALL_GET_ARGS_6(pid_t, pid,
                       const struct iovec *, local_iov,
                       unsigned long, liovcnt,
                       const struct iovec *, remote_iov,
                       unsigned long, riovcnt,
                       unsigned long, flags);
    realPid = VIRTUAL_TO_REAL_PID(pid);
    ret = _real_syscall(SYS_process_vm_readv,
                        realPid,
                        local_iov,
                        liovcnt,
                        remote_iov,
                        riovcnt,
                        flags);
    DMTCP_PLUGIN_ENABLE_CKPT();
    break;
  }
  case SYS_process_vm_writev:
  {
    pid_t realPid;
    DMTCP_PLUGIN_DISABLE_CKPT();
    SYSCALL_GET_ARGS_6(pid_t, pid,
                       const struct iovec *, local_iov,
                       unsigned long, liovcnt,
                       const struct iovec *, remote_iov,
                       unsigned long, riovcnt,
                       unsigned long, flags);
    realPid = VIRTUAL_TO_REAL_PID(pid);
    ret = _real_syscall(SYS_process_vm_writev,
                        realPid,
                        local_iov,
                        liovcnt,
                        remote_iov,
                        riovcnt,
                        flags);
    DMTCP_PLUGIN_ENABLE_CKPT();
    break;
  }
#endif // ifdef HAS_CMA

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

#ifdef HAS_CMA
EXTERNC
ssize_t
process_vm_readv(pid_t pid,
                 const struct iovec *local_iov,
                 unsigned long liovcnt,
                 const struct iovec *remote_iov,
                 unsigned long riovcnt,
                 unsigned long flags)
{
  pid_t realPid;
  ssize_t ret;

  DMTCP_PLUGIN_DISABLE_CKPT();
  realPid = VIRTUAL_TO_REAL_PID(pid);
  ret = _real_process_vm_readv(realPid, local_iov, liovcnt,
                               remote_iov, riovcnt, flags);
  DMTCP_PLUGIN_ENABLE_CKPT();

  return ret;
}

EXTERNC
ssize_t
process_vm_writev(pid_t pid,
                  const struct iovec *local_iov,
                  unsigned long liovcnt,
                  const struct iovec *remote_iov,
                  unsigned long riovcnt,
                  unsigned long flags)
{
  pid_t realPid;
  ssize_t ret;

  DMTCP_PLUGIN_DISABLE_CKPT();
  realPid = VIRTUAL_TO_REAL_PID(pid);
  ret = _real_process_vm_writev(realPid, local_iov, liovcnt,
                                remote_iov, riovcnt, flags);
  DMTCP_PLUGIN_ENABLE_CKPT();

  return ret;
}
#endif // ifdef HAS_CMA
