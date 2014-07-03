/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, and gene@ccs.neu.edu          *
 *                                                                          *
 *   This file is part of the DMTCP.                                        *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include "dmtcp.h"
#include "processinfo.h"
#include "procmapsarea.h"
#include "jassert.h"
#include "util.h"

#define _real_open NEXT_FNC(open)
#define _real_close NEXT_FNC(close)

using namespace dmtcp;

EXTERNC int dmtcp_infiniband_enabled(void) __attribute__((weak));

static const int END_OF_NSCD_AREAS = -1;

/* Internal routines */
//static void sync_shared_mem(void);
static void writememoryarea (int fd, Area *area,
                             int stack_was_seen, int vsyscall_exists);
static void preprocess_special_segments(int *vsyscall_exists);

static void remap_nscd_areas(Area remap_nscd_areas_array[],
                             int  num_remap_nscd_areas);

/*****************************************************************************
 *
 *  This routine is called from time-to-time to write a new checkpoint file.
 *  It assumes all the threads are suspended.
 *
 *****************************************************************************/

void mtcp_writememoryareas(int fd)
{
  Area area;
  //DeviceInfo dev_info;
  int stack_was_seen = 0;

  JTRACE("Performing checkpoint.");

  // Here we want to sync the shared memory pages with the backup files
  // FIXME: Why do we need this?
  //JTRACE("syncing shared memory with backup files");
  //sync_shared_mem();

  int vsyscall_exists = 0;
  // Preprocess special segments like vsyscall, stack, heap etc.
  preprocess_special_segments(&vsyscall_exists);

  /**************************************************************************/
  /* We can't do any more mallocing at this point because malloc stuff is   */
  /* outside the limits of the libmtcp.so image, so it won't get            */
  /* checkpointed, and it's possible that we would checkpoint an            */
  /* inconsistent state.  See note in restoreverything routine.             */
  /**************************************************************************/

  int num_remap_nscd_areas = 0;
  Area remap_nscd_areas_array[10];
  remap_nscd_areas_array[9].flags = END_OF_NSCD_AREAS;

  /* Finally comes the memory contents */
  int mapsfd = _real_open("/proc/self/maps", O_RDONLY);
  while (Util::readProcMapsLine(mapsfd, &area)) {
    VA area_begin = area.addr;
    /* VA area_end   = area_begin + area.size; */

    if ((uint64_t)area.addr == ProcessInfo::instance().restoreBufAddr()) {
      JASSERT(area.size == ProcessInfo::instance().restoreBufLen())
        ((void*) area.addr) (area.size) (ProcessInfo::instance().restoreBufLen());
      continue;
    }

    /* Original comment:  Skip anything in kernel address space ---
     *   beats me what's at FFFFE000..FFFFFFFF - we can't even read it;
     * Added: That's the vdso section for earlier Linux 2.6 kernels.  For later
     *  2.6 kernels, vdso occurs at an earlier address.  If it's unreadable,
     *  then we simply won't copy it.  But let's try to read all areas, anyway.
     * **COMMENTED OUT:** if (area_begin >= HIGHEST_VA) continue;
     */
    /* If it's readable, but it's VDSO, it will be dangerous to restore it.
     * In 32-bit mode later Red Hat RHEL Linux 2.6.9 releases use 0xffffe000,
     * the last page of virtual memory.  Note 0xffffe000 >= HIGHEST_VA
     * implies we're in 32-bit mode.
     */
    if (area_begin >= HIGHEST_VA && area_begin == (VA)0xffffe000)
      continue;
#ifdef __x86_64__
    /* And in 64-bit mode later Red Hat RHEL Linux 2.6.9 releases
     * use 0xffffffffff600000 for VDSO.
     */
    if (area_begin >= HIGHEST_VA && area_begin == (VA)0xffffffffff600000)
      continue;
#endif

    /* Skip anything that has no read or execute permission.  This occurs
     * on one page in a Linux 2.6.9 installation.  No idea why.  This code
     * would also take care of kernel sections since we don't have read/execute
     * permission there.
     *
     * EDIT: We should only skip the "---p" section for the shared libraries.
     * Anonymous memory areas with no rwx permission should be saved regardless
     * as the process might have removed the permissions temporarily and might
     * want to use it later.
     *
     * This happens, for example, with libpthread where the pthread library
     * tries to recycle thread stacks. When a thread exits, libpthread will
     * remove the access permissions from the thread stack and later, when a
     * new thread is created, it will provide the proper permission to this
     * area and use it as the thread stack.
     *
     * If we do not restore this area on restart, the area might be returned by
     * some mmap() call. Later on, when pthread wants to use this area, it will
     * just try to use this area which now belongs to some other object. Even
     * worse, the other object can then call munmap() on that area after
     * libpthread started using it as thread stack causing the parts of thread
     * stack getting munmap()'d from the memory resulting in a SIGSEGV.
     *
     * We suspect that libpthread is using mmap() instead of mprotect to change
     * the permission from "---p" to "rw-p".
     */

    if (!((area.prot & PROT_READ) || (area.prot & PROT_WRITE)) &&
        area.name[0] != '\0') {
      continue;
    }

    if (Util::strStartsWith(area.name, DEV_ZERO_DELETED_STR) ||
        Util::strStartsWith(area.name, DEV_NULL_DELETED_STR)) {
      /* If the process has an area labelled as "/dev/zero (deleted)", we mark
       *   the area as Anonymous and save the contents to the ckpt image file.
       * If this area has a MAP_SHARED attribute, it should be replaced with
       *   MAP_PRIVATE and we won't do any harm because, the /dev/zero file is
       *   an absolute source and sink. Anything written to it will be
       *   discarded and anything read from it will be all zeros.
       * The following call to mmap will create "/dev/zero (deleted)" area
       *         mmap(addr, size, protection, MAP_SHARED | MAP_ANONYMOUS, 0, 0)
       *
       * The above explanation also applies to "/dev/null (deleted)"
       */
      JTRACE("saving area as Anonymous") (area.name);
      area.flags = MAP_PRIVATE | MAP_ANONYMOUS;
      area.name[0] = '\0';
    } else if (Util::strStartsWith(area.name, SYS_V_SHMEM_FILE)) {
      JTRACE("saving area as Anonymous") (area.name);
      area.flags = MAP_PRIVATE | MAP_ANONYMOUS;
      area.name[0] = '\0';
    } else if (Util::strStartsWith(area.name, NSCD_MMAP_STR1) ||
               Util::strStartsWith(area.name, NSCD_MMAP_STR2) ||
               Util::strStartsWith(area.name, NSCD_MMAP_STR3)) {
      /* Special Case Handling: nscd is enabled*/
      JTRACE("NSCD daemon shared memory area present.\n"
              "  MTCP will now try to remap this area in read/write mode as\n"
              "  private (zero pages), so that glibc will automatically\n"
              "  stop using NSCD or ask NSCD daemon for new shared area\n")
        (area.name);
      area.prot = PROT_READ | PROT_WRITE | MTCP_PROT_ZERO_PAGE;
      area.flags = MAP_PRIVATE | MAP_ANONYMOUS;

      /* We're still using proc-maps in mtcp_readmapsline();
       * So, remap NSCD later.
       */
      remap_nscd_areas_array[num_remap_nscd_areas++] = area;
      Util::writeAll(fd, &area, sizeof(area));
      continue;
    }
    else if (Util::strStartsWith(area.name, INFINIBAND_SHMEM_FILE)) {
      // TODO: Don't checkpoint infiniband shared area for now.
      continue;
    }
    else if (Util::strEndsWith(area.name, DELETED_FILE_SUFFIX)) {
      /* Deleted File */
    } else if (area.name[0] == '/' && strstr(&area.name[1], "/") != NULL) {
      /* If an absolute pathname
       * Posix and SysV shared memory segments can be mapped as /XYZ
       */
#if 0
      struct stat statbuf;
      unsigned int long devnum;
      if (stat(area.name, &statbuf) < 0) {
        JWARNING(false) (area.name) (JASSERT_ERRNO)
          .Text("Error statting file.");
      } else {
        devnum = makedev(dev_info.devmajor, dev_info.devminor);
        JWARNING (devnum == statbuf.st_dev && dev_info.inodenum == statbuf.st_ino)
          (area.name) (statbuf.st_dev) (statbuf.st_ino)
          (devnum) (dev_info.inodenum);
      }
#endif
    }

    area.filesize = 0;
    if (area.name[0] != '\0') {
      int ffd = _real_open(area.name, O_RDONLY, 0);
      if (ffd != -1) {
        area.filesize = lseek(ffd, 0, SEEK_END);
        if (area.filesize == -1)
          area.filesize = 0;
      }
      _real_close(ffd);
    }

    /* Force the anonymous flag if it's a private writeable section, as the
     * data has probably changed from the contents of the original images.
     */

    /* We also do this for read-only private sections as it's possible
     * to modify a page there, too (via mprotect).
     */

    if ((area.flags & MAP_PRIVATE) /*&& (area.prot & PROT_WRITE)*/) {
      area.flags |= MAP_ANONYMOUS;
    }

    if (area.flags & MAP_SHARED) {
      /* invalidate shared memory pages so that the next read to it (when we are
       * writing them to ckpt file) will cause them to be reloaded from the
       * disk.
       */
      JASSERT(msync(area.addr, area.size, MS_INVALIDATE) == 0)
        (area.addr) (area.size) (area.name) (area.offset) (JASSERT_ERRNO);
    }


    /* Only write this image if it is not CS_RESTOREIMAGE.
     * Skip any mapping for this image - it got saved as CS_RESTOREIMAGE
     * at the beginning.
     */

    if (strstr (area.name, "[stack]"))
      stack_was_seen = 1;
    // the whole thing comes after the restore image
    writememoryarea(fd, &area, stack_was_seen, vsyscall_exists);
  }

  /* It's now safe to do this, since we're done using mtcp_readmapsline() */
  remap_nscd_areas(remap_nscd_areas_array, num_remap_nscd_areas);

  close (mapsfd);

  area.addr = NULL; // End of data
  area.size = -1; // End of data
  Util::writeAll(fd, &area, sizeof(area));

  /* That's all folks */
  JASSERT(_real_close (fd) == 0);
}

/* FIXME:
 * We should read /proc/self/maps into temporary array and mtcp_readmapsline
 * should then read from it.  This is cleaner than this hack here.
 * Then this body can go back to replacing:
 *    remap_nscd_areas_array[num_remap_nscd_areas++] = area;
 * - Gene
 */
static void remap_nscd_areas(Area remap_nscd_areas_array[],
			     int  num_remap_nscd_areas)
{
  Area *area;
  for (area = remap_nscd_areas_array; num_remap_nscd_areas-- > 0; area++) {
    JASSERT(area->flags != END_OF_NSCD_AREAS)
      .Text("Too many NSCD areas to remap.");
    JASSERT(munmap(area->addr, area->size) == 0) (JASSERT_ERRNO)
      .Text("error unmapping NSCD shared area");
    JASSERT(mmap(area->addr, area->size, area->prot, area->flags, 0, 0)
            != MAP_FAILED)
      (JASSERT_ERRNO) .Text("error remapping NSCD shared area.");
    memset(area->addr, 0, area->size);
  }
}


/* This function returns a range of zero or non-zero pages. If the first page
 * is non-zero, it searches for all contiguous non-zero pages and returns them.
 * If the first page is all-zero, it searches for contiguous zero pages and
 * returns them.
 */
static void mtcp_get_next_page_range(Area *area, size_t *size, int *is_zero)
{
  char *pg;
  char *prevAddr;
  size_t count = 0;
  const size_t one_MB = (1024 * 1024);
  if (area->size < one_MB) {
    *size = area->size;
    *is_zero = 0;
    return;
  }
  *size = one_MB;
  *is_zero = Util::areZeroPages(area->addr, one_MB / MTCP_PAGE_SIZE);
  prevAddr = area->addr;
  for (pg = area->addr + one_MB;
       pg < area->addr + area->size;
       pg += one_MB) {
    size_t minsize = MIN(one_MB, (size_t)(area->addr + area->size - pg));
    if (*is_zero != Util::areZeroPages(pg, minsize / MTCP_PAGE_SIZE)) {
      break;
    }
    *size += minsize;
    if (*is_zero && ++count % 10 == 0) { // madvise every 10MB
      if (madvise(prevAddr, area->addr + *size - prevAddr,
                  MADV_DONTNEED) == -1) {
        JNOTE("error doing madvise(..., MADV_DONTNEED)")
          (JASSERT_ERRNO) ((void*)area->addr) ((int)*size);
        prevAddr = pg;
      }
    }
  }
}

static void mtcp_write_non_rwx_and_anonymous_pages(int fd, Area *orig_area)
{
  Area area = *orig_area;
  /* Now give read permission to the anonymous pages that do not have read
   * permission. We should remove the permission as soon as we are done
   * writing the area to the checkpoint image
   *
   * NOTE: Changing the permission here can results in two adjacent memory
   * areas to become one (merged), if they have similar permissions. This can
   * results in a modified /proc/self/maps file. We shouldn't get affected by
   * the changes because we are going to remove the PROT_READ later in the
   * code and that should reset the /proc/self/maps files to its original
   * condition.
   */
  JASSERT(orig_area->name[0] == '\0');

  if ((orig_area->prot & PROT_READ) == 0) {
    JASSERT(mprotect(orig_area->addr, orig_area->size,
                     orig_area->prot | PROT_READ) == 0)
      (JASSERT_ERRNO) (orig_area->size) (orig_area->addr)
      .Text("error adding PROT_READ to mem region");
  }

  while (area.size > 0) {
    size_t size;
    int is_zero;
    Area a = area;
    if (dmtcp_infiniband_enabled && dmtcp_infiniband_enabled()) {
      size = area.size;
      is_zero = 0;
    } else {
      mtcp_get_next_page_range(&a, &size, &is_zero);
    }

    a.prot |= is_zero ? MTCP_PROT_ZERO_PAGE : 0;
    a.size = size;

    Util::writeAll(fd, &a, sizeof(a));
    if (!is_zero) {
      Util::writeAll(fd, a.addr, a.size);
    } else {
      if (madvise(a.addr, a.size, MADV_DONTNEED) == -1) {
        JNOTE("error doing madvise(..., MADV_DONTNEED)")
          (JASSERT_ERRNO) (a.addr) ((int)a.size);
      }
    }
    area.addr += size;
    area.size -= size;
  }

  /* Now remove the PROT_READ from the area if it didn't have it originally
  */
  if ((orig_area->prot & PROT_READ) == 0) {
    JASSERT(mprotect(orig_area->addr, orig_area->size, orig_area->prot) == 0)
      (JASSERT_ERRNO) (orig_area->addr) (orig_area->size)
      .Text("error removing PROT_READ from mem region.");
  }
}

static void writememoryarea (int fd, Area *area, int stack_was_seen,
			     int vsyscall_exists)
{
  static void * orig_stack = NULL;
  void *addr = area->addr;

  /* Write corresponding descriptor to the file */

  if (orig_stack == NULL && 0 == strcmp(area -> name, "[stack]"))
    orig_stack = area -> addr + area -> size;

  if (0 == strcmp(area -> name, "[vdso]") && !stack_was_seen)
    JTRACE("skipping over [vdso] section") (addr) (area->size);
  else if (0 == strcmp(area -> name, "[vsyscall]") && !stack_was_seen)
    JTRACE("skipping over [vsyscall] section") (addr) (area->size);
  else if (0 == strcmp(area -> name, "[vectors]") && !stack_was_seen)
    JTRACE("skipping over [vectors] section") (addr) (area->size);
  else if (0 == strcmp(area -> name, "[stack]") &&
	   orig_stack != area -> addr + area -> size)
    /* Kernel won't let us munmap this.  But we don't need to restore it. */
    JTRACE("skipping over [stack] segment (not the orig stack)")
      (addr) (area->size);
  else if (!(area -> flags & MAP_ANONYMOUS))
    JTRACE("save region") (addr) (area->size) (area->name) (area->offset);
  else if (area -> name[0] == '\0')
    JTRACE("save anonymous") (addr) (area->size);
  else
    JTRACE("save anonymous") (addr) (area->size) (area->name) (area->offset);

  if ((area -> name[0]) == '\0') {
    char *brk = (char*)sbrk(0);
    if (brk > area -> addr && brk <= area -> addr + area -> size)
      strcpy(area -> name, "[heap]");
  }

  if (area->prot == 0 ||
      (area->name[0] == '\0' &&
       ((area->flags & MAP_ANONYMOUS) != 0) &&
       ((area->flags & MAP_PRIVATE) != 0))) {
    /* Detect zero pages and do not write them to ckpt image.
     * Currently, we detect zero pages in non-rwx mapping and anonymous
     * mappings only
     */
    mtcp_write_non_rwx_and_anonymous_pages(fd, area);
  } else if (0 != strcmp(area -> name, "[vsyscall]")
             && 0 != strcmp(area -> name, "[vectors]")
             && ((0 != strcmp(area -> name, "[vdso]")
                  || vsyscall_exists /* which implies vdso can be overwritten */
                  || !stack_was_seen))) /* If vdso appeared before stack, it can be
                                         replaced */
  {
    /* Anonymous sections need to have their data copied to the file,
     *   as there is no file that contains their data
     * We also save shared files to checkpoint file to handle shared memory
     *   implemented with backing files
     */
    JASSERT((area->flags & MAP_ANONYMOUS) || (area->flags & MAP_SHARED));
    Util::writeAll(fd, area, sizeof(*area));
    Util::writeAll(fd, area->addr, area->size);
  }
}

static void preprocess_special_segments(int *vsyscall_exists)
{
  Area area;
  int mapsfd = _real_open("/proc/self/maps", O_RDONLY);
  JASSERT(mapsfd != -1) (JASSERT_ERRNO) .Text("Error opening /proc/self/maps");

  while (Util::readProcMapsLine(mapsfd, &area)) {
    if (0 == strcmp(area.name, "[vsyscall]")) {
      /* Determine if [vsyscall] exists.  If [vdso] and [vsyscall] exist,
       * [vdso] will be saved and restored.
       * NOTE:  [vdso] is relocated if /proc/sys/kernel/randomize_va_space == 2.
       * We must restore old [vdso] and also keep [vdso] in that case.
       * On Linux 2.6.25:
       *   32-bit Linux has:  [heap], /lib/ld-2.7.so, [vdso], libs, [stack].
       *   64-bit Linux has:  [stack], [vdso], [vsyscall].
       * and at least for gcl, [stack], libmtcp.so, [vsyscall] seen.
       * If 32-bit process in 64-bit Linux:
       *     [stack] (0xffffd000), [vdso] (0xffffe0000)
       * On 32-bit Linux, mtcp_restart has [vdso], /lib/ld-2.7.so, [stack]
       * Need to restore old [vdso] into mtcp_restart, to restart.
       * With randomize_va_space turned off, libraries start at high address
       *     0xb8000000 and are loaded progressively at lower addresses.
       * mtcp_restart loads vdso (which looks like a shared library) first.
       * But libpthread/libdl/libc libraries are loaded above vdso in user
       * image.
       * So, we must use the opposite of the user's setting (no randomization if
       *     user turned it on, and vice versa).  We must also keep the
       *     new vdso segment, provided by mtcp_restart.
       */
      *vsyscall_exists = 1;
    } else if (/*!mtcp_saved_heap_start && */strcmp(area.name, "[heap]") == 0) {
      // Record start of heap which will later be used in mtcp_restore_finish()
      //mtcp_saved_heap_start = area.addr;
    } else if (strcmp(area.name, "[stack]") == 0) {
      /*
       * When using Matlab with dmtcp_launch, sometimes the bottom most
       * page of stack (the page with highest address) which contains the
       * environment strings and the argv[] was not shown in /proc/self/maps.
       * This is arguably a bug in the Linux kernel as of version 2.6.32, etc.
       * This happens on some odd combination of environment passed on to
       * Matlab process. As a result, the page was not checkpointed and hence
       * the process segfaulted on restart. The fix is to try to mprotect this
       * page with RWX permission to make the page visible again. This call
       * will fail if no stack page was invisible to begin with.
       */
      // FIXME : If the area following the stack is not empty, don't
      //         exercise this path.
      int ret = mprotect(area.addr + area.size, 0x1000,
                         PROT_READ | PROT_WRITE | PROT_EXEC);
      if (ret == 0) {
        JNOTE("bottom-most page of stack (page with highest address) was \n"
              "  invisible in /proc/self/maps. It is made visible again now.");
      }
    }
  }
  close(mapsfd);
}

#if 0

/*****************************************************************************
 *
 *  Sync shared memory pages with backup files on disk
 *
 *****************************************************************************/
static void sync_shared_mem(void)
{
  int mapsfd;
  Area area;

  mapsfd = _real_open("/proc/self/maps", O_RDONLY);
  JASSERT(mapsfd != -1) (JASSERT_ERRNO) .Text("Error opening /proc/self/maps");

  while (mtcp_readmapsline (mapsfd, &area, NULL)) {
    /* Skip anything that has no read or execute permission.  This occurs on one
     * page in a Linux 2.6.9 installation.  No idea why.  This code would also
     * take care of kernel sections since we don't have read/execute permission
     * there.
     */

    if (!((area.prot & PROT_READ) || (area.prot & PROT_WRITE))) continue;

    if (!(area.flags & MAP_SHARED)) continue;

    if (Util::strEndsWith(area.name, DELETED_FILE_SUFFIX)) continue;

    /* Don't sync the DRI shared memory region for OpenGL */
    if (Util::strStartsWith(area.name, DEV_DRI_SHMEM)) continue;

#ifdef IBV
    // TODO: Don't checkpoint infiniband shared area for now.
   if (Util::strstartswith(area.name, INFINIBAND_SHMEM_FILE)) {
     continue;
   }
#endif

    JTRACE("syncing shared memory region")
      (area.size) (area.addr) (area.name) (area.offset);

    JASSERT(msync(area.addr, area.size, MS_SYNC) == 0)
      (area.addr) (area.size) (area.name) (area.offset) (JASSERT_ERRNO);
  }

  _real_close(mapsfd);
}
#endif
