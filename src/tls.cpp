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
#include <elf.h>
#include <errno.h>
#include <linux/version.h>
#include <stdlib.h>
#include <string.h>
#include <sys/personality.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif
#include <unistd.h>
#include <sys/types.h>

#include "config.h"  // define WSL if present
#include "mtcp/mtcp_sys.h"
#include "syscallwrappers.h"
#include "util.h"
#include "dmtcp_assert.h"

#if defined(__x86_64__) || defined(__aarch64__)
# define ELF_AUXV_T Elf64_auxv_t
# define UINT_T     uint64_t
#else /* if defined(__x86_64__) || defined(__aarch64__) */

// else __i386__ and __arm__
# define ELF_AUXV_T Elf32_auxv_t
# define UINT_T     uint32_t
#endif /* if defined(__x86_64__) || defined(__aarch64__) */

const char *tlsErrorMsg = "*** DMTCP: Error restoring TLS information\n.";

#if !__GLIBC_PREREQ(2, 1)
# error "glibc version too old"
#endif /* if !__GLIBC_PREREQ(2, 1) */

/*****************************************************************************
 *
 *****************************************************************************/
#ifdef __x86_64__
# include <asm/prctl.h>
# include <sys/prctl.h>
void
TLSInfo_GetThreadArea(ThreadTLSInfo *tlsInfo, pid_t tid)
{
  ASSERT_NE(-1,
    _real_syscall(SYS_arch_prctl, ARCH_GET_FS,
                  (long)&tlsInfo->fs, 0, 0, 0, 0, 0),
    "failed to read FS TLS register: tid={}", tid);
  ASSERT_NE(-1,
    _real_syscall(SYS_arch_prctl, ARCH_GET_GS,
                  (long)&tlsInfo->gs, 0, 0, 0, 0, 0),
    "failed to read GS TLS register: tid={}", tid);
}

void
TLSInfo_SetThreadArea(ThreadTLSInfo *tlsInfo)
{
  int mtcp_sys_errno __attribute__((unused));

  if (mtcp_inline_syscall(arch_prctl, 2, ARCH_SET_FS, tlsInfo->fs)
      != 0) {
    printf("\n*** DMTCP: Error restoring TLS.\n\n");
    abort();
  };

  if (mtcp_inline_syscall(arch_prctl, 2, ARCH_SET_GS, tlsInfo->gs)
      != 0) {
    printf("\n*** DMTCP: Error restoring TLS.\n\n");
    abort();
  }
}
#endif

#ifdef __i386__
void
TLSInfo_GetThreadArea(ThreadTLSInfo *tlsInfo, pid_t tid)
{
  asm volatile ("movw %%fs,%0" : "=m" (tlsInfo->fs));
  asm volatile ("movw %%gs,%0" : "=m" (tlsInfo->gs));

  memset(&tlsInfo->gdtentrytls, 0, sizeof tlsInfo->gdtentrytls);

  tlsInfo->gdtentrytls.entry_number = tlsInfo->gs / 8;

  ASSERT_NE(-1,
    _real_syscall(SYS_get_thread_area,
                  (long)&tlsInfo->gdtentrytls,
                  0, 0, 0, 0, 0, 0),
    "failed to read i386 TLS GDT entry: tid={} entry={}",
    tid, tlsInfo->gdtentrytls.entry_number);
}

void
TLSInfo_SetThreadArea(ThreadTLSInfo *tlsInfo)
{
  int mtcp_sys_errno __attribute__((unused));

  if (mtcp_inline_syscall(set_thread_area, 1, &tlsInfo->gdtentrytls)
        != 0) {
    printf("\n*** DMTCP: Error restoring TLS.\n\n");
    abort();
  };

  /* Finally, if this is i386, we need to set %gs to refer to the segment
   * descriptor that we're using above.  We restore the original pointer.
   * For the other architectures (not i386), the kernel call above
   * already did the equivalent work of setting up thread registers.
   */
  asm volatile ("movw %0,%%fs" : : "m" (tlsInfo->fs));
  asm volatile ("movw %0,%%gs" : : "m" (tlsInfo->gs));
}
#endif  // ifdef __i386__

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

void
TLSInfo_GetThreadArea(ThreadTLSInfo *tlsInfo, pid_t tid)
{
  unsigned long int addr;
  asm volatile ("mrc     p15, 0, %0, c13, c0, 3  @ load_tp_hard\n\t"
                : "=r" (addr));

  (void)tid;
  tlsInfo->tlsAddr = addr - 1216; /* sizeof(struct pthread) = 1216 */  \
}

void
TLSInfo_SetThreadArea(ThreadTLSInfo *tlsInfo)
{
  int mtcp_sys_errno __attribute__((unused));
  if (mtcp_syscall(__ARM_NR_set_thread_area,
                   &mtcp_sys_errno,
                   tlsInfo->tlsAddr) != 0) {
    printf("\n*** DMTCP: Error restoring TLS.\n\n");
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

void
TLSInfo_GetThreadArea(ThreadTLSInfo *tlsInfo, pid_t tid)
{
  unsigned long int addr;
  asm volatile ("mrs   %0, tpidr_el0" : "=r" (addr));
  (void)tid;
  tlsInfo->tlsAddr = addr - 1776;  // sizeof(struct pthread) = 1776
}

void
TLSInfo_SetThreadArea(ThreadTLSInfo *tlsInfo)
{
  unsigned long int addr = tlsInfo->tlsAddr + 1776;
  asm volatile ("msr     tpidr_el0, %[gs]" : :[gs] "r" (addr));
}
#endif /* end __aarch64__ */
// FIXME: The branch feature/riscv-experimental has a commit with commit message
//              Add libc_tcbhead_t padding for pthread_t for riscv
// That commit has padding for all CPUs.  We should replace this with
// the new commit when it's pushed in.

#ifdef __riscv
/* This allocation hack will work only if calls to mtcp_sys_get_thread_area
 * and mtcp_sys_get_thread_area are both inside the same file (mtcp.c).
 * This is all because get_thread_area is not implemented for aarch64.
 * 	For RISCV, the thread pointer seems to point to the next slot
 * after the 'struct pthread'.  Why??  So, we subtract that address.
 * After that, tid/pid will be located at offset 208/212 as expected
 * for glibc-2.17.
 * NOTE:  'struct pthread' defined in glibc/nptl/descr.h
 * 	The value below (1776) is current for glibc-2.17.
 * 	See PORTING file for easy way to compute these numbers.
 * 	May have to update 'sizeof(struct pthread)' for new versions of glibc.
 * 	We can automate this by searching for negative offset from end
 * 	of 'struct pthread' in tls_tid_offset, tls_pid_offset in mtcp.c.
 */

void
TLSInfo_GetThreadArea(ThreadTLSInfo *tlsInfo, pid_t tid)
{
  unsigned long int addr;
  asm volatile ("addi %0, tp, 0" : "=r" (addr));
  (void)tid;
  tlsInfo->tlsAddr = addr;
}

void
TLSInfo_SetThreadArea(ThreadTLSInfo *tlsInfo)
{
  unsigned long int addr = tlsInfo->tlsAddr;
  asm volatile("addi tp, %[gs], 0" : : [gs] "r" (addr));
}
#endif  /* end __riscv */

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

  ASSERT_NULL(*stack,
                  "expected argv[argc] to be null while scanning auxv");

  // stack[-1] should be argv[argc-1]
  ASSERT((void **)stack[-1] >= stack && (void **)stack[-1] <= stack + 100000,
         "candidate argv[argc-1] failed consistency check: stack={} "
         "candidate={}",
         stack, stack[-1]);

  for (i = 1; stack[i] != NULL; i++) {
    ASSERT((void **)stack[i] >= stack && (void **)stack[i] <= stack + 10000,
           "candidate argv entry failed consistency check: index={} stack={} "
           "candidate={}",
           i, stack, stack[i]);
  }
  stack = &stack[i + 1];

  // Now stack is beginning of auxiliary vector (auxv)
  // auxv->a_type = AT_NULL marks the end of auxv
  for (auxv = (ELF_AUXV_T *)stack; auxv->a_type != AT_NULL; auxv++) {
    // mtcp_printf("0x%x 0x%x\n", auxv->a_type, auxv->a_un.a_val);
    if (auxv->a_type == (UINT_T)AT_SYSINFO) {
      // NOTE("AT_SYSINFO: ptr={} value={}",
      //      &auxv->a_un.a_val, auxv->a_un.a_val);
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
  static int result = -1;  // Reset to 0 or 1 on first call.
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
#elif defined(__riscv)
    asm volatile("addi %0, tp, 0" : "=r" (sysinfo));
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
#elif defined(__riscv)
  asm volatile ("addi %0, tp, 0" : "=r" (sysinfo));
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
#elif defined(__riscv)
  asm volatile("addi tp, %[gs], 0" :: [gs] "r" (sysinfo));
#else /* if defined(__i386__) || defined(__x86_64__) */
# error "current architecture not supported"
#endif /* if defined(__i386__) || defined(__x86_64__) */
}

void
TLSInfo_RestoreTLSTidPid(Thread *thread)
{
  int mtcp_sys_errno __attribute__((unused));

  dmtcp::Util::Version glibc = dmtcp::Util::glibcVersion();
  if (glibc.major == 2 && glibc.minor <= 24) {
    ASSERT_NOT_NULL(thread->pthreadShim.pidAddr());
    *thread->pthreadShim.pidAddr() = getpid();
  }

  ASSERT_NOT_NULL(thread->pthreadShim.tidAddr());
  thread->pthreadShim.setTid(thread->tid);
}
