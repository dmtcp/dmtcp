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
  VA restore_end;
  size_t restore_size;
  VA vdsoStart;
  VA vdsoEnd;
  VA vvarStart;
  VA vvarEnd;
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
static int read_one_memory_area(int fd);
#if 0
// Not currently used
static void mmapfile(int fd, void *buf, size_t size, int prot, int flags);
#endif
static void read_shared_memory_area_from_file(int fd, Area* area, int flags);
static void lock_file(int fd, char* name, short l_type);
static char* fix_parent_directories(char* filename);
// static char* fix_filename_if_new_cwd(char* filename);
static int open_shared_file(char* filename);
static void adjust_for_smaller_file_size(Area *area, int fd);
static void restorememoryareas(RestoreInfo *rinfo_ptr);
static void restore_brk(VA saved_brk, VA restore_begin, VA restore_end);
static void restart_fast_path(void);
static void restart_slow_path(void);
static int doAreasOverlap(VA addr1, size_t size1, VA addr2, size_t size2);
static int hasOverlappingMapping(VA addr, size_t size);
static void getTextAddr(VA *textAddr, size_t *size);
static void mtcp_simulateread(int fd, MtcpHeader *mtcpHdr);
void restore_libc(ThreadTLSInfo *tlsInfo, int tls_pid_offset,
                  int tls_tid_offset, MYINFO_GS_T myinfo_gs);
static void unmap_memory_areas_and_restore_vdso(RestoreInfo *rinfo);


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
  return mtcp_memset(s, c, n);
}

void *memcpy(void *dest, const void *src, size_t n) {
  return mtcp_memcpy(dest, src, n);
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

// TODO(karya0): Remove vDSO checks after 2.4.0-rc3 release, and after testing.
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
#endif

  rinfo.fd = -1;
  rinfo.use_gdb = 0;
  rinfo.text_offset = -1;
  shift;
  while (argc > 0) {
    // Flags for standalone debugging
    if (argc == 1) {
      // We would use MTCP_PRINTF, but it's also for output of util/readdmtcp.sh
      mtcp_printf("Considering '%s' as a ckpt image.\n", argv[0]);
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
    int rc = -1;
    rinfo.fd = mtcp_sys_open2(ckptImage, O_RDONLY);
    if (rinfo.fd == -1) {
      MTCP_PRINTF("***ERROR opening ckpt image (%s); errno: %d\n",
                  ckptImage, mtcp_sys_errno);
      mtcp_abort();
    }
    // This assumes that the MTCP header signature is unique.
    // We repeatedly look for mtcpHdr because the first header will be
    //   for DMTCP.  So, we look deeper for the MTCP header.  The MTCP
    //   header is guaranteed to start on an offset that's an integer
    //   multiple of sizeof(mtcpHdr), which is currently 4096 bytes.
    do {
      rc = mtcp_readfile(rinfo.fd, &mtcpHdr, sizeof mtcpHdr);
    } while (rc > 0 && mtcp_strcmp(mtcpHdr.signature, MTCP_SIGNATURE) != 0);
    if (rc == 0) { /* if end of file */
      MTCP_PRINTF("***ERROR: ckpt image doesn't match MTCP_SIGNATURE\n");
      return 1;  /* exit with error code 1 */
    }
  }

  DPRINTF("For debugging:\n"
          "    (gdb) add-symbol-file ../../bin/mtcp_restart %p\n",
          mtcpHdr.restore_addr + rinfo.text_offset);
  if (rinfo.text_offset == -1)
    DPRINTF("... but add to the above the result, 1 +"
            " `text_offset.sh mtcp_restart`\n    in the mtcp subdirectory.\n");

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
  rinfo.post_restart = mtcpHdr.post_restart;
  rinfo.motherofall_tls_info = mtcpHdr.motherofall_tls_info;
  rinfo.tls_pid_offset = mtcpHdr.tls_pid_offset;
  rinfo.tls_tid_offset = mtcpHdr.tls_tid_offset;
  rinfo.myinfo_gs = mtcpHdr.myinfo_gs;

  restore_brk(rinfo.saved_brk, rinfo.restore_addr,
              rinfo.restore_addr + rinfo.restore_size);
  getTextAddr(&rinfo.text_addr, &rinfo.text_size);
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
      MTCP_PRINTF("***WARNING: munmap failed; errno: %d\n", mtcp_sys_errno);
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
    MTCP_PRINTF("mmap failed with error; errno: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }

  size_t offset = (char*)&restorememoryareas - rinfo.text_addr;
  rinfo.restorememoryareas_fptr = (fnptr_t)(rinfo.restore_addr + offset);
/* For __arm__
 *    should be able to use kernel call: __ARM_NR_cacheflush(start, end, flag)
 *    followed by copying new text below, followed by DSB and ISB,
 *    to eliminstate need for delay loop.  But this needs more testing.
 */
  mtcp_memcpy(rinfo.restore_addr, rinfo.text_addr, rinfo.text_size);
  mtcp_memcpy(rinfo.restore_addr + rinfo.text_size, &rinfo, sizeof(rinfo));
  void *stack_ptr = rinfo.restore_addr + rinfo.restore_size - MB;

#if defined(__INTEL_COMPILER) && defined(__x86_64__)
  memfence();
  asm volatile (CLEAN_FOR_64_BIT(mov %0,%%esp;)
                CLEAN_FOR_64_BIT(xor %%ebp,%%ebp)
                : : "g" (stack_ptr) : "memory");
  // This is copied from gcc assembly output for:
  //     rinfo.restorememoryareas_fptr(&rinfo);
  // Intel icc-13.1.3 output uses register rbp here.  It's no longer available.
  asm volatile(
   "movq    64+rinfo(%%rip), %%rdx;" /* rinfo.restorememoryareas_fptr */
   "leaq    rinfo(%%rip), %%rdi;"    /* &rinfo */
   "movl    $0, %%eax;"
   "call    *%%rdx"
   : : );
  /* NOTREACHED */
#endif

#if defined(__arm__) || defined(__aarch64__)
# if 0
  memfence();
# else
// FIXME: Replace this code by memfence() for __aarch64__, once it is stable.
/* This delay loop was required for:
 *    ARM v7 (rev 3, v71), SAMSUNG EXYNOS5 (Flattened Device Tree)
 *    gcc-4.8.1 (Ubuntu pre-release for 14.04) ; Linux 3.13.0+ #54
 */
{int x = 10000000;
int y = 1000000000;
for (; x>0; x--) for (; y>0; y--);
}
# endif
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
		   It's here to force a hard error early, in case of a bug.*/
                CLEAN_FOR_64_BIT(xor %%ebp,%%ebp)
                : : "g" (stack_ptr) : "memory");
#elif defined(__arm__)
  asm volatile ("mov sp,%0\n\t"
                : : "r" (stack_ptr) : "memory");
  /* If we're going to have an error, force a hard error early, to debug. */
  asm volatile ("mov fp,#0\n\tmov ip,#0\n\tmov lr,#0" : : );
#elif defined(__aarch64__)
  asm volatile ("mov sp,%0\n\t"
                : : "r" (stack_ptr) : "memory");
  /* If we're going to have an error, force a hard error early, to debug. */
  // FIXME:  Add a hard error here in assembly.
#else
# error "assembly instruction not translated"
#endif

  /* IMPORTANT:  We just changed to a new stack.  The call frame for this
   * function on the old stack is no longer available.  The only way to pass
   * rinfo into the next function is by passing a pointer to a global variable
   * We call restorememoryareas_fptr(), which points to the copy of the
   * function in higher memory.  We will be unmapping the original fnc.
   */
  rinfo.restorememoryareas_fptr(&rinfo);
  /* NOTREACHED */
}

__attribute__((optimize(0)))
static void restart_slow_path()
{
  restorememoryareas(&rinfo);
}

// Used by util/readdmtcp.sh
// So, we use mtcp_printf to stdout instead of MTCP_PRINTF (diagnosis for DMTCP)
static void mtcp_simulateread(int fd, MtcpHeader *mtcpHdr)
{
  int mtcp_sys_errno;

  // Print miscellaneous information:
  char buf[MTCP_SIGNATURE_LEN+1];
  mtcp_memcpy(buf, mtcpHdr->signature, MTCP_SIGNATURE_LEN);
  buf[MTCP_SIGNATURE_LEN] = '\0';
  mtcp_printf("\nMTCP: %s", buf);
  mtcp_printf("**** mtcp_restart (will be copied here): %p-%p\n",
            mtcpHdr->restore_addr, mtcpHdr->restore_addr + mtcpHdr->restore_size);
  mtcp_printf("**** DMTCP entry point (ThreadList::postRestart()): %p\n",
              mtcpHdr->post_restart);
  mtcp_printf("**** brk (sbrk(0)): %p\n", mtcpHdr->saved_brk);
  mtcp_printf("**** vdso: %p-%p\n", mtcpHdr->vdsoStart, mtcpHdr->vdsoEnd);
  mtcp_printf("**** vvar: %p-%p\n", mtcpHdr->vvarStart, mtcpHdr->vvarEnd);

  Area area;
  mtcp_printf("\n**** Listing ckpt image area:\n");
  while(1) {
    mtcp_readfile(fd, &area, sizeof area);
    if (area.size == -1) break;
    if ((area.prot & MTCP_PROT_ZERO_PAGE) == 0) {
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
          "or if compiler uses relative addressing for rodata with -fPIC\n");

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
  mtcp_memcpy(&restore_info, rinfo_ptr, sizeof (restore_info));

#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(xor %%eax,%%eax ; movw %%ax,%%fs)
				: : : CLEAN_FOR_64_BIT(eax));
#elif defined(__arm__)
  mtcp_sys_kernel_set_tls(0);  /* Uses 'mcr', a kernel-mode instr. on ARM */
#elif defined(__aarch64__)
# warning __FUNCTION__ "TODO: Implementation for ARM64"
#endif

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
  readmemoryareas (restore_info.fd);

  /* Everything restored, close file and finish up */

  DPRINTF("close cpfd %d\n", restore_info.fd);
  mtcp_sys_close (restore_info.fd);

  IMB; /* flush instruction cache, since mtcp_restart.c code is now gone. */

  /* Restore libc */
  DPRINTF("Memory is now restored.  Will next restore libc internals.\n");
  restore_libc(&restore_info.motherofall_tls_info, restore_info.tls_pid_offset,
               restore_info.tls_tid_offset, restore_info.myinfo_gs);
  /* System calls and libc library calls should now work. */

  DPRINTF("MTCP restore is now complete.  Continuing by jumping to\n"
          "  ThreadList:postRestart() back inside libdmtcp.so: %p...\n",
          restore_info.post_restart);
  restore_info.post_restart();
}

__attribute__((optimize(0)))
static void unmap_memory_areas_and_restore_vdso(RestoreInfo *rinfo)
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
    mtcp_abort ();
  }

  while (mtcp_readmapsline(mapsfd, &area)) {
    if (area.addr >= rinfo->restore_addr && area.addr < rinfo->restore_end) {
      // Do not unmap this restore image.
    } else if (mtcp_strcmp(area.name, "[vdso]") == 0) {
      // Do not unmap vdso.
      vdsoStart = area.addr;
      vdsoEnd = area.endAddr;
      DPRINTF("***INFO: vDSO found (%p-%p)\n orignal vDSO: (%p-%p)\n",
              area.addr, area.endAddr, rinfo->vdsoStart, rinfo->vdsoEnd);
    } else if (mtcp_strcmp(area.name, "[vvar]") == 0) {
      // Do not unmap vvar.
      vvarStart = area.addr;
      vvarEnd = area.endAddr;
    } else if (mtcp_strcmp(area.name, "[vsyscall]") == 0) {
      // Do not unmap vsyscall.
    } else if (mtcp_strcmp(area.name, "[vectors]") == 0) {
      // Do not unmap vectors.  (used in Linux 3.10 on __arm__)
    } else if (area.size > 0 ) {
      DPRINTF("***INFO: munmapping (%p-%p)\n", area.addr, area.endAddr);
      if (mtcp_sys_munmap(area.addr, area.size) == -1) {
        MTCP_PRINTF("***WARNING: munmap(%s, %x, %p, %d) failed; errno: %d\n",
                    area.name, area.flags, area.addr, area.size,
                    mtcp_sys_errno);
        mtcp_abort();
      }
      // Rewind and reread maps.
      mtcp_sys_lseek(mapsfd, 0, SEEK_SET);
    }
  }
  mtcp_sys_close (mapsfd);

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
                "                vDSO/vvar sections.\n");
    mtcp_abort();
  }

  if (vdsoStart != NULL) {
    void *vdso = mtcp_sys_mremap(vdsoStart,
                                 vdsoEnd - vdsoStart,
                                 vdsoEnd - vdsoStart,
                                 MREMAP_FIXED | MREMAP_MAYMOVE,
                                 rinfo->vdsoStart);
    if (vdso == MAP_FAILED) {
      MTCP_PRINTF("***Error: failed to mremap vdso; errno: %d.\n", mtcp_sys_errno);
      mtcp_abort();
    }
    MTCP_ASSERT(vdso == rinfo->vdsoStart);

#if defined(__i386__)
    // In commit dec2c26c1eb13eb1c12edfdc9e8e811e4cc0e3c2 , the mremap
    //   code above was added, and then caused a segfault on restart for
    //   __i386__ in CentOS 7.  In that case ENABLE_VDSO_CHECK was not defined.
    //   This version was needed in that commit for __i386__
    //   (i.e., for multi-arch.sh) to succeed.
    // Newer Linux kernels, such as __x86_64__, provide a separate vsyscall segment
    //   for kernel calls while using vdso for system calls that can be
    //   executed purely in user space through kernel-specific user-space code.
    // On older kernels like __x86__, both purposes are squeezed into vdso.
    //   Since vdso will use randomized addresses (unlike the standard practice
    //   for vsyscall), this implies that kernel calls on __x86__ can go through
    //   randomized addresses, and so they need special treatment.
    vdso = mtcp_sys_mmap(vdsoStart, vdsoEnd - vdsoStart,
                         PROT_EXEC | PROT_WRITE | PROT_READ,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    // The new vdso was remapped to the location of the old vdso, since the
    //   restarted application code remembers the old vdso address.
    //   But for __i386__, a kernel call will go through the old vdso address
    //   into the kernel, and then the kernel will return to the new vdso address
    //   that was created by this kernel.  So, we need to copy the new vdso
    //   code from its current location at the old vdso address back into
    //   the new vdso address that was just mmap'ed.
    if (vdso == MAP_FAILED) {
      MTCP_PRINTF("***Error: failed to mremap vdso; errno: %d\n", mtcp_sys_errno);
      mtcp_abort();
    }
    MTCP_ASSERT(vdso == vdsoStart);
    mtcp_memcpy(vdsoStart, rinfo->vdsoStart, vdsoEnd - vdsoStart);
#endif
  }

  if (vvarStart != NULL) {
    void *vvar = mtcp_sys_mremap(vvarStart,
                                 vvarEnd - vvarStart,
                                 vvarEnd - vvarStart,
                                 MREMAP_FIXED | MREMAP_MAYMOVE,
                                 rinfo->vvarStart);
    if (vvar == MAP_FAILED) {
      MTCP_PRINTF("***Error: failed to mremap vvar; errno: %d.\n", mtcp_sys_errno);
      mtcp_abort();
    }
    MTCP_ASSERT(vvar == rinfo->vvarStart);

#if defined(__i386__)
    vvar = mtcp_sys_mmap(vvarStart, vvarEnd - vvarStart,
                         PROT_EXEC | PROT_WRITE | PROT_READ,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (vvar == MAP_FAILED) {
      MTCP_PRINTF("***Error: failed to mremap vvar; errno: %d\n", mtcp_sys_errno);
      mtcp_abort();
    }
    MTCP_ASSERT(vvar == vvarStart);
    mtcp_memcpy(vvarStart, rinfo->vvarStart, vvarEnd - vvarStart);
#endif
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

static void readmemoryareas(int fd)
{ while (1) {
    if (read_one_memory_area(fd) == -1) {
      break; /* error */
    }
  }
#if defined(__arm__) || defined(__aarch64__)
  /* On ARM, with gzip enabled, we sometimes see SEGFAULT without this.
   * The SEGFAULT occurs within the initial thread, before any user threads
   * are unblocked.  WHY DOES THIS HAPPEN?
   */
  WMB;
#endif
}

__attribute__((optimize(0)))
static int read_one_memory_area(int fd)
{
  int mtcp_sys_errno;
  int flags, imagefd;
  void *mmappedat;
  int try_skipping_existing_segment = 0;

  /* Read header of memory area into area; next mtcp_readfile() reads data  */
  Area area;
  mtcp_readfile(fd, &area, sizeof area);
  if (area.size == -1) return -1;

  if (area.name && mtcp_strstr(area.name, "[heap]")
      && mtcp_sys_brk(NULL) != area.addr + area.size) {
    DPRINTF("WARNING: break (%p) not equal to end of heap (%p)\n",
            mtcp_sys_brk(NULL), area.addr + area.size);
  }

 read_data:
  /* FIXME:  Where is MAP_ANONYMOUS turned off? */
  if ((area.flags & MAP_ANONYMOUS) && (area.flags & MAP_SHARED)) {
    MTCP_PRINTF("\n\n*** WARNING:  Next area specifies MAP_ANONYMOUS"
      	  "  and MAP_SHARED.\n"
      	  "*** Turning off MAP_ANONYMOUS and hoping for best.\n\n");
  }

  /* CASE MAPPED AS ZERO PAGE: */
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

    imagefd = -1;
    if (area.name[0] == '/') { /* If not null string, not [stack] or [vdso] */
      imagefd = mtcp_sys_open (area.name, O_RDONLY, 0);
      if (imagefd >= 0)
        area.flags ^= MAP_ANONYMOUS;
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

      int can_create_area = 1;
      // NOTE: mtcp_sys_open may also fail if filename is ".../myfile (deleted)"
      //       or if we migrated to new filesystem where filename doesn't exist.
      imagefd = mtcp_sys_open(area.name, flags, 0);  // Can we open file?
      if (imagefd < 0 && mtcp_sys_errno == ENOENT) {
        // File doesn't exist.  Do we have perm to create it and write data?
        imagefd = mtcp_sys_open(area.name, O_CREAT|O_RDWR, 0);
        if (imagefd >= 0) {
          mtcp_sys_unlink(area.name);
        } else {
          // Maybe we failed because the parent directory is missing:
          if (fix_parent_directories(area.name) != area.name)
            can_create_area = 0;
        }
      }
      if (imagefd >= 0) {
        mtcp_sys_close(imagefd);
      }

      if (can_create_area) {
        // We know we can re-create filename.
        // So, read_shared_memory_area_from_file() will recreate if needed.
        // (For example, user might have unlinked this file before checkpoint.)
        read_shared_memory_area_from_file(fd, &area, flags);
      } else {
        // We don't have permission to re-create shared file.
        // Open it as anonymous private, and hope for the best.
        // FIXME: Or maybe we do have permission to re-create shared file,
        // but it requires us to also re-create the parent directory.
        area.flags ^= MAP_SHARED;
        area.flags |= MAP_PRIVATE;
        area.flags |= MAP_ANONYMOUS;
        if (area.prot & PROT_WRITE) {
          area.prot ^= PROT_WRITE;
          MTCP_PRINTF("Found old underlying file %s\n"
                      " that no longer exists, but it's mapped shared, with"
                      " write permission,\n and we don't have permission"
                      " to re-create it.  We will map it read-only\n"
                      " and hope for the best.\n", area.name);
        }
        goto read_data;
      }
    } else { /* not MAP_ANONYMOUS, not MAP_SHARED */
      /* During checkpoint, MAP_ANONYMOUS flag is forced whenever MAP_PRIVATE
       * is set. There is no reason for any mapping to have MAP_PRIVATE and
       * not have MAP_ANONYMOUS at this point. (MAP_ANONYMOUS is handled
       * earlier in this function.)
       */
      MTCP_PRINTF("Unreachable. MAP_PRIVATE implies MAP_ANONYMOUS\n");
      mtcp_abort();
    }
  }
  return 0;
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
  int deletedFile = 0;
  int imagefd, rc;
  char *area_name = area->name; /* Modified in fix_filename_if_new_cwd below. */

  if (!(area->prot & MAP_SHARED)) {
    MTCP_PRINTF("Illegal function call\n");
    mtcp_abort();
  }

  /* Check to see if the filename ends with " (deleted)" */
  if (mtcp_strendswith(area_name, DELETED_FILE_SUFFIX)) {
    // FIXME:  Shouldn't we also unlink area_name after re-mapping it?
    area_name[ mtcp_strlen(area_name) - mtcp_strlen(DELETED_FILE_SUFFIX) ] =
      '\0';
    deletedFile = 1;
#if 0
    if (fix_parent_directories(area_name) != area_name) {
      MTCP_PRINTF("error %d re-creating directory %s\n",
                  mtcp_sys_errno, area_name);
      mtcp_abort();
    }
#endif
  }

  imagefd = mtcp_sys_open (area_name, flags, 0);  // open it

  if (imagefd < 0 && mtcp_sys_errno != ENOENT) {
    MTCP_PRINTF("error %d opening mmapped file %s with flags:%d\n",
                mtcp_sys_errno, area_name, flags);
    mtcp_abort();
  }

  if (imagefd < 0) {
    // If the shared file doesn't exist on the disk, we try to create it
    DPRINTF("Shared file %s not found. Creating new one.\n", area_name);

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
        // FIXME:  if and else conditions are the same.  Why?
        if (mtcp_strstartswith(area->name, "/usr/") ||
            mtcp_strstartswith(area->name, "/var/")) {
          DPRINTF("mapping current version of %s into memory;\n"
                  "  _not_ file as it existed at time of checkpoint.\n"
                  "  (Or this may be a file shared by multiple processes.)\n"
                  "  Change %s:%d and re-compile, if you want different "
                  "behavior.\n",
                  area->name, __FILE__, __LINE__);
        } else {
          MTCP_PRINTF("mapping current version of %s into memory;\n"
                      "  _not_ file as it existed at time of checkpoint.\n"
                      "  (Or this may be a file shared by"
                      " multiple processes.)\n"
                      "  Change %s:%d and re-compile, if you want different "
                      "behavior.\n",
                      area->name, __FILE__, __LINE__);
        }
      }
      mtcp_skipfile(fd, area->size);
    }
  }
  if (imagefd >= 0)
    mtcp_sys_close (imagefd); // don't leave dangling fd in way of other stuff

  /* On restart, mtcp_restart would recreate the (deleted) MAP_SHARED files.
   * After mmapping the files, it should unlink those files. Otherwise, Upon
   * the second checkpoint, the FILE plugin will treat them as undeleted files.
   * This would cause a problem on the second restart.
   */
  if (deletedFile)
    mtcp_sys_unlink (area->name);
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

static char* fix_parent_directories(char* filename) {
  int mtcp_sys_errno;
  int i;
  for (i=1; filename[i] != '\0'; i++) {
    if (filename[i] == '/') {
      int rc;
      filename[i] = '\0';
      rc = mtcp_sys_mkdir(filename, S_IRWXU);
      if (rc < 0 && mtcp_sys_errno != EEXIST ) {
        MTCP_PRINTF("error %d re-creating directory %s\n",
		    mtcp_sys_errno, filename);
        filename[i] = '/';  // Restore original filename
	mtcp_abort();
      } // else parent directory already exists or was re-created
      // FIXME:  We should unlink parent directories if we re-created them.
      filename[i] = '/';  // Restore original filename
    }
  }
  return filename;  // Success.  All parent directories now exist.
}

#if 0
/* Adjust for new pwd, and recreate any missing subdirectories. */
/* FIXME:  This logic could be modifed to use fix_parent_directories in each
 *         of the two parts of the function.
 */
static char* fix_filename_if_new_cwd(char* filename)
{
  int mtcp_sys_errno;
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
  int mtcp_sys_errno = 0;
  int fd;
  size_t i;

  /* Create the directory structure */
  char dir[PATH_MAX];
  mtcp_memset(dir, 0, sizeof(dir));
  mtcp_strcpy(dir, filename);
  for (i = mtcp_strlen(dir) - 1; i > 0; i--) {
    /* Remove the filename from the string */
    if (dir[i] == '/') {
      dir[i] = '\0';
      mtcp_mkdir(dir);
      break;
    }
  }

  /* Create the file */
  fd = mtcp_sys_open(filename, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR);
  if (fd<0){
    MTCP_PRINTF("unable to create file %s; errno: %d\n", filename, mtcp_sys_errno);
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
static void getTextAddr(VA *text_addr, size_t *size)
{
  int mtcp_sys_errno;
  Area area;
  VA this_fn = (VA) &getTextAddr;
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
      break;
    }
  }
  mtcp_sys_close (mapsfd);
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
