#ifndef THREADINFO_H
#define THREADINFO_H

#include <linux/version.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>
#include "../jalib/jalloc.h"
#include "dmtcp.h"
#include "glibc_pthread.h"

// For i386 and x86_64, SETJMP currently has bugs.  Don't turn this
// on for them until they are debugged.
// Default is to use  setcontext/getcontext.
#if defined(__arm__) || defined(__aarch64__) || defined(__riscv)
# define SETJMP /* setcontext/getcontext not defined for ARM glibc */
#endif // if defined(__arm__) || defined(__aarch64__)

#ifdef SETJMP
# include <setjmp.h>
#else // ifdef SETJMP
# include <ucontext.h>
#endif // ifdef SETJMP

typedef int (*fptr)(void *);

#ifdef __i386__
typedef struct _ThreadTLSInfo {
  unsigned short fs;
  unsigned short gs;  // thread local storage pointers
  struct user_desc gdtentrytls;
} ThreadTLSInfo;
#endif

#if __x86_64__
typedef struct _ThreadTLSInfo {
  unsigned long int fs;
  unsigned long int gs;
} ThreadTLSInfo;
#endif

#if defined(__arm__) || defined(__aarch64__) || defined(__riscv)
typedef struct _ThreadTLSInfo {
  unsigned long int tlsAddr;
} ThreadTLSInfo;
#endif // ifdef __i386__

typedef enum ThreadState {
  ST_RUNNING,
  ST_SIGNALED,
  ST_SUSPINPROG,
  ST_SUSPENDED,
  ST_CKPNTHREAD,
  ST_THREAD_CREATE
} ThreadState;

class ThreadInfo;
using Thread = ThreadInfo;

namespace dmtcp
{
namespace SigInfo
{
int ckptSignal();
}
}

class ThreadInfo {
 public:
#ifdef JALIB_ALLOCATOR
  static void *operator new(size_t nbytes, void *p) { return p; }

  static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

  static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR

  ThreadInfo() = default;
  bool updateState(ThreadState newState, ThreadState oldState);
  void saveSigState();
  void restoreSigState();
  void saveTLSState();
  void restoreTLSState();
  void verifyTLSPidTid(pid_t pid);
  int sendSignal(int sig);
  void markExiting();
  void initPthreadFields();
  void setSigmask();

  pid_t tid = 0;
  ThreadState state = ST_RUNNING;
  int exiting = 0;

  char procname[17] = {};
  pthread_t pthread = {};

  int flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SYSVSEM |
              CLONE_SIGHAND | CLONE_THREAD | CLONE_SETTLS |
              CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID;
  pid_t *ptid = nullptr;
  pid_t *ctid = nullptr;

  sigset_t sigblockmask = {}; // blocked signals
  sigset_t sigpending = {};   // pending signals

  void *saved_sp = nullptr; // at restart, we use a temporary stack just
                            // beyond original stack (red zone)

  LibcPthreadShim pthreadShim = {};
  ThreadTLSInfo tlsInfo = {};

  // JA: new code ported from v54b
#ifdef SETJMP
  sigjmp_buf jmpbuf = {}; // sigjmp_buf saved by sigsetjmp on ckpt
#else // ifdef SETJMP
  ucontext_t savctx = {}; // context saved on suspend
#endif // ifdef SETJMP

  uint32_t wrapperLockCount = 0;

  Thread *next = nullptr;
  Thread *prev = nullptr;
};

Thread *dmtcp_get_current_thread();

// This symbol is added as weak to allow linkage from dmtcp_launch, etc., via
// CoordinatorAPI.
bool dmtcp_is_ckpt_thread() __attribute((weak));

EXTERNC pid_t dmtcp_get_real_tid() __attribute((weak));
EXTERNC pid_t dmtcp_get_real_pid() __attribute((weak));
EXTERNC int dmtcp_real_tgkill(pid_t pid, pid_t tid, int sig)
  __attribute((weak));
EXTERNC pid_t dmtcp_update_virtual_to_real_tid(pid_t tid) __attribute((weak));

#endif // ifndef THREADINFO_H
