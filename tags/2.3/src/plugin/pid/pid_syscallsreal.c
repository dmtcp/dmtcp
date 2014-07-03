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


#define _GNU_SOURCE
#define _XOPEN_SOURCE 500
// These next two are defined in features.h based on the user macros above.
// #define GNU_SRC
// #define __USE_UNIX98
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "pidwrappers.h"

typedef int ( *funcptr_t ) ();
typedef pid_t ( *funcptr_pid_t ) ();
typedef funcptr_t ( *signal_funcptr_t ) ();
typedef void* (*dlsym_fnptr_t) (void *handle, const char *symbol);

static void *pid_real_func_addr[numPidVirtWrappers];
static int pid_wrappers_initialized = 0;

#define GET_FUNC_ADDR(name) \
  pid_real_func_addr[PIDVIRT_ENUM(name)] = _real_dlsym(RTLD_NEXT, #name);

#define GET_FUNC_ADDR_V(name, v) \
  pid_real_func_addr[PIDVIRT_ENUM(name)] = dlvsym(RTLD_NEXT, #name, v); \
  if (pid_real_func_addr[PIDVIRT_ENUM(name)] == NULL) { \
    /* Symbol version not found, try the default and hope for the best */ \
    GET_FUNC_ADDR(name); \
  }

#ifdef __i386__
// On 32-bit Linuxes, glibc provides two versions (GLIBC_2.0 and GLIBC_2.2) for
// semctl, msgctl and shmctl. dlsym(RTLD_NEXT, ...) returns the address of the
// GLIBC_2.0 version, whereas we need the GLIBC_2.2 version. In 64-bit glibc,
// there is only one version.
// Similarly, for fopen/fclose/fdopen, there is a GLIBC_2.1 in addition to
// GLIBC_2.0 version.
# define GET_SYSVIPC_CTL_FUNC_ADDR(name) GET_FUNC_ADDR_V(name, "GLIBC_2.2")
# define GET_FOPEN_FUNC_ADDR(name) GET_FUNC_ADDR_V(name, "GLIBC_2.1")
#else
# define GET_SYSVIPC_CTL_FUNC_ADDR(name) GET_FUNC_ADDR(name)
# define GET_FOPEN_FUNC_ADDR(name) GET_FUNC_ADDR(name)
#endif

LIB_PRIVATE
void pid_initialize_wrappers()
{
  if (!pid_wrappers_initialized) {
    FOREACH_PIDVIRT_WRAPPER(GET_FUNC_ADDR);
    FOREACH_SYSVIPC_CTL_WRAPPER(GET_SYSVIPC_CTL_FUNC_ADDR);
    FOREACH_FOPEN_WRAPPER(GET_FOPEN_FUNC_ADDR);
    pid_wrappers_initialized = 1;
  }
}


#define REAL_FUNC_PASSTHROUGH_WORK(name) \
  if (fn == NULL) { \
    if (pid_real_func_addr[PIDVIRT_ENUM(name)] == NULL) \
      pid_initialize_wrappers(); \
    fn = pid_real_func_addr[PIDVIRT_ENUM(name)]; \
    if (fn == NULL) { \
      fprintf(stderr, "%s:%d: *** DMTCP: Error: lookup failed for %s.\n" \
                      "           The symbol wasn't found in current library" \
                      " loading sequence.\n" \
                      "    Aborting.\n", __FILE__, __LINE__, #name); \
      abort(); \
    } \
  }

#define REAL_FUNC_PASSTHROUGH(name)  REAL_FUNC_PASSTHROUGH_TYPED(int, name)

#define REAL_FUNC_PASSTHROUGH_TYPED(type,name) \
  static type (*fn)() = NULL;                  \
  REAL_FUNC_PASSTHROUGH_WORK(name)             \
  return (*fn)

#define REAL_FUNC_PASSTHROUGH_VOID(name) \
  static void (*fn)() = NULL;            \
  REAL_FUNC_PASSTHROUGH_WORK(name)       \
  (*fn)

#define REAL_FUNC_PASSTHROUGH_NORETURN(name)                 \
  static void (*fn)() __attribute__ ((__noreturn__)) = NULL; \
  REAL_FUNC_PASSTHROUGH_WORK(name)                           \
  (*fn)

void *dmtcp_get_libc_dlsym_addr();

LIB_PRIVATE
void *_real_dlsym ( void *handle, const char *symbol ) {
  static dlsym_fnptr_t _libc_dlsym_fnptr = NULL;
  if (_libc_dlsym_fnptr == NULL) {
    _libc_dlsym_fnptr = (dlsym_fnptr_t) dmtcp_get_libc_dlsym_addr();
  }

  return (void*) (*_libc_dlsym_fnptr) ( handle, symbol );
}


LIB_PRIVATE
pid_t _real_getpid(void){
  // libc caches pid of the process and hence after restart, libc:getpid()
  // returns the pre-ckpt value.
  return (pid_t) _real_syscall(SYS_getpid);
}

LIB_PRIVATE
pid_t _real_getppid(void){
  // libc caches ppid of the process and hence after restart, libc:getppid()
  // returns the pre-ckpt value.
  return (pid_t) _real_syscall(SYS_getppid);
}

LIB_PRIVATE
pid_t _real_tcgetsid(int fd){
  REAL_FUNC_PASSTHROUGH ( tcgetsid ) ( fd );
}

LIB_PRIVATE
int _real_tcsetpgrp(int fd, pid_t pgrp){
  REAL_FUNC_PASSTHROUGH ( tcsetpgrp ) ( fd, pgrp );
}

LIB_PRIVATE
pid_t _real_tcgetpgrp(int fd) {
  REAL_FUNC_PASSTHROUGH ( tcgetpgrp ) ( fd );
}

LIB_PRIVATE
pid_t _real_getpgrp(void) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, getpgrp ) ( );
}

LIB_PRIVATE
pid_t _real_setpgrp(void) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, setpgrp ) ( );
}

LIB_PRIVATE
pid_t _real_getpgid(pid_t pid) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, getpgid ) ( pid );
}

LIB_PRIVATE
int   _real_setpgid(pid_t pid, pid_t pgid) {
  REAL_FUNC_PASSTHROUGH ( setpgid ) ( pid, pgid );
}

LIB_PRIVATE
pid_t _real_getsid(pid_t pid) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, getsid ) ( pid );
}

LIB_PRIVATE
pid_t _real_setsid(void) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, setsid ) ( );
}

LIB_PRIVATE
int   _real_kill(pid_t pid, int sig) {
  REAL_FUNC_PASSTHROUGH ( kill ) ( pid, sig );
}

LIB_PRIVATE
pid_t _real_wait(__WAIT_STATUS stat_loc) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, wait ) ( stat_loc );
}

LIB_PRIVATE
pid_t _real_waitpid(pid_t pid, int *stat_loc, int options) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, waitpid ) ( pid, stat_loc, options );
}

LIB_PRIVATE
int   _real_waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options) {
  REAL_FUNC_PASSTHROUGH ( waitid ) ( idtype, id, infop, options );
}

LIB_PRIVATE
pid_t _real_wait3(__WAIT_STATUS status, int options, struct rusage *rusage) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, wait3 ) ( status, options, rusage );
}

LIB_PRIVATE
pid_t _real_wait4(pid_t pid, __WAIT_STATUS status, int options, struct rusage *rusage) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, wait4 ) ( pid, status, options, rusage );
}

LIB_PRIVATE
int _real_ioctl(int d, unsigned long int request, ...) {
  void * arg;
  va_list ap;

  // Most calls to ioctl take 'void *', 'int' or no extra argument
  // A few specialized ones take more args, but we don't need to handle those.
  va_start(ap, request);
  arg = va_arg(ap, void *);
  va_end(ap);

  // /usr/include/unistd.h says syscall returns long int (contrary to man page)
  REAL_FUNC_PASSTHROUGH_TYPED ( int, ioctl ) ( d, request, arg );
}

LIB_PRIVATE
int _real_setgid(gid_t gid) {
  REAL_FUNC_PASSTHROUGH( setgid ) (gid);
}

LIB_PRIVATE
int _real_setuid(uid_t uid) {
  REAL_FUNC_PASSTHROUGH( setuid ) (uid);
}

LIB_PRIVATE
long _real_ptrace(enum __ptrace_request request, pid_t pid, void *addr,
                  void *data) {
  REAL_FUNC_PASSTHROUGH_TYPED ( long, ptrace ) ( request, pid, addr, data );
}

// gettid / tkill / tgkill are not defined in libc.
LIB_PRIVATE
pid_t _real_gettid(void){
  // No glibc wrapper for gettid, although even if it had one, we would have
  // the issues similar to getpid/getppid().
  return (pid_t) _real_syscall(SYS_gettid);
}

LIB_PRIVATE
int   _real_tkill(int tid, int sig) {
  // No glibc wrapper for tkill, although even if it had one, we would have
  // the issues similar to getpid/getppid().
  return (int) _real_syscall(SYS_tkill, tid, sig);
}

LIB_PRIVATE
int   _real_tgkill(int tgid, int tid, int sig) {
  // No glibc wrapper for tgkill, although even if it had one, we would have
  // the issues similar to getpid/getppid().
  return (int) _real_syscall(SYS_tgkill, tgid, tid, sig);
}

LIB_PRIVATE
SYSCALL_ARG_RET_TYPE _real_syscall(SYSCALL_ARG_RET_TYPE sys_num, ... ) {
  int i;
  void * arg[7];
  va_list ap;

  va_start(ap, sys_num);
  for (i = 0; i < 7; i++)
    arg[i] = va_arg(ap, void *);
  va_end(ap);

  // /usr/include/unistd.h says syscall returns long int (contrary to man page)
  REAL_FUNC_PASSTHROUGH_TYPED ( SYSCALL_ARG_RET_TYPE, syscall ) (
                                                      sys_num, arg[0], arg[1],
                                                      arg[2], arg[3], arg[4],
                                                      arg[5], arg[6] );
}

LIB_PRIVATE
pid_t _real_fork() {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, fork ) ();
}

LIB_PRIVATE
int _real_clone ( int ( *function ) (void *), void *child_stack, int flags, void *arg, int *parent_tidptr, struct user_desc *newtls, int *child_tidptr )
{
  REAL_FUNC_PASSTHROUGH ( __clone ) ( function, child_stack, flags, arg,
                                      parent_tidptr, newtls, child_tidptr );
}

LIB_PRIVATE
int _real_shmget (key_t key, size_t size, int shmflg) {
  REAL_FUNC_PASSTHROUGH ( shmget ) (key, size, shmflg);
}

LIB_PRIVATE
void* _real_shmat (int shmid, const void *shmaddr, int shmflg) {
  REAL_FUNC_PASSTHROUGH_TYPED ( void*, shmat ) (shmid, shmaddr, shmflg);
}

LIB_PRIVATE
int _real_shmdt (const void *shmaddr) {
  REAL_FUNC_PASSTHROUGH ( shmdt ) (shmaddr);
}

LIB_PRIVATE
int _real_shmctl (int shmid, int cmd, struct shmid_ds *buf) {
  REAL_FUNC_PASSTHROUGH ( shmctl ) (shmid, cmd, buf);
}

LIB_PRIVATE
int _real_semctl(int semid, int semnum, int cmd, ...) {
  union semun uarg;
  va_list arg;
  va_start (arg, cmd);
  uarg = va_arg (arg, union semun);
  va_end (arg);
  REAL_FUNC_PASSTHROUGH ( semctl ) (semid, semnum, cmd, uarg);
}

LIB_PRIVATE
int _real_msgctl(int msqid, int cmd, struct msqid_ds *buf)
{
  REAL_FUNC_PASSTHROUGH (msgctl) (msqid, cmd, buf);
}

LIB_PRIVATE
int _real_mq_notify(mqd_t mqdes, const struct sigevent *sevp) {
  REAL_FUNC_PASSTHROUGH (mq_notify) (mqdes, sevp);
}

LIB_PRIVATE
int _real_clock_getcpuclockid(pid_t pid, clockid_t *clock_id) {
  REAL_FUNC_PASSTHROUGH (clock_getcpuclockid) (pid, clock_id);
}

LIB_PRIVATE
void _real_pthread_exit (void *retval) {
  REAL_FUNC_PASSTHROUGH_VOID ( pthread_exit ) (retval);
}

LIB_PRIVATE
int _real_fcntl(int fd, int cmd, void *arg) {
  REAL_FUNC_PASSTHROUGH (fcntl) (fd, cmd, arg);
}


int _real_open(const char *path, int flags, ...) {
  mode_t mode = 0;
  // Handling the variable number of arguments
  if (flags & O_CREAT) {
    va_list arg;
    va_start (arg, flags);
    mode = va_arg (arg, int);
    va_end (arg);
  }
  REAL_FUNC_PASSTHROUGH(open) (path, flags, mode);
}

int _real_open64(const char *path, int flags, ...) {
  mode_t mode = 0;
  // Handling the variable number of arguments
  if (flags & O_CREAT) {
    va_list arg;
    va_start (arg, flags);
    mode = va_arg (arg, int);
    va_end (arg);
  }
  REAL_FUNC_PASSTHROUGH(open64) (path, flags, mode);
}

FILE* _real_fopen(const char *path, const char *mode) {
  REAL_FUNC_PASSTHROUGH_TYPED(FILE*, fopen) (path, mode);
}

FILE* _real_fopen64(const char *path, const char *mode) {
  REAL_FUNC_PASSTHROUGH_TYPED(FILE*, fopen) (path, mode);
}

int _real_fclose(FILE *fp) {
  REAL_FUNC_PASSTHROUGH(fclose) (fp);
}

int _real_xstat(int vers, const char *path, struct stat *buf) {
  REAL_FUNC_PASSTHROUGH(__xstat) (vers, path, buf);
}

int _real_xstat64(int vers, const char *path, struct stat64 *buf) {
  REAL_FUNC_PASSTHROUGH(__xstat64) (vers, path, buf);
}

int _real_lxstat(int vers, const char *path, struct stat *buf) {
  REAL_FUNC_PASSTHROUGH(__lxstat) (vers, path, buf);
}

int _real_lxstat64(int vers, const char *path, struct stat64 *buf) {
  REAL_FUNC_PASSTHROUGH(__lxstat64) (vers, path, buf);
}

ssize_t _real_readlink(const char *path, char *buf, size_t bufsiz) {
  REAL_FUNC_PASSTHROUGH(readlink) (path, buf, bufsiz);
}

