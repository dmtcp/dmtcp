/*****************************************************************************
 * Copyright (C) 2014 Kapil Arya <kapil@ccs.neu.edu>                         *
 * Copyright (C) 2014 Gene Cooperman <gene@ccs.neu.edu>                      *
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

/* Algorithm:
 *    When DMTCP originally launched, it mmap'ed a region of memory sufficient
 *  to hold the mtcp_restart executable.  This mtcp_restart was compiled
 *  as position-independent code (-fPIC).  So, we can copy the code into
 *  the memory (holebase) that was reserved for us during DMTCP launch.
 *    When we move to high memory, we will also use a temporary stack
 *  within the reserved memory (holebase).  Changing from the original
 *  mtcp_restart stack to a new stack must be done with care.  All information
 *  to be passed will be stored in the variable rinfo, a global struct.
 *  When we enter the copy of restorememoryareas() in the reserved memory,
 *  we will copy the data of rinfo from the global rinfo data to our
 *  new call frame.
 *    It is then safe to munmap the old text, data, and stack segments.
 *  Once we have done the munmap, we have almost finished bootstrapping
 *  ourselves.  We only need to copy the memory sections from the checkpoint
 *  image to the original addresses in memory.  Finally, we will then jump
 *  back using the old program counter of the checkpoint thread.
 *    Now, the copy of mtcp_restart is "dead code", and we will not use
 *  this memory region again until we restart from the next checkpoint.
 */

#define _GNU_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stddef.h>

#include "../membarrier.h"
#include "config.h"
#include "mtcp_check_vdso.ic"
#include "mtcp_header.h"
#include "mtcp_sys.h"
#include "mtcp_util.ic"
#include "procmapsarea.h"
#include "tlsutil.h"

/* The use of NO_OPTIMIZE is deprecated and will be removed, since we
 * compile mtcp_restart.c with the -O0 flag already.
 */
#ifdef __clang__
# define NO_OPTIMIZE __attribute__((optnone)) /* Supported only in late 2014 */
#else /* ifdef __clang__ */
# define NO_OPTIMIZE __attribute__((optimize(0)))
#endif /* ifdef __clang__ */

void mtcp_check_vdso(char **environ);
#ifdef FAST_RST_VIA_MMAP
static void mmapfile(int fd, void *buf, size_t size, int prot, int flags);
#endif

#define BINARY_NAME     "mtcp_restart"
#define BINARY_NAME_M32 "mtcp_restart-32"

/* struct RestoreInfo to pass all parameters from one function to next.
 * This must be global (not on stack) at the time that we jump from
 * original stack to copy of restorememoryareas() on new stack.
 * This is because we will wait until we are in the new call frame and then
 * copy the global data into the new call frame.
 */
typedef void (*fnptr_t)();
#define STACKSIZE 4 * 1024 * 1024

// static long long tempstack[STACKSIZE];
typedef struct RestoreInfo {
  int fd;
  int stderr_fd;  /* FIXME:  This is never used. */

  // int mtcp_sys_errno;

  VA saved_brk;
  VA restore_addr;
  VA restore_end;
  size_t restore_size;
  VA vdsoStart;
  VA vdsoEnd;
  VA vvarStart;
  VA vvarEnd;
  VA endOfStack;
  fnptr_t post_restart;
  fnptr_t post_restart_debug;
  // NOTE: Update the offset when adding fields to the RestoreInfo struct
  // See note below in the restart_fast_path() function.
  fnptr_t restorememoryareas_fptr;

  VA old_stack_addr;
  size_t old_stack_size;
  VA new_stack_addr;
  size_t stack_offset;

  // void (*post_restart)();
  // void (*post_restart_debug)();
  // void (*restorememoryareas_fptr)();
  int use_gdb;
  VA mtcp_restart_text_addr;
  ThreadTLSInfo motherofall_tls_info;
  int tls_pid_offset;
  int tls_tid_offset;
#ifdef TIMING
  struct timeval startValue;
#endif
  MYINFO_GS_T myinfo_gs;
  int mtcp_restart_pause;  // Used by env. var. DMTCP_RESTART_PAUSE
} RestoreInfo;
static RestoreInfo rinfo;

/* Internal routines */
static void readmemoryareas(int fd, VA endOfStack);
static int read_one_memory_area(int fd, VA endOfStack);
#if 0
static void adjust_for_smaller_file_size(Area *area, int fd);
#endif /* if 0 */
static void restorememoryareas(RestoreInfo *rinfo_ptr);
static void restore_brk(VA saved_brk, VA restore_begin, VA restore_end);
static void restart_fast_path(void);
static void restart_slow_path(void);
static int doAreasOverlap(VA addr1, size_t size1, VA addr2, size_t size2);
static int hasOverlappingMapping(VA addr, size_t size);
static void remapMtcpRestartToReservedArea(RestoreInfo *rinfo);
static void mtcp_simulateread(int fd, MtcpHeader *mtcpHdr);
void restore_libc(ThreadTLSInfo *tlsInfo,
                  int tls_pid_offset,
                  int tls_tid_offset,
                  MYINFO_GS_T myinfo_gs);
static void unmap_memory_areas_and_restore_vdso(RestoreInfo *rinfo);


#define MB                 1024 * 1024
#define RESTORE_STACK_SIZE 16 * MB
#define RESTORE_MEM_SIZE   16 * MB
#define RESTORE_TOTAL_SIZE (RESTORE_STACK_SIZE + RESTORE_MEM_SIZE)

// const char service_interp[] __attribute__((section(".interp"))) =
// "/lib64/ld-linux-x86-64.so.2";


int
__libc_start_main(int (*main)(int,
                              char **,
                              char **), int argc, char **argv, void (*init)(
                    void), void (*fini)(
                    void), void (*rtld_fini)(void), void *stack_end)
{
  int mtcp_sys_errno;
  char **envp = argv + argc + 1;
  int result = main(argc, argv, envp);

  mtcp_sys_exit(result);
  (void)mtcp_sys_errno; /* Stop compiler warning about unused variable */
  while (1) {}
}

void
__libc_csu_init(int argc, char **argv, char **envp) {}

void
__libc_csu_fini(void) {}

void __stack_chk_fail(void);   /* defined at end of file */
void
abort(void) { mtcp_abort(); }

/* Implement memcpy() and memset() inside mtcp_restart. Although we are not
 * calling memset, the compiler may generate a call to memset() when trying to
 * initialize a large array etc.
 */
void *
memset(void *s, int c, size_t n)
{
  return mtcp_memset(s, c, n);
}

void *
memcpy(void *dest, const void *src, size_t n)
{
  return mtcp_memcpy(dest, src, n);
}

#define shift argv++; argc--;
NO_OPTIMIZE
int
main(int argc, char *argv[], char **environ)
{
  char *ckptImage = NULL;
  MtcpHeader mtcpHdr;
  int mtcp_sys_errno;
  int simulate = 0;

  if (argc == 1) {
    MTCP_PRINTF("***ERROR: This program should not be used directly.\n");
    mtcp_sys_exit(1);
  }

#if 0
  MTCP_PRINTF("Attach for debugging.");
  { int x = 1; while (x) {}
  }
#endif /* if 0 */

  // TODO(karya0): Remove vDSO checks after 2.4.0-rc3 release, and after
  // testing.
  // Without mtcp_check_vdso, CentOS 7 fails on dmtcp3, dmtcp5, others.
#define ENABLE_VDSO_CHECK

  // TODO(karya0): Remove this block and the corresponding file after sufficient
  // testing:  including testing for __i386__, __arm__ and __aarch64__
#ifdef ENABLE_VDSO_CHECK

  /* i386 uses random addresses for vdso.  Make sure that its location
   * will not conflict with other memory regions.
   * (Other arch's may also need this in the future.  So, we do it for all.)
   * Note that we may need to keep the old and the new vdso.  We may
   * have checkpointed inside gettimeofday inside the old vdso, and the
   * kernel, on restart, knows only the new vdso.
   */
  mtcp_check_vdso(environ);
#endif /* ifdef ENABLE_VDSO_CHECK */

  rinfo.fd = -1;
  rinfo.mtcp_restart_pause = 0; /* false */
  rinfo.use_gdb = 0;
  shift;
  while (argc > 0) {
    if (mtcp_strcmp(argv[0], "--use-gdb") == 0) {
      rinfo.use_gdb = 1;
      shift;
      // Flags for call by dmtcp_restart follow here:
    } else if (mtcp_strcmp(argv[0], "--fd") == 0) {
      rinfo.fd = mtcp_strtol(argv[1]);
      shift; shift;
    } else if (mtcp_strcmp(argv[0], "--stderr-fd") == 0) {
      rinfo.stderr_fd = mtcp_strtol(argv[1]);
      shift; shift;
    } else if (mtcp_strcmp(argv[0], "--mtcp-restart-pause") == 0) {
      rinfo.mtcp_restart_pause = argv[1][0] - '0'; /* true */
      shift; shift;
    } else if (mtcp_strcmp(argv[0], "--simulate") == 0) {
      simulate = 1;
      shift;
    } else if (argc == 1) {
      // We would use MTCP_PRINTF, but it's also for output of util/readdmtcp.sh
      mtcp_printf("Considering '%s' as a ckpt image.\n", argv[0]);
      ckptImage = argv[0];
      break;
    } else {
      MTCP_PRINTF("MTCP Internal Error\n");
      return -1;
    }
  }

  if ((rinfo.fd != -1) ^ (ckptImage == NULL)) {
    MTCP_PRINTF("***MTCP Internal Error\n");
    mtcp_abort();
  }

#ifdef TIMING
  mtcp_sys_gettimeofday(&rinfo.startValue, NULL);
#endif
  if (rinfo.fd != -1) {
    mtcp_readfile(rinfo.fd, &mtcpHdr, sizeof mtcpHdr);
  } else {
    int rc = -1;
    rinfo.fd = mtcp_sys_open2(ckptImage, O_RDONLY);
    if (rinfo.fd == -1) {
      MTCP_PRINTF("***ERROR opening ckpt image (%s); errno: %d\n",
                  ckptImage, mtcp_sys_errno);
      mtcp_abort();
    }

    // This assumes that the MTCP header signature is unique.
    // We repeatedly look for mtcpHdr because the first header will be
    // for DMTCP.  So, we look deeper for the MTCP header.  The MTCP
    // header is guaranteed to start on an offset that's an integer
    // multiple of sizeof(mtcpHdr), which is currently 4096 bytes.
    do {
      rc = mtcp_readfile(rinfo.fd, &mtcpHdr, sizeof mtcpHdr);
    } while (rc > 0 && mtcp_strcmp(mtcpHdr.signature, MTCP_SIGNATURE) != 0);
    if (rc == 0) { /* if end of file */
      MTCP_PRINTF("***ERROR: ckpt image doesn't match MTCP_SIGNATURE\n");
      return 1;  /* exit with error code 1 */
    }
  }

  if (simulate) {
    mtcp_simulateread(rinfo.fd, &mtcpHdr);
    return 0;
  }

  rinfo.saved_brk = mtcpHdr.saved_brk;
  rinfo.restore_addr = mtcpHdr.restore_addr;
  rinfo.restore_end = mtcpHdr.restore_addr + mtcpHdr.restore_size;
  rinfo.restore_size = mtcpHdr.restore_size;
  rinfo.vdsoStart = mtcpHdr.vdsoStart;
  rinfo.vdsoEnd = mtcpHdr.vdsoEnd;
  rinfo.vvarStart = mtcpHdr.vvarStart;
  rinfo.vvarEnd = mtcpHdr.vvarEnd;
  rinfo.endOfStack = mtcpHdr.end_of_stack;
  rinfo.post_restart = mtcpHdr.post_restart;
  rinfo.post_restart_debug = mtcpHdr.post_restart_debug;
  rinfo.motherofall_tls_info = mtcpHdr.motherofall_tls_info;
  rinfo.tls_pid_offset = mtcpHdr.tls_pid_offset;
  rinfo.tls_tid_offset = mtcpHdr.tls_tid_offset;
  rinfo.myinfo_gs = mtcpHdr.myinfo_gs;

  restore_brk(rinfo.saved_brk, rinfo.restore_addr,
              rinfo.restore_addr + rinfo.restore_size);

  if (hasOverlappingMapping(rinfo.restore_addr, rinfo.restore_size)) {
    MTCP_PRINTF("*** Not Implemented.\n\n");
    mtcp_abort();
    restart_slow_path();
  } else {
    restart_fast_path();
  }
  return 0;  /* Will not reach here, but need to satisfy the compiler */
}

NO_OPTIMIZE
static void
restore_brk(VA saved_brk, VA restore_begin, VA restore_end)
{
  int mtcp_sys_errno;
  VA current_brk;
  VA new_brk;

  /* The kernel (2.6.9 anyway) has a variable mm->brk that we should restore.
   * The only access we have is brk() which basically sets mm->brk to the new
   * value, but also has a nasty side-effect (as far as we're concerned) of
   * mmapping an anonymous section between the old value of mm->brk and the
   * value being passed to brk().  It will munmap the bracketed memory if the
   * value being passed is lower than the old value.  But if zero, it will
   * return the current mm->brk value.
   *
   * So we're going to restore the brk here.  As long as the current mm->brk
   * value is below the static restore region, we're ok because we 'know' the
   * restored brk can't be in the static restore region, and we don't care if
   * the kernel mmaps something or munmaps something because we're going to wipe
   * it all out anyway.
   */

  current_brk = mtcp_sys_brk(NULL);
  if ((current_brk > restore_begin) &&
      (saved_brk < restore_end)) {
    MTCP_PRINTF("current_brk %p, saved_brk %p, restore_begin %p,"
                " restore_end %p\n",
                current_brk, saved_brk, restore_begin,
                restore_end);
    mtcp_abort();
  }

  if (current_brk <= saved_brk) {
    new_brk = mtcp_sys_brk(saved_brk);
    rinfo.saved_brk = NULL; // We no longer need the value of saved_brk.
  } else {
    new_brk = saved_brk;

    // If saved_brk < current_brk, then brk() does munmap; we can lose rinfo.
    // So, keep the value rinfo.saved_brk, and call mtcp_sys_brk() later.
    return;
  }
  if (new_brk == (VA)-1) {
    MTCP_PRINTF("sbrk(%p): errno: %d (bad heap)\n",
                saved_brk, mtcp_sys_errno);
    mtcp_abort();
  } else if (new_brk > current_brk) {
    // Now unmap the just mapped extended heap. This is to ensure that we don't
    // have overlap with the restore region.
    if (mtcp_sys_munmap(current_brk, new_brk - current_brk) == -1) {
      MTCP_PRINTF("***WARNING: munmap failed; errno: %d\n", mtcp_sys_errno);
    }
  }
  if (new_brk != saved_brk) {
    if (new_brk == current_brk && new_brk > saved_brk) {
      DPRINTF("new_brk == current_brk == %p\n; saved_break, %p,"
              " is strictly smaller;\n  data segment not extended.\n",
              new_brk, saved_brk);
    } else {
      if (new_brk == current_brk) {
        MTCP_PRINTF("error: new/current break (%p) != saved break (%p)\n",
                    current_brk, saved_brk);
      } else {
        MTCP_PRINTF("error: new break (%p) != current break (%p)\n",
                    new_brk, current_brk);
      }

      // mtcp_abort ();
    }
  }
}

#ifdef __aarch64__
// From Dynamo RIO, file:  dr_helper.c
// See https://github.com/DynamoRIO/dynamorio/wiki/AArch64-Port
//   (self-modifying code) for background.
# define ALIGN_FORWARD(addr,align) (void *)(((unsigned long)addr + align - 1) & ~(unsigned long)(align-1))
# define ALIGN_BACKWARD(addr,align) (void *)((unsigned long)addr & ~(unsigned long)(align-1))
void
clear_icache(void *beg, void *end)
{
    static size_t cache_info = 0;
    size_t dcache_line_size;
    size_t icache_line_size;
    typedef unsigned int* ptr_uint_t;
    ptr_uint_t beg_uint = (ptr_uint_t)beg;
    ptr_uint_t end_uint = (ptr_uint_t)end;
    ptr_uint_t addr;

    if (beg_uint >= end_uint)
        return;

    /* "Cache Type Register" contains:
     * CTR_EL0 [31]    : 1
     * CTR_EL0 [19:16] : Log2 of number of 4-byte words in smallest dcache line
     * CTR_EL0 [3:0]   : Log2 of number of 4-byte words in smallest icache line
     */
    if (cache_info == 0)
        __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(cache_info));
    dcache_line_size = 4 << (cache_info >> 16 & 0xf);
    icache_line_size = 4 << (cache_info & 0xf);

    /* Flush data cache to point of unification, one line at a time. */
    addr = ALIGN_BACKWARD(beg_uint, dcache_line_size);
    do {
        __asm__ __volatile__("dc cvau, %0" : : "r"(addr) : "memory");
        addr += dcache_line_size;
    } while (addr != ALIGN_FORWARD(end_uint, dcache_line_size));

    /* Data Synchronization Barrier */
    __asm__ __volatile__("dsb ish" : : : "memory");

    /* Invalidate instruction cache to point of unification, one line at a time. */
    addr = ALIGN_BACKWARD(beg_uint, icache_line_size);
    do {
        __asm__ __volatile__("ic ivau, %0" : : "r"(addr) : "memory");
        addr += icache_line_size;
    } while (addr != ALIGN_FORWARD(end_uint, icache_line_size));

    /* Data Synchronization Barrier */
    __asm__ __volatile__("dsb ish" : : : "memory");

    /* Instruction Synchronization Barrier */
    __asm__ __volatile__("isb" : : : "memory");
}
#endif

NO_OPTIMIZE
static void
restart_fast_path()
{
#ifdef LOGGING
  int mtcp_sys_errno;  // for sake of DPRINTF()
#endif

  /* For __arm__ and __aarch64__ will need to invalidate cache after this.
   */
  remapMtcpRestartToReservedArea(&rinfo);

  // Copy over old stack to new location;
  mtcp_memcpy(rinfo.new_stack_addr, rinfo.old_stack_addr, rinfo.old_stack_size);

  DPRINTF("We have copied mtcp_restart to higher address.  We will now\n"
          "    jump into a copy of restorememoryareas().\n");

  // Read the current value of sp and bp/fp registers and subtract the
  // stack_offset to compute the new sp and bp values. We have already copied
  // all the bits from old stack to the new one and so any one referring to
  // stack data using sp/bp should be fine.
  // NOTE: changing the value of bp/fp register is optional and only useful for
  // doing a return from this function or to access any local variables. Since
  // we don't use any local variables from here on, we can ignore bp/fp
  // registers.
  // NOTE: 32-bit ARM doesn't have an fp register.

#if defined(__i386__) || defined(__x86_64__)
  asm volatile ("mfence" ::: "memory");

  asm volatile (CLEAN_FOR_64_BIT(sub %0, %%esp; )
                CLEAN_FOR_64_BIT(sub %0, %%ebp; )
                : : "r" (rinfo.stack_offset) : "memory");

#elif defined(__arm__)
  asm volatile ("sub sp, sp, %0"
                : : "r" (rinfo.stack_offset) : "memory");

#elif defined(__aarch64__)
  // Use x29 instead of fp because GCC's inline assembler does not recognize fp.
  asm volatile ("sub sp, sp, %0\n\t"
                "sub x29, x29, %0"
                : : "r" (rinfo.stack_offset) : "memory");

#else /* if defined(__i386__) || defined(__x86_64__) */

# error "assembly instruction not translated"

#endif /* if defined(__i386__) || defined(__x86_64__) */

  /* IMPORTANT:  We just changed to a new stack.  The call frame for this
   * function on the old stack is no longer available.  The only way to pass
   * rinfo into the next function is by passing a pointer to a global variable
   * We call restorememoryareas_fptr(), which points to the copy of the
   * function in higher memory.  We will be unmapping the original fnc.
   */
  rinfo.restorememoryareas_fptr(&rinfo);

  /* NOTREACHED */
}

NO_OPTIMIZE
static void
restart_slow_path()
{
  restorememoryareas(&rinfo);
}

// Used by util/readdmtcp.sh
// So, we use mtcp_printf to stdout instead of MTCP_PRINTF (diagnosis for DMTCP)
static void
mtcp_simulateread(int fd, MtcpHeader *mtcpHdr)
{
  int mtcp_sys_errno;

  // Print miscellaneous information:
  char buf[MTCP_SIGNATURE_LEN + 1];

  mtcp_memcpy(buf, mtcpHdr->signature, MTCP_SIGNATURE_LEN);
  buf[MTCP_SIGNATURE_LEN] = '\0';
  mtcp_printf("\nMTCP: %s", buf);
  mtcp_printf("**** mtcp_restart (will be copied here): %p..%p\n",
              mtcpHdr->restore_addr,
              mtcpHdr->restore_addr + mtcpHdr->restore_size);
  mtcp_printf("**** DMTCP entry point (ThreadList::postRestart()): %p\n",
              mtcpHdr->post_restart);
  mtcp_printf("**** brk (sbrk(0)): %p\n", mtcpHdr->saved_brk);
  mtcp_printf("**** vdso: %p..%p\n", mtcpHdr->vdsoStart, mtcpHdr->vdsoEnd);
  mtcp_printf("**** vvar: %p..%p\n", mtcpHdr->vvarStart, mtcpHdr->vvarEnd);
  mtcp_printf("**** end of stack: %p\n", mtcpHdr->end_of_stack);

  Area area;
  mtcp_printf("\n**** Listing ckpt image area:\n");
  while (1) {
    mtcp_readfile(fd, &area, sizeof area);
    if (area.size == -1) {
      break;
    }
    if ((area.properties & DMTCP_ZERO_PAGE) == 0 &&
        (area.properties & DMTCP_SKIP_WRITING_TEXT_SEGMENTS) == 0) {
      void *addr = mtcp_sys_mmap(0, area.size, PROT_WRITE | PROT_READ,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (addr == MAP_FAILED) {
        MTCP_PRINTF("***Error: mmap failed; errno: %d\n", mtcp_sys_errno);
        mtcp_abort();
      }
      mtcp_readfile(fd, addr, area.size);
      if (mtcp_sys_munmap(addr, area.size) == -1) {
        MTCP_PRINTF("***Error: munmap failed; errno: %d\n", mtcp_sys_errno);
        mtcp_abort();
      }
    }

    mtcp_printf("%p-%p %c%c%c%c "

                // "%x %u:%u %u"
                "          %s\n",
                area.addr, area.addr + area.size,
                (area.prot & PROT_READ  ? 'r' : '-'),
                (area.prot & PROT_WRITE ? 'w' : '-'),
                (area.prot & PROT_EXEC  ? 'x' : '-'),
                (area.flags & MAP_SHARED ? 's'
                 : (area.flags & MAP_ANONYMOUS ? 'p' : '-')),

                // area.offset, area.devmajor, area.devminor, area.inodenum,
                area.name);
  }
}

NO_OPTIMIZE
static void
restorememoryareas(RestoreInfo *rinfo_ptr)
{
  int mtcp_sys_errno;

  DPRINTF("Entering copy of restorememoryareas().  Will now unmap old memory"
          "\n    and restore memory sections from the checkpoint image.\n");

  DPRINTF("DPRINTF may fail when we unmap, since strings are in rodata.\n"
          "But we may be lucky if the strings have been cached by the O/S\n"
          "or if compiler uses relative addressing for rodata with -fPIC\n");

  if (rinfo_ptr->use_gdb) {
    MTCP_PRINTF("Called with --use-gdb.  A useful command is:\n"
                "    (gdb) info proc mapping\n"
                "    (gdb) add-symbol-file ../../bin/mtcp_restart %p\n",
                rinfo_ptr->mtcp_restart_text_addr);

#if defined(__i386__) || defined(__x86_64__)
    asm volatile ("int3"); // Do breakpoint; send SIGTRAP, caught by gdb
#else /* if defined(__i386__) || defined(__x86_64__) */
    MTCP_PRINTF(
      "IN GDB: interrupt (^C); add-symbol-file ...; (gdb) print x=0\n");
    { int x = 1; while (x) {}
    }                         // Stop execution for user to type command.
#endif /* if defined(__i386__) || defined(__x86_64__) */
  }

  RestoreInfo restore_info;
  mtcp_memcpy(&restore_info, rinfo_ptr, sizeof(restore_info));
  if (rinfo_ptr->saved_brk != NULL) {
    // Now, we can do the pending mtcp_sys_brk(rinfo.saved_brk).
    // It's now safe to do this, even though it can munmap memory holding rinfo.
    if ((intptr_t)mtcp_sys_brk(rinfo_ptr->saved_brk) == -1) {
       MTCP_PRINTF("error restoring brk: %d\n", mtcp_sys_errno);
    }
  }

#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(xor %%eax, %%eax; movw %%ax, %%fs)
                  : : : CLEAN_FOR_64_BIT(eax));
#elif defined(__arm__)
  mtcp_sys_kernel_set_tls(0);  /* Uses 'mcr', a kernel-mode instr. on ARM */
#elif defined(__aarch64__)
# warning __FUNCTION__ "TODO: Implementation for ARM64"
#endif /* if defined(__i386__) || defined(__x86_64__) */

  /* Unmap everything except for vdso, vvar, vsyscall and this image as
   *  everything we need is contained in the libmtcp.so image.
   * Unfortunately, in later Linuxes, it's important also not to wipe
   *   out [vsyscall] if it exists (we may not have permission to remove it).
   * Further, if the [vdso] when we restart is different from the old
   *   [vdso] that was saved at checkpoint time, then we need to overwrite
   *   the old vdso with the new one (using mremap).
   *   Similarly for vvar.
   */
  unmap_memory_areas_and_restore_vdso(&restore_info);
  /* Restore memory areas */
  DPRINTF("restoring memory areas\n");
  readmemoryareas(restore_info.fd, restore_info.endOfStack);

  /* Everything restored, close file and finish up */

  DPRINTF("close cpfd %d\n", restore_info.fd);
  mtcp_sys_close(restore_info.fd);
  double readTime = 0.0;
#ifdef TIMING
  struct timeval endValue;
  mtcp_sys_gettimeofday(&endValue, NULL);
  struct timeval diff;
  timersub(&endValue, &restore_info.startValue, &diff);
  readTime = diff.tv_sec + (diff.tv_usec / 1000000.0);
#endif

  IMB; /* flush instruction cache, since mtcp_restart.c code is now gone. */

  /* Restore libc */
  DPRINTF("Memory is now restored.  Will next restore libc internals.\n");
  restore_libc(&restore_info.motherofall_tls_info, restore_info.tls_pid_offset,
               restore_info.tls_tid_offset, restore_info.myinfo_gs);

  /* System calls and libc library calls should now work. */

  DPRINTF("MTCP restore is now complete.  Continuing by jumping to\n"
          "  ThreadList::postRestart() back inside libdmtcp.so: %p...\n",
          restore_info.post_restart);

  if (restore_info.mtcp_restart_pause) {
    MTCP_PRINTF(
      "\nStopping due to env. var DMTCP_RESTART_PAUSE or MTCP_RESTART_PAUSE\n"
      "(DMTCP_RESTART_PAUSE can be set after creating the checkpoint image.)\n"
      "Attach to the computation with GDB from another window:\n"
      "(This won't work well unless you configure DMTCP with --enable-debug)\n"
      "  gdb PROGRAM_NAME %d\n"
      "You should now be in 'ThreadList::postRestartDebug()'\n"
      "  (gdb) list\n"
      "  (gdb) p dummy = 0\n"
      "  # Since Linux 3.10 (prctl:PR_SET_MM), you will also need to do:\n"
      "  (gdb) source DMTCP_ROOT/util/gdb-add-symbol-files-all\n",
      "  (gdb) add-symbol-files-all\n",
      mtcp_sys_getpid()
    );
    restore_info.post_restart_debug(readTime, restore_info.mtcp_restart_pause);
    // int dummy = 1;
    // while (dummy);
  } else {
    restore_info.post_restart(readTime);
  }
  // NOTREACHED
}

NO_OPTIMIZE
static void
unmap_memory_areas_and_restore_vdso(RestoreInfo *rinfo)
{
  /* Unmap everything except this image, vdso, vvar and vsyscall. */
  int mtcp_sys_errno;
  Area area;
  VA vdsoStart = NULL;
  VA vdsoEnd = NULL;
  VA vvarStart = NULL;
  VA vvarEnd = NULL;

  int mapsfd = mtcp_sys_open2("/proc/self/maps", O_RDONLY);

  if (mapsfd < 0) {
    MTCP_PRINTF("error opening /proc/self/maps; errno: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }

  while (mtcp_readmapsline(mapsfd, &area)) {
    if (area.addr >= rinfo->restore_addr && area.addr < rinfo->restore_end) {
      // Do not unmap this restore image.
    } else if (mtcp_strcmp(area.name, "[vdso]") == 0) {
      // Do not unmap vdso.
      vdsoStart = area.addr;
      vdsoEnd = area.endAddr;
      DPRINTF("***INFO: vDSO found (%p..%p)\n original vDSO: (%p..%p)\n",
              area.addr, area.endAddr, rinfo->vdsoStart, rinfo->vdsoEnd);
    }
#if defined(__i386__) && LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 18)
    else if (area.addr == 0xfffe0000 && area.size == 4096) {
      // It's a vdso page from a time before Linux displayed the annotation.
      // Do not unmap vdso.
    }
#endif /* if defined(__i386__) && LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 18)
          */
    else if (mtcp_strcmp(area.name, "[vvar]") == 0) {
      // Do not unmap vvar.
      vvarStart = area.addr;
      vvarEnd = area.endAddr;
    } else if (mtcp_strcmp(area.name, "[vsyscall]") == 0) {
      // Do not unmap vsyscall.
    } else if (mtcp_strcmp(area.name, "[vectors]") == 0) {
      // Do not unmap vectors.  (used in Linux 3.10 on __arm__)
    } else if (area.size > 0) {
      DPRINTF("***INFO: munmapping (%p..%p)\n", area.addr, area.endAddr);
      if (mtcp_sys_munmap(area.addr, area.size) == -1) {
        MTCP_PRINTF("***WARNING: %s(%x): munmap(%p, %d) failed; errno: %d\n",
                    area.name, area.flags, area.addr, area.size,
                    mtcp_sys_errno);
        mtcp_abort();
      }

      // Rewind and reread maps.
      mtcp_sys_lseek(mapsfd, 0, SEEK_SET);
    }
  }
  mtcp_sys_close(mapsfd);

  if ((vdsoStart == vvarEnd && rinfo->vdsoStart != rinfo->vvarEnd) ||
      (vvarStart == vdsoEnd && rinfo->vvarStart != rinfo->vdsoEnd)) {
    MTCP_PRINTF("***Error: vdso/vvar order was different during ckpt.\n");
    mtcp_abort();
  }

  if (vdsoEnd - vdsoStart != rinfo->vdsoEnd - rinfo->vdsoStart) {
    MTCP_PRINTF("***Error: vdso size mismatch.\n");
    mtcp_abort();
  }

  if (vvarEnd - vvarStart != rinfo->vvarEnd - rinfo->vvarStart) {
    MTCP_PRINTF("***Error: vvar size mismatch.\n");
    mtcp_abort();
  }

  if (vdsoStart == rinfo->vdsoStart) {
    // If the new vDSO is at the same address as the old one, do nothing.
    MTCP_ASSERT(vvarStart == rinfo->vvarStart);
    return;
  }

  // Check for overlap between newer and older vDSO/vvar sections.
  if (doAreasOverlap(vdsoStart, vdsoEnd - vdsoStart,
                     rinfo->vdsoStart, rinfo->vdsoEnd - rinfo->vdsoStart) ||
      doAreasOverlap(vdsoStart, vdsoEnd - vdsoStart,
                     rinfo->vvarStart, rinfo->vvarEnd - rinfo->vvarStart) ||
      doAreasOverlap(vvarStart, vvarEnd - vvarStart,
                     rinfo->vdsoStart, rinfo->vdsoEnd - rinfo->vdsoStart) ||
      doAreasOverlap(vdsoStart, vdsoEnd - vdsoStart,
                     rinfo->vvarStart, rinfo->vvarEnd - rinfo->vvarStart)) {
    MTCP_PRINTF("*** MTCP Error: Overlapping addresses for older and newer\n"
                "                vDSO/vvar sections.\n"
                "      vdsoStart: %p vdsoEnd: %p vvarStart: %p vvarEnd: %p\n"
                "rinfo:vdsoStart: %p vdsoEnd: %p vvarStart: %p vvarEnd: %p\n",
                vdsoStart,
                vdsoEnd,
                vvarStart,
                vvarEnd,
                rinfo->vdsoStart,
                rinfo->vdsoEnd,
                rinfo->vvarStart,
                rinfo->vvarEnd);
    mtcp_abort();
  }

  if (vdsoStart != NULL) {
    void *vdso = mtcp_sys_mremap(vdsoStart,
                                 vdsoEnd - vdsoStart,
                                 vdsoEnd - vdsoStart,
                                 MREMAP_FIXED | MREMAP_MAYMOVE,
                                 rinfo->vdsoStart);
    if (vdso == MAP_FAILED) {
      MTCP_PRINTF("***Error: failed to mremap vdso; errno: %d.\n",
                  mtcp_sys_errno);
      mtcp_abort();
    }
    MTCP_ASSERT(vdso == rinfo->vdsoStart);

#if defined(__i386__)

    // In commit dec2c26c1eb13eb1c12edfdc9e8e811e4cc0e3c2 , the mremap
    // code above was added, and then caused a segfault on restart for
    // __i386__ in CentOS 7.  In that case ENABLE_VDSO_CHECK was not defined.
    // This version was needed in that commit for __i386__
    // (i.e., for multi-arch.sh) to succeed.
    // Newer Linux kernels, such as __x86_64__, provide a separate vsyscall
    // segment
    // for kernel calls while using vdso for system calls that can be
    // executed purely in user space through kernel-specific user-space code.
    // On older kernels like __x86__, both purposes are squeezed into vdso.
    // Since vdso will use randomized addresses (unlike the standard practice
    // for vsyscall), this implies that kernel calls on __x86__ can go through
    // randomized addresses, and so they need special treatment.
    vdso = mtcp_sys_mmap(vdsoStart, vdsoEnd - vdsoStart,
                         PROT_EXEC | PROT_WRITE | PROT_READ,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

    // The new vdso was remapped to the location of the old vdso, since the
    // restarted application code remembers the old vdso address.
    // But for __i386__, a kernel call will go through the old vdso address
    // into the kernel, and then the kernel will return to the new vdso address
    // that was created by this kernel.  So, we need to copy the new vdso
    // code from its current location at the old vdso address back into
    // the new vdso address that was just mmap'ed.
    if (vdso == MAP_FAILED) {
      MTCP_PRINTF("***Error: failed to mremap vdso; errno: %d\n",
                  mtcp_sys_errno);
      mtcp_abort();
    }
    MTCP_ASSERT(vdso == vdsoStart);
    mtcp_memcpy(vdsoStart, rinfo->vdsoStart, vdsoEnd - vdsoStart);
#endif /* if defined(__i386__) */
  }

  if (vvarStart != NULL) {
    void *vvar = mtcp_sys_mremap(vvarStart,
                                 vvarEnd - vvarStart,
                                 vvarEnd - vvarStart,
                                 MREMAP_FIXED | MREMAP_MAYMOVE,
                                 rinfo->vvarStart);
    if (vvar == MAP_FAILED) {
      MTCP_PRINTF("***Error: failed to mremap vvar; errno: %d.\n",
                  mtcp_sys_errno);
      mtcp_abort();
    }
    MTCP_ASSERT(vvar == rinfo->vvarStart);

#if defined(__i386__)
    vvar = mtcp_sys_mmap(vvarStart, vvarEnd - vvarStart,
                         PROT_EXEC | PROT_WRITE | PROT_READ,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (vvar == MAP_FAILED) {
      MTCP_PRINTF("***Error: failed to mremap vvar; errno: %d\n",
                  mtcp_sys_errno);
      mtcp_abort();
    }
    MTCP_ASSERT(vvar == vvarStart);
    // On i386, only the first page is readable. Reading beyond that
    // results in a bus error.
    // Arguably, this is a bug in the kernel, since /proc/*/maps indicates
    // that both pages of vvar memory have read permission.
    // This issue arose due to a change in the Linux kernel pproximately in
    // version 4.0
    // TODO: Find a way to automatically detect the readable bytes.
    mtcp_memcpy(vvarStart, rinfo->vvarStart, MTCP_PAGE_SIZE);
#endif /* if defined(__i386__) */
  }
}

/**************************************************************************
 *
 *  Read memory area descriptors from checkpoint file
 *  Read memory area contents and/or mmap original file
 *  Four cases: MAP_ANONYMOUS (if file /proc/.../maps reports file,
 *		   handle it as if MAP_PRIVATE and not MAP_ANONYMOUS,
 *		   but restore from ckpt image: no copy-on-write);
 *		 private, currently assumes backing file exists
 *               shared, but need to recreate file;
 *		 shared and file currently exists
 *		   (if writeable by us and memory map has write
 *		    protection, then write to it from checkpoint file;
 *		    else skip ckpt image and map current data of file)
 *		 NOTE:  Linux option MAP_SHARED|MAP_ANONYMOUS
 *		    currently not supported; result is undefined.
 *		    If there is an important use case, we will fix this.
 *		 (NOTE:  mmap requires that if MAP_ANONYMOUS
 *		   was not set, then mmap must specify a backing store.
 *		   Further, a reference by mmap constitutes a reference
 *		   to the file, and so the file cannot truly be deleted
 *		   until the process no longer maps it.  So, if we don't
 *		   see the file on restart and there is no MAP_ANONYMOUS,
 *		   then we have a responsibility to recreate the file.
 *		   MAP_ANONYMOUS is not currently POSIX.)
 *
 **************************************************************************/
static void
readmemoryareas(int fd, VA endOfStack)
{
  while (1) {
    if (read_one_memory_area(fd, endOfStack) == -1) {
      break; /* error */
    }
  }
#if defined(__arm__) || defined(__aarch64__)

  /* On ARM, with gzip enabled, we sometimes see SEGFAULT without this.
   * The SEGFAULT occurs within the initial thread, before any user threads
   * are unblocked.  WHY DOES THIS HAPPEN?
   */
  WMB;
#endif /* if defined(__arm__) || defined(__aarch64__) */
}

NO_OPTIMIZE
static int
read_one_memory_area(int fd, VA endOfStack)
{
  int mtcp_sys_errno;
  int imagefd;
  void *mmappedat;
  int try_skipping_existing_segment = 0;

  /* Read header of memory area into area; mtcp_readfile() will read header */
  Area area;

  mtcp_readfile(fd, &area, sizeof area);
  if (area.size == -1) {
    return -1;
  }

  if (area.name[0] && mtcp_strstr(area.name, "[heap]")
      && mtcp_sys_brk(NULL) != area.addr + area.size) {
    DPRINTF("WARNING: break (%p) not equal to end of heap (%p)\n",
            mtcp_sys_brk(NULL), area.addr + area.size);
  }
  /* MAP_GROWSDOWN flag is required for stack region on restart to make
   * stack grow automatically when application touches any address within the
   * guard page region(usually, one page less then stack's start address).
   *
   * The end of stack is detected dynamically at checkpoint time. See
   * prepareMtcpHeader() in threadlist.cpp and ProcessInfo::growStack()
   * in processinfo.cpp.
   */
  if (area.name[0] && mtcp_strstr(area.name, "stack")
      || (area.endAddr == endOfStack)) {
    area.flags = area.flags | MAP_GROWSDOWN;
    DPRINTF("Detected stack area. End of stack (%p); Area end address (%p)\n",
            endOfStack, area.endAddr);
  }

  // We could have replaced MAP_SHARED with MAP_PRIVATE in writeckpt.cpp
  // instead of here. But we do it this way for debugging purposes. This way,
  // readdmtcp.sh will still be able to properly list the shared memory areas.
  if (area.flags & MAP_SHARED) {
    area.flags = area.flags ^ MAP_SHARED;
    area.flags = area.flags | MAP_PRIVATE | MAP_ANONYMOUS;
  }

  /* Now mmap the data of the area into memory. */

  /* CASE MAPPED AS ZERO PAGE: */
  if ((area.properties & DMTCP_ZERO_PAGE) != 0) {
    DPRINTF("restoring non-rwx anonymous area, %p bytes at %p\n",
            area.size, area.addr);
    mmappedat = mtcp_sys_mmap(area.addr, area.size,
                              area.prot,
                              area.flags | MAP_FIXED, -1, 0);

    if (mmappedat != area.addr) {
      DPRINTF("error %d mapping %p bytes at %p\n",
              mtcp_sys_errno, area.size, area.addr);
      mtcp_abort();
    }
  }

#ifdef FAST_RST_VIA_MMAP
    /* CASE MAP_ANONYMOUS with FAST_RST enabled
     * We only want to do this in the MAP_ANONYMOUS case, since we don't want
     *   any writes to RAM to be reflected back into the underlying file.
     * Note that in order to map from a file (ckpt image), we must turn off
     *   anonymous (~MAP_ANONYMOUS).  It's okay, since the fd
     *   should have been opened with read permission, only.
     */
    else if (area.flags & MAP_ANONYMOUS) {
      mmapfile (fd, area.addr, area.size, area.prot,
                area.flags & ~MAP_ANONYMOUS);
    }
#endif

  /* CASE MAP_ANONYMOUS (usually implies MAP_PRIVATE):
   * For anonymous areas, the checkpoint file contains the memory contents
   * directly.  So mmap an anonymous area and read the file into it.
   * If file exists, turn off MAP_ANONYMOUS: standard private map
   */
  else if (area.flags & MAP_ANONYMOUS) {
    /* If there is a filename there, though, pretend like we're mapping
     * to it so a new /proc/self/maps will show a filename there like with
     * original process.  We only need read-only access because we don't
     * want to ever write the file.
     */

    imagefd = -1;
    if (area.name[0] == '/') { /* If not null string, not [stack] or [vdso] */
      imagefd = mtcp_sys_open(area.name, O_RDONLY, 0);
      if (imagefd >= 0) {
        /* If the current file size is smaller than the original, we map the region
         * as private anonymous. Note that with this we lose the name of the region
         * but most applications may not care.
         */
        off_t curr_size = mtcp_sys_lseek(imagefd, 0, SEEK_END);
        MTCP_ASSERT(curr_size != -1);
        if (curr_size < area.offset + area.size) {
          mtcp_sys_close(imagefd);
          imagefd = -1;
          area.offset = 0;
        } else {
          area.flags ^= MAP_ANONYMOUS;
        }
      }
    }

    if (area.flags & MAP_ANONYMOUS) {
      DPRINTF("restoring anonymous area, %p  bytes at %p\n",
              area.size, area.addr);
    } else {
      DPRINTF("restoring to non-anonymous area from anonymous area,"
              " %p bytes at %p from %s + 0x%X\n",
              area.size, area.addr, area.name, area.offset);
    }

    /* Create the memory area */

    /* POSIX says mmap would unmap old memory.  Munmap never fails if args
     * are valid.  Can we unmap vdso and vsyscall in Linux?  Used to use
     * mtcp_safemmap here to check for address conflicts.
     */
    mmappedat = mtcp_sys_mmap(area.addr, area.size, area.prot | PROT_WRITE,
                              area.flags, imagefd, area.offset);

    if (mmappedat == MAP_FAILED) {
      DPRINTF("error %d mapping %p bytes at %p\n",
              mtcp_sys_errno, area.size, area.addr);
      if (mtcp_sys_errno == ENOMEM) {
        MTCP_PRINTF(
          "\n**********************************************************\n"
          "****** Received ENOMEM.  Trying to continue, but may fail.\n"
          "****** Please run 'free' to see if you have enough swap space.\n"
          "**********************************************************\n\n");
      }
      try_skipping_existing_segment = 1;
    }
    if (mmappedat != area.addr && !try_skipping_existing_segment) {
      MTCP_PRINTF("area at %p got mmapped to %p\n", area.addr, mmappedat);
      mtcp_abort();
    }

#if 0

    /*
     * The function is not used but is truer to maintaining the user's
     * view of /proc/ * /maps. It can be enabled again in the future after
     * we fix the logic to handle zero-sized files.
     */
    if (imagefd >= 0) {
      adjust_for_smaller_file_size(&area, imagefd);
    }
#endif /* if 0 */

    /* Close image file (fd only gets in the way) */
    if (imagefd >= 0 && !(area.flags & MAP_ANONYMOUS)) {
      mtcp_sys_close(imagefd);
    }

    if (try_skipping_existing_segment) {
      // This fails on teracluster.  Presumably extra symbols cause overflow.
      mtcp_skipfile(fd, area.size);
    } else if ((area.properties & DMTCP_SKIP_WRITING_TEXT_SEGMENTS) == 0) {
      /* This mmapfile after prev. mmap is okay; use same args again.
       *  Posix says prev. map will be munmapped.
       */

      /* ANALYZE THE CONDITION FOR DOING mmapfile MORE CAREFULLY. */
      mtcp_readfile(fd, area.addr, area.size);
      if (!(area.prot & PROT_WRITE)) {
        if (mtcp_sys_mprotect(area.addr, area.size, area.prot) < 0) {
          MTCP_PRINTF("error %d write-protecting %p bytes at %p\n",
                      mtcp_sys_errno, area.size, area.addr);
          mtcp_abort();
        }
      }
    }
  }
  /* CASE NOT MAP_ANONYMOUS:
   * Otherwise, we mmap the original file contents to the area.
   * This case is now delegated to DMTCP.  Nothing to do for MTCP.
   */
  else { /* Internal error. */
    MTCP_ASSERT(0);
  }
  return 0;
}

#if 0

// See note above.
NO_OPTIMIZE
static void
adjust_for_smaller_file_size(Area *area, int fd)
{
  int mtcp_sys_errno;
  off_t curr_size = mtcp_sys_lseek(fd, 0, SEEK_END);

  if (curr_size == -1) {
    return;
  }
  if (area->offset + area->size > curr_size) {
    size_t diff_in_size = (area->offset + area->size) - curr_size;
    size_t anon_area_size = (diff_in_size + MTCP_PAGE_SIZE - 1)
      & MTCP_PAGE_MASK;
    VA anon_start_addr = area->addr + (area->size - anon_area_size);

    DPRINTF("For %s, current size (%ld) smaller than original (%ld).\n"
            "mmap()'ng the difference as anonymous.\n",
            area->name, curr_size, area->size);
    VA mmappedat = mtcp_sys_mmap(anon_start_addr, anon_area_size,
                                 area->prot | PROT_WRITE,
                                 MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS,
                                 -1, 0);

    if (mmappedat == MAP_FAILED) {
      DPRINTF("error %d mapping %p bytes at %p\n",
              mtcp_sys_errno, anon_area_size, anon_start_addr);
    }
    if (mmappedat != anon_start_addr) {
      MTCP_PRINTF("area at %p got mmapped to %p\n", anon_start_addr, mmappedat);
      mtcp_abort();
    }
  }
}
#endif /* if 0 */


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
restore_libc(ThreadTLSInfo *tlsInfo,
             int tls_pid_offset,
             int tls_tid_offset,
             MYINFO_GS_T myinfo_gs)
{
  int mtcp_sys_errno;

  /* Every architecture needs a register to point to the current
   * TLS (thread-local storage).  This is where we set it up.
   */

  /* Patch 'struct user_desc' (gdtentrytls) of glibc to contain the
   * the new pid and tid.
   */
  *(pid_t *)(*(unsigned long *)&(tlsInfo->gdtentrytls[0].base_addr)
             + tls_pid_offset) = mtcp_sys_getpid();
  if (mtcp_sys_kernel_gettid() == mtcp_sys_getpid()) {
    *(pid_t *)(*(unsigned long *)&(tlsInfo->gdtentrytls[0].base_addr)
               + tls_tid_offset) = mtcp_sys_getpid();
  }

  /* Now pass this to the kernel, so it can adjust the segment descriptor.
   *   i386, x86_65: tls_set_thread_areaa() uses arg1 for fs and arg2 for gs.
   * This will make different kernel calls according to the CPU architecture. */
#if defined(__i386__) || defined(__x86_64__)
  if (tls_set_thread_area(&(tlsInfo->gdtentrytls[0]),
                          &(tlsInfo->gdtentrytls[1])) != 0)
#elif defined(__arm__) || defined(__aarch64__)
  // FIXME: ARM uses tls_get_thread_area with incompatible syntax,
  //        setting global variable myinfo_gs.  Fix this to work
  //        for per-thread storage (multiple threads).
  //        See commit 591a1631 (2.6.0), 7d02a2e0 (3.0):  PR #609
  if (tls_set_thread_area (&(tlsInfo->gdtentrytls[0]), myinfo_gs) != 0)
#endif
  {
    MTCP_PRINTF("Error restoring GDT TLS entry; errno: %d\n", mtcp_sys_errno);
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
#elif defined(__arm__) || defined(__aarch64__)

  /* ARM treats this same as x86_64 above. */
#endif /* ifdef __i386__ */
}

NO_OPTIMIZE
static int
doAreasOverlap(VA addr1, size_t size1, VA addr2, size_t size2)
{
  VA end1 = (char *)addr1 + size1;
  VA end2 = (char *)addr2 + size2;

  return (addr1 >= addr2 && addr1 < end2) || (addr2 >= addr1 && addr2 < end1);
}

NO_OPTIMIZE
static int
hasOverlappingMapping(VA addr, size_t size)
{
  int mtcp_sys_errno;
  int ret = 0;
  Area area;
  int mapsfd = mtcp_sys_open2("/proc/self/maps", O_RDONLY);

  if (mapsfd < 0) {
    MTCP_PRINTF("error opening /proc/self/maps: errno: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }

  while (mtcp_readmapsline(mapsfd, &area)) {
    if (doAreasOverlap(addr, size, area.addr, area.size)) {
      ret = 1;
      break;
    }
  }
  mtcp_sys_close(mapsfd);
  return ret;
}

// This is the entrypoint to the binary. We'll need it for adding symbol file.
int _start();

NO_OPTIMIZE
static void
remapMtcpRestartToReservedArea(RestoreInfo *rinfo)
{
  int mtcp_sys_errno;

  const size_t MAX_MTCP_RESTART_MEM_REGIONS = 16;

  Area mem_regions[MAX_MTCP_RESTART_MEM_REGIONS];

  size_t num_regions = 0;

  // First figure out mtcp_restart memory regions.
  int mapsfd = mtcp_sys_open2("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0) {
    MTCP_PRINTF("error opening /proc/self/maps: errno: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }

  Area area;
  while (mtcp_readmapsline(mapsfd, &area)) {
    if ((mtcp_strendswith(area.name, BINARY_NAME) ||
         mtcp_strendswith(area.name, BINARY_NAME_M32))) {
      MTCP_ASSERT(num_regions < MAX_MTCP_RESTART_MEM_REGIONS);
      mem_regions[num_regions++] = area;
    }

    // Also compute the stack location.
    if (area.addr < (VA) &area && area.endAddr > (VA) &area) {
      // We've found stack.
      rinfo->old_stack_addr = area.addr;
      rinfo->old_stack_size = area.size;
    }
  }

  mtcp_sys_close(mapsfd);

  size_t restore_region_offset = rinfo->restore_addr - mem_regions[0].addr;

  // TODO: _start can sometimes be different than text_offset. The foolproof
  // method would be to read the elf headers for the mtcp_restart binary and
  // compute text offset from there.
  size_t entrypoint_offset = (VA)&_start - (VA) mem_regions[0].addr;
  rinfo->mtcp_restart_text_addr = rinfo->restore_addr + entrypoint_offset;

  // Make sure we can fit all mtcp_restart regions in the restore area.
  MTCP_ASSERT(mem_regions[num_regions - 1].endAddr - mem_regions[0].addr <=
                rinfo->restore_size);

  // Now remap mtcp_restart at the restore location. Note that for memory
  // regions with write permissions, we copy over the bits from the original
  // location.
  int mtcp_restart_fd = mtcp_sys_open2("/proc/self/exe", O_RDONLY);
  if (mtcp_restart_fd < 0) {
    MTCP_PRINTF("error opening /proc/self/exe: errno: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }

  VA target_addr = rinfo->restore_addr;
  size_t i;
  for (i = 0; i < num_regions; i++) {
    void *addr = mtcp_sys_mmap(mem_regions[i].addr + restore_region_offset,
                               mem_regions[i].size,
                               mem_regions[i].prot,
                               MAP_PRIVATE | MAP_FIXED,
                               mtcp_restart_fd,
                               mem_regions[i].offset);

    if (addr == MAP_FAILED) {
      MTCP_PRINTF("mmap failed with error; errno: %d\n", mtcp_sys_errno);
      mtcp_abort();
    }

    // This probably is some memory that was initialized by the loader; let's
    // copy over the bits.
    if (mem_regions[i].prot & PROT_WRITE) {
      mtcp_memcpy(addr, mem_regions[i].addr, mem_regions[i].size);
    }
  }

  // Create a guard page without read permissions and use the remaining region
  // for the stack.

  VA guard_page =
    mem_regions[num_regions - 1].endAddr + restore_region_offset;

  MTCP_ASSERT(mtcp_sys_mmap(guard_page,
                            MTCP_PAGE_SIZE,
                            PROT_NONE,
                            MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED,
                            -1,
                            0) == guard_page);
  MTCP_ASSERT(guard_page != MAP_FAILED);

  VA guard_page_end_addr = guard_page + MTCP_PAGE_SIZE;

  size_t remaining_restore_area =
    rinfo->restore_addr + rinfo->restore_size - guard_page_end_addr;

#ifdef WSL
  // FIXME:  The assert below used to fail on WSL.  (Still a work in progress.)
  //   NOTE that PR #774 raises the following also in src/processinfo.h
  //     #define RESTORE_STACK_SIZE 16 * MB
  //     #define RESTORE_MEM_SIZE   16 * MB
  //   The new values seem to allow the assert below to pass now on WSL.
  //   Maybe this must correspond between processinfo.h and mtcp_restart.c.
  //   Maybe it must be a multiple of 2 MB to support HUGEPAGES for WSL ???
  //   Maybe WSL needs a larger stack, since they don't support MAP_GROWSDOWN
  // These debugging prints were to catch a failed asserg when
  //   processinfo.h was set to only 5 MB earlier.  Let's see if the
  //   assert fails in the future on WSL.  (work in progress)
  // DPRINTF("remaining_restore_area: %x\na", remaining_restore_area);
  // DPRINTF("rinfo->old_stack_size: %x\na", rinfo->old_stack_size);
  // REMOVE ALL OF THESE COMMENTS WHEN THIS CODE IS MATURE.
#endif
  MTCP_ASSERT(remaining_restore_area >= rinfo->old_stack_size);

  void *new_stack_end_addr = rinfo->restore_addr + rinfo->restore_size;
  void *new_stack_start_addr = new_stack_end_addr - rinfo->old_stack_size;

  rinfo->new_stack_addr =
    mtcp_sys_mmap(new_stack_start_addr,
                  rinfo->old_stack_size,
                  PROT_READ | PROT_WRITE,
                  MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED,
                  -1,
                  0);
  MTCP_ASSERT(rinfo->new_stack_addr != MAP_FAILED);

  rinfo->stack_offset = rinfo->old_stack_addr - rinfo->new_stack_addr;

  rinfo->restorememoryareas_fptr =
    (fnptr_t)(&restorememoryareas + restore_region_offset);

  DPRINTF("For debugging:\n"
          "    (gdb) add-symbol-file ../../bin/mtcp_restart %p\n",
          rinfo->mtcp_restart_text_addr);
}

// gcc can generate calls to these.
// Eventually, we'll isolate the PIC code in a library, and this can go away.
void
__stack_chk_fail(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("ERROR: Stack Overflow detected.\n");
  mtcp_abort();
}

void
__stack_chk_fail_local(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("ERROR: Stack Overflow detected.\n");
  mtcp_abort();
}

void
__stack_chk_guard(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("ERROR: Stack Overflow detected.\n");
  mtcp_abort();
}

void
_Unwind_Resume(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("MTCP Internal Error: %s Not Implemented.\n", __FUNCTION__);
  mtcp_abort();
}

void
__gcc_personality_v0(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("MTCP Internal Error: %s Not Implemented.\n", __FUNCTION__);
  mtcp_abort();
}

void
__intel_security_cookie(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("MTCP Internal Error: %s Not Implemented.\n", __FUNCTION__);
  mtcp_abort();
}

void
__intel_security_check_cookie(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("MTCP Internal Error: %s Not Implemented.\n", __FUNCTION__);
  mtcp_abort();
}

#ifdef FAST_RST_VIA_MMAP
static void mmapfile(int fd, void *buf, size_t size, int prot, int flags)
{
  int mtcp_sys_errno;
  void *addr;
  int rc;

  /* Use mmap for this portion of checkpoint image. */
  addr = mtcp_sys_mmap(buf, size, prot, flags,
                       fd, mtcp_sys_lseek(fd, 0, SEEK_CUR));
  if (addr != buf) {
    if (addr == MAP_FAILED) {
      MTCP_PRINTF("error %d reading checkpoint file\n", mtcp_sys_errno);
    } else {
      MTCP_PRINTF("Requested address %p, but got address %p\n", buf, addr);
    }
    mtcp_abort();
  }
  /* Now update fd so as to work the same way as readfile() */
  rc = mtcp_sys_lseek(fd, size, SEEK_CUR);
  if (rc == -1) {
    MTCP_PRINTF("mtcp_sys_lseek failed with errno %d\n", mtcp_sys_errno);
    mtcp_abort();
  }
}
#endif
