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
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include "jassert.h"
#include "constants.h"
#include "dmtcp.h"
#include "processinfo.h"
#include "procmapsarea.h"
#include "procselfmaps.h"
#include "shareddata.h"
#include "util.h"

#define DEV_ZERO_DELETED_STR "/dev/zero (deleted)"
#define DEV_NULL_DELETED_STR "/dev/null (deleted)"

/* Shared memory regions for Direct Rendering Infrastructure */
#define DEV_DRI_SHMEM        "/dev/dri/card"

#define DELETED_FILE_SUFFIX  " (deleted)"


#define _real_open           NEXT_FNC(open)
#define _real_close          NEXT_FNC(close)

using namespace dmtcp;

EXTERNC int dmtcp_infiniband_enabled(void) __attribute__((weak));

static bool skipWritingTextSegments = false;

// FIXME:  Why do we create two global variable here?  They should at least
// be static (file-private), and preferably local to a function.
ProcSelfMaps *procSelfMaps = NULL;
vector<ProcMapsArea> *nscdAreas = NULL;

// FIXME:  If we allocate in the middle of reading
// /proc/self/maps, we modify the mapping.  But whenever we
// add to nscdAreas, we risk allocating memory.  So, we're depending
// on this memory being smaller than any pre-allocated memory,
// so that the memory allocator does not call mmap in the middle
// of reading /proc/self/maps.  A better design would be to create
// an nscdArea method of the ProcSelfMaps class, and that
// class can then be careful about allocating memory.


/* Internal routines */

// static void sync_shared_mem(void);
static void writememoryarea(int fd, Area *area, int stack_was_seen);

static void remap_nscd_areas(const vector<ProcMapsArea> &areas);

/*****************************************************************************
 *
 *  This routine is called from time-to-time to write a new checkpoint file.
 *  It assumes all the threads are suspended.
 *
 *  NOTE: Any memory allocated in this function should be released explicitly
 *  during the next ckpt cycle. Otherwise, on restart, we never come back to
 *  this function which can cause memory leaks.
 *
 *****************************************************************************/
void
mtcp_writememoryareas(int fd)
{
  Area area;

  // DeviceInfo dev_info;
  int stack_was_seen = 0;

  if (getenv(ENV_VAR_SKIP_WRITING_TEXT_SEGMENTS) != NULL) {
    skipWritingTextSegments = true;
  }

  JTRACE("Performing checkpoint.");

  // Here we want to sync the shared memory pages with the backup files
  // FIXME: Why do we need this?
  // JTRACE("syncing shared memory with backup files");
  // sync_shared_mem();

  /**************************************************************************/
  /* We can't do any more mallocing at this point because malloc stuff is   */
  /* outside the limits of the libmtcp.so image, so it won't get            */
  /* checkpointed, and it's possible that we would checkpoint an            */
  /* inconsistent state.  See note in restoreverything routine.             */
  /**************************************************************************/

  {
    if (nscdAreas == NULL) {
      nscdAreas = new vector<ProcMapsArea>();
    }
    nscdAreas->clear();

    // This block is to ensure that the object is deleted as soon as we leave
    // this block.
    ProcSelfMaps procSelfMaps;

    // Preprocess memory regions as needed.
    while (procSelfMaps.getNextArea(&area)) {
      if (Util::isNscdArea(area)) {
        /* Special Case Handling: nscd is enabled*/
        JTRACE("NSCD daemon shared memory area present.\n"
               "  DMTCP will now try to remap this area in read/write mode as\n"
               "  private (zero pages), so that glibc will automatically\n"
               "  stop using NSCD or ask NSCD daemon for new shared area\n")
          (area.name);

        nscdAreas->push_back(area);
      }
    }
  }

  if (procSelfMaps != NULL) {
    // We need to explicitly delete this object here because on restart, we
    // never get back to this function and the object is never released.
    delete procSelfMaps;
  }

  /* Finally comes the memory contents */
  JTRACE("addr and len of restoreBuf (to hold mtcp_restart code)")
    ((void *)ProcessInfo::instance().restoreBufAddr())
    (ProcessInfo::instance().restoreBufLen());
  procSelfMaps = new ProcSelfMaps();
  // We must not cause an mmap() here, or the mem regions will not be correct.
  while (procSelfMaps->getNextArea(&area)) {
    // TODO(kapil): Verify that we are not doing any operation that might
    // result in a change of memory layout. For example, a call to JALLOC_NEW
    // will invoke mmap if the JAlloc arena is full. Similarly, for STL objects
    // such as vector and string.

    if ((uint64_t)area.addr == ProcessInfo::instance().restoreBufAddr()) {
      JASSERT(area.size == ProcessInfo::instance().restoreBufLen())
        ((void *)area.addr)
        (area.size)
        (ProcessInfo::instance().restoreBufLen());
      continue;
    } else if (SharedData::isSharedDataRegion(area.addr)) {
      continue;
    }

    /* Original comment:  Skip anything in kernel address space ---
     *   beats me what's at FFFFE000..FFFFFFFF - we can't even read it;
     * Added: That's the vdso section for earlier Linux 2.6 kernels.  For later
     *  2.6 kernels, vdso occurs at an earlier address.  If it's unreadable,
     *  then we simply won't copy it.  But let's try to read all areas, anyway.
     * **COMMENTED OUT:** if (area.addr >= HIGHEST_VA) continue;
     */

    /* If it's readable, but it's VDSO, it will be dangerous to restore it.
     * In 32-bit mode later Red Hat RHEL Linux 2.6.9 releases use 0xffffe000,
     * the last page of virtual memory.  Note 0xffffe000 >= HIGHEST_VA
     * implies we're in 32-bit mode.
     */
    if (area.addr >= HIGHEST_VA && area.addr == (VA)0xffffe000) {
      continue;
    }
#ifdef __x86_64__

    /* And in 64-bit mode later Red Hat RHEL Linux 2.6.9 releases
     * use 0xffffffffff600000 for VDSO.
     */
    if (area.addr >= HIGHEST_VA && area.addr == (VA)0xffffffffff600000) {
      continue;
    }
#endif // ifdef __x86_64__

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
     *
     * Also, on SUSE 12, if this region was part of heap, the protected region
     * may have the label "[heap]".  So, we also save the memory region if it
     * has label "[heap]", "[stack]", or  "[stack:XXX]".
     */

    if (!((area.prot & PROT_READ) || (area.prot & PROT_WRITE)) &&
        (area.name[0] != '\0') && strcmp(area.name, "[heap]") &&
        strcmp(area.name,
               "[stack]") && (!Util::strStartsWith(area.name, "[stack:XXX]"))) {
      continue;
    }

    if (Util::strStartsWith(area.name, DEV_ZERO_DELETED_STR) ||
        Util::strStartsWith(area.name, DEV_NULL_DELETED_STR)) {
      /* If the process has an area labeled as "/dev/zero (deleted)", we mark
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
    } else if (Util::isSysVShmArea(area)) {
      JTRACE("saving area as Anonymous") (area.name);
      area.flags = MAP_PRIVATE | MAP_ANONYMOUS;
      area.name[0] = '\0';
    } else if (Util::isNscdArea(area)) {
      /* Special Case Handling: nscd is enabled*/
      area.prot = PROT_READ | PROT_WRITE;
      area.properties |= DMTCP_ZERO_PAGE;
      area.flags = MAP_PRIVATE | MAP_ANONYMOUS;
      Util::writeAll(fd, &area, sizeof(area));
      continue;
    } else if (Util::isIBShmArea(area)) {
      // TODO: Don't checkpoint infiniband shared area for now.
      continue;
    } else if (Util::strEndsWith(area.name, DELETED_FILE_SUFFIX)) {
      /* Deleted File */
    } else if (area.name[0] == '/' && strstr(&area.name[1], "/") != NULL) {
      /* If an absolute pathname
       * Posix and SysV shared memory segments can be mapped as /XYZ
       */
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

    /* Only write this image if it is not CS_RESTOREIMAGE.
     * Skip any mapping for this image - it got saved as CS_RESTOREIMAGE
     * at the beginning.
     */

    if (strstr(area.name, "[stack]")) {
      stack_was_seen = 1;
    }

    // the whole thing comes after the restore image
    writememoryarea(fd, &area, stack_was_seen);
  }

  // Release the memory.
  delete procSelfMaps;
  procSelfMaps = NULL;

  /* It's now safe to do this, since we're done using writememoryarea() */
  remap_nscd_areas(*nscdAreas);

  area.addr = NULL; // End of data
  area.size = -1; // End of data
  Util::writeAll(fd, &area, sizeof(area));

  /* That's all folks */
  JASSERT(_real_close(fd) == 0);
}

static void
remap_nscd_areas(const vector<ProcMapsArea> &areas)
{
  for (size_t i = 0; i < areas.size(); i++) {
    JASSERT(munmap(areas[i].addr, areas[i].size) == 0) (JASSERT_ERRNO)
    .Text("error unmapping NSCD shared area");
    JASSERT(mmap(areas[i].addr, areas[i].size, areas[i].prot,
                 MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, 0, 0) != MAP_FAILED)
      (JASSERT_ERRNO).Text("error remapping NSCD shared area.");
  }
}

/* This function returns a range of zero or non-zero pages. If the first page
 * is non-zero, it searches for all contiguous non-zero pages and returns them.
 * If the first page is all-zero, it searches for contiguous zero pages and
 * returns them.
 */
static void
mtcp_get_next_page_range(Area *area, size_t *size, int *is_zero)
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
          (JASSERT_ERRNO) ((void *)area->addr) ((int)*size);
        prevAddr = pg;
      }
    }
  }
}

static void
mtcp_write_non_rwx_and_anonymous_pages(int fd, Area *orig_area)
{
  Area area = *orig_area;

  /* Now give read permission to the anonymous/[heap]/[stack]/[stack:XXX] pages
   * that do not have read permission. We should remove the permission
   * as soon as we are done writing the area to the checkpoint image
   *
   * NOTE: Changing the permission here can results in two adjacent memory
   * areas to become one (merged), if they have similar permissions. This can
   * results in a modified /proc/self/maps file. We shouldn't get affected by
   * the changes because we are going to remove the PROT_READ later in the
   * code and that should reset the /proc/self/maps files to its original
   * condition.
   */

  JASSERT(orig_area->name[0] == '\0' || (strcmp(orig_area->name,
                                                "[heap]") == 0) ||
          (strcmp(orig_area->name, "[stack]") == 0) ||
          (Util::strStartsWith(area.name, "[stack:XXX]")));

  if ((orig_area->prot & PROT_READ) == 0) {
    JASSERT(mprotect(orig_area->addr, orig_area->size,
                     orig_area->prot | PROT_READ) == 0)
      (JASSERT_ERRNO) (orig_area->size) (orig_area->addr)
    .Text("error adding PROT_READ to mem region");
  }

  while (area.size > 0) {
    int rc = 0;
    size_t size;
    int is_zero;
    Area a = area;
    if (dmtcp_infiniband_enabled && dmtcp_infiniband_enabled()) {
      size = area.size;
      is_zero = 0;
    } else {
      mtcp_get_next_page_range(&a, &size, &is_zero);
    }

    a.properties = is_zero ? DMTCP_ZERO_PAGE : 0;
    a.size = size;

    rc = Util::writeAll(fd, &a, sizeof(a));
    JASSERT(rc != -1)(JASSERT_ERRNO).Text("writeAll failed during ckpt");
    if (!is_zero) {
      rc = Util::writeAll(fd, a.addr, a.size);
      JASSERT(rc != -1)(JASSERT_ERRNO).Text("writeAll failed during ckpt");
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

static void
writememoryarea(int fd, Area *area, int stack_was_seen)
{
  int rc = 0;
  void *addr = area->addr;

  if (!(area->flags & MAP_ANONYMOUS)) {
    JTRACE("save region") (addr) (area->size) (area->name) (area->offset);
  } else if (area->name[0] == '\0') {
    JTRACE("save anonymous") (addr) (area->size);
  } else {
    JTRACE("save anonymous") (addr) (area->size) (area->name) (area->offset);
  }

  if ((area->name[0]) == '\0') {
    char *brk = (char *)sbrk(0);
    if (brk > area->addr && brk <= area->addr + area->size) {
      strcpy(area->name, "[heap]");
    }
  }

  if (area->size == 0) {
    /* Kernel won't let us munmap this.  But we don't need to restore it. */
    JTRACE("skipping over [stack] segment (not the orig stack)")
      (addr) (area->size);
  } else if (0 == strcmp(area -> name, "[vsyscall]") ||
             0 == strcmp(area -> name, "[vectors]") ||
             0 == strcmp(area -> name, "[vvar]"))
             // NOTE: We can't trust kernel's "[vdso]" label here.  See below.
  {
    JTRACE("skipping over memory special section")
      (area->name) (addr) (area->size);
  } else if ( area->__addr == ProcessInfo::instance().vdsoStart() ) {
    //  vDSO issue:
    //    As always, we never want to save the vdso section.  We will use
    //  the vdso section code provided by the kernel on restart.  Further,
    //  the user code on restart has already been initialized and so it
    //  will continue to use the original vdso section determined during
    //  program launch.  Luckily, during the DMTCP_INIT event, DMTCP recorded
    //  this vdso address when it called ProcessInfo::instance().init().
    //    Now, here's the bad news.  During the first restart, the kernel
    //  may choose to locate the vdso at a new address.  So, in
    //  src/mtcp/mtcp_restart, DMTCP will mremap the kernel's vdso back
    //  to the original address known during program launch.  This is as
    //  it should be.  But when DMTCP does an mremap of vdso, the kernel
    //  fails to update its own "[vds0]" label.  So, during the second
    //  checkpoint (after the first restart), we can't trust the "[vdso]"
    //  label to tell us where the vdso section really is.  And it's even
    //  worse.  During mtcp_restart, we may have done the mremap, and there
    //  may even now be some user data that was restored to the address
    //  where the kernel thinks the "[vdso]" label belongs.  So, we would
    //  be saving the original vdso section (which is wrong), and we would
    //  be failing to save the user's memory that was restored into the
    //  location labelled by the kernel's "[vdso]" label.  This last
    //  case is even worse, since we have now failed to restore some user data.
    //    This was observed to happen in RHEL 6.6.  The solution is to
    //  trust DMTCP for the vdso location (as in the if condition above),
    //  and not to trust the kernel's "[vdso]" label.
    JTRACE("skipping vDSO special section")
      (area->name) (addr) (area->size);
  } else if (area->prot == 0 ||
             (area->name[0] == '\0' &&
              ((area->flags & MAP_ANONYMOUS) != 0) &&
              ((area->flags & MAP_PRIVATE) != 0))) {
    /* Detect zero pages and do not write them to ckpt image.
     * Currently, we detect zero pages in non-rwx mapping and anonymous
     * mappings only
     */
    mtcp_write_non_rwx_and_anonymous_pages(fd, area);
  } else {
    /* Anonymous sections need to have their data copied to the file,
     *   as there is no file that contains their data
     * We also save shared files to checkpoint file to handle shared memory
     *   implemented with backing files
     */
    JASSERT((area->flags & MAP_ANONYMOUS) || (area->flags & MAP_SHARED));

    if (skipWritingTextSegments && (area->prot & PROT_EXEC)) {
      area->properties |= DMTCP_SKIP_WRITING_TEXT_SEGMENTS;
      rc = Util::writeAll(fd, area, sizeof(*area));
      JASSERT(rc != -1)(JASSERT_ERRNO).Text("writeAll failed during ckpt");
      JTRACE("Skipping over text segments") (area->name) ((void *)area->addr);
    } else {
      rc = Util::writeAll(fd, area, sizeof(*area));
      JASSERT(rc != -1)(JASSERT_ERRNO).Text("writeAll failed during ckpt");
      rc = Util::writeAll(fd, area->addr, area->size);
      JASSERT(rc != -1)(JASSERT_ERRNO).Text("writeAll failed during ckpt");
    }
  }
}
