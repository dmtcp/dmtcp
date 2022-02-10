#ifndef THREADINFO_H
#define THREADINFO_H

#include <linux/version.h>
#include <signal.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>
#include "mtcp/restore_libc.h"
#include "protectedfds.h"
#include "syscallwrappers.h" /* for _real_syscall */

// For i386 and x86_64, SETJMP currently has bugs.  Don't turn this
// on for them until they are debugged.
// Default is to use  setcontext/getcontext.
#if defined(__arm__) || defined(__aarch64__)
# define SETJMP /* setcontext/getcontext not defined for ARM glibc */
#endif // if defined(__arm__) || defined(__aarch64__)

#ifdef SETJMP
# include <setjmp.h>
#else // ifdef SETJMP
# include <ucontext.h>
#endif // ifdef SETJMP

#ifdef __cplusplus
extern "C"
{
#endif // ifdef __cplusplus

#define GETTID()              (pid_t)_real_syscall(SYS_gettid)
#define TGKILL(pid, tid, sig) _real_syscall(SYS_tgkill, pid, tid, sig)

pid_t dmtcp_get_real_tid() __attribute((weak));
pid_t dmtcp_get_real_pid() __attribute((weak));
int dmtcp_real_tgkill(pid_t pid, pid_t tid, int sig) __attribute((weak));

#define THREAD_REAL_PID() \
  (dmtcp_get_real_pid != NULL ? dmtcp_get_real_pid() : getpid())

#define THREAD_REAL_TID() \
  (dmtcp_get_real_tid != NULL ? dmtcp_get_real_tid() : GETTID())

#define THREAD_TGKILL(pid, tid, sig)                            \
  (dmtcp_real_tgkill != NULL ? dmtcp_real_tgkill(pid, tid, sig) \
                             : TGKILL(pid, tid, sig))

typedef int (*fptr)(void *);

typedef enum ThreadState {
  ST_RUNNING,
  ST_SIGNALED,
  ST_SUSPINPROG,
  ST_SUSPENDED,
  ST_ZOMBIE,
  ST_CKPNTHREAD
} ThreadState;

typedef struct Thread Thread;

struct Thread {
  pid_t tid;
  int state;

  char procname[17];

  int (*fn)(void *);
  void *arg;
  int flags;
  pid_t *ptid;
  pid_t *ctid;

  pid_t virtual_tid;
  sigset_t sigblockmask; // blocked signals
  sigset_t sigpending;   // pending signals

  void *saved_sp; // at restart, we use a temporary stack just
                  // beyond original stack (red zone)

  ThreadTLSInfo tlsInfo;

  // JA: new code ported from v54b
#ifdef SETJMP
  sigjmp_buf jmpbuf;     // sigjmp_buf saved by sigsetjmp on ckpt
#else // ifdef SETJMP
  ucontext_t savctx;     // context saved on suspend
#endif // ifdef SETJMP

  /* This field is used by the ckpt thread to store and print the time
   * mtcp_restart took to read and map memory regions from the ckpt
   * image. This is only used when configured with --enable-timing.
   */
  double ckptReadTime;

  Thread *next;
  Thread *prev;
};

#ifdef __cplusplus
}
#endif // ifdef __cplusplus
#endif // ifndef THREADINFO_H
