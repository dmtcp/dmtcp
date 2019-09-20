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

#ifndef PIDWRAPPERS_H
#define PIDWRAPPERS_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif  // ifndef _GNU_SOURCE

// FIXME:  Why are we adding all these includes here, if we're declaring
// only our own _real_XXX() functions?  Some *wrappers.cpp files
// use these includes.  But, then we should split up these includes
// among the individual *wrappers.cpp files that actually need them,
// and not declare every possible include in one giant .h file.
#include <linux/version.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "config.h"

// To support CMA (Cross Memory Attach)
#ifdef HAS_CMA
#include <sys/uio.h>
#endif  // ifdef HAS_CMA

// This was needed for 64-bit SUSE LINUX Enterprise Server 9 (Linux 2.6.5):
#ifndef PTRACE_GETEVENTMSG
#include <sys/ptrace.h>
#endif  // ifndef PTRACE_GETEVENTMSG
#include <stdarg.h>
#if defined(__arm__) || defined(__aarch64__)
struct user_desc {
  int dummy;
};                    /* <asm/ldt.h> is missing in Ubuntu 14.04 */
#else                 // if defined(__arm__) || defined(__aarch64__)
#include <asm/ldt.h>  // Needed for 'struct user_desc' (arg 6 of __clone)
#endif                // if defined(__arm__) || defined(__aarch64__)
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/procfs.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <syslog.h>
#include <thread_db.h>

// FIXME:   We define _real_msgctl() in terms of msgctl() here.  So,
// we need sys/msg.h.  But sys/msg.h also declares msgrcv().
// SLES 10 declares msgrcv() one way, and others define it differently.
// So, we need this hack.  (Do we really need msgctl() defined here?
// Note that the file pidwrappers.cpp doesn't use msgctl().)
// msgrcv has conflicting return types on some systems (e.g. SLES 10)
// So, we temporarily rename it so that type declarations are not for msgrcv.
#define msgrcv msgrcv_glibc
# include <sys/msg.h>
#undef msgrcv
#include <dirent.h>
#include <grp.h>
#include <mqueue.h>
#include <netdb.h>
#include <pwd.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dmtcp.h"

// Keep in sync with dmtcp/src/constants.h
#define ENV_VAR_VIRTUAL_PID "DMTCP_VIRTUAL_PID"

#ifdef __cplusplus
extern "C"
{
#endif // ifdef __cplusplus

union semun {
  int val;                   /* Value for SETVAL */
  struct semid_ds *buf;      /* Buffer for IPC_STAT, IPC_SET */
  unsigned short *array;     /* Array for GETALL, SETALL */
  struct seminfo *__buf;     /* Buffer for IPC_INFO (Linux-specific) */
};

void dmtcpResetPidPpid();
void dmtcpResetTid(pid_t tid);

LIB_PRIVATE void *_real_dlsym(void *handle, const char *symbol);

/* The following function are defined in pidwrappers.cpp */
LIB_PRIVATE pid_t dmtcp_gettid();
LIB_PRIVATE int dmtcp_tkill(int tid, int sig);
LIB_PRIVATE int dmtcp_tgkill(int tgid, int tid, int sig);

#define FOREACH_PIDVIRT_WRAPPER(MACRO) \
  MACRO(fork)                          \
  MACRO(__clone)                       \
  MACRO(gettid)                        \
  MACRO(tkill)                         \
  MACRO(tgkill)                        \
  MACRO(syscall)                       \
  MACRO(shmget)                        \
  MACRO(shmat)                         \
  MACRO(shmdt)                         \
  MACRO(mq_notify)                     \
  MACRO(clock_getcpuclockid)           \
  MACRO(timer_create)                  \
  MACRO(getppid)                       \
  MACRO(tcgetsid)                      \
  MACRO(tcgetpgrp)                     \
  MACRO(tcsetpgrp)                     \
  MACRO(getpgrp)                       \
  MACRO(setpgrp)                       \
  MACRO(getpgid)                       \
  MACRO(setpgid)                       \
  MACRO(getsid)                        \
  MACRO(setsid)                        \
  MACRO(kill)                          \
  MACRO(wait)                          \
  MACRO(waitpid)                       \
  MACRO(waitid)                        \
  MACRO(wait3)                         \
  MACRO(wait4)                         \
  MACRO(ioctl)                         \
  MACRO(setgid)                        \
  MACRO(setuid)                        \
  MACRO(ptrace)                        \
  MACRO(pthread_exit)                  \
  MACRO(fcntl)                         \
  MACRO(open)                          \
  MACRO(open64)                        \
  MACRO(close)                         \
  MACRO(dup2)                          \
  MACRO(fopen64)                       \
  MACRO(opendir)                       \
  MACRO(__xstat)                       \
  MACRO(__xstat64)                     \
  MACRO(__lxstat)                      \
  MACRO(__lxstat64)                    \
  MACRO(readlink)

#define FOREACH_SYSVIPC_CTL_WRAPPER(MACRO) \
  MACRO(shmctl)                            \
  MACRO(semctl)                            \
  MACRO(msgctl)

#define FOREACH_FOPEN_WRAPPER(MACRO) \
  MACRO(fopen)                       \
  MACRO(fclose)

#define FOREACH_SCHED_WRAPPER(MACRO) \
  MACRO(sched_setaffinity)           \
  MACRO(sched_getaffinity)           \
  MACRO(sched_setscheduler)          \
  MACRO(sched_getscheduler)          \
  MACRO(sched_setparam)              \
  MACRO(sched_getparam)              \
  MACRO(sched_setattr)               \
  MACRO(sched_getattr)

#ifdef HAS_CMA
# define FOREACH_CMA_WRAPPER(MACRO) \
  MACRO(process_vm_readv)           \
  MACRO(process_vm_writev)
#endif // ifdef HAS_CMA

#define PIDVIRT_ENUM(x)     pid_enum_ ## x
#define PIDVIRT_GEN_ENUM(x) PIDVIRT_ENUM(x),
typedef enum {
  FOREACH_PIDVIRT_WRAPPER(PIDVIRT_GEN_ENUM)
  FOREACH_SYSVIPC_CTL_WRAPPER(PIDVIRT_GEN_ENUM)
  FOREACH_FOPEN_WRAPPER(PIDVIRT_GEN_ENUM)
  FOREACH_SCHED_WRAPPER(PIDVIRT_GEN_ENUM)
#ifdef HAS_CMA
  FOREACH_CMA_WRAPPER(PIDVIRT_GEN_ENUM)
#endif // ifdef HAS_CMA
  numPidVirtWrappers
} PidVirtWrapperOffset;

pid_t _real_fork();
int _real_clone(int (*fn)(void *arg),
                void *child_stack,
                int flags,
                void *arg,
                int *parent_tidptr,
                struct user_desc *newtls,
                int *child_tidptr);

pid_t _real_gettid(void);
int _real_tkill(int tid, int sig);
int _real_tgkill(int tgid, int tid, int sig);

long int _real_syscall(long int sys_num, ...);

/* System V shared memory */
int _real_shmget(key_t key, size_t size, int shmflg);
void *_real_shmat(int shmid, const void *shmaddr, int shmflg);
int _real_shmdt(const void *shmaddr);
int _real_shmctl(int shmid, int cmd, struct shmid_ds *buf);
int _real_semctl(int semid, int semnum, int cmd, ...);
int _real_msgctl(int msqid, int cmd, struct msqid_ds *buf);
int _real_mq_notify(mqd_t mqdes, const struct sigevent *sevp);
int _real_clock_getcpuclockid(pid_t pid, clockid_t *clock_id);
int _real_timer_create(clockid_t clockid,
                       struct sigevent *sevp,
                       timer_t *timerid);

pid_t _real_getpid(void);
pid_t _real_getppid(void);

pid_t _real_tcgetsid(int fd);
pid_t _real_tcgetpgrp(int fd);
int _real_tcsetpgrp(int fd, pid_t pgrp);

pid_t _real_getpgrp(void);
pid_t _real_setpgrp(void);

pid_t _real_getpgid(pid_t pid);
int _real_setpgid(pid_t pid, pid_t pgid);

pid_t _real_getsid(pid_t pid);
pid_t _real_setsid(void);

int _real_kill(pid_t pid, int sig);

pid_t _real_wait(__WAIT_STATUS stat_loc);
pid_t _real_waitpid(pid_t pid, int *stat_loc, int options);
int _real_waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options);

pid_t _real_wait3(__WAIT_STATUS status, int options, struct rusage *rusage);
pid_t _real_wait4(pid_t pid,
                  __WAIT_STATUS status,
                  int options,
                  struct rusage *rusage);
LIB_PRIVATE extern int send_sigwinch;
int _real_ioctl(int d, unsigned long int request, ...) __THROW;

int _real_setgid(gid_t gid);
int _real_setuid(uid_t uid);

long _real_ptrace(enum __ptrace_request request,
                  pid_t pid,
                  void *addr,
                  void *data);

void _real_pthread_exit(void *retval);
int _real_fcntl(int fd, int cmd, void *arg);

int _real_open(const char *pathname, int flags, ...);
int _real_open64(const char *pathname, int flags, ...);
int _real_close(int fd);
int _real_dup2(int fd1, int fd2);
FILE *_real_fopen(const char *path, const char *mode);
FILE *_real_fopen64(const char *path, const char *mode);
int _real_fclose(FILE *fp);
DIR *_real_opendir(const char *name);
int _real_xstat(int vers, const char *path, struct stat *buf);
int _real_xstat64(int vers, const char *path, struct stat64 *buf);
int _real_lxstat(int vers, const char *path, struct stat *buf);
int _real_lxstat64(int vers, const char *path, struct stat64 *buf);
ssize_t _real_readlink(const char *path, char *buf, size_t bufsiz);
#ifdef HAS_CMA
ssize_t _real_process_vm_readv(pid_t pid,
                               const struct iovec *local_iov,
                               unsigned long liovcnt,
                               const struct iovec *remote_iov,
                               unsigned long riovcnt,
                               unsigned long flags);
ssize_t _real_process_vm_writev(pid_t pid,
                                const struct iovec *local_iov,
                                unsigned long liovcnt,
                                const struct iovec *remote_iov,
                                unsigned long riovcnt,
                                unsigned long flags);
#endif // ifdef HAS_CMA

int _real_sched_setaffinity(pid_t pid, size_t cpusetsize,
                            const cpu_set_t *mask);
int _real_sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask);
int _real_sched_setscheduler(pid_t pid,
                             int policy,
                             const struct sched_param *param);
int _real_sched_getscheduler(pid_t pid);
int _real_sched_setparam(pid_t pid, const struct sched_param *param);
int _real_sched_getparam(pid_t pid, struct sched_param *param);
#if 0
int _real_sched_setattr(pid_t pid,
                        const struct sched_attr *attr,
                        unsigned int flags);
int _real_sched_getattr(pid_t pid,
                        const struct sched_attr *attr,
                        unsigned int size,
                        unsigned int flags);
#endif // if 0

#ifdef __cplusplus
}
#endif // ifdef __cplusplus
#endif // ifndef PIDWRAPPERS_H
