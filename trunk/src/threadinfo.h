#ifndef THREADINFO_H
#define THREADINFO_H

#include <ucontext.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <linux/version.h>
#include "protectedfds.h"

// For i386 and x86_64, SETJMP currently has bugs.  Don't turn this
//   on for them until they are debugged.
// Default is to use  setcontext/getcontext.
#if defined(__arm__)
# define SETJMP /* setcontext/getcontext not defined for ARM glibc */
#endif

#ifdef SETJMP
# include <setjmp.h>
#else
# include <ucontext.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif

// ARM is missing asm/ldt.h in Ubuntu 11.10 (Linux 3.0, glibc-2.13)
#if defined(__arm__)
/* Structure passed to `modify_ldt', 'set_thread_area', and 'clone' calls.
   This seems to have been stable since the beginning of Linux 2.6  */
struct user_desc
{
  unsigned int entry_number;
  unsigned long int base_addr;
  unsigned int limit;
  unsigned int seg_32bit:1;
  unsigned int contents:2;
  unsigned int read_exec_only:1;
  unsigned int limit_in_pages:1;
  unsigned int seg_not_present:1;
  unsigned int useable:1;
  unsigned int empty:25;  /* Some variations leave this out. */
};
#else
   // Defines struct user_desc
# include <asm/ldt.h>
  // WARNING: /usr/include/linux/version.h often has out-of-date version.
/* struct user_desc * uinfo; */
/* In Linux 2.6.9 for i386, uinfo->base_addr is
 *   correctly typed as unsigned long int.
 * In Linux 2.6.9, uinfo->base_addr is  incorrectly typed as
 *   unsigned int.  So, we'll just lie about the type.
 */
/* SuSE Linux Enterprise Server 9 uses Linux 2.6.5 and requires original
 * struct user_desc from /usr/include/.../ldt.h
 * Perhaps kernel was patched by backport.  Let's not re-define user_desc.
 */
/* RHEL 4 (Update 3) / Rocks 4.1.1-2.0 has <linux/version.h> saying
 *  LINUX_VERSION_CODE is 2.4.20 (and UTS_RELEASE=2.4.20)
 *  while uname -r says 2.6.9-34.ELsmp.  Here, it acts like a version earlier
 *  than the above 2.6.9.  So, we conditionalize on its 2.4.20 version.
 */
# if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
   /* struct modify_ldt_ldt_s   was defined instead of   struct user_desc   */
#  define user_desc modify_ldt_ldt_s
# endif
#endif

#ifdef __i386__
typedef unsigned short segreg_t;
#elif __x86_64__
typedef unsigned int segreg_t;
#elif __arm__
typedef unsigned int segreg_t;
#endif

#ifdef __x86_64__
# define eax rax
# define ebx rbx
# define ecx rcx
# define edx rax
# define ebp rbp
# define esi rsi
# define edi rdi
# define esp rsp
# define CLEAN_FOR_64_BIT(args...) CLEAN_FOR_64_BIT_HELPER(args)
# define CLEAN_FOR_64_BIT_HELPER(args...) #args
#elif __i386__
# define CLEAN_FOR_64_BIT(args...) #args
#else
# define CLEAN_FOR_64_BIT(args...) "CLEAN_FOR_64_BIT_undefined"
#endif

#define PRINTF(fmt, ...) \
  do { \
    char buf[4096]; \
    int c = sprintf(buf, "[%d] %s:%d in %s; REASON= " fmt, \
                    getpid(), __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
    write(PROTECTED_STDERR_FD, buf, c + 1); \
  } while (0);

#ifdef DEBUG
# define DPRINTF PRINTF
#else
# define DPRINTF(args...) // debug printing
#endif

#define ASSERT(condition) \
  do { \
    if (! (condition)) { \
      PRINTF("Assertion failed: %s\n", #condition); \
      _exit(0); \
    } \
  } while (0);

#define ASSERT_NOT_REACHED() \
  do { \
    PRINTF("NOT_REACHED Assertion failed.\n"); \
    _exit(0); \
  } while (0);


#define GETTID() _real_syscall(SYS_gettid)
#define TGKILL(pid,tid,sig) _real_syscall(SYS_tgkill, pid, tid, sig)

pid_t dmtcp_get_real_tid() __attribute((weak));
pid_t dmtcp_get_real_pid() __attribute((weak));
int dmtcp_real_tgkill(pid_t pid, pid_t tid, int sig) __attribute((weak));

#define THREAD_REAL_PID() \
  (dmtcp_get_real_pid != NULL ? dmtcp_get_real_pid() : getpid())

#define THREAD_REAL_TID() \
  (dmtcp_get_real_tid != NULL ? dmtcp_get_real_tid() : GETTID())

#define THREAD_TGKILL(pid, tid, sig) \
  (dmtcp_real_tgkill != NULL ? dmtcp_real_tgkill(pid,tid,sig) \
                                 : TGKILL(pid, tid, sig))

typedef int (*fptr)(void*);

typedef enum ThreadState {
  ST_RUNNING,
  ST_SIGNALED,
  ST_SUSPINPROG,
  ST_SUSPENDED,
  ST_ZOMBIE,
  ST_CKPNTHREAD
} ThreadState;

typedef struct Thread Thread;

typedef struct Thread {
  pid_t tid;
  Thread *next;
  Thread *prev;
  int state;

  int (*fn)(void *);
  void *arg;
  int flags;
  pid_t *ptid;
  pid_t *ctid;

  pid_t virtual_tid;
  sigset_t sigblockmask; // blocked signals
  sigset_t sigpending;   // pending signals

  void *saved_sp; // at restart, we use a temporary stack just
  //   beyond original stack (red zone)
  segreg_t fs, gs;  // thread local storage pointers
  struct user_desc gdtentrytls[1];

  ///JA: new code ported from v54b
#ifdef SETJMP
  sigjmp_buf jmpbuf;     // sigjmp_buf saved by sigsetjmp on ckpt
#else
  ucontext_t savctx;     // context saved on suspend
#endif

} Thread;

void Thread_Init();
void Thread_PostRestart();

int Thread_UpdateState(Thread *th, ThreadState newval, ThreadState oldval);

void Thread_SaveSigState(Thread *th);
void Thread_RestoreSigState(Thread *th);

#ifdef __cplusplus
}
#endif

#endif
