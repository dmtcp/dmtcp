/*****************************************************************************
 * Copyright (C) 2010-2014 Kapil Arya <kapil@ccs.neu.edu>                    *
 * Copyright (C) 2010-2014 Gene Cooperman <gene@ccs.neu.edu>                 *
 *                                                                           *
 * DMTCP is free software: you can redistribute it and/or                    *
 * modify it under the terms of the GNU Lesser General Public License as     *
 * published by the Free Software Foundation, either version 3 of the        *
 * License, or (at your option) any later version.                           *
 *                                                                           *
 * DMTCP is distributed in the hope that it will be useful,                  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of            *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
 * GNU Lesser General Public License for more details.                       *
 *                                                                           *
 * You should have received a copy of the GNU Lesser General Public          *
 * License along with DMTCP.  If not, see <http://www.gnu.org/licenses/>.    *
 *****************************************************************************/

#include "mtcp_sys.h"
#include "restore_libc.h"
#include "tlsutil.h"
#include "config.h" // define WSL if present
#include <pthread.h> // for pthread_self(), needed for WSL
#include <elf.h>
#include <errno.h>
#include <gnu/libc-version.h>
#include <linux/version.h>
#include <stdlib.h>
#include <string.h>
#include <sys/personality.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

int mtcp_sys_errno;

#if defined(__x86_64__) || defined(__aarch64__)
# define ELF_AUXV_T Elf64_auxv_t
# define UINT_T     uint64_t
#else /* if defined(__x86_64__) || defined(__aarch64__) */

// else __i386__ and __arm__
# define ELF_AUXV_T Elf32_auxv_t
# define UINT_T     uint32_t
#endif /* if defined(__x86_64__) || defined(__aarch64__) */

extern MYINFO_GS_T myinfo_gs;

static int glibcMajorVersion()
{
  static int major = 0;
  if (major == 0) {
    major = (int) strtol(gnu_get_libc_version(), NULL, 10);
    ASSERT (major == 2);
  }
  return major;
}

static int glibcMinorVersion()
{
  static long minor = 0;
  if (minor == 0) {
    char *ptr;
    int major = (int) strtol(gnu_get_libc_version(), &ptr, 10);
    ASSERT (major == 2);
    minor = (int) strtol(ptr+1, NULL, 10);
  }
  return minor;
}

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

#if !__GLIBC_PREREQ(2, 1)
# error "glibc version too old"
#endif /* if !__GLIBC_PREREQ(2, 1) */

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
static int
STATIC_TLS_TID_OFFSET()
{
  static int offset = -1;

  if (offset != -1) {
    return offset;
  }

#ifdef __x86_64__
  // NEEDED FOR WSL
  if (glibcMinorVersion() >= 23) {
    offset = 720;
    return offset;
  }
#endif

  if (glibcMinorVersion() >= 11) {
#ifdef __x86_64__
    offset = 26 * sizeof(void *) + 512;
#else /* ifdef __x86_64__ */
    offset = 26 * sizeof(void *);
#endif /* ifdef __x86_64__ */
  } else if (glibcMinorVersion() == 10) {
    offset = 26 * sizeof(void *);
  } else {
    offset = 18 * sizeof(void *);
  }

  return offset;
}

#if 0
# if __GLIBC_PREREQ(2, 11)
#  ifdef __x86_64__
#   define STATIC_TLS_TID_OFFSET() (26 *sizeof(void *) + 512)
#  else /* ifdef __x86_64__ */
#   define STATIC_TLS_TID_OFFSET() (26 *sizeof(void *))
#  endif /* ifdef __x86_64__ */

# elif __GLIBC_PREREQ(2, 10)
#  define STATIC_TLS_TID_OFFSET() (26 *sizeof(void *))

# else /* if __GLIBC_PREREQ(2, 11) */
#  define STATIC_TLS_TID_OFFSET() (18 *sizeof(void *))
# endif /* if __GLIBC_PREREQ(2, 11) */
#endif /* if 0 */

#define STATIC_TLS_PID_OFFSET() (STATIC_TLS_TID_OFFSET() + sizeof(pid_t))

/* WHEN WE HAVE CONFIDENCE IN THIS VERSION, REMOVE ALL OTHER __GLIBC_PREREQ
 * AND MAKE THIS THE ONLY VERSION.  IT SHOULD BE BACKWARDS COMPATIBLE.
 */

/* These function definitions should succeed independently of the glibc version.
 * They use get_thread_area() to match (tid, pid) and find offset.
 * In other code, on restart, that offset is used to set (tid,pid) to
 *   the latest tid and pid of the new thread, instead of the (tid,pid)
 *   of the original thread.
 * SEE: "struct pthread" in glibc-2.XX/nptl/descr.h for 'struct pthread'.
 */

/* Can remove the unused attribute when this __GLIBC_PREREQ is the only one. */
static char *memsubarray(char *array, char *subarray, size_t len)
__attribute__((unused));
static uint64_t get_tls_segreg(void);
static void *get_tls_base_addr(void);
extern void **motherofall_saved_sp;
extern ThreadTLSInfo *motherofall_tlsInfo;

/*****************************************************************************
 *
 *****************************************************************************/
int
TLSInfo_GetTidOffset(void)
{
  static int tid_offset = -1;

  if (tid_offset == -1) {
    struct { pid_t tid; pid_t pid; } tid_pid;

    /* struct pthread has adjacent fields, tid and pid, in that order.
     * Try to find at what offset that bit patttern occurs in struct pthread.
     */
    char *tmp;
    tid_pid.tid = mtcp_sys_getpid();
    tid_pid.pid = mtcp_sys_getpid();

    /* Get entry number of current thread descriptor from its segment register:
     * Segment register / 8 is the entry_number for the "thread area", which
     * is of type 'struct user_desc'.   The base_addr field of that struct
     * points to the struct pthread for the thread with that entry_number.
     * The tid and pid are contained in the 'struct pthread'.
     *   So, to access the tid/pid fields, first find the entry number.
     * Then fill in the entry_number field of an empty 'struct user_desc', and
     * get_thread_area(struct user_desc *uinfo) will fill in the rest.
     * Then use the filled in base_address field to get the 'struct pthread'.
     * The function get_tls_base_addr() returns this 'struct pthread' addr.
     */
    void *pthread_desc = get_tls_base_addr();

    /* A false hit for tid_offset probably can't happen since a new
     * 'struct pthread' is zeroed out before adding tid and pid.
     * pthread_desc below is defined as 'struct pthread' in glibc:nptl/descr.h
     */
    tmp = memsubarray((char *)pthread_desc, (char *)&tid_pid, sizeof(tid_pid));

    if (tmp == NULL && glibcMajorVersion() == 2 && glibcMinorVersion() >= 24) {
      // starting with glibc-2.25 (including 2.24.90 on Fedora), the pid field
      // is deprecated and set to zero.
      tid_pid.pid = 0;
      tmp = memsubarray((char *)pthread_desc, (char *)&tid_pid, sizeof(tid_pid));
    }

    if (tmp == NULL) {
      PRINTF("WARNING: Couldn't find offsets of tid/pid in thread_area.\n"
             "  Now relying on the value determined using the\n"
             "  glibc version with which DMTCP was compiled.");
      return STATIC_TLS_TID_OFFSET();

      // mtcp_abort();
    }

    tid_offset = tmp - (char *)pthread_desc;
    if (tid_offset != STATIC_TLS_TID_OFFSET()) {
      PRINTF("WARNING: tid_offset (%d) different from expected.\n"
             "  It is possible that DMTCP was compiled with a different\n"
             "  glibc version than the one it's dynamically linking to.\n"
             "  Continuing anyway.  If this fails, please try again.",
             tid_offset);
    }
    DPRINTF("DEBUGGING INFO: tid_offset: %d\n", tid_offset);
    if (tid_offset % sizeof(int) != 0) {
      PRINTF("WARNING: tid_offset is not divisible by sizeof(int).\n"
             "  Now relying on the value determined using the\n"
             "  glibc version with which DMTCP was compiled.");
      return STATIC_TLS_TID_OFFSET();

      // mtcp_abort();
    }

    /* Should we do a double-check, and spawn a new thread and see
     *  if its TID matches at this tid_offset?  This would give greater
     *  confidence, but for the reasons above, it's probably not necessary.
     */
  }
  return tid_offset;
}

/*****************************************************************************
 *
 *****************************************************************************/
int
TLSInfo_GetPidOffset(void)
{
  static int pid_offset = -1;

  struct { pid_t tid; pid_t pid; } tid_pid;
  if (pid_offset == -1) {
    int tid_offset = TLSInfo_GetTidOffset();
    pid_offset = tid_offset + (char *)&(tid_pid.pid) - (char *)&tid_pid;
    DPRINTF("DEBUGGING_INFO: pid_offset: %d\n", pid_offset);
  }
  return pid_offset;
}

static char *
memsubarray(char *array, char *subarray, size_t len)
{
  char *i_ptr;
  size_t j;
  int word1 = *(int *)subarray;

  // Assume subarray length is at least sizeof(int) and < 2048.
  ASSERT(len >= sizeof(int));
  for (i_ptr = array; i_ptr < array + 2048; i_ptr++) {
    if (*(int *)i_ptr == word1) {
      for (j = 0; j < len; j++) {
        if (i_ptr[j] != subarray[j]) {
          break;
        }
      }
      if (j == len) {
        return i_ptr;
      }
    }
  }
  return NULL;
}

static uint64_t
get_tls_segreg(void)
{
  segreg_t tlssegreg;

#ifdef __i386__
  asm volatile ("movw %%gs,%0" : "=g" (tlssegreg)); /* any general register */
#elif __x86_64__

  /* q = a,b,c,d for i386; 8 low bits of r class reg for x86_64 */
  asm volatile ("movw %%fs,%0" : "=g" (tlssegreg));
#elif __arm__
  asm volatile ("mrc     p15, 0, %0, c13, c0, 3  @ load_tp_hard\n\t"
                : "=r" (tlssegreg));
#elif defined(__aarch64__)
  asm volatile ("mrs  %0, tpidr_el0" : "=r" (tlssegreg));
#endif /* ifdef __i386__ */
  return (uint64_t)tlssegreg;
}

/*
static int
tls_get_thread_area(void *uinfo, MYINFO_GS_T dummy)
{
  asm volatile ("mrs   %0, tpidr_el0" : "=r" (myinfo_gs) );
  myinfo_gs = myinfo_gs - 1776;
  *(unsigned long int *)&(((struct user_desc *)uinfo)->base_addr) = myinfo_gs;
   return myinfo_gs;
}

static int
tls_set_thread_area(void *uinfo, MYINFO_GS_T dummy)
{
  myinfo_gs = *(unsigned long int *)&(((struct user_desc *)uinfo)->base_addr);
  myinfo_gs = myinfo_gs + 1776;
  asm volatile ("msr     tpidr_el0, %[gs]" : : [gs] "r" (myinfo_gs) );
}
*/
static void *
get_tls_base_addr()
{
  struct user_desc gdtentrytls[2];

  gdtentrytls[0].entry_number = get_tls_segreg() / 8;

#if defined(__i386__) || defined(__x86_64__)
  if (tls_get_thread_area(&gdtentrytls[0], &gdtentrytls[1]) == -1)
#elif defined(__arm__) || defined(__aarch64__)
  // FIXME: ARM uses tls_get_thread_area with incompatible syntax,
  //        setting global variable myinfo_gs.  Fix this to work
  //        for per-thread storage (multiple threads).
  //        See commit 591a1631 (2.6.0), 7d02a2e0 (3.0):  PR #609
  if (tls_get_thread_area(&gdtentrytls[0], myinfo_gs) == -1)
#endif
  {
    PRINTF("Error getting GDT TLS entry: %d\n", errno);
    _exit(0);
  }
#ifdef WSL
  // This could be used for regular non-WSL also, although it depends on
  //   the GNU implementation.  pthread_self() returns thread area.
  // NOTE:  The syscall arch_prctl(ARCH_GET_FS,...) discovers the base
  //   address of the thread descriptor for non-WSL, but it returns
  //   different values when invoked at different call sites in WSL.
  //   Probably, this is because Windows is also using the $fs register.
  return (void *)pthread_self();
#endif
  return (void *)(*(unsigned long *)&(gdtentrytls[0].base_addr));
}

// Returns value for AT_SYSINFO in kernel's auxv
// Ideally:  mtcp_at_sysinfo() == *mtcp_addr_sysinfo()
// Best if we call this early, before the user makes problems
// by moving environment variables, putting in a weird stack, etc.
extern char **environ;
static void *
get_at_sysinfo()
{
  void **stack;
  int i;
  ELF_AUXV_T *auxv;
  static char **my_environ = NULL;

  if (my_environ == NULL) {
    my_environ = environ;
  }
#if 0

  // Walk the stack.
# if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(mov %%ebp, %0 \ n \ t)
                  : "=g" (stack));
# else /* if defined(__i386__) || defined(__x86_64__) */
#  error "current architecture not supported"
# endif /* if defined(__i386__) || defined(__x86_64__) */
  MTCP_PRINTF("stack 2: %p\n", stack);

  // When popping stack/%ebp yields zero, that's the ELF loader telling us that
  // this is "_start", the first call frame, which was created by ELF.
  for (; *stack != NULL; stack = *stack) {}

  // Go beyond first call frame:
  // Next look for &(argv[argc]) on stack;  (argv[argc] == NULL)
  for (i = 1; stack[i] != NULL; i++) {}

  // Do some error checks
  if (&(stack[i]) - stack > 100000) {
    MTCP_PRINTF("Error:  overshot stack\n");
    exit(1);
  }
  stack = &stack[i];
#else /* if 0 */
  stack = (void **)&my_environ[-1];

  if (*stack != NULL) {
    PRINTF(
      "Error: This should be argv[argc] == NULL and it's not. NO &argv[argc]");
    _exit(0);
  }
#endif /* if 0 */

  // stack[-1] should be argv[argc-1]
  if ((void **)stack[-1] < stack || (void **)stack[-1] > stack + 100000) {
    PRINTF("Error: candidate argv[argc-1] failed consistency check");
    _exit(0);
  }
  for (i = 1; stack[i] != NULL; i++) {
    if ((void **)stack[i] < stack || (void **)stack[i] > stack + 10000) {
      PRINTF("Error: candidate argv[i] failed consistency check");
      _exit(0);
    }
  }
  stack = &stack[i + 1];

  // Now stack is beginning of auxiliary vector (auxv)
  // auxv->a_type = AT_NULL marks the end of auxv
  for (auxv = (ELF_AUXV_T *)stack; auxv->a_type != AT_NULL; auxv++) {
    // mtcp_printf("0x%x 0x%x\n", auxv->a_type, auxv->a_un.a_val);
    if (auxv->a_type == (UINT_T)AT_SYSINFO) {
      // JNOTE("AT_SYSINFO") (&auxv->a_un.a_val) (auxv->a_un.a_val);
      return (void *)auxv->a_un.a_val;
    }
  }
  return NULL;  /* Couldn't find AT_SYSINFO */
}

// From glibc-2.7: glibc-2.7/nptl/sysdeps/i386/tls.h
// SYSINFO_OFFSET given by:
// #include "glibc-2.7/nptl/sysdeps/i386/tls.h"
// tcbhead_t dummy;
// #define SYSINFO_OFFSET &(dummy.sysinfo) - &dummy

// Some reports say it was 0x18 in past.  Should we also check that?
#define DEFAULT_SYSINFO_OFFSET "0x10"

int
TLSInfo_HaveThreadSysinfoOffset()
{
#ifdef RESET_THREAD_SYSINFO
  static int result = -1; // Reset to 0 or 1 on first call.
#else /* ifdef RESET_THREAD_SYSINFO */
  static int result = 0;
#endif /* ifdef RESET_THREAD_SYSINFO */
  if (result == -1) {
    void *sysinfo;
#if defined(__i386__) || defined(__x86_64__)
    asm volatile (CLEAN_FOR_64_BIT(mov %%
                                   gs:) DEFAULT_SYSINFO_OFFSET ", %0\n\t"
                  : "=r" (sysinfo));
#elif defined(__arm__)
    asm volatile ("mrc     p15, 0, %0, c13, c0, 3  @ load_tp_hard\n\t"
                  : "=r" (sysinfo));
#elif defined(__aarch64__)
    asm volatile ("mrs     %0, tpidr_el0" : "=r" (sysinfo));
#else /* if defined(__i386__) || defined(__x86_64__) */
# error "current architecture not supported"
#endif /* if defined(__i386__) || defined(__x86_64__) */
    result = (sysinfo == get_at_sysinfo());
  }
  return result;
}

// AT_SYSINFO is what kernel calls sysenter address in vdso segment.
// Kernel saves it for each thread in %gs:SYSINFO_OFFSET ??
// as part of kernel TCB (thread control block) at beginning of TLS ??
void *
TLSInfo_GetThreadSysinfo()
{
  void *sysinfo;

#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(mov %%gs:) DEFAULT_SYSINFO_OFFSET ", %0\n\t"
                : "=r" (sysinfo));
#elif defined(__arm__)
  asm volatile ("mrc     p15, 0, %0, c13, c0, 3  @ load_tp_hard\n\t"
                : "=r" (sysinfo));
#elif defined(__aarch64__)
  asm volatile ("mrs     %0, tpidr_el0" : "=r" (sysinfo));
#else /* if defined(__i386__) || defined(__x86_64__) */
# error "current architecture not supported"
#endif /* if defined(__i386__) || defined(__x86_64__) */
  return sysinfo;
}

void
TLSInfo_SetThreadSysinfo(void *sysinfo)
{
#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(mov %0, %%gs:) DEFAULT_SYSINFO_OFFSET "\n\t"
                : : "r" (sysinfo));
#elif defined(__arm__)
  mtcp_sys_kernel_set_tls(sysinfo);
#elif defined(__aarch64__)
  asm volatile ("msr     tpidr_el0, %[gs]" : :[gs] "r" (sysinfo));
#else /* if defined(__i386__) || defined(__x86_64__) */
# error "current architecture not supported"
#endif /* if defined(__i386__) || defined(__x86_64__) */
}

/*****************************************************************************
 *
 *
 *****************************************************************************/
void
TLSInfo_VerifyPidTid(pid_t pid, pid_t tid)
{
  pid_t tls_pid, tls_tid;
  char *addr = (char *)get_tls_base_addr();

  tls_pid = *(pid_t *)(addr + TLSInfo_GetPidOffset());
  tls_tid = *(pid_t *)(addr + TLSInfo_GetTidOffset());

  if (tls_tid != tid) {
    PRINTF("ERROR: tls tid(%d) doesn't match the thread tid (%d)\n",
           tls_tid, tid);
    _exit(0);
  }

  // For glibc > 2.24, pid field is unused. Here we do the <24 check to ensure
  // that distros with glibc 2.24-NNN are covered as well.
  if (glibcMajorVersion() == 2 && glibcMinorVersion() < 24 && tls_pid != pid) {
    PRINTF("ERROR: tls pid (%d) doesn't match getpid (%d)\n",
           tls_pid, (int)mtcp_sys_getpid());
    _exit(0);
  }
}

void
TLSInfo_UpdatePid()
{
  // For glibc > 2.24, pid field is unused.
  if (glibcMajorVersion() == 2 && glibcMinorVersion() <= 24) {
    pid_t *tls_pid =
      (pid_t *)((char *)get_tls_base_addr() + TLSInfo_GetPidOffset());
    *tls_pid = mtcp_sys_getpid();
  }
}

/*****************************************************************************
 *
 *  Save state necessary for TLS restore
 *  Linux saves stuff in the GDT, switching it on a per-thread basis
 *
 *****************************************************************************/
void
TLSInfo_SaveTLSState(ThreadTLSInfo *tlsInfo)
{
  int i;

#if defined(__i386__)
  asm volatile ("movw %%fs,%0" : "=m" (tlsInfo->fs));
  asm volatile ("movw %%gs,%0" : "=m" (tlsInfo->gs));
#elif defined(__x86_64__)
  //asm volatile ("movl %%fs,%0" : "=m" (tlsInfo->fs));
  //asm volatile ("movl %%gs,%0" : "=m" (tlsInfo->gs));
#elif defined(__arm__) || defined(__aarch64__)
  // Follow x86_64 for arm.
#endif /* if defined(__i386__) */

  memset(tlsInfo->gdtentrytls, 0, sizeof tlsInfo->gdtentrytls);

  /* FIXME:  IF %fs IS NOT READ into tlsInfo->fs AT BEGINNING OF THIS
   *   FUNCTION, HOW CAN WE REFER TO IT AS  tlsInfo->TLSSEGREG?
   *   WHAT IS THIS CODE DOING?
   */
  i = tlsInfo->TLSSEGREG / 8;
  tlsInfo->gdtentrytls[0].entry_number = i;
#if defined(__i386__) || defined(__x86_64__)
  if (tls_get_thread_area(&(tlsInfo->gdtentrytls[0]),
			  &(tlsInfo->gdtentrytls[1])) == -1)
#elif defined(__arm__) || defined(__aarch64__)
  // FIXME: ARM uses tls_get_thread_area with incompatible syntax,
  //        setting global variable myinfo_gs.  Fix this to work
  //        for per-thread storage (multiple threads).
  //        See commit 591a1631 (2.6.0), 7d02a2e0 (3.0):  PR #609
  if (tls_get_thread_area (&(tlsInfo->gdtentrytls[0]), myinfo_gs) == -1)
#endif
  {
    PRINTF("Error saving GDT TLS entry: %d\n", errno);
    _exit(0);
  }

  // PRINTF("TLSINFO base_addr: %p \n\n", tlsInfo->gdtentrytls[0].base_addr);
}

/*****************************************************************************
 *
 *  Restore the GDT entries that are part of a thread's state
 *
 *  The kernel provides set_thread_area system call for a thread to alter a
 *  particular range of GDT entries, and it switches those entries on a
 *  per-thread basis.  So from our perspective, this is per-thread state that is
 *  saved outside user addressable memory that must be manually saved.
 *
 *****************************************************************************/
void
TLSInfo_RestoreTLSState(ThreadTLSInfo *tlsInfo)
{
  /* Every architecture needs a register to point to the current
   * TLS (thread-local storage).  This is where we set it up.
   */

  /* Patch 'struct user_desc' (gdtentrytls) of glibc to contain the
   * the new pid and tid.
   */

  if (glibcMajorVersion() == 2 && glibcMinorVersion() <= 24) {
    *(pid_t *)(*(unsigned long *)&(tlsInfo->gdtentrytls[0].base_addr)
               + TLSInfo_GetPidOffset()) = mtcp_sys_getpid();
  }

  if (mtcp_sys_kernel_gettid() == mtcp_sys_getpid()) {
    *(pid_t *)(*(unsigned long *)&(tlsInfo->gdtentrytls[0].base_addr)
               + TLSInfo_GetTidOffset()) = mtcp_sys_getpid();
  }

  /* Now pass this to the kernel, so it can adjust the segment descriptors:
   *   i386, x86_64: tls_set_thread_areaa() uses arg1 for fs and arg2 for gs.
   * This will make different kernel calls according to the CPU architecture. */
#if defined(__i386__) || defined(__x86_64__)
  if (tls_set_thread_area(&(tlsInfo->gdtentrytls[0]),
                          &(tlsInfo->gdtentrytls[1])) != 0)
#elif defined(__arm__) || defined(__aarch64__)
  if (tls_set_thread_area (&(tlsInfo->gdtentrytls[0]), myinfo_gs) != 0)
#endif
  {
    PRINTF("Error restoring GDT TLS entry: %d\n", errno);
    mtcp_abort();
  }

  /* Finally, if this is i386, we need to set %gs to refer to the segment
   * descriptor that we're using above.  We restore the original pointer.
   * For the other architectures (not i386), the kernel call above
   * already did the equivalent work of setting up thread registers.
   */
#ifdef __i386__
  asm volatile ("movw %0,%%fs" : : "m" (tlsInfo->fs));
  asm volatile ("movw %0,%%gs" : : "m" (tlsInfo->gs));
#elif __x86_64__

  /* Don't directly set fs.  It would only set 32 bits, and we just
   *  set the full 64-bit base of fs, using sys_set_thread_area,
   *  which called arch_prctl.
   *asm volatile ("movl %0,%%fs" : : "m" (tlsInfo->fs));
   *asm volatile ("movl %0,%%gs" : : "m" (tlsInfo->gs));
   */
#elif __arm__

  /* ARM treats this same as x86_64 above. */
#elif defined(__aarch64__)
# warning "TODO: Implementation for ARM64?"
#endif /* ifdef __i386__ */
}

#if 0

/*****************************************************************************
 *
 *  The original program's memory and files have been restored
 *
 *****************************************************************************/
void
TLSInfo_PostRestart()
{
  /* Now we can access all of the memory from the time of the checkpoint.
   * We will continue to use the temporary stack from mtcp_restart.c, for now.
   * When we return from the signal handler, this primary thread will
   *   return to its original stack.
   */

  /* However, we can only make direct kernel calls, and not libc calls.
   * TLSInfo_RestoreTLSState will do the following:
   * Step 1:  Patch 'struct user_desc' (gdtentrytls) of glibc to have
   *     the new pid and tid.
   * Step 2:  Make kernel call to set up the corresponding
   *     segment descriptor and/or set any TLS per-thread register.
   * Step 3 (for Intel only):  Restore %fs and %gs to refer to the the
   *     segment descriptor for this primary thread (only %gs needed for i386).
   */

  // TLSInfo_RestoreTLSState(motherofall_tlsInfo);

# if 0

  /* FOR DEBUGGING:  Now test if we can make a kernel call through libc. */
  mtcp_sys_write(1, "shell ../../util/gdb-add-libdmtcp-symbol-file.py PID PC\n",
                 sizeof(
                   "shell ../../util/gdb-add-libdmtcp-symbol-file.py PID PC\n"));
#  if defined(__i386__) || defined(__x86_64__)
  asm volatile ("int3");     // Do breakpoint; send SIGTRAP, caught by gdb
#  elif defined(__aarch64__)
  asm ("brk 0");
#  else /* if defined(__i386__) || defined(__x86_64__) */
  DPRINTF("IN GDB: interrupt (^C); add-symbol-file ...; (gdb) print x=0\n");
  { int x = 1; while (x) {}
  }                             // Stop execution for user to type command.
#  endif /* if defined(__i386__) || defined(__x86_64__) */
  int rc = nice(0);
# endif /* if 0 */

  // We have now verified that libc functionality is restored.
  Thread_RestoreAllThreads();
}
#endif /* if 0 */
