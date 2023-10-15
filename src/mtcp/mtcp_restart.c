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
#include <elf.h>
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
#include <sys/time.h>
#include <unistd.h>
#include <stddef.h>

#include "../membarrier.h"
#include "config.h"
#include "mtcp_header.h"
#include "mtcp_sys.h"
#include "mtcp_restart.h"
#include "mtcp_util.h"
#include "procmapsarea.h"

#ifdef FAST_RST_VIA_MMAP
static void mmapfile(int fd, void *buf, size_t size, int prot, int flags);
#endif

#define BINARY_NAME     "mtcp_restart"

/* struct RestoreInfo to pass all parameters from one function to next.
 * This must be global (not on stack) at the time that we jump from
 * original stack to copy of restorememoryareas() on new stack.
 * This is because we will wait until we are in the new call frame and then
 * copy the global data into the new call frame.
 */
#define STACKSIZE 4 * 1024 * 1024

static RestoreInfo rinfo;

/* Internal routines */
static void readmemoryareas(int fd, VA endOfStack);
static int read_one_memory_area(int fd, VA endOfStack);
static void restorememoryareas(RestoreInfo *rinfo_ptr);
static void restore_brk(RestoreInfo *rinfo);
static int doAreasOverlap(VA addr1, size_t size1, VA addr2, size_t size2);
static int doAreasOverlap2(char *addr, int length,
               char *vdsoStart, char *vdsoEnd, char *vvarStart, char *vvarEnd);
static int hasOverlappingMapping(VA addr, size_t size);
static int mremap_move(void *dest, void *src, size_t size);
static void remapExistingAreasToReservedArea(RestoreInfo *rinfo, void (*restore_func)(RestoreInfo*));
static void remapMtcpRestartToReservedArea(RestoreInfo *rinfo,
                                           Area *mem_regions,
                                           size_t num_regions,
                                           void (*restore_func)(RestoreInfo*),
                                           VA *endAddr);
static void unmap_one_memory_area_and_rewind(Area *area, int mapsfd);
static void restore_vdso_vvar(RestoreInfo *rinfo);
static void compute_vdso_vvar_addr(RestoreInfo *rinfo);
static void compute_regions_to_munmap(RestoreInfo *rinfo);
static void validateRestoreBufferLocation(RestoreInfo *rinfo);

// const char service_interp[] __attribute__((section(".interp"))) =
// "/lib64/ld-linux-x86-64.so.2";

#define shift argv++; argc--;

static RestoreInfo rinfo;

NO_OPTIMIZE
void
mtcp_restart_process_args(int argc, char *argv[], char **environ, void (*restore_func)(RestoreInfo *))
{
  int mtcp_sys_errno;

  if (argc == 1) {
    MTCP_PRINTF("***ERROR: This program should not be used directly.\n");
    mtcp_sys_exit(1);
  }

  rinfo.argc = argc;
  rinfo.argv = argv;
  rinfo.environ = environ;
  rinfo.fd = -1;
  rinfo.use_gdb = 0;


  char *restart_pause_str = mtcp_getenv("DMTCP_RESTART_PAUSE", environ);
  if (restart_pause_str == NULL) {
    rinfo.restart_pause = 0; /* false */
  } else {
    rinfo.restart_pause = mtcp_strtol(restart_pause_str);
  }

  shift;
  while (argc > 0) {
    if (mtcp_strcmp(argv[0], "--use-gdb") == 0) {
      rinfo.use_gdb = 1;
      shift;
    } else if (mtcp_strcmp(argv[0], "--mpi") == 0) {
      rinfo.mpiMode = 1;
      shift;
      // Flags for call by dmtcp_restart follow here:
    } else if (mtcp_strcmp(argv[0], "--fd") == 0) {
      rinfo.fd = mtcp_strtol(argv[1]);
      shift; shift;
    } else if (mtcp_strcmp(argv[0], "--stderr-fd") == 0) {
      rinfo.stderr_fd = mtcp_strtol(argv[1]);
      shift; shift;
    } else if (mtcp_strcmp(argv[0], "--restore-buffer-addr") == 0) {
      rinfo.restore_addr = (VA) mtcp_strtol(argv[1]);
      shift; shift;
    } else if (mtcp_strcmp(argv[0], "--restore-buffer-len") == 0) {
      rinfo.restore_size = mtcp_strtol(argv[1]);
      shift; shift;
    } else if (mtcp_strcmp(argv[0], "--mtcp-restart-pause") == 0) {
      rinfo.restart_pause = argv[1][0] - '0'; /* true */
      shift; shift;
    } else if (mtcp_strcmp(argv[0], "--simulate") == 0) {
      rinfo.simulate = 1;
      shift;
    } else if (argc == 1) {
      // We would use MTCP_PRINTF, but it's also for output of util/readdmtcp.sh
      mtcp_printf("Considering '%s' as a ckpt image.\n", argv[0]);
      mtcp_strcpy(rinfo.ckptImage, argv[0]);
      break;
    } else if (rinfo.mpiMode) {
      // N.B.: The assumption here is that the user provides the `--mpi` flag
      // followed by a list of checkpoint images
      break;
    } else {
      MTCP_PRINTF("MTCP Internal Error: Unknown argument: %s\n", argv[0]);
      mtcp_sys_exit(1);
    }
  }

  if (rinfo.simulate) {
    mtcp_simulateread(&rinfo);
    mtcp_sys_exit(0);
  }

  validateRestoreBufferLocation(&rinfo);

  compute_regions_to_munmap(&rinfo);

  /* For __arm__ and __aarch64__ will need to invalidate cache after this.
   */
  remapExistingAreasToReservedArea(&rinfo, restore_func);

  // Copy over old stack to new location;
  mtcp_memcpy(rinfo.new_stack_addr, rinfo.old_stack_addr, rinfo.old_stack_size);

  DPRINTF("We have copied mtcp_restart to higher address.  We will now\n"
          "    jump into a copy of restorememoryareas().\n");

  RELOCATE_STACK(rinfo.stack_offset);

  /* IMPORTANT:  We just changed to a new stack.  The call frame for this
   * function on the old stack is no longer available.  The only way to pass
   * rinfo into the next function is by passing a pointer to a global variable
   * We call restore_func(), which points to the copy of the
   * function in higher memory.  We will be unmapping the original fnc.
   */
  rinfo.mtcp_restart_new_stack(&rinfo);

  /* NOTREACHED */
}

NO_OPTIMIZE
void
mtcp_restart_new_stack(RestoreInfo *rinfoGlobal)
{
  int mtcp_sys_errno;

  // Make a local copy so that we can unmap the original mtcp_restart text+data.
  RestoreInfo rinfo = *rinfoGlobal;

  // Reset environ.
  rinfo.environ = (char**) (((VA) rinfo.environ) - rinfo.stack_offset);

  DPRINTF("Entered copy of mtcp_restart_new_stack().  Will now unmap old memory "
          "and call restore function supplied by main().");

  DPRINTF("DPRINTF may fail when we unmap, since strings are in rodata.\n"
          "But we may be lucky if the strings have been cached by the O/S\n"
          "or if compiler uses relative addressing for rodata with -fPIC\n");

#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(xor %%eax, %%eax; movw %%ax, %%fs)
                  : : : CLEAN_FOR_64_BIT(eax));
#elif defined(__arm__)
  mtcp_sys_kernel_set_tls(0);  /* Uses 'mcr', a kernel-mode instr. on ARM */
#elif defined(__aarch64__)
# warning __FUNCTION__ "TODO: Implementation for ARM64"
#endif /* if defined(__i386__) || defined(__x86_64__) */


  DPRINTF("Unmapping original text, data, and stack.");

  // Unmap original text, data, and stack.
  for (size_t i = 0; i < rinfo.num_regions_to_munmap; i++) {
    void* startAddr = rinfo.regions_to_munmap[i].startAddr;
    size_t len = rinfo.regions_to_munmap[i].endAddr - (VA)startAddr;
    if (mtcp_sys_munmap(startAddr, len) == -1) {
      MTCP_PRINTF("***WARNING: munmap (%p, %p)failed; errno: %d\n",
                  startAddr, len, mtcp_sys_errno);
    }
  }

  DPRINTF("Calling restore_func.");
  rinfo.restore_func(&rinfo);
}

NO_OPTIMIZE
void
validateRestoreBufferLocation(RestoreInfo *rinfo)
{
  int mtcp_sys_errno;
  int mapsfd = mtcp_sys_open2("/proc/self/maps", O_RDONLY);
  MTCP_ASSERT (mapsfd >= 0);

  Area area;
  while (mtcp_readmapsline(mapsfd, &area)) {
    if (doAreasOverlap(area.addr, area.size, (VA) rinfo->restore_addr, rinfo->restore_size)) {
      MTCP_PRINTF("***ERROR: Restore buffer overlaps with memory area %p-%p\n",
                  area.addr, area.endAddr);
      mtcp_abort();
    }
  }

  mtcp_sys_close(mapsfd);
}

NO_OPTIMIZE
void
mtcp_restart(RestoreInfo *rinfo, MtcpHeader *mtcpHdr)
{
  int mtcp_sys_errno;
  if (rinfo->restart_pause == 1) {
    MTCP_PRINTF("*** (gdb) set rinfo.restart_pause=2 # to go to next stmt\n");
  }
  // In GDB, 'set rinfo.restart_pause=2' to continue to next statement.
  DMTCP_RESTART_PAUSE_WHILE(rinfo->restart_pause == 1);

#ifdef TIMING
  mtcp_sys_gettimeofday(&rinfo->startValue, NULL);
#endif

  rinfo->saved_brk = mtcpHdr->saved_brk;
  rinfo->vdsoStart = mtcpHdr->vdsoStart;
  rinfo->vdsoEnd = mtcpHdr->vdsoEnd;
  rinfo->vvarStart = mtcpHdr->vvarStart;
  rinfo->vvarEnd = mtcpHdr->vvarEnd;
  rinfo->endOfStack = mtcpHdr->end_of_stack;
  rinfo->post_restart = mtcpHdr->post_restart;

  restore_vdso_vvar(rinfo);
  restore_brk(rinfo);
  restorememoryareas(rinfo);
}

NO_OPTIMIZE
static void
restore_brk(RestoreInfo *rinfo)
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
  if ((current_brk > (VA) rinfo->restore_addr) &&
      (rinfo->saved_brk < (VA) rinfo->restore_end)) {
    MTCP_PRINTF("current_brk %p, saved_brk %p, restore_begin %p,"
                " restore_end %p\n",
                current_brk, rinfo->saved_brk, (VA) rinfo->restore_addr,
                (VA) rinfo->restore_end);
    mtcp_abort();
  }

  if (current_brk <= rinfo->saved_brk) {
    new_brk = mtcp_sys_brk(rinfo->saved_brk);
  } else {
    new_brk = rinfo->saved_brk;

    // If saved_brk < current_brk, then brk() does munmap; we can lose rinfo.
    // So, keep the value rinfo.saved_brk, and call mtcp_sys_brk() later.
    return;
  }

  if (new_brk == (VA)-1) {
    MTCP_PRINTF("sbrk(%p): errno: %d (bad heap)\n",
                rinfo->saved_brk, mtcp_sys_errno);
    mtcp_abort();
  } else if (new_brk > current_brk) {
    // Now unmap the just mapped extended heap. This is to ensure that we don't
    // have overlap with the restore region.
    if (mtcp_sys_munmap(current_brk, new_brk - current_brk) == -1) {
      DPRINTF("***WARNING: munmap failed; errno: %d\n", mtcp_sys_errno);
    }
  }

  if (new_brk != rinfo->saved_brk) {
    if (new_brk == current_brk && new_brk > rinfo->saved_brk) {
      MTCP_PRINTF("new_brk == current_brk == %p\n; saved_break, %p,"
              " is strictly smaller;\n  data segment not extended.\n",
              new_brk, rinfo->saved_brk);
    } else {
      if (new_brk == current_brk) {
        DPRINTF("error: new/current break (%p) != saved break (%p);" \
                " continuing without resetting heap\n",
                current_brk, rinfo->saved_brk);
      } else {
        DPRINTF("error: new break (%p) != current break (%p);" \
                " continuing without resetting heap\n",
                new_brk, current_brk);
      }

      // mtcp_abort ();
    }
  }

  // Unmap heap -- we don't need it anymore.

  int mapsfd = mtcp_sys_open2("/proc/self/maps", O_RDONLY);
  MTCP_ASSERT (mapsfd >= 0);

  Area area;
  int foundHeap = 0;
  while (mtcp_readmapsline(mapsfd, &area)) {
    if (mtcp_strcmp(area.name, "[heap]") == 0) {
      foundHeap = 1;
      MTCP_ASSERT(area.endAddr == new_brk);
      MTCP_ASSERT(mtcp_sys_munmap(area.addr, area.size) != -1);
      break;
    }
  }

  mtcp_sys_close(mapsfd);
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

// Used by util/readdmtcp.sh
// So, we use mtcp_printf to stdout instead of MTCP_PRINTF (diagnosis for DMTCP)
int
mtcp_open_ckpt_image_and_read_header(RestoreInfo *rinfo, MtcpHeader *mtcpHdr)
{
  int mtcp_sys_errno;
  int rc = -1;

  MTCP_ASSERT(rinfo->fd == -1);

  rinfo->fd = mtcp_sys_open2(rinfo->ckptImage, O_RDONLY);
  if (rinfo->fd == -1) {
    MTCP_PRINTF("***ERROR opening ckpt image (%s); errno: %d\n",
                rinfo->ckptImage, mtcp_sys_errno);
    mtcp_abort();
  }

  // This assumes that the MTCP header signature is unique.
  // We repeatedly look for mtcpHdr because the first header will be
  // for DMTCP.  So, we look deeper for the MTCP header.  The MTCP
  // header is guaranteed to start on an offset that's an integer
  // multiple of sizeof(mtcpHdr), which is currently 4096 bytes.
  do {
    rc = mtcp_readfile(rinfo->fd, mtcpHdr, sizeof *mtcpHdr);
  } while (rc > 0 && mtcp_strcmp(mtcpHdr->signature, MTCP_SIGNATURE) != 0);

  if (rc == 0) { /* if end of file */
    MTCP_PRINTF("***ERROR: ckpt image doesn't match MTCP_SIGNATURE\n");
    mtcp_sys_close(rinfo->fd);
    return -1;  /* exit with error code 1 */
  }

  return rinfo->fd;
}

// Used by util/readdmtcp.sh
// So, we use mtcp_printf to stdout instead of MTCP_PRINTF (diagnosis for DMTCP)
void
mtcp_simulateread(RestoreInfo *rinfo)
{
  int mtcp_sys_errno;
  MtcpHeader mtcpHdr;

  MTCP_ASSERT(rinfo->simulate == 1);

  if (rinfo->ckptImage[0] == '\0') {
    MTCP_PRINTF("*** Simulate flag requires a checkpoint-image path as argument.\n");
    mtcp_abort();
  }

  MTCP_ASSERT(rinfo->fd == -1);

  rinfo->fd = mtcp_open_ckpt_image_and_read_header(rinfo, &mtcpHdr);
  if (rinfo->fd == -1) {
    MTCP_PRINTF("***ERROR: ckpt image not found.\n");
    mtcp_abort();
  }

  mtcp_printf("\nMTCP: %s", mtcpHdr.signature);
  mtcp_printf("**** mtcp_restart (will be copied here): %p..%p\n",
              mtcpHdr.restore_addr,
              mtcpHdr.restore_addr + mtcpHdr.restore_size);
  mtcp_printf("**** DMTCP entry point (ThreadList::postRestart()): %p\n",
              mtcpHdr.post_restart);
  mtcp_printf("**** brk (sbrk(0)): %p\n", mtcpHdr.saved_brk);
  mtcp_printf("**** vdso: %p..%p\n", mtcpHdr.vdsoStart, mtcpHdr.vdsoEnd);
  mtcp_printf("**** vvar: %p..%p\n", mtcpHdr.vvarStart, mtcpHdr.vvarEnd);
  mtcp_printf("**** end of stack: %p\n", mtcpHdr.end_of_stack);

  Area area;
  mtcp_printf("\n**** Listing ckpt image area:\n");
  while (1) {
    mtcp_readfile(rinfo->fd, &area, sizeof area);
    if (area.size == -1) {
      break;
    }

    if ((area.properties & DMTCP_ZERO_PAGE) == 0 &&
        (area.properties & DMTCP_ZERO_PAGE_PARENT_HEADER) == 0) {

      off_t seekLen = area.size;
      if (!(area.flags & MAP_ANONYMOUS) && area.mmapFileSize > 0) {
        seekLen =  area.mmapFileSize;
      }
      if (mtcp_sys_lseek(rinfo->fd, seekLen, SEEK_CUR) < 0) {
         mtcp_printf("Could not seek!\n");
         break;
      }
    }

    if ((area.properties & DMTCP_ZERO_PAGE_CHILD_HEADER) == 0) {
      mtcp_printf("%p-%p %c%c%c%c %s          %s\n",
                  area.addr, area.endAddr,
                  ((area.prot & PROT_READ)  ? 'r' : '-'),
                  ((area.prot & PROT_WRITE) ? 'w' : '-'),
                  ((area.prot & PROT_EXEC)  ? 'x' : '-'),
                  ((area.flags & MAP_SHARED)
                    ? 's'
                    : ((area.flags & MAP_PRIVATE) ? 'p' : '-')),
                  ((area.flags & MAP_ANONYMOUS) ? "Anon" : "    "),

                  // area.offset, area.devmajor, area.devminor, area.inodenum,
                  area.name);
    }
  }
}

NO_OPTIMIZE
static void
restorememoryareas(RestoreInfo *rinfo)
{
  int mtcp_sys_errno;
  /* Restore memory areas */
  DPRINTF("restoring memory areas\n");
  readmemoryareas(rinfo->fd, rinfo->endOfStack);

  /* Everything restored, close file and finish up */

  DPRINTF("close cpfd %d\n", rinfo->fd);
  mtcp_sys_close(rinfo->fd);
  double readTime = 0.0;
#ifdef TIMING
  struct timeval endValue;
  mtcp_sys_gettimeofday(&endValue, NULL);
  struct timeval diff;
  timersub(&endValue, &rinfo->startValue, &diff);
  readTime = diff.tv_sec + (diff.tv_usec / 1000000.0);
#endif

  IMB; /* flush instruction cache, since mtcp_restart.c code is now gone. */

  /* System calls and libc library calls should now work. */

  DPRINTF("MTCP restore is now complete.  Continuing by jumping to\n"
          "  ThreadList::postRestart() back inside libdmtcp.so: %p...\n",
          rinfo->post_restart);

  if (rinfo->restart_pause) {
    MTCP_PRINTF(
      "\nStopping due to env. var DMTCP_RESTART_PAUSE or MTCP_RESTART_PAUSE\n"
      "(DMTCP_RESTART_PAUSE can be set after creating the checkpoint image.)\n"
      "Attach to the computation with GDB from another window,\n"
      "  where PROGRAM_NAME is the original target application:\n"
      "(This won't work well unless you configure DMTCP with --enable-debug)\n"
      "  gdb PROGRAM_NAME %d\n"
      "You will then be in 'ThreadList::postRestart()' or later\n"
      "  (gdb) list\n"
      "  (gdb) p restartPauseLevel = 0  # Or set it to next higher level.\n"
      "  # In most recent Linuxes/glibc/gdb, you will also need to do:\n"
      "  (gdb) source DMTCP_ROOT/util/gdb-dmtcp-utils\n"
      "  (gdb) load-symbols # (better for recent GDB: try it)\n"
      "  (gdb) load-symbols-library ADDR_OR_FILE"
      "  # Better for newer GDB versions\n"
      "  (gdb) add-symbol-files-all # (better for GDB-8 and earlier)\n",
      mtcp_sys_getpid()
    );
  }
  rinfo->post_restart(readTime, rinfo->restart_pause);
  // NOTREACHED
}

NO_OPTIMIZE
static void
compute_vdso_vvar_addr(RestoreInfo *rinfo)
{
  int mtcp_sys_errno;
  Area area;
  rinfo->currentVdsoStart = NULL;
  rinfo->currentVdsoEnd = NULL;
  rinfo->currentVvarStart = NULL;
  rinfo->currentVvarEnd = NULL;

  int mapsfd = mtcp_sys_open2("/proc/self/maps", O_RDONLY);

  if (mapsfd < 0) {
    MTCP_PRINTF("error opening /proc/self/maps; errno: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }

  while (mtcp_readmapsline(mapsfd, &area)) {
    if (mtcp_strcmp(area.name, "[vdso]") == 0) {
      // Do not unmap vdso.
      rinfo->currentVdsoStart = area.addr;
      rinfo->currentVdsoEnd = area.endAddr;
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
      rinfo->currentVvarStart = area.addr;
      rinfo->currentVvarEnd = area.endAddr;
    }
  }

  mtcp_sys_close(mapsfd);
}

NO_OPTIMIZE
void
compute_regions_to_munmap(RestoreInfo *rinfo)
{
  int mtcp_sys_errno;
  Area area;

  int mapsfd = mtcp_sys_open2("/proc/self/maps", O_RDONLY);
  MTCP_ASSERT (mapsfd >= 0);

  rinfo->num_regions_to_munmap = 0;
  while (mtcp_readmapsline(mapsfd, &area)) {
    if (mtcp_strcmp(area.name, "[vdso]") == 0 ||
        mtcp_strcmp(area.name, "[vvar]") == 0 ||
        mtcp_strcmp(area.name, "[vsyscall]") == 0) {
      // Do not unmap vdso, vvar, or vsyscall.
    } else {
      int idx = rinfo->num_regions_to_munmap++;
      rinfo->regions_to_munmap[idx].startAddr = area.addr;
      rinfo->regions_to_munmap[idx].endAddr = area.endAddr;

      MTCP_ASSERT(rinfo->num_regions_to_munmap < MAX_REGIONS_TO_MUNMAP);
    }
  }

  mtcp_sys_close(mapsfd);
}


// Check if this area is part of memory regions to unmap as computed earlier.
static int
should_unmap_mem_region(RestoreInfo *rinfo, Area *area)
{
  for (size_t i = 0; i < rinfo->num_regions_to_munmap; i++) {
    if (area->addr == rinfo->regions_to_munmap[i].startAddr &&
        area->endAddr == rinfo->regions_to_munmap[i].endAddr) {
      return 1;
    }
  }

  return 0;
}

NO_OPTIMIZE
static void
unmap_one_memory_area_and_rewind(Area *area, int mapsfd)
{
  int mtcp_sys_errno;
  DPRINTF("***INFO: munmapping (%p..%p)\n", area->addr, area->endAddr);
  if (mtcp_sys_munmap(area->addr, area->size) == -1) {
    MTCP_PRINTF("***WARNING: %s(%x): munmap(%p, %d) failed; errno: %d\n",
                area->name, area->flags, area->addr, area->size,
                mtcp_sys_errno);
    mtcp_abort();
  }

  // Since we just unmapped a region, the size and contents of the
  // /proc/self/maps file has changed. We should rewind and reread this file to
  // ensure that we don't miss any regions.
  // TODO(kapil): Create a list of all areas that we want to munmap and then unmap them all at once.
  mtcp_sys_lseek(mapsfd, 0, SEEK_SET);
}

NO_OPTIMIZE
static void
restore_vdso_vvar(RestoreInfo *rinfo)
{
  int mtcp_sys_errno;

  if (rinfo->currentVdsoEnd - rinfo->currentVdsoStart != rinfo->vdsoEnd - rinfo->vdsoStart) {
    MTCP_PRINTF("***Error: vdso size mismatch.\n");
    mtcp_abort();
  }

  if (rinfo->currentVvarEnd - rinfo->currentVvarStart != rinfo->vvarEnd - rinfo->vvarStart) {
    MTCP_PRINTF("***Error: vvar size mismatch. Current: %p..%p; existing %p..%p\n",
      rinfo->currentVvarEnd, rinfo->currentVvarStart, rinfo->vvarEnd, rinfo->vvarStart);
    mtcp_abort();
  }

  if (rinfo->currentVvarStart != NULL) {
    int rc = mremap_move(rinfo->vvarStart,
                         rinfo->currentVvarStart,
                         rinfo->currentVvarEnd - rinfo->currentVvarStart);
    if (rc == -1) {
      MTCP_PRINTF("***Error: failed to restore vvar to pre-ckpt location.");
      mtcp_abort();
    }
  }

  if (rinfo->currentVdsoStart != NULL) {
    int rc = mremap_move(rinfo->vdsoStart,
                         rinfo->currentVdsoStart,
                         rinfo->currentVdsoEnd - rinfo->currentVdsoStart);
    if (rc == -1) {
      MTCP_PRINTF("***Error: failed to restore vdso to pre-ckpt location.");
      mtcp_abort();
    }
    mtcp_setauxval(rinfo->environ, AT_SYSINFO_EHDR,
                   (unsigned long int) rinfo->vdsoStart);
  }
}

/**************************************************************************
 *
 *  Read memory area descriptors from checkpoint file
 *  Read memory area contents and/or mmap original file
 *  Four cases:
 *    * MAP_ANONYMOUS (if file /proc/.../maps reports file):
 *      handle it as if MAP_PRIVATE and not MAP_ANONYMOUS, but restore from ckpt
 *      image: no copy-on-write);
 *    * private, currently assumes backing file exists
 *    * shared, but need to recreate file;
 *    * shared and file currently exists
 *      (if writeable by us and memory map has write protection, then write to
 *      it from checkpoint file; else skip ckpt image and map current data of
 *      file)
 * NOTE: Linux option MAP_SHARED|MAP_ANONYMOUS currently not supported; result
 *       is undefined.  If there is an important use case, we will fix this.
 * NOTE: mmap requires that if MAP_ANONYMOUS was not set, then mmap must specify
 *       a backing store.  Further, a reference by mmap constitutes a reference
 *       to the file, and so the file cannot truly be deleted until the process
 *       no longer maps it.  So, if we don't see the file on restart and there
 *       is no MAP_ANONYMOUS, then we have a responsibility to recreate the
 *       file.  MAP_ANONYMOUS is not currently POSIX.)
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

  /* Read header of memory area into area; mtcp_readfile() will read header */
  Area area;

  mtcp_readfile(fd, &area, sizeof area);
  if (area.addr == NULL) {
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
  if ((area.name[0] && area.name[0] != '/' && mtcp_strstr(area.name, "stack"))
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
    DPRINTF("restoring zero-paged anonymous area, %p bytes at %p\n",
            area.size, area.addr);
    // No need to mmap since the region has already been mmapped by the parent
    // header.
    // Just restore write-protection if needed.
    if (!(area.prot & PROT_WRITE)) {
      if (mtcp_sys_mprotect(area.addr, area.size, area.prot) < 0) {
        MTCP_PRINTF("error %d write-protecting %p bytes at %p\n",
                    mtcp_sys_errno, area.size, area.addr);
        mtcp_abort();
      }
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
  else {
    /* If there is a filename there, though, pretend like we're mapping
     * to it so a new /proc/self/maps will show a filename there like with
     * original process.  We only need read-only access because we don't
     * want to ever write the file.
     */

    if ((area.properties & DMTCP_ZERO_PAGE_CHILD_HEADER) == 0) {
      // MMAP only if it's not a child header.
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
          if ((curr_size < area.offset + area.size) && (area.prot & PROT_WRITE)) {
            DPRINTF("restoring non-anonymous area %s as anonymous: %p  bytes at %p\n",
                    area.name, area.size, area.addr);
            mtcp_sys_close(imagefd);
            imagefd = -1;
            area.offset = 0;
            area.flags |= MAP_ANONYMOUS;
          }
        }
      }

      if (area.flags & MAP_ANONYMOUS) {
        DPRINTF("restoring anonymous area, %p  bytes at %p\n",
                area.size, area.addr);
      } else {
        DPRINTF("restoring to non-anonymous area,"
                " %p bytes at %p from %s + 0x%X\n",
                area.size, area.addr, area.name, area.offset);
      }

      /* Create the memory area */

      // If the region is marked as private but without a backing file (i.e.,
      // the file was deleted on ckpt), restore it as MAP_ANONYMOUS.
      // TODO: handle huge pages by detecting and passing MAP_HUGETLB in flags.
      if (imagefd == -1 && (area.flags & MAP_PRIVATE)) {
        area.flags |= MAP_ANONYMOUS;
      }

      /* POSIX says mmap would unmap old memory.  Munmap never fails if args
      * are valid.  Can we unmap vdso and vsyscall in Linux?  Used to use
      * mtcp_safemmap here to check for address conflicts.
      */
      mmappedat =
        mmap_fixed_noreplace(area.addr, area.size, area.prot | PROT_WRITE,
                            area.flags, imagefd, area.offset);

      MTCP_ASSERT(mmappedat == area.addr);

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
      if (imagefd >= 0) {
        mtcp_sys_close(imagefd);
      }
    }

    if ((area.properties & DMTCP_ZERO_PAGE_PARENT_HEADER) == 0) {
      // Parent header doesn't have any follow on data.

      /* This mmapfile after prev. mmap is okay; use same args again.
       *  Posix says prev. map will be munmapped.
       */

      /* ANALYZE THE CONDITION FOR DOING mmapfile MORE CAREFULLY. */
      if (area.mmapFileSize > 0 && area.name[0] == '/') {
        DPRINTF("restoring memory region %p of %p bytes at %p\n",
                    area.mmapFileSize, area.size, area.addr);
        mtcp_readfile(fd, area.addr, area.mmapFileSize);
      } else {
        mtcp_readfile(fd, area.addr, area.size);
      }

      if (!(area.prot & PROT_WRITE)) {
        if (mtcp_sys_mprotect(area.addr, area.size, area.prot) < 0) {
          MTCP_PRINTF("error %d write-protecting %p bytes at %p\n",
                      mtcp_sys_errno, area.size, area.addr);
          mtcp_abort();
        }
      }
    }
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
    VA mmappedat = mmap_fixed_noreplace(anon_start_addr, anon_area_size,
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

NO_OPTIMIZE
static int
doAreasOverlap(VA addr1, size_t size1, VA addr2, size_t size2)
{
  VA end1 = (char *)addr1 + size1;
  VA end2 = (char *)addr2 + size2;

  return (size1 > 0 && addr1 >= addr2 && addr1 < end2) ||
         (size2 > 0 && addr2 >= addr1 && addr2 < end1);
}

NO_OPTIMIZE
static int
doAreasOverlap2(char *addr, int length, char *vdsoStart, char *vdsoEnd,
                                        char *vvarStart, char *vvarEnd) {
  return doAreasOverlap(addr, length, vdsoStart, vdsoEnd - vdsoStart) ||
         doAreasOverlap(addr, length, vvarStart, vvarEnd - vvarStart);
}

/* This uses MREMAP_FIXED | MREMAP_MAYMOVE to move a memory segment.
 * Note that we need 'MREMAP_MAYMOVE'.  With only 'MREMAP_FIXED', the
 * kernel can overwrite an existing memory region.
 * Note that 'mremap' and hence 'mremap_move' do not allow overlapping src and dest.
 */
NO_OPTIMIZE
static int
mremap_move(void *dest, void *src, size_t size) {
  int mtcp_sys_errno;
  if (dest == src) {
    return 0; // Success
  }
  void *rc = mtcp_sys_mremap(src, size, size, MREMAP_FIXED | MREMAP_MAYMOVE, dest);
  if (rc == dest) {
    return 0; // Success
  } else if (rc == MAP_FAILED) {
    MTCP_PRINTF("***Error: failed to mremap; src->dest: %p->%p, size: 0x%x;"
                " errno: %d.\n", src, dest, size, mtcp_sys_errno);
    return -1; // Error
  } else {
    // Else 'MREMAP_MAYMOVE' forced the remap to the wrong location.  So, the
    // memory was moved to the wrong desgination.  Undo the move, and return -1.
    mremap_move(src, rc, size);
    return -1; // Error
  }
}

// This is the entrypoint to the binary. We'll need it for adding symbol file.
int _start();

static int
isVdsoArea(Area* area)
{
#if defined(__i386__) && LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 18)
    // It's a vdso page from a time before Linux displayed the annotation.
    if (area.addr == 0xfffe0000 && area.size == 4096) {
      return 1;
    }
#endif // if defined(__i386__) && LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 18)

  return mtcp_strcmp(area->name, "[vdso]") == 0;
}

NO_OPTIMIZE
static void
remapMtcpRestartToReservedArea(RestoreInfo *rinfo,
                               Area *mem_regions,
                               size_t num_regions,
                               void (*restore_func)(RestoreInfo*),
                               VA *endAddr)
{
  int mtcp_sys_errno;

  ptrdiff_t restore_region_offset = (VA) rinfo->restore_addr - mem_regions[0].addr;
  rinfo->restore_func = (fnptr_t)
    ((uint64_t)restore_func + restore_region_offset);
  rinfo->mtcp_restart_new_stack = (fnptr_t)
    ((uint64_t)mtcp_restart_new_stack + restore_region_offset);


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

  // Reserve the entire restore area. This would ensure no other memory regions
  // get mapped in this location.
  void *addr = mmap_fixed_noreplace(rinfo->restore_addr,
                                    RESTORE_TOTAL_SIZE,
                                    PROT_NONE,
                                    MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED,
                                    -1,
                                    0);
  MTCP_ASSERT(addr != MAP_FAILED);

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

    *endAddr = (VA) addr + mem_regions[i].size;

    // This probably is some memory that was initialized by the loader; let's
    // copy over the bits.
    if (mem_regions[i].prot & PROT_WRITE) {
      mtcp_memcpy(addr, mem_regions[i].addr, mem_regions[i].size);
    }
  }

  mtcp_sys_close(mtcp_restart_fd);
  mtcp_restart_fd = -1;
}

NO_OPTIMIZE
static void
remapExistingAreasToReservedArea(RestoreInfo *rinfo,
                                 void (*restore_func)(RestoreInfo*))
{
  int mtcp_sys_errno;

  const size_t MAX_MTCP_RESTART_MEM_REGIONS = 16;

  Area mem_regions[MAX_MTCP_RESTART_MEM_REGIONS];

  size_t num_regions = 0;

  char binary_name[PATH_MAX+1];
  MTCP_ASSERT(mtcp_sys_readlink("/proc/self/exe", binary_name, PATH_MAX) != -1);

  rinfo->currentVdsoStart = NULL;
  rinfo->currentVdsoEnd = NULL;
  rinfo->currentVvarStart = NULL;
  rinfo->currentVvarEnd = NULL;
  rinfo->old_stack_addr = NULL;
  rinfo->old_stack_size = 0;

  // First figure out mtcp_restart memory regions.
  int mapsfd = mtcp_sys_open2("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0) {
    MTCP_PRINTF("error opening /proc/self/maps: errno: %d\n", mtcp_sys_errno);
    mtcp_abort();
  }

  Area area;
  while (mtcp_readmapsline(mapsfd, &area)) {
    if (isVdsoArea(&area)) {
      rinfo->currentVdsoStart = area.addr;
      rinfo->currentVdsoEnd = area.endAddr;
    } else if (mtcp_strcmp(area.name, "[vvar]") == 0) {
      rinfo->currentVvarStart = area.addr;
      rinfo->currentVvarEnd = area.endAddr;
    } else if (mtcp_strcmp(area.name, binary_name) == 0) {
      MTCP_ASSERT(num_regions < MAX_MTCP_RESTART_MEM_REGIONS);
      mem_regions[num_regions++] = area;
    } else if (area.addr < (VA) &area && area.endAddr > (VA) &area) {
      // We've found stack.
      rinfo->old_stack_addr = area.addr;
      rinfo->old_stack_size = area.size;
    }
  }

  mtcp_sys_close(mapsfd);

  VA endAddr;
  remapMtcpRestartToReservedArea(rinfo, mem_regions, num_regions, restore_func, &endAddr);

  // Create a guard page without read permissions.
  MTCP_ASSERT(mtcp_sys_mmap(endAddr,
                            MTCP_PAGE_SIZE,
                            PROT_NONE,
                            MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED,
                            -1,
                            0) == endAddr);

  endAddr += MTCP_PAGE_SIZE;

  // Now move vvar to the end of guard page.
  if (rinfo->currentVvarStart != NULL) {
    size_t size = rinfo->currentVvarEnd - rinfo->currentVvarStart;
    int rc = mremap_move(endAddr, rinfo->currentVvarStart, size);
    if (rc == -1) {
      MTCP_PRINTF("***Error: failed to remap vvarStart to reserved area.");
      mtcp_abort();
    }

    rinfo->currentVvarStart = endAddr;
    rinfo->currentVvarEnd = endAddr + size;
    endAddr = rinfo->currentVvarEnd;
  }

  // Now move vdso to the end of vvar page.
  if (rinfo->currentVdsoStart != NULL) {
    size_t size = rinfo->currentVdsoEnd - rinfo->currentVdsoStart;
    int rc = mremap_move(endAddr, rinfo->currentVdsoStart, size);
    if (rc == -1) {
      MTCP_PRINTF("***Error: failed to remap vdsoStart to reserved area.");
      mtcp_abort();
    }

    rinfo->currentVdsoStart = endAddr;
    rinfo->currentVdsoEnd = endAddr + size;
    endAddr = rinfo->currentVdsoEnd;
    mtcp_setauxval(rinfo->environ, AT_SYSINFO_EHDR,
                   (unsigned long int) rinfo->currentVdsoStart);
  }

  uint64_t remaining_restore_area =
    (uint64_t) (rinfo->restore_addr + rinfo->restore_size - endAddr);

  MTCP_ASSERT(remaining_restore_area >= rinfo->old_stack_size);

  VA new_stack_end_addr = rinfo->restore_addr + RESTORE_TOTAL_SIZE;
  VA new_stack_start_addr = new_stack_end_addr - rinfo->old_stack_size;

  rinfo->new_stack_addr = (VA)
    mtcp_sys_mmap(new_stack_start_addr,
                  rinfo->old_stack_size,
                  PROT_READ | PROT_WRITE,
                  MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED,
                  -1,
                  0);
  MTCP_ASSERT(rinfo->new_stack_addr != MAP_FAILED);

  rinfo->stack_offset = rinfo->old_stack_addr - rinfo->new_stack_addr;
}

#ifdef FAST_RST_VIA_MMAP
static void mmapfile(int fd, void *buf, size_t size, int prot, int flags)
{
  int mtcp_sys_errno;
  void *addr;
  int rc;

  /* Use mmap for this portion of checkpoint image. */
  addr = mmap_fixed_noreplace(buf, size, prot, flags,
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
