/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
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

#include "dmtcprestartinternal.h"
#include "../include/procmapsarea.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

using namespace dmtcp;

#define MTCP_RESTART_BINARY "mtcp_restart"

static int restoreMemoryArea(int fd, DmtcpCkptHeader *ckptHdr);

int
main(int argc, char **argv)
{
  DmtcpRestart dmtcpRestart(argc, argv, BINARY_NAME, MTCP_RESTART_BINARY);

  string ckptImage = dmtcpRestart.ckptImages[0];

  RestoreTarget *t = new RestoreTarget(ckptImage);
  JASSERT(t->pid() != 0);

  t->initialize();

  // If we were the session leader, become one now.
  if (t->sid() == t->pid()) {
    if (getsid(0) != t->pid()) {
      JWARNING(setsid() != -1)
      (getsid(0))(JASSERT_ERRNO)
        .Text("Failed to restore this process as session leader.");
    }
  }

  DmtcpCkptHeader ckptHdr;
  int rc = read(t->fd(), &ckptHdr, sizeof (ckptHdr));
  ASSERT_EQ(rc, sizeof(ckptHdr));

  ASSERT_EQ(string(ckptHdr.ckptSignature), string(DMTCP_CKPT_SIGNATURE));

  // restore_vdso_vvar(rinfo);
  // restore_brk(rinfo);

  while (1) {
    if (restoreMemoryArea(t->fd(), &ckptHdr) == -1) {
      break; /* end of ckpt image */
    }
  }

  /* Everything restored, close file and finish up */
  close(t->fd());

  // IMB; /* flush instruction cache, since mtcp_restart.c code is now gone. */

  PostRestartFnPtr_t postRestart = (PostRestartFnPtr_t) ckptHdr.postRestartAddr;
  postRestart(0, 0);

  // Unreachable.
  return 0;
}


static int restoreMemoryArea(int fd, DmtcpCkptHeader *ckptHdr)
{
  int imagefd;
  void *mmappedat;

  /* Read header of memory area into area; mtcp_readfile() will read header */
  ProcMapsArea area;

  int rc = read(fd, &area, sizeof area);
  if (area.addr == NULL) {
    return -1;
  }

  if (area.name[0] && strstr(area.name, "[heap]")
      && (VA) brk(NULL) != area.addr + area.size) {
    JWARNING(false);
   // ("WARNING: break (%p) not equal to end of heap (%p)\n", mtcp_sys_brk(NULL), area.addr + area.size);
  }

  /* MAP_GROWSDOWN flag is required for stack region on restart to make
   * stack grow automatically when application touches any address within the
   * guard page region(usually, one page less then stack's start address).
   *
   * The end of stack is detected dynamically at checkpoint time. See
   * prepareMtcpHeader() in threadlist.cpp and ProcessInfo::growStack()
   * in processinfo.cpp.
   */
  if ((area.name[0] && area.name[0] != '/' && strstr(area.name, "stack"))
      || (area.endAddr == (VA) ckptHdr->endOfStack)) {
    area.flags = area.flags | MAP_GROWSDOWN;
    JTRACE("Detected stack area")(ckptHdr->endOfStack) (area.endAddr);
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
    JTRACE("restoring zero-paged anonymous area, %p bytes at %p\n") (area.size) (area.addr);
    // No need to mmap since the region has already been mmapped by the parent
    // header.
    // Just restore write-protection if needed.
    if (!(area.prot & PROT_WRITE)) {
      JASSERT(mprotect(area.addr, area.size, area.prot) == 0);
    }
  }

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
        imagefd = open(area.name, O_RDONLY, 0);
        if (imagefd >= 0) {
          /* If the current file size is smaller than the original, we map the region
          * as private anonymous. Note that with this we lose the name of the region
          * but most applications may not care.
          */
          off_t curr_size = lseek(imagefd, 0, SEEK_END);
          JASSERT(curr_size != -1);
          if ((curr_size < area.offset + area.size) && (area.prot & PROT_WRITE)) {
            JTRACE("restoring non-anonymous area %s as anonymous: %p  bytes at %p\n") (area.name) (area.size) (area.addr);
            close(imagefd);
            imagefd = -1;
            area.offset = 0;
            area.flags |= MAP_ANONYMOUS;
          }
        }
      }

      if (area.flags & MAP_ANONYMOUS) {
        JTRACE("restoring anonymous area, %p  bytes at %p\n") (area.size) (area.addr);
      } else {
        JTRACE("restoring to non-anonymous area,"
                " %p bytes at %p from %s + 0x%X\n") (area.size) (area.addr) (area.name) (area.offset);
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
        mmap(area.addr, area.size, area.prot | PROT_WRITE,
                            area.flags | MAP_FIXED_NOREPLACE, imagefd, area.offset);

      JASSERT(mmappedat == area.addr);

      /* Close image file (fd only gets in the way) */
      if (imagefd >= 0) {
        close(imagefd);
      }
    }

    if ((area.properties & DMTCP_ZERO_PAGE_PARENT_HEADER) == 0) {
      // Parent header doesn't have any follow on data.

      /* This mmapfile after prev. mmap is okay; use same args again.
       *  Posix says prev. map will be munmapped.
       */

      /* ANALYZE THE CONDITION FOR DOING mmapfile MORE CAREFULLY. */
      if (area.mmapFileSize > 0 && area.name[0] == '/') {
        JTRACE("restoring memory region %p of %p bytes at %p\n") (area.mmapFileSize) (area.size) (area.addr);
        int rc = read(fd, area.addr, area.mmapFileSize);
        JASSERT(rc == area.mmapFileSize);
      } else {
        int rc = read(fd, area.addr, area.size);
        JASSERT(rc == area.size);
      }

      if (!(area.prot & PROT_WRITE)) {
        JASSERT(mprotect(area.addr, area.size, area.prot) == 0);
      }
    }
  }
  return 0;
}
