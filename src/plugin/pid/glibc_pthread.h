#ifndef DMTCP_GLIBC_PTHREAD_H
#define DMTCP_GLIBC_PTHREAD_H
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <thread_db.h>
#include <gnu/libc-version.h>

#define USE_VIRTUAL_TID_LIBC_STRUCT_PTHREAD
#ifdef USE_VIRTUAL_TID_LIBC_STRUCT_PTHREAD

#if 0
/*****************************************************************************
 *
 *****************************************************************************/
/* Offset computed (&x.pid - &x) for
 *   struct pthread x;
 * as found in:  glibc-2.5/nptl/descr.h
 * It was 0x4c and 0x48 for pid and tid for i386.
 * Roughly, the definition is:
 *glibc-2.5/nptl/descr.h:
 * struct pthread
 * {
 *  union {
 *   tcbheader_t tcbheader;
 *   void *__padding[16];
 *  };
 *  list_t list;
 *  pid_t tid;
 *  pid_t pid;
 *  ...
 * } __attribute ((aligned (TCB_ALIGNMENT)));
 *
 *glibc-2.5/nptl/sysdeps/pthread/list.h:
 * typedef struct list_head
 * {
 *  struct list_head *next;
 *  struct list_head *prev;
 * } list_t;
 *
 * NOTE: glibc-2.10 changes the size of __padding from 16 to 24.  --KAPIL
 *
 * NOTE: glibc-2.11 further changes the size tcphead_t without updating the
 *       size of __padding in struct pthread. We need to add an extra 512 bytes
 *       to accommodate this.                                    -- KAPIL
 */

// NOTE: tls_tid_offset, tls_pid_offset determine offset independently of
// glibc version.  These STATIC_... versions serve as a double check.
// Calculate offsets of pid/tid in pthread 'struct user_desc'
// The offsets are needed for two reasons:
// 1. glibc pthread functions cache the pid; must update this after restart
// 2. glibc pthread functions cache the tid; pthread functions pass address
// of cached tid to clone, and MTCP grabs it; But MTCP is still missing
// the address where pthread cached the tid of motherofall.  So, it can't
// update.
/*
 * For those who want to dig deeper into glibc and its thread descriptor,
 *   see below.  Note that pthread_self returns a pthread_t, which is a
 *   pointer to the 'struct pthread' below.
 * FROM: glibc-2.23/sysdeps/x86_64/nptl/tls.h
 * // Return the thread descriptor for the current thread.
 * # define THREAD_SELF \
 *   ({ struct pthread *__self;                                                  \
 *      asm ("mov %%fs:%c1,%0" : "=r" (__self)                                   \
 *           : "i" (offsetof (struct pthread, header.self)));                    \
 *      __self;})
 * // struct pthread is defined in glibc-2.23/nptl/descr.h
 * // type = struct pthread {
 * //     union {
 * //         tcbhead_t header;
 * //         void *__padding[24];
 * //     };
 * //     list_t list; // 2*sizeof(void *)
 * //     pid_t tid;
 * //     pid_t pid;
 * // NOTE: sizeof(tcbhead_t) + 2*sizeof(void *) == 720
 * //       where:  sizeof(tcbhead_t) == 704
 */

/* NOTE:  For future reference, the STATIC_TLS_TID_OFFSET() for a glibc version
 *  can be easily discvoered as long as a debug version of glibc is present:
 *  (gdb) p (char *)&(((struct pthread *)pthread_self())->tid) - \
 *                                                      (char *)pthread_self()
 *  $14 = 720  # So, 720 is the correct offset in this example.
 */
#endif

// Libc-internal signals.
#define SIGCANCEL       __SIGRTMIN
#define SIGTIMER        SIGCANCEL
#define SIGSETXID       (__SIGRTMIN + 1)

#define ATTR_FLAG_DETACHSTATE 0x0001
#define ATTR_FLAG_NOTINHERITSCHED 0x0002
#define ATTR_FLAG_SCOPEPROCESS 0x0004
#define ATTR_FLAG_STACKADDR 0x0008
#define ATTR_FLAG_OLDATTR 0x0010
#define ATTR_FLAG_SCHED_SET 0x0020
#define ATTR_FLAG_POLICY_SET 0x0040
#define ATTR_FLAG_DO_RSEQ 0x0080

// TODO(kapil): Add dynamic check for this.
#define _STACK_GROWS_DOWN 1

struct libc_tcbhead_t {
  char pad[704];
};

struct libc_list_t {
  struct list_head *next;
  struct list_head *prev;
};

struct libc_robust_list_head {
  void *list;
  long futex_offset;
  void *list_op_pending;
};

struct libc_pthread_key_data {
  uintptr_t seq;
  void *data;
};

typedef char libc_Bool;
typedef unsigned long libc_Unwind_Word;

struct libc_Unwind_Exception

{
  union __union
  {
    struct __struct
    {
      unsigned long /*_Unwind_Exception_Class*/ exception_class;
      void /*_Unwind_Exception_Cleanup_Fn*/ *exception_cleanup;
      libc_Unwind_Word private_1;
      libc_Unwind_Word private_2;
    } _struct;

    /* The IA-64 ABI says that this structure must be double-word aligned.
    */
    libc_Unwind_Word unwind_exception_align[2]
      __attribute__ ((__aligned__ (2 * sizeof (libc_Unwind_Word))));
  } _union;
};

#define IS_DETACHED(pd) ((pd)->joinid == (pd))

struct libc2_33_pthread {
  libc_tcbhead_t header;
  libc_list_t list;

  pid_t tid;

  void *robust_prev;
  struct libc_robust_list_head robust_head;

  void /*struct libc_pthread_cleanup_buffer*/ *cleanup;
  void /*struct pthread_unwind_buf*/ *cleanup_jmp_buf;
  int cancelhandling;
  int flags;
  struct libc_pthread_key_data specific_1stblock[32];
  struct libc_pthread_key_data *specific[32];
  libc_Bool specific_used;
  libc_Bool report_events;
  libc_Bool user_stack;
  libc_Bool stopped_start;
  union {
    int setup_failed;
    int parent_cancelhandling;
  };
  int lock;
  unsigned int setxid_futex;
  pthread_t joinid;
  void *result;
  struct sched_param schedparam;
  int schedpolicy;
    void *(*start_routine)(void *);
    void *arg;
    td_eventbuf_t eventbuf;
    struct pthread *nextevent;
    struct libc_Unwind_Exception exc;
    void *stackblock;
    size_t stackblock_size;
    size_t guardsize;
    size_t reported_guardsize;
    /*
    struct priority_protection_data *tpp;
    struct __res_state res;
    internal_sigset_t sigmask;
    struct rtld_catch *rtld_catch;
    _Bool c11;
    _Bool exiting;
    int exit_lock;
    struct tls_internal_t tls_state;
    struct rseq rseq_area;
    */
  char end_padding[];
};

struct libc2_11_pthread {
  libc_tcbhead_t header;
  libc_list_t list;

  pid_t tid;
  pid_t pid;

  void *robust_prev;
  struct libc_robust_list_head robust_head;

  void /*struct libc_pthread_cleanup_buffer*/ *cleanup;
  void /*struct pthread_unwind_buf*/ *cleanup_jmp_buf;
  int cancelhandling;
  int flags;
  struct libc_pthread_key_data specific_1stblock[32];
  struct libc_pthread_key_data *specific[32];
  libc_Bool specific_used;
  libc_Bool report_events;
  libc_Bool user_stack;
  libc_Bool stopped_start;
  union {
    int setup_failed;
    int parent_cancelhandling;
  };
  int lock;
  unsigned int setxid_futex;
  pthread_t joinid;
  void *result;
  struct sched_param schedparam;
  int schedpolicy;
    void *(*start_routine)(void *);
    void *arg;
    td_eventbuf_t eventbuf;
    struct pthread *nextevent;
    struct libc_Unwind_Exception exc;
    void *stackblock;
    size_t stackblock_size;
    size_t guardsize;
    size_t reported_guardsize;
    /*
    struct priority_protection_data *tpp;
    struct __res_state res;
    internal_sigset_t sigmask;
    struct rtld_catch *rtld_catch;
    _Bool c11;
    _Bool exiting;
    int exit_lock;
    struct tls_internal_t tls_state;
    struct rseq rseq_area;
    */
  char end_padding[];
};

struct libc2_10_pthread {
  struct {
    void *_pad[24];
  } header;
  libc_list_t list;

  pid_t tid;
  pid_t pid;

  void *robust_prev;
  struct libc_robust_list_head robust_head;

  void /*struct libc_pthread_cleanup_buffer*/ *cleanup;
  void /*struct pthread_unwind_buf*/ *cleanup_jmp_buf;
  int cancelhandling;
  int flags;
  struct libc_pthread_key_data specific_1stblock[32];
  struct libc_pthread_key_data *specific[32];
  libc_Bool specific_used;
  libc_Bool report_events;
  libc_Bool user_stack;
  libc_Bool stopped_start;
  union {
    int setup_failed;
    int parent_cancelhandling;
  };
  int lock;
  unsigned int setxid_futex;
  pthread_t joinid;
  void *result;
  struct sched_param schedparam;
  int schedpolicy;
    void *(*start_routine)(void *);
    void *arg;
    td_eventbuf_t eventbuf;
    struct pthread *nextevent;
    struct libc_Unwind_Exception exc;
    void *stackblock;
    size_t stackblock_size;
    size_t guardsize;
    size_t reported_guardsize;
    /*
    struct priority_protection_data *tpp;
    struct __res_state res;
    internal_sigset_t sigmask;
    struct rtld_catch *rtld_catch;
    _Bool c11;
    _Bool exiting;
    int exit_lock;
    struct tls_internal_t tls_state;
    struct rseq rseq_area;
    */
  char end_padding[];
};

struct libc2_x_pthread {
  struct {
    void *_pad[16];
  } header;
  libc_list_t list;

  pid_t tid;
  pid_t pid;

  void *robust_prev;
  struct libc_robust_list_head robust_head;

  void /*struct libc_pthread_cleanup_buffer*/ *cleanup;
  void /*struct pthread_unwind_buf*/ *cleanup_jmp_buf;
  int cancelhandling;
  int flags;
  struct libc_pthread_key_data specific_1stblock[32];
  struct libc_pthread_key_data *specific[32];
  libc_Bool specific_used;
  libc_Bool report_events;
  libc_Bool user_stack;
  libc_Bool stopped_start;
  union {
    int setup_failed;
    int parent_cancelhandling;
  };
  int lock;
  unsigned int setxid_futex;
  pthread_t joinid;
  void *result;
  struct sched_param schedparam;
  int schedpolicy;
    void *(*start_routine)(void *);
    void *arg;
    td_eventbuf_t eventbuf;
    struct pthread *nextevent;
    struct libc_Unwind_Exception exc;
    void *stackblock;
    size_t stackblock_size;
    size_t guardsize;
    size_t reported_guardsize;
    /*
    struct priority_protection_data *tpp;
    struct __res_state res;
    internal_sigset_t sigmask;
    struct rtld_catch *rtld_catch;
    _Bool c11;
    _Bool exiting;
    int exit_lock;
    struct tls_internal_t tls_state;
    struct rseq rseq_area;
    */
  char end_padding[];
};

struct libc_pthread_addr {
  pid_t *tid;
  int *cancelhandling;
  int *flags;
  int *lock;
  pthread_t *joinid;
  struct sched_param *schedparam;
  int *schedpolicy;
  void **stackblock;
  size_t *stackblock_size;
  size_t *guardsize;
  size_t *reported_guardsize;
};

#define INVALID_TD_P(pd) 0

void glibc_lll_lock(int *futex);
void glibc_lll_unlock(int *futex);

struct libc_pthread_addr dmtcp_pthread_get_addrs(pthread_t th);

void dmtcp_pthread_lll_lock(pthread_t th);
void dmtcp_pthread_lll_unlock(pthread_t th);

pid_t *dmtcp_pthread_get_tid_addr(pthread_t th);

pid_t dmtcp_pthread_get_tid(pthread_t th);
void dmtcp_pthread_set_tid(pthread_t th, pid_t tid);

int dmtcp_pthread_get_flags(pthread_t th);
void dmtcp_pthread_set_flags(pthread_t th, int flags);

void dmtcp_pthread_set_schedparam(pthread_t thread,
                                  int policy,
                                  const struct sched_param *param);
void dmtcp_pthread_get_schedparam(pthread_t thread,
                                  int *policy,
                                  struct sched_param *param);

// Copied from kernel-posix-cpu-timer.h in glibc.
#define CPUCLOCK_PERTHREAD_MASK 4
#define CPUCLOCK_SCHED 2

static inline clockid_t
make_process_cpuclock(unsigned int pid, clockid_t clock)
{
  return ((~pid) << 3) | clock;
}

static inline clockid_t
make_thread_cpuclock(unsigned int tid, clockid_t clock)
{
  return make_process_cpuclock(tid, clock | CPUCLOCK_PERTHREAD_MASK);
}

#endif // #ifdef USE_VIRTUAL_TID_LIBC_STRUCT_PTHREAD
#endif // #ifndef DMTCP_GLIBC_PTHREAD_H
