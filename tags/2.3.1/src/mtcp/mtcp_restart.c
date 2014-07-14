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
#include <unistd.h>
#include <unistd.h>
#include <sys/types.h>

#include "mtcp_sys.h"
#include "mtcp_util.ic"
#include "mtcp_check_vdso.ic"
#include "../membarrier.h"
#include "procmapsarea.h"
#include "mtcp_header.h"
#include "tlsutil.h"

void mtcp_check_vdso(char **environ);

#define BINARY_NAME "mtcp_restart"
#define BINARY_NAME_M32 "mtcp_restart-32"

/* struct RestoreInfo to pass all parameters from one function to next.
 * This must be global (not on stack) at the time that we jump from
 * original stack to copy of restorememoryareas() on new stack.
 * This is becasue we will wait until we are in the new call frame and then
 * copy the global data into the new call frame.
 */
typedef void (*fnptr_t)();
#define STACKSIZE 4*1024*1024
  //static long long tempstack[STACKSIZE];
typedef struct RestoreInfo {
  int fd;
  int stderr_fd;  /* FIXME:  This is never used. */
  // int mtcp_sys_errno;
  VA text_addr;
  size_t text_size;
  VA saved_brk;
  VA restore_addr;
  size_t restore_size;
  VA highest_va;
  fnptr_t post_restart;
  fnptr_t restorememoryareas_fptr;
  //void (*post_restart)();
  //void (*restorememoryareas_fptr)();
  int use_gdb;
  int text_offset;
  ThreadTLSInfo motherofall_tls_info;
  int tls_pid_offset;
  int tls_tid_offset;
  MYINFO_GS_T myinfo_gs;
} RestoreInfo;
static RestoreInfo rinfo;

/* Internal routines */
static void readmemoryareas(int fd);
#if 0
// Not currently used
static void mmapfile(int fd, void *buf, size_t size, int prot, int flags);
#endif
static void read_shared_memory_area_from_file(int fd, Area* area, int flags);
static VA highest_userspace_address (VA *vdso_addr, VA *vsyscall_addr,
                                     VA * stack_end_addr);
static void lock_file(int fd, char* name, short l_type);
// static char* fix_filename_if_new_cwd(char* filename);
static int open_shared_file(char* filename);
static void adjust_for_smaller_file_size(Area *area, int fd);
static void restorememoryareas(RestoreInfo *rinfo_ptr);
static void restore_brk(VA saved_brk, VA restore_begin, VA restore_end);
static void restart_fast_path(void);
static void restart_slow_path(void);
static int doAreasOverlap(VA addr1, size_t size1, VA addr2, size_t size2);
static int hasOverlappingMapping(VA addr, size_t size);
static void getMiscAddrs(VA *textAddr, size_t *size, VA *highest_va);
static void mtcp_simulateread(int fd);
void restore_libc(ThreadTLSInfo *tlsInfo, int tls_pid_offset,
                  int tls_tid_offset, MYINFO_GS_T myinfo_gs);


#define MB 1024*1024
#define RESTORE_STACK_SIZE 5*MB
#define RESTORE_MEM_SIZE 5*MB
#define RESTORE_TOTAL_SIZE (RESTORE_STACK_SIZE+RESTORE_MEM_SIZE)

//const char service_interp[] __attribute__((section(".interp"))) = "/lib64/ld-linux-x86-64.so.2";


int __libc_start_main (int (*main) (int, char **, char **),
                       int argc, char **argv,
                       void (*init) (void), void (*fini) (void),
                       void (*rtld_fini) (void), void *stack_end)
{
  int mtcp_sys_errno;
  char **envp = argv + argc + 1;
  int result = main(argc, argv, envp);
  mtcp_sys_exit(result);
  (void)mtcp_sys_errno; /* Stop compiler warning about unused variable */
  while(1);
}
void __libc_csu_init (int argc, char **argv, char **envp) { }
void __libc_csu_fini (void) { }
void __stack_chk_fail (void);  /* defined at end of file */
void abort(void) { mtcp_abort(); }
/* Implement memcpy() and memset() inside mtcp_restart. Although we are not
 * calling memset, the compiler may generate a call to memset() when trying to
 * initialize a large array etc.
 */
void *memset(void *s, int c, size_t n) {
  char *p = s;
  while (n-- > 0) {
    *p++ = (char)c;
  }
  return s;
}
void *memcpy(void *dest, const void *src, size_t n) {
  mtcp_sys_memcpy(dest, src, n);
  return dest;
}

#define shift argv++; argc--;
__attribute__((optimize(0)))
int main(int argc, char *argv[], char **environ)
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
{int x=1; while(x);}
#endif

  /* i386 uses random addresses for vdso.  Make sure that its location
   * will not conflict with other memory regions.
   * (Other arch's may also need this in the future.  So, we do it for all.)
   * Note that we may need to keep the old and the new vdso.  We may
   * have checkpointed inside gettimeofday inside the old vdso, and the
   * kernel, on restart, knows only the new vdso.
   */
  mtcp_check_vdso(environ);

  rinfo.fd = -1;
  rinfo.use_gdb = 0;
  rinfo.text_offset = -1;
  shift;
  while (argc > 0) {
    // Flags for standalone debugging
    if (argc == 1) {
      MTCP_PRINTF("Considering '%s' as a ckpt image.\n", argv[0]);
      ckptImage = argv[0];
      break;
    } else if (mtcp_strcmp(argv[0], "--use-gdb") == 0) {
      rinfo.use_gdb = 1;
      shift;
    } else if (mtcp_strcmp(argv[0], "--text-offset") == 0) {
      rinfo.text_offset = mtcp_strtol(argv[1]);
      shift; shift;
    // Flags for call by dmtcp_restart follow here:
    } else if (mtcp_strcmp(argv[0], "--fd") == 0) {
      rinfo.fd = mtcp_strtol(argv[1]);
      shift; shift;
    } else if (mtcp_strcmp(argv[0], "--stderr-fd") == 0) {
      rinfo.stderr_fd = mtcp_strtol(argv[1]);
      shift; shift;
    } else if (mtcp_strcmp(argv[0], "--simulate") == 0) {
      simulate = 1;
      shift;
    } else {
      MTCP_PRINTF("MTCP Internal Error\n");
      return -1;
    }
  }

  if ((rinfo.fd != -1) ^ (ckptImage == NULL)) {
    MTCP_PRINTF("***MTCP Internal Error\n");
    mtcp_abort();
  }

  if (rinfo.fd != -1) {
    mtcp_readfile(rinfo.fd, &mtcpHdr, sizeof mtcpHdr);
  } else {
    rinfo.fd = mtcp_sys_open2(ckptImage, O_RDONLY);
    if (rinfo.fd == -1) {
      MTCP_PRINTF("***ERROR opening ckpt image (%s): %d\n",
                  ckptImage, mtcp_sys_errno);
      mtcp_abort();
    }
    // This assumes that the MTCP header signature is unique.
    do {
      mtcp_readfile(rinfo.fd, &mtcpHdr, sizeof mtcpHdr);
    } while (mtcp_strcmp(mtcpHdr.signature, MTCP_SIGNATURE) != 0);
  }

  DPRINTF("For debugging:\n"
          "    (gdb) add-symbol-file ../../bin/mtcp_restart %p\n",
          mtcpHdr.restore_addr + rinfo.text_offset);
  if (rinfo.text_offset == -1)
    DPRINTF("... but add to the above the result, 1 +"
            " `text_offset.sh mtcp_restart`\n    in the mtcp subdirectory.\n");

  if (simulate) {
    mtcp_simulateread(rinfo.fd);
    return 0;
  }

  rinfo.saved_brk = mtcpHdr.saved_brk;
  rinfo.restore_addr = mtcpHdr.restore_addr;
  rinfo.restore_size = mtcpHdr.restore_size;
  rinfo.post_restart = mtcpHdr.post_restart;
  rinfo.motherofall_tls_info = mtcpHdr.motherofall_tls_info;
  rinfo.tls_pid_offset = mtcpHdr.tls_pid_offset;
  rinfo.tls_tid_offset = mtcpHdr.tls_tid_offset;
  rinfo.myinfo_gs = mtcpHdr.myinfo_gs;

  restore_brk(rinfo.saved_brk, rinfo.restore_addr,
              rinfo.restore_addr + rinfo.restore_size);
  // We will not use rinfo.highest_va.  It fails on several distros/CPUs.
  getMiscAddrs(&rinfo.text_addr, &rinfo.text_size, &rinfo.highest_va);
  if (hasOverlappingMapping(rinfo.restore_addr, rinfo.restore_size)) {
    MTCP_PRINTF("*** Not Implemented.\n\n");
    mtcp_abort();
    restart_slow_path();
  } else {
    restart_fast_path();
  }
  return 0;  /* Will not reach here, but need to satisfy the compiler */
}

__attribute__((optimize(0)))
static void restore_brk(VA saved_brk, VA restore_begin, VA restore_end)
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

  current_brk = mtcp_sys_brk (NULL);
  if ((current_brk > restore_begin) &&
      (saved_brk < restore_end)) {
    MTCP_PRINTF("current_brk %p, saved_brk %p, restore_begin %p,"
                " restore_end %p\n",
                 current_brk, saved_brk, restore_begin,
                 restore_end);
    mtcp_abort ();
  }

  new_brk = mtcp_sys_brk (saved_brk);
  if (new_brk == (VA)-1) {
    MTCP_PRINTF("sbrk(%p): errno: %d (bad heap)\n",
		 saved_brk, mtcp_sys_errno );
    mtcp_abort();
  } else if (new_brk > current_brk) {
    // Now unmap the just mapped extended heap. This is to ensure that we don't
    // have overlap with the restore region.
    if (mtcp_sys_munmap(current_brk, new_brk - current_brk) == -1) {
      MTCP_PRINTF("***WARNING: munmap failed: %d\n", mtcp_sys_errno);
    }
  }
  if (new_brk != saved_brk) {
    if (new_brk == current_brk && new_brk > saved_brk)
      DPRINTF("new_brk == current_brk == %p\n; saved_break, %p,"
              " is strictly smaller;\n  data segment not extended.\n",
              new_brk, saved_brk);
    else {
      if (new_brk == current_brk)
        MTCP_PRINTF("error: new/current break (%p) != saved break (%p)\n",
                    current_brk, saved_brk);
      else
        MTCP_PRINTF("error: new break (%p) != current break (%p)\n",
                    new_brk, current_brk);
      //mtcp_abort ();
    }
  }
}

__attribute__((optimize(0)))
static void restart_fast_path()
{
  int mtcp_sys_errno;
  void *addr = mtcp_sys_mmap(rinfo.restore_addr, rinfo.restore_size,
                             PROT_READ|PROT_WRITE|PROT_EXEC,
                             MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  if (addr == MAP_FAILED) {
    MTCP_PRINTF("mmap failed with error: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }

  size_t offset = (char*)&restorememoryareas - rinfo.text_addr;
  rinfo.restorememoryareas_fptr = (fnptr_t)(rinfo.restore_addr + offset);
/* For __arm__
 *    should be able to use kernel call: __ARM_NR_cacheflush(start, end, flag)
 *    followed by copying new text below, followed by DSB and ISB,
 *    to eliminstate need for delay loop.  But this needs more testing.
 */
  mtcp_sys_memcpy(rinfo.restore_addr, rinfo.text_addr, rinfo.text_size);
  mtcp_sys_memcpy(rinfo.restore_addr + rinfo.text_size, &rinfo, sizeof(rinfo));
  void *stack_ptr = rinfo.restore_addr + rinfo.restore_size - MB;

#ifdef __arm__
/* This delay loop was required for:
 *    ARM v7 (rev 3, v71), SAMSUNG EXYNOS5 (Flattened Device Tree)
 *    gcc-4.8.1 (Ubuntu pre-release for 14.04) ; Linux 3.13.0+ #54
 */
{int x = 10000000;
int y = 1000000000;
for (; x>0; x--) for (; y>0; y--);
}
#endif

#if 0
  RMB; // refresh instruction cache, for new memory
  WMB; // refresh instruction cache, for new memory
  IMB; // refresh instruction cache, for new memory
#endif

  DPRINTF("We have copied mtcp_restart to higher address.  We will now\n"
          "    jump into a copy of restorememoryareas().\n");

#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(mov %0,%%esp;)
                /* This next assembly language confuses gdb.  Set a future
                   future breakpoint, or attach after this point, if in gdb.
		   It's here to force a hard error ealry , in case of a bug.*/
                CLEAN_FOR_64_BIT(xor %%ebp,%%ebp)
                : : "g" (stack_ptr) : "memory");
#elif defined(__arm__)
  asm volatile ("mov sp,%0\n\t"
                : : "r" (stack_ptr) : "memory");
  /* If we're going to have an error, force a hard error early, to debug. */
  asm volatile ("mov fp,#0\n\tmov ip,#0\n\tmov lr,#0" : : );
#else
# error "assembly instruction not translated"
#endif

  /* IMPORTANT:  We just changed stack pointers.  The call frame for this
   * function is no longer available.  The only way to pass rinfo into
   * the next function is by passing a pointer to a global variable.
   * We call restorememoryareas_fptr(), which points to the copy of the
   * the function in higher memory.  We will be unmapping the original fnc.
   */
  rinfo.restorememoryareas_fptr(&rinfo);
}

__attribute__((optimize(0)))
static void restart_slow_path()
{
  restorememoryareas(&rinfo);
}

static void mtcp_simulateread(int fd)
{
  int mtcp_sys_errno;
  Area area;
  MTCP_PRINTF("Listing ckpt image area:\n");
  while(1) {
    mtcp_readfile(fd, &area, sizeof area);
    if (area.size == -1) break;
    if ((area.prot & MTCP_PROT_ZERO_PAGE) == 0) {
      void *addr = mtcp_sys_mmap(0, area.size, PROT_WRITE | PROT_READ,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (addr == MAP_FAILED) {
        MTCP_PRINTF("***Error: mmap failed: %d\n", mtcp_sys_errno);
        mtcp_abort();
      }
      mtcp_readfile(fd, addr, area.size);
      if (mtcp_sys_munmap(addr, area.size) == -1) {
        MTCP_PRINTF("***Error: munmap failed: %d\n", mtcp_sys_errno);
        mtcp_abort();
      }
    }

    mtcp_printf("%p-%p %c%c%c%c "
               // "%x %u:%u %u"
                "          %s\n",
                area.addr, area.addr + area.size,
                ( area.prot & PROT_READ  ? 'r' : '-' ),
                ( area.prot & PROT_WRITE ? 'w' : '-' ),
                ( area.prot & PROT_EXEC  ? 'x' : '-' ),
                ( area.flags & MAP_SHARED ? 's'
                  : ( area.flags & MAP_ANONYMOUS ? 'p' : '-' ) ),
                //area.offset, area.devmajor, area.devminor, area.inodenum,
                area.name);
  }
}

__attribute__((optimize(0)))
static void restorememoryareas(RestoreInfo *rinfo_ptr)
{
  int mtcp_sys_errno;

  DPRINTF("Entering copy of restorememoryareas().  Will now unmap old memory"
          "\n    and restore memory sections from the checkpoint image.\n");

  DPRINTF("DPRINTF may fail when we unmap, since strings are in rodata.\n"
          "But we may be lucky if the strings have been cached by the O/S\n"
          "or if compiler uses relative addressing for rodata with -fPIC\m");

  int rc;
  VA holebase;
  VA highest_va;
  VA vdso_addr = NULL, vsyscall_addr = NULL, stack_end_addr = NULL;
  vdso_addr = vsyscall_addr = stack_end_addr = 0;
  // Compute these now while it's safe; Use values later.
  highest_va = highest_userspace_address(&vdso_addr, &vsyscall_addr,
                                         &stack_end_addr);

  if (rinfo_ptr->use_gdb) {
    MTCP_PRINTF("Called with --use-gdb.  A useful command is:\n"
            "    (gdb) info proc mapping");
    if (rinfo_ptr->text_offset != -1) {
      MTCP_PRINTF("Called with --text-offset 0x%x.  A useful command is:\n"
              "(gdb) add-symbol-file ../../bin/mtcp_restart %p\n",
              rinfo_ptr->text_offset,
              rinfo_ptr->restore_addr + rinfo_ptr->text_offset);
#if defined(__i386__) || defined(__x86_64__)
      asm volatile ("int3"); // Do breakpoint; send SIGTRAP, caught by gdb
#else
      MTCP_PRINTF("IN GDB: interrupt (^C); add-symbol-file ...; (gdb) print x=0\n");
      { int x = 1; while (x); } // Stop execution for user to type command.
#endif
    }
  }

  RestoreInfo restore_info;
  mtcp_sys_memcpy(&restore_info, rinfo_ptr, sizeof (restore_info));


  /* Unmap everything except for this image as everything we need
   *   is contained in the libmtcp.so image.
   * Unfortunately, in later Linuxes, it's important also not to wipe
   *   out [vsyscall] if it exists (we may not have permission to remove it).
   *   In any case, [vsyscall] is the highest section if it exists.
   * Further, if the [vdso] when we restart is different from the old
   *   [vdso] that was saved at checkpoint time, then we need to keep
   *   both of them.  The old one may be needed if we're returning from
   *   a system call at checkpoint time.  The new one is needed for future
   *   system calls.
   * Highest_userspace_address is determined heuristically.  Primarily, it
   *   was intended to make sure we don't overwrite [vdso] or [vsyscall].
   *   But it was heuristically chosen as a constant (works for earlier
   *   Linuxes), or as the end of stack.  Probably, we should review that,
   *   and just make it beginning of [vsyscall] where that exists.
   */

  holebase = restore_info.restore_addr;
  // WAS: holebase  = mtcp_shareable_begin;
  // WAS: holebase = (VA)((unsigned long int)holebase & -MTCP_PAGE_SIZE);
  // The unmaps will wipe what it points to anyway.
  // Force a hard error if any code tries to use the thread-pointer register.
#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(xor %%eax,%%eax ; movw %%ax,%%fs)
				: : : CLEAN_FOR_64_BIT(eax));
#elif defined(__arm__)
  mtcp_sys_kernel_set_tls(0);  /* Uses 'mcr', a kernel-mode instr. on ARM */
#endif

  // so make sure we get a hard failure just in case
  // ... it's left dangling on something I want
  // asm volatile (CLEAN_FOR_64_BIT(xor %%eax,%%eax ; movw %%ax,%%gs)
  //                                : : : CLEAN_FOR_64_BIT(eax));

  /* Unmap from address 0 to holebase, except for [vdso] section */
  if (stack_end_addr == 0) /* 0 means /proc/self/maps doesn't mark "[stack]" */
    highest_va = HIGHEST_VA;
  else
    highest_va = stack_end_addr;
  // DPRINTF("new_brk (end of heap): %\n", new_brk);
  DPRINTF("holebase (libmtcp.so): %p,\n"
          " stack_end_addr: %p, vdso_addr: %p, highest_va: %p,\n"
          " vsyscall_addr: %p\n",
          holebase, stack_end_addr,
          vdso_addr, highest_va, vsyscall_addr);

  if (vdso_addr != NULL && vdso_addr < holebase) {
    DPRINTF("unmapping %p..%p, %p..%p\n",
            NULL, vdso_addr-1, vdso_addr+MTCP_PAGE_SIZE, holebase - 1);
    rc = mtcp_sys_munmap (NULL, (size_t)vdso_addr);
    rc |= mtcp_sys_munmap (vdso_addr + MTCP_PAGE_SIZE,
			   holebase - vdso_addr - MTCP_PAGE_SIZE);
  } else {
    DPRINTF("unmapping 0..%p\n", holebase - 1);
    rc = mtcp_sys_munmap (NULL, holebase);
  }
  if (rc == -1) {
      MTCP_PRINTF("error %d unmapping from 0 to %p\n",
                  mtcp_sys_errno, holebase);
      mtcp_abort ();
  }

  /* Unmap from address holebase to highest_va, except for [vdso] section */
  /* Value of mtcp_shareable_end (end of data segment) can change from before */
  holebase = restore_info.restore_addr + restore_info.restore_size;
  /* restore_info.highest_va had bugs for ARM, for older Linuxes
   * (incl. 2.6.18, and maybe 2.6.31 under Ubuntu 9.04).  So, we're reverting to
   * highest_userspace_address(), with years of experience on various distros.
   */
  if (vdso_addr != NULL && vdso_addr + MTCP_PAGE_SIZE <= highest_va) {
    if (vdso_addr > holebase) {
      DPRINTF("unmapping %p..%p, %p..%p\n",
              holebase, vdso_addr-1, vdso_addr + MTCP_PAGE_SIZE,
              highest_va - 1);
      rc = mtcp_sys_munmap (holebase, vdso_addr - holebase);
      rc |= mtcp_sys_munmap (vdso_addr + MTCP_PAGE_SIZE,
                             highest_va - vdso_addr - MTCP_PAGE_SIZE);
    } else {
      DPRINTF("unmapping %p..%p\n", holebase, highest_va - 1);
      if (highest_va < holebase) {
        MTCP_PRINTF("error unmapping: highest_va(%p) < holebase(%p)\n",
                    highest_va, holebase);
        mtcp_abort ();
      }
      rc = mtcp_sys_munmap (holebase, highest_va - holebase);
    }
  }
  if (rc == -1) {
      MTCP_PRINTF("error %d unmapping from %p by %p bytes\n",
                  mtcp_sys_errno, holebase, highest_va - holebase);
      mtcp_abort ();
  }
  DPRINTF("\n"); /* end of munmap */

  /* Restore memory areas */
  DPRINTF("restoring memory areas\n");
  readmemoryareas (restore_info.fd);

  /* Everything restored, close file and finish up */

  DPRINTF("close cpfd %d\n", restore_info.fd);
  mtcp_sys_close (restore_info.fd);

  //IMB; // flush instruction cache, since mtcp_restart.c code is now gone.
  DPRINTF("restore complete, resuming by jumping to %p...\n",
          restore_info.post_restart);

  /* Restore libc */
  restore_libc(&restore_info.motherofall_tls_info, restore_info.tls_pid_offset,
               restore_info.tls_tid_offset, restore_info.myinfo_gs);

  /* Jump to finishrestore in original program's libmtcp.so image.
   * This is in libdmtcp.so, and is called: restore_libc.c:TLSInfo_PostRestart
   */
  restore_info.post_restart();
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

__attribute__((optimize(0)))
static void readmemoryareas(int fd)
{
  int mtcp_sys_errno;
  Area area;
  int flags, imagefd;
  void *mmappedat;

  while (1) {
    int try_skipping_existing_segment = 0;
    mtcp_readfile(fd, &area, sizeof area);
    if (area.size == -1) break;

    if (area.name && mtcp_strstr(area.name, "[heap]")
        && mtcp_sys_brk(NULL) != area.addr + area.size) {
      DPRINTF("WARNING: break (%p) not equal to end of heap (%p)\n",
              mtcp_sys_brk(NULL), area.addr + area.size);
    }

    if ((area.flags & MAP_ANONYMOUS) && (area.flags & MAP_SHARED)) {
      MTCP_PRINTF("\n\n*** WARNING:  Next area specifies MAP_ANONYMOUS"
		  "  and MAP_SHARED.\n"
		  "*** Turning off MAP_ANONYMOUS and hoping for best.\n\n");
    }

    if ((area.prot & MTCP_PROT_ZERO_PAGE) != 0) {
      DPRINTF("restoring non-rwx anonymous area, %p bytes at %p\n",
              area.size, area.addr);
      mmappedat = mtcp_sys_mmap (area.addr, area.size,
                                 area.prot & ~MTCP_PROT_ZERO_PAGE,
                                 area.flags | MAP_FIXED, -1, 0);

      if (mmappedat != area.addr) {
        DPRINTF("error %d mapping %p bytes at %p\n",
                mtcp_sys_errno, area.size, area.addr);
        mtcp_abort ();
      }
    }

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

      if (area.flags & MAP_ANONYMOUS) {
        DPRINTF("restoring anonymous area, %p  bytes at %p\n",
                area.size, area.addr);
      } else {
        DPRINTF("restoring to non-anonymous area from anonymous area,"
                " %p bytes at %p from %s + 0x%X\n",
                area.size, area.addr, area.name, area.offset);
      }
      imagefd = -1;
      if (area.name[0] == '/') { /* If not null string, not [stack] or [vdso] */
        imagefd = mtcp_sys_open (area.name, O_RDONLY, 0);
        if (imagefd >= 0)
          area.flags ^= MAP_ANONYMOUS;
      }

      /* Create the memory area */

      /* POSIX says mmap would unmap old memory.  Munmap never fails if args
       * are valid.  Can we unmap vdso and vsyscall in Linux?  Used to use
       * mtcp_safemmap here to check for address conflicts.
       */
      mmappedat = mtcp_sys_mmap (area.addr, area.size, area.prot | PROT_WRITE,
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
        mtcp_abort ();
      }

      if (imagefd >= 0)
        adjust_for_smaller_file_size(&area, imagefd);

      /* Close image file (fd only gets in the way) */
      if (!(area.flags & MAP_ANONYMOUS)) mtcp_sys_close (imagefd);

      if (try_skipping_existing_segment) {
        // This fails on teracluster.  Presumably extra symbols cause overflow.
        mtcp_skipfile(fd, area.size);
      } else {
        /* This mmapfile after prev. mmap is okay; use same args again.
         *  Posix says prev. map will be munmapped.
         */
        /* ANALYZE THE CONDITION FOR DOING mmapfile MORE CAREFULLY. */
        mtcp_readfile(fd, area.addr, area.size);
        if (!(area.prot & PROT_WRITE)) {
          if (mtcp_sys_mprotect (area.addr, area.size, area.prot) < 0) {
            MTCP_PRINTF("error %d write-protecting %p bytes at %p\n",
                        mtcp_sys_errno, area.size, area.addr);
            mtcp_abort ();
          }
        }
      }
    }

    /* CASE NOT MAP_ANONYMOUS:
     * Otherwise, we mmap the original file contents to the area
     */

    else {
      DPRINTF("restoring mapped area, %p bytes at %p to %s + 0x%X\n",
              area.size, area.addr, area.name, area.offset);
      flags = 0;            // see how to open it based on the access required
      // O_RDONLY = 00
      // O_WRONLY = 01
      // O_RDWR   = 02
      if (area.prot & PROT_WRITE) flags = O_WRONLY;
      if (area.prot & (PROT_EXEC | PROT_READ)){
        flags = O_RDONLY;
        if (area.prot & PROT_WRITE) flags = O_RDWR;
      }

      if (area.prot & MAP_SHARED) {
        read_shared_memory_area_from_file(fd, &area, flags);
      } else { /* not MAP_ANONYMOUS, not MAP_SHARED */
        /* During checkpoint, MAP_ANONYMOUS flag is forced whenever MAP_PRIVATE
         * is set. There is no reason for any mapping to have MAP_PRIVATE and
         * not have MAP_ANONYMOUS at this point. (MAP_ANONYMOUS is handled
         * earlier in this function.
         */
        MTCP_PRINTF("Unreachable. MAP_PRIVATE implies MAP_ANONYMOUS\n");
        mtcp_abort();
      }
    }
  }
#if __arm__
  /* On ARM, with gzip enabled, we sometimes see SEGFAULT without this.
   * The SEGFAULT occurs within the initial thread, before any user threads
   * are unblocked.  WHY DOES THIS HAPPEN?
   */
  WMB;
#endif
}

__attribute__((optimize(0)))
static void adjust_for_smaller_file_size(Area *area, int fd)
{
  int mtcp_sys_errno;
  off_t curr_size = mtcp_sys_lseek(fd, 0, SEEK_END);
  if (curr_size == -1) return;
  if (curr_size < area->filesize && (area->offset + area->size > curr_size)) {
    size_t diff_in_size = (area->offset + area->size) - curr_size;
    size_t anon_area_size = (diff_in_size + MTCP_PAGE_SIZE - 1)
                             & MTCP_PAGE_MASK;
    VA anon_start_addr = area->addr + (area->size - anon_area_size);

    DPRINTF("For %s, current size (%ld) smaller than original (%ld).\n"
            "mmap()'ng the difference as anonymous.\n",
            area->name, curr_size, area->size);
    VA mmappedat = mtcp_sys_mmap (anon_start_addr, anon_area_size,
                                  area->prot | PROT_WRITE,
                                  MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS,
                                  -1, 0);

    if (mmappedat == MAP_FAILED) {
      DPRINTF("error %d mapping %p bytes at %p\n",
              mtcp_sys_errno, anon_area_size, anon_start_addr);
    }
    if (mmappedat != anon_start_addr) {
      MTCP_PRINTF("area at %p got mmapped to %p\n", anon_start_addr, mmappedat);
      mtcp_abort ();
    }
  }
}

/*
 * CASE NOT MAP_ANONYMOUS, MAP_SHARED :
 *
 * If the shared file does NOT exist on the system, the restart process creates
 * the file on the disk and writes the contents from the ckpt image into this
 * recreated file. The file is later mapped into memory with MAP_SHARED and
 * correct protection flags.
 *
 * If the file already exists on the disk, there are two possible scenarios as
 * follows:
 * 1. The shared memory has WRITE access: In this case it is possible that the
 *    file was modified by the checkpoint process and so we restore the file
 *    contents from the checkpoint image. In doing so, we can fail however if
 *    we do not have sufficient access permissions.
 * 2. The shared memory has NO WRITE access: In this case, we use the current
 *    version of the file rather than the one that existed at checkpoint time.
 *    We map the file with correct flags and discard the checkpointed copy of
 *    the file contents.
 *
 * Other than these, if we can't access the file, we print an error message
 * and quit.
 */
__attribute__((optimize(0)))
static void read_shared_memory_area_from_file(int fd, Area* area, int flags)
{
  int mtcp_sys_errno;
  void *mmappedat;
  int areaContentsAlreadyRead = 0;
  int imagefd, rc;
  char *area_name = area->name; /* Modified in fix_filename_if_new_cwd below. */

  if (!(area->prot & MAP_SHARED)) {
    MTCP_PRINTF("Illegal function call\n");
    mtcp_abort();
  }

  /* Check to see if the filename ends with " (deleted)" */
  if (mtcp_strendswith(area_name, DELETED_FILE_SUFFIX)) {
    area_name[ mtcp_strlen(area_name) - mtcp_strlen(DELETED_FILE_SUFFIX) ] =
      '\0';
  }

  imagefd = mtcp_sys_open (area_name, flags, 0);  // open it

  if (imagefd < 0 && mtcp_sys_errno != ENOENT) {
    MTCP_PRINTF("error %d opening mmap file %s with flags:%d\n",
                mtcp_sys_errno, area_name, flags);
    mtcp_abort();
  }

  if (imagefd < 0) {
    // If the shared file doesn't exist on the disk, we try to create it
    DPRINTF("Shared file %s not found. Creating new\n", area_name);

    /* Dangerous for DMTCP:  Since file is created with O_CREAT,
     * hopefully, a second process should ignore O_CREAT and just
     *  duplicate the work of the first process, with no ill effect.
     */
    //area_name = fix_filename_if_new_cwd(area_name);
    imagefd = open_shared_file(area_name);

    /* Acquire write lock on the file before writing anything to it
     * If we don't, then there is a weird RACE going on between the
     * restarting processes which causes problems with mmap()ed area for
     * this file and hence the restart fails. We still don't know the
     * reason for it.                                       --KAPIL
     * NOTE that we don't need to unlock the file as it will be
     * automatically done when we close it.
     */
    DPRINTF("Acquiring write lock on shared file :%s\n", area_name);
    lock_file(imagefd, area_name, F_WRLCK);
    DPRINTF("After Acquiring write lock on shared file :%s\n", area_name);

    // Create a temp area in the memory exactly of the size of the
    // shared file.  We read the contents of the shared file from
    // checkpoint file(.mtcp) into system memory. From system memory,
    // the contents are written back to newly created replica of the shared
    // file (at the same path where it used to exist before checkpoint).
    // Note that we overwrite the contents of the shared file with
    // the original contents from the checkpoint image.
    mmappedat = mtcp_sys_mmap (area->addr, area->size, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mmappedat == MAP_FAILED) {
      MTCP_PRINTF("error %d mapping temp memory at %p\n",
                  mtcp_sys_errno, area->addr);
      mtcp_abort ();
    }

    // Overwrite mmap'ed memory region with contents from original ckpt image.
    mtcp_readfile(fd, area->addr, area->size);

    areaContentsAlreadyRead = 1;

    if ( mtcp_sys_write(imagefd, area->addr,area->size) < 0 ){
      MTCP_PRINTF("error %d creating mmap file %s\n",
                  mtcp_sys_errno, area_name);
      mtcp_abort();
    }

    // unmap the temp memory allocated earlier
    rc = mtcp_sys_munmap (area->addr, area->size);
    if (rc == -1) {
      MTCP_PRINTF("error %d unmapping temp memory at %p\n",
                  mtcp_sys_errno, area->addr);
      mtcp_abort ();
    }

    // set file permissions as per memory area protection.
    // UPDATE: Always assign 0600 permission. Some other process might have
    // mapped the file with RW access.
    int fileprot = S_IRUSR | S_IWUSR;
    if (area->prot & PROT_EXEC)  fileprot |= S_IXUSR;
    mtcp_sys_fchmod(imagefd, fileprot);

    //close the file
    mtcp_sys_close(imagefd);

    // now open the file again, this time with appropriate flags
    imagefd = mtcp_sys_open (area_name, flags, 0);
    if (imagefd < 0){
      MTCP_PRINTF("error %d opening mmap file %s\n", mtcp_sys_errno, area_name);
      mtcp_abort ();
    }
  } else { /* else file exists */
    /* This prevents us writing to an mmap()ed file whose length is smaller
     * than the region in memory.  This occurred when checkpointing from within
     * Open MPI.
     */
    int file_size = mtcp_sys_lseek(imagefd, 0, SEEK_END);
    if (area->size > (file_size + MTCP_PAGE_SIZE)) {
      DPRINTF("File size (%d) on disk smaller than mmap() size (%d).\n"
              "  Extending it to mmap() size.\n", file_size, area->size);
      mtcp_sys_ftruncate(imagefd, area->size);
    }

    /* Acquire read lock on the shared file before doing an mmap. See
     * detailed comments above.
     */
    DPRINTF("Acquiring read lock on shared file :%s\n", area_name);
    lock_file(imagefd, area_name, F_RDLCK);
    DPRINTF("After Acquiring read lock on shared file :%s\n", area_name);
  }

  mmappedat = mtcp_sys_mmap (area->addr, area->size, area->prot,
                             area->flags, imagefd, area->offset);
  if (mmappedat == MAP_FAILED) {
    MTCP_PRINTF("error %d mapping %s offset %d at %p\n",
                 mtcp_sys_errno, area_name, area->offset, area->addr);
    mtcp_abort ();
  }
  if (mmappedat != area->addr) {
    MTCP_PRINTF("area at %p got mmapped to %p\n", area->addr, mmappedat);
    mtcp_abort ();
  }

  // TODO: Simplify this entire logic.
  if ( areaContentsAlreadyRead == 0 ){
#if 0
    // If we have write permission or execute permission on file,
    //   then we use data in checkpoint image,
    // If MMAP_SHARED, this reverts the file to data at time of checkpoint.
    // In the case of DMTCP, multiple processes may duplicate this work.
    // NOTE: man 2 access: access  may  not  work  correctly on NFS file
    //   systems with UID mapping enabled, because UID mapping is done
    //   on the server and hidden from the client, which checks permissions.
    /* if (flags == O_WRONLY || flags == O_RDWR) */
    if ( ( (imagefd = mtcp_sys_open(area->name, O_WRONLY, 0)) >= 0
          && ( (flags == O_WRONLY || flags == O_RDWR) ) )
        || (0 == mtcp_sys_access(area->name, X_OK)) ) {

      MTCP_PRINTF("mapping %s with data from ckpt image\n", area->name);
      mtcp_readfile(fd, area->addr, area->size);
      mtcp_sys_close (imagefd); // don't leave dangling fd
    }
#else
    if (area->prot & PROT_WRITE) {
      if (mtcp_strstr(area->name, "openmpi-sessions") != NULL) {
        DPRINTF("mapping %s with data from ckpt image\n", area->name);
      } else {
        MTCP_PRINTF("mapping %s with data from ckpt image\n", area->name);
      }
      mtcp_readfile(fd, area->addr, area->size);
    }
#endif
    // If we have no write permission on file, then we should use data
    //   from version of file at restart-time (not from checkpoint-time).
    // Because Linux library files have execute permission,
    //   the dynamic libraries from time of checkpoint will be used.

    // If we haven't created the file (i.e. the shared file _does_ exist
    // when this process wants to map it) and the memory area does not have
    // WRITE access, we want to skip the checkpoint
    // file pointer and move to the end of the shared file. We can not
    // use lseek() function as it can fail if we are using a pipe to read
    // the contents of checkpoint file (we might be using gzip to
    // uncompress checkpoint file on the fly). Thus we have to read or
    // skip contents using the following code.

    // NOTE: man 2 access: access  may  not  work  correctly on NFS file
    //   systems with UID mapping enabled, because UID mapping is done
    //   on the server and hidden from the client, which checks permissions.
    else {
      /* read-exec permission ==> executable library, unlikely to change;
       * For example, gconv-modules.cache is shared with read-exec perm.
       * If read-only permission, warn user that we're using curr. file.
       */
      if (imagefd >= 0 && -1 == mtcp_sys_access(area->name, X_OK)) {
        if (mtcp_strstartswith(area->name, "/usr/") ||
            mtcp_strstartswith(area->name, "/var/")) {
          DPRINTF("mapping current version of %s into memory;\n"
                  "  _not_ file as it existed at time of checkpoint.\n"
                  "  Change %s:%d and re-compile, if you want different "
                  "behavior.\n",
                  area->name, __FILE__, __LINE__);
        } else {
          MTCP_PRINTF("mapping current version of %s into memory;\n"
                      "  _not_ file as it existed at time of checkpoint.\n"
                      "  Change %s:%d and re-compile, if you want different "
                      "behavior. %d: %d\n",
                      area->name, __FILE__, __LINE__);
        }
      }
      mtcp_skipfile(fd, area->size);
    }
  }
  if (imagefd >= 0)
    mtcp_sys_close (imagefd); // don't leave dangling fd in way of other stuff
}

/* Modelled after mtcp_safemmap.  - Gene */
static VA highest_userspace_address (VA *vdso_addr, VA *vsyscall_addr,
				     VA *stack_end_addr)
{
    int mtcp_sys_errno;
    char c;
    int mapsfd, i;
    VA endaddr, startaddr;
    VA highaddr = 0; /* high stack address should be highest userspace addr */
    const char *stackstring = "[stack]";
    const char *vdsostring = "[vdso]";
    const char *vsyscallstring = "[vsyscall]";
    const int bufsize = 1 + sizeof "[vsyscall]"; /* largest of last 3 strings */
    char buf[bufsize];

    buf[0] = '\0';
    buf[bufsize - 1] = '\0';

    /* Scan through the mappings of this process */

    mapsfd = mtcp_sys_open ("/proc/self/maps", O_RDONLY, 0);
    if (mapsfd < 0) {
	MTCP_PRINTF("couldn't open /proc/self/maps; error %d\n",
                    mtcp_sys_errno);
	mtcp_abort();
    }

    *vdso_addr = NULL;
    while (1) {

	/* Read a line from /proc/self/maps */

	c = mtcp_readhex (mapsfd, &startaddr);
	if (c == '\0') break;
	if (c != '-') continue; /* skip to next line */
	c = mtcp_readhex (mapsfd, &endaddr);
	if (c == '\0') break;
	if (c != ' ') continue; /* skip to next line */

	while ((c != '\0') && (c != '\n')) {
	    if (c != ' ') {
		for (i = 0; (i < bufsize) && (c != ' ')
			 && (c != 0) && (c != '\n'); i++) {
		    buf[i] = c;
		    c = mtcp_readchar (mapsfd);
		}
	    } else {
		c = mtcp_readchar (mapsfd);
	    }
	}

	if (0 == mtcp_strncmp(buf, stackstring, mtcp_strlen(stackstring))) {
	    *stack_end_addr = endaddr;
	    highaddr = endaddr;  /* We found "[stack]" in /proc/self/maps */
	}

	if (0 == mtcp_strncmp(buf, vdsostring, mtcp_strlen(vdsostring))) {
	    *vdso_addr = startaddr;
	    highaddr = endaddr;  /* We found "[vdso]" in /proc/self/maps */
	}

	if (0 == mtcp_strncmp(buf, vsyscallstring, mtcp_strlen(vsyscallstring))) {
	    *vsyscall_addr = startaddr;
	    highaddr = endaddr;  /* We found "[vsyscall]" in /proc/self/maps */
	}
    }

    mtcp_sys_close (mapsfd);

    return highaddr;
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
void restore_libc(ThreadTLSInfo *tlsInfo, int tls_pid_offset,
                  int tls_tid_offset, MYINFO_GS_T myinfo_gs)
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
   * This will make different kernel calls according to the CPU architecture. */
  if (tls_set_thread_area (&(tlsInfo->gdtentrytls[0]), myinfo_gs) != 0) {
    MTCP_PRINTF("Error restoring GDT TLS entry: %d\n", mtcp_sys_errno);
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
#endif
}

/*****************************************************************************
 *
 *****************************************************************************/
__attribute__((optimize(0)))
static void lock_file(int fd, char* name, short l_type)
{
  int mtcp_sys_errno;
  struct flock fl;

  /* If a file is in /var or /usr, we shouldn't try to restore
   * the old version.  More generally, we should not try to restore a file
   * which is mapped MAP_SHARED and for which we don't have write permission.
   * But /var and /usr/ should cover almost all of those cases.
   *     Locking of files in /var and /usr will fail if a daemon has a write
   * lock on them.  This occurs for * /var/lib/sss/mc/passwd and sssd daemon,
   * as seen with openmpi's mpirun.  At ckpt time, no one had tried to save the
   * old contents, since we didn't have write permission.  But at restart time,
   * each process blocks on a read lock anyway, in case some DMTCP clients has
   * a write lock and is restoring the old contents.  If the daemon has a
   * write lock, we will hang unless we return here, without the read lock.
   */
  if (mtcp_strstartswith(name, "/usr/") ||
      mtcp_strstartswith(name, "/var/"))
    return;

  fl.l_type   = l_type;   /* F_RDLCK, F_WRLCK, F_UNLCK    */
  fl.l_whence = SEEK_SET; /* SEEK_SET, SEEK_CUR, SEEK_END */
  fl.l_start  = 0;        /* Offset from l_whence         */
  fl.l_len    = 0;        /* length, 0 = to EOF           */

  int result;
  do {
    /* F_GETLK, F_SETLK, F_SETLKW */
    result = mtcp_sys_fcntl3(fd, F_SETLKW, &fl);
  } while (result == -1 && mtcp_sys_errno == EINTR);

  /* Coverity static analyser stated the following code as DEAD. It is not
   * DEADCODE because it is possible that mtcp_sys_fcntl3() fails with some
   * error other than EINTR
   */
  if ( result == -1 ) {
    MTCP_PRINTF("error %d locking shared file: %s\n", mtcp_sys_errno, name);
    mtcp_abort();
  }
}

#if 0
/* Adjust for new pwd, and recreate any missing subdirectories. */
static char* fix_filename_if_new_cwd(char* filename)
{
  int i;
  int fIndex;
  int errorFilenameFromPreviousCwd = 0;
  char currentFolder[FILENAMESIZE];

  /* Find the starting index where the actual filename begins */
  for ( i=0; filename[i] != '\0'; i++ ){
    if ( filename[i] == '/' )
      fIndex = i+1;
  }

  /* We now try to create the directories structure from the given path */
  for ( i=0 ; i<fIndex ; i++ ){
    if (filename[i] == '/' && i > 0){
      int res;
      currentFolder[i] = '\0';
      res = mtcp_sys_mkdir(currentFolder, S_IRWXU);
      if (res<0 && mtcp_sys_errno != EEXIST ){
        if (mtcp_strstartswith(filename, mtcp_saved_working_directory)) {
          errorFilenameFromPreviousCwd = 1;
          break;
        }
        MTCP_PRINTF("error %d creating directory %s in path of %s\n",
		    mtcp_sys_errno, currentFolder, filename);
	mtcp_abort();
      }
    }
    currentFolder[i] = filename[i];
  }

  /* If filename began with previous cwd and wasn't found there,
   * then let's try creating in current cwd (current working directory).
   */
  if (errorFilenameFromPreviousCwd) {
    int prevCwdLen;
    i=mtcp_strlen(mtcp_saved_working_directory);
    while (filename[i] == '/')
      i++;
    prevCwdLen = i;
    for ( i=prevCwdLen ; i<fIndex ; i++ ){
      if (filename[i] == '/'){
        int res;
        currentFolder[i-prevCwdLen] = '\0';
        res = mtcp_sys_mkdir(currentFolder, S_IRWXU);
        if (res<0 && mtcp_sys_errno != EEXIST ){
          MTCP_PRINTF("error %d creating directory %s in path of %s in cwd\n",
		      mtcp_sys_errno, currentFolder, filename);
	  mtcp_abort();
        }
      }
      currentFolder[i-prevCwdLen] = filename[i];
    }
    filename = filename + prevCwdLen;  /* Now filename is relative filename. */
  }
  return filename;
}
#endif

__attribute__((optimize(0)))
static int open_shared_file(char* filename)
{
  int mtcp_sys_errno;
  int fd;
  /* Create the file */
  fd = mtcp_sys_open(filename, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR);
  if (fd<0){
    MTCP_PRINTF("unable to create file %s\n", filename);
    mtcp_abort();
  }
  return fd;
}

#if 0
// Not currently used
__attribute__((optimize(0)))
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

__attribute__((optimize(0)))
static int doAreasOverlap(VA addr1, size_t size1, VA addr2, size_t size2)
{
  VA end1 = (char*)addr1 + size1;
  VA end2 = (char*)addr2 + size2;
  return (addr1 >= addr2 && addr1 < end2) || (addr2 >= addr1 && addr2 < end1);
}

__attribute__((optimize(0)))
static int hasOverlappingMapping(VA addr, size_t size)
{
  int mtcp_sys_errno;
  int ret = 0;
  Area area;
  int mapsfd = mtcp_sys_open2("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0) {
    MTCP_PRINTF("error opening /proc/self/maps: errno: %d\n", mtcp_sys_errno);
    mtcp_abort ();
  }

  while (mtcp_readmapsline(mapsfd, &area)) {
    if (doAreasOverlap(addr, size, area.addr, area.size)) {
      ret = 1;
      break;
    }
  }
  mtcp_sys_close (mapsfd);
  return ret;
}

__attribute__((optimize(0)))
static void getMiscAddrs(VA *text_addr, size_t *size, VA *highest_va)
{
  int mtcp_sys_errno;
  Area area;
  VA area_end = NULL;
  VA this_fn = (VA) &getMiscAddrs;
  int mapsfd = mtcp_sys_open2("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0) {
    MTCP_PRINTF("error opening /proc/self/maps: errno: %d\n", mtcp_sys_errno);
    mtcp_abort ();
  }

  while (mtcp_readmapsline(mapsfd, &area)) {
    if ((mtcp_strendswith(area.name, BINARY_NAME) ||
         mtcp_strendswith(area.name, BINARY_NAME_M32)) &&
        (area.prot & PROT_EXEC) &&
        /* On ARM/Ubuntu 14.04, mtcp_restart is mapped twice with RWX
         * permissions. Not sure why? Here is an example:
         *
         * 00008000-00010000 r-xp 00000000 b3:02 144874     .../bin/mtcp_restart
         * 00017000-00019000 rwxp 00007000 b3:02 144874     .../bin/mtcp_restart
         * befdf000-bf000000 rwxp 00000000 00:00 0          [stack]
         * ffff0000-ffff1000 r-xp 00000000 00:00 0          [vectors]
         */
        (area.addr < this_fn && (area.addr + area.size) > this_fn)) {
      *text_addr = area.addr;
      *size = area.size;
    }
    if (mtcp_strstr (area.name, "[vsyscall]") != NULL) {
      /* vsyscall is highest section, when it exists */
      break;
    }
    area_end = area.addr + area.size;
  }
  mtcp_sys_close (mapsfd);
  *highest_va = area_end;
}

// gcc can generate calls to these.
// Eventually, we'll isolate the PIC code in a library, and this can go away.
void __stack_chk_fail(void)
{
  int mtcp_sys_errno;
  MTCP_PRINTF("ERROR: Stack Overflow detected.\n");
  mtcp_abort();
}

void __stack_chk_fail_local(void)
{
  int mtcp_sys_errno;
  MTCP_PRINTF("ERROR: Stack Overflow detected.\n");
  mtcp_abort();
}

void __stack_chk_guard(void)
{
  int mtcp_sys_errno;
  MTCP_PRINTF("ERROR: Stack Overflow detected.\n");
  mtcp_abort();
}

void _Unwind_Resume(void)
{
  int mtcp_sys_errno;
  MTCP_PRINTF("MTCP Internal Error: %s Not Implemented.\n", __FUNCTION__);
  mtcp_abort();
}

void __gcc_personality_v0(void)
{
  int mtcp_sys_errno;
  MTCP_PRINTF("MTCP Internal Error: %s Not Implemented.\n", __FUNCTION__);
  mtcp_abort();
}

void __intel_security_cookie(void)
{
  int mtcp_sys_errno;
  MTCP_PRINTF("MTCP Internal Error: %s Not Implemented.\n", __FUNCTION__);
  mtcp_abort();
}

void __intel_security_check_cookie(void)
{
  int mtcp_sys_errno;
  MTCP_PRINTF("MTCP Internal Error: %s Not Implemented.\n", __FUNCTION__);
  mtcp_abort();
}
