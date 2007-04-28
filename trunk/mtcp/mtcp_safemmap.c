//+++2006-01-17
//    Copyright (C) 2006  Mike Rieker, Beverly, MA USA
//    EXPECT it to FAIL when someone's HeALTh or PROpeRTy is at RISk
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; version 2 of the License.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//---2006-01-17

/********************************************************************************************************************************/
/*																*/
/*  Just like mmap, except it fails (EBUSY) if you do a MAP_FIXED and the space is already (partially) occupied			*/
/*  The regular mmap will unmap anything that was there and overlay it with the new stuff					*/
/*																*/
/********************************************************************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "mtcp_internal.h"

/* This goes with mtcp_sys.h.  We allocate it here in mtcp_safemmap.c,
 * because any object using mtcp_sys.h should also be
 * linking with mtcp_safemmap.c.
 */
int mtcp_sys_errno;

__attribute__ ((visibility ("hidden"))) void * mtcp_safemmap (void *start, size_t length, int prot, int flags, int fd, off_t offset)

{
  char c;
  int indx, mapsfd, size;
  VA endaddr, startaddr;

  /* If mapping to a fixed address, make sure there's nothing there now */

  if (flags & MAP_FIXED) {

    /* Scan through the mappings of this process */

    mapsfd = mtcp_sys_open ("/proc/self/maps", O_RDONLY, 0);
    if (mapsfd < 0) return (MAP_FAILED);

    size = 0;
    indx = 0;

    while (1) {

      /* Read a line from /proc/self/maps */

      c = mtcp_readhex (mapsfd, &startaddr);
      if (c != '-') goto skipeol;
      c = mtcp_readhex (mapsfd, &endaddr);
      if (c != ' ') goto skipeol;

      /* If overlaps with what caller is trying to map, fail */

      if (((VA)start + length > startaddr) && ((VA)start < endaddr)) {
        mtcp_sys_close (mapsfd);
        mtcp_sys_errno = EBUSY;
        return (MAP_FAILED);
      }

      /* No overlap, skip to next line */

skipeol:
      while ((c != 0) && (c != '\n')) {
        c = mtcp_readchar (mapsfd);
      }
      if (c == 0) break;
    }

    /* It's ok */

    mtcp_sys_close (mapsfd);
  }

  return ((void *)mtcp_sys_mmap (start, length, prot, flags, fd, offset));
}
