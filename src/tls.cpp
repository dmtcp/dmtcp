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

#include "tls.h"
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
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/types.h>

#include "config.h" // define WSL if present
#include "jassert.h"
#include "mtcp/mtcp_sys.h"

#if defined(__x86_64__) || defined(__aarch64__)
# define ELF_AUXV_T Elf64_auxv_t
# define UINT_T     uint64_t
#else /* if defined(__x86_64__) || defined(__aarch64__) */

// else __i386__ and __arm__
# define ELF_AUXV_T Elf32_auxv_t
# define UINT_T     uint32_t
#endif /* if defined(__x86_64__) || defined(__aarch64__) */

const char *tlsErrorMsg = "*** DMTCP: Error restoring TLS information\n.";

static int glibcMajorVersion()
{
  static int major = 0;
  if (major == 0) {
    major = (int) strtol(gnu_get_libc_version(), NULL, 10);
    JASSERT(major == 2);
  }
  return major;
}

static int glibcMinorVersion()
{
  static long minor = 0;
  if (minor == 0) {
    char *ptr;
    int major = (int) strtol(gnu_get_libc_version(), &ptr, 10);
    JASSERT(major == 2);
    minor = (int) strtol(ptr+1, NULL, 10);
  }
  return minor;
}

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
int
TLSInfo_GetTidOffset(void)
{
  static int offset = -1;

  if (offset != -1) {
    return offset;
  }

  // tcbhead_t, etc., were introduced in glibc 2.4. We don't support earlier
  // versions.
  JASSERT(glibcMajorVersion() == 2) (glibcMajorVersion());
  JASSERT(glibcMinorVersion() >= 4) (glibcMinorVersion());

#ifdef __x86_64__
  if (glibcMinorVersion() >= 11) {
    offset = 720; // sizeof(tcbhead_t) + sizeof(list_t)
    return offset;
  }
#endif

  if (glibcMinorVersion() >= 10) {
    offset = 26 * sizeof(void *); // sizeof(__padding) + sizeof(list_t)
  } else {
    offset = 18 * sizeof(void *); // sizeof(__padding) + sizeof(list_t)
  }

  return offset;
}

/*****************************************************************************
 *
 *****************************************************************************/
int
TLSInfo_GetPidOffset(void)
{
  static int pid_offset = TLSInfo_GetTidOffset() + sizeof(pid_t);

  return pid_offset;
}

/*****************************************************************************
 *
 *****************************************************************************/
#ifdef __x86_64__
# include <asm/prctl.h>
# include <sys/prctl.h>
static void
tls_get_thread_area(Thread *thread)
{
  JASSERT(_real_syscall(SYS_arch_prctl, ARCH_GET_FS, &thread->tlsInfo.fs) == 0)
    (JASSERT_ERRNO);
  JASSERT(_real_syscall(SYS_arch_prctl, ARCH_GET_GS, &thread->tlsInfo.gs) == 0)
    (JASSERT_ERRNO);
}

void
tls_set_thread_area(Thread *thread)
{
  int mtcp_sys_errno __attribute__((unused));

  if (mtcp_inline_syscall(arch_prctl, 2, ARCH_SET_FS, thread->tlsInfo.fs)
      != 0) {
    printf("\n*** DMTCP: Error restorig TLS.\n\n");
    abort();
  };

  if (mtcp_inline_syscall(arch_prctl, 2, ARCH_SET_GS, thread->tlsInfo.gs)
      != 0) {
    printf("\n*** DMTCP: Error restorig TLS.\n\n");
    abort();
  }
}
#endif

#ifdef __i386__
static void
tls_get_thread_area(Thread *thread)
{
  asm volatile ("movw %%fs,%0" : "=m" (thread->tlsInfo.fs));
  asm volatile ("movw %%gs,%0" : "=m" (thread->tlsInfo.gs));

  memset(&thread->tlsInfo.gdtentrytls, 0, sizeof thread->tlsInfo.gdtentrytls);

  thread->tlsInfo.gdtentrytls.entry_number = thread->tlsInfo.gs / 8;

  JASSERT(_real_syscall(SYS_get_thread_area,
                        &thread->tlsInfo.gdtentrytls) == 0)
    (JASSERT_ERRNO);
}

static void
tls_set_thread_area(Thread *thread)
{
  int mtcp_sys_errno __attribute__((unused));

  if (mtcp_inline_syscall(set_thread_area, 1, &thread->tlsInfo.gdtentrytls)
        != 0) {
    printf("\n*** DMTCP: Error restorig TLS.\n\n");
    abort();
  };

  /* Finally, if this is i386, we need to set %gs to refer to the segment
   * descriptor that we're using above.  We restore the original pointer.
   * For the other architectures (not i386), the kernel call above
   * already did the equivalent work of setting up thread registers.
   */
  asm volatile ("movw %0,%%fs" : : "m" (thread->tlsInfo.fs));
  asm volatile ("movw %0,%%gs" : : "m" (thread->tlsInfo.gs));
}
#endif // ifdef __i386__

#ifdef __arm__

/* This allocation hack will work only if calls to mtcp_sys_get_thread_area
 * and mtcp_sys_get_thread_area are both inside the same file (mtcp.c).
 * This is all because get_thread_area is not implemented for arm.
 *     For ARM, the thread pointer seems to point to the next slot
 * after the 'struct pthread'.  Why??  So, we subtract that address.
 * After that, tid/pid will be located at  offset 104/108 as expected
 * for glibc-2.13.
 * NOTE:  'struct pthread' defined in glibc/nptl/descr.h
 *     The value below (1216) is current for glibc-2.13.
 *     May have to update 'sizeof(struct pthread)' for new versions of glibc.
 *     We can automate this by searching for negative offset from end
 *     of 'struct pthread' in tls_tid_offset, tls_pid_offset in mtcp.c.
 */

static void
tls_get_thread_area(Thread *thread)
{
  unsigned long int addr;
  asm volatile ("mrc     p15, 0, %0, c13, c0, 3  @ load_tp_hard\n\t"
                : "=r" (addr));

  thread->tlsInfo.tlsAddr = addr - 1216; /* sizeof(struct pthread) = 1216 */  \
}

static void
tls_set_thread_area(Thread *thread)
{
  int mtcp_sys_errno __attribute__((unused));
  if (mtcp_syscall(__ARM_NR_set_thread_area,
                   &mtcp_sys_errno,
                   thread->tlsInfo.tlsAddr) != 0) {
    printf("\n*** DMTCP: Error restorig TLS.\n\n");
    abort();
  };
}
#endif

#ifdef __aarch64__
/* This allocation hack will work only if calls to mtcp_sys_get_thread_area
 * and mtcp_sys_get_thread_area are both inside the same file (mtcp.c).
 * This is all because get_thread_area is not implemented for aarch64.
 *     For ARM, the thread pointer seems to point to the next slot
 * after the 'struct pthread'.  Why??  So, we subtract that address.
 * After that, tid/pid will be located at offset 208/212 as expected
 * for glibc-2.17.
 * NOTE:  'struct pthread' defined in glibc/nptl/descr.h
 *     The value below (1776) is current for glibc-2.17.
 #     See PORTING file for easy way to compute these numbers.
 *     May have to update 'sizeof(struct pthread)' for new versions of glibc.
 *     We can automate this by searching for negative offset from end
 *     of 'struct pthread' in tls_tid_offset, tls_pid_offset in mtcp.c.
 */

static void
tls_get_thread_area(Thread *thread)
{
  unsigned long int addr;
  asm volatile ("mrs   %0, tpidr_el0" : "=r" (addr));
  thread->tlsInfo.tlsAddr = addr - 1776; // sizeof(struct pthread) = 1776
}

static void
tls_set_thread_area(Thread *thread)
{
  unsigned long int addr = thread->tlsInfo.tlsAddr + 1776;
  asm volatile ("msr     tpidr_el0, %[gs]" : :[gs] "r" (addr));
}
#endif /* end __aarch64__ */


/*****************************************************************************
 *
 *****************************************************************************/
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

  stack = (void **)&my_environ[-1];

  JASSERT (*stack == NULL) (*stack)
    .Text("This should be argv[argc] == NULL and it's not. NO &argv[argc]");

  // stack[-1] should be argv[argc-1]
  JASSERT((void **)stack[-1] >= stack && (void **)stack[-1] >= stack + 100000)
    .Text("Error: candidate argv[argc-1] failed consistency check");

  for (i = 1; stack[i] != NULL; i++) {
    JASSERT ((void **)stack[i] >= stack && (void **)stack[i] <= stack + 10000)
      .Text("Error: candidate argv[i] failed consistency check");
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
  char *addr = (char *) pthread_self();

  tls_pid = *(pid_t *)(addr + TLSInfo_GetPidOffset());
  tls_tid = *(pid_t *)(addr + TLSInfo_GetTidOffset());

  JASSERT (tls_tid == tid) (tls_tid) (tid)
    .Text("tls tid doesn't match the thread tid");

  // For glibc > 2.24, pid field is unused. Here we do the <24 check to ensure
  // that distros with glibc 2.24-NNN are covered as well.
  JASSERT((glibcMajorVersion() == 2 && glibcMinorVersion() >= 24)
          || tls_pid == pid)
    (tls_pid) (pid) .Text("tls pid doesn't match getpid");
}

/*****************************************************************************
 *
 *  Save state necessary for TLS restore
 *  Linux saves stuff in the GDT, switching it on a per-thread basis
 *
 *****************************************************************************/
void
TLSInfo_SaveTLSState(Thread *thread)
{
  thread->pthreadSelf = (void*) pthread_self();
  tls_get_thread_area(thread);
  return;
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
TLSInfo_RestoreTLSState(Thread *thread)
{
  /* Every architecture needs a register to point to the current
   * TLS (thread-local storage).  This is where we set it up.
   */
  tls_set_thread_area(thread);
}

void
TLSInfo_RestoreTLSTidPid(Thread *thread)
{
  int mtcp_sys_errno __attribute__((unused));

  if (glibcMajorVersion() == 2 && glibcMinorVersion() <= 24) {
    *(pid_t *)((char*) thread->pthreadSelf + TLSInfo_GetPidOffset()) =
      getpid();
  }

  *(pid_t *)((char*) thread->pthreadSelf + TLSInfo_GetTidOffset()) =
     gettid();
}
