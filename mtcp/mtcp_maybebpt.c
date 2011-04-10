/*****************************************************************************
 *   Copyright (C) 2006-2008 by Michael Rieker, Jason Ansel, Kapil Arya, and *
 *                                                            Gene Cooperman *
 *   mrieker@nii.net, jansel@csail.mit.edu, kapil@ccs.neu.edu, and           *
 *                                                          gene@ccs.neu.edu *
 *                                                                           *
 *   This file is part of the MTCP module of DMTCP (DMTCP:mtcp).             *
 *                                                                           *
 *  DMTCP:mtcp is free software: you can redistribute it and/or              *
 *  modify it under the terms of the GNU Lesser General Public License as    *
 *  published by the Free Software Foundation, either version 3 of the       *
 *  License, or (at your option) any later version.                          *
 *                                                                           *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,       *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *  GNU Lesser General Public License for more details.                      *
 *                                                                           *
 *  You should have received a copy of the GNU Lesser General Public         *
 *  License along with DMTCP:dmtcp/src.  If not, see                         *
 *  <http://www.gnu.org/licenses/>.                                          *
 *****************************************************************************/

/*****************************************************************************
 *
 *  If parent is gdb, do a breakpoint, else nop
 *
 *****************************************************************************/

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

// Force mtcp_sys.h to define this.
#define MTCP_SYS_MEMCPY
#define MTCP_SYS_MEMMOVE
#include "mtcp_internal.h"
#define mtcp_sys_memcmp memcmp

void mtcp_maybebpt (void)

{
  char buff[64];
  int i, j;
  pid_t pid;

  static int known = -1;

  if (known < 0) { // see if it is known if we are child of gdb
    known = 0;     // not known, assume that we aren't child of gdb
    pid = mtcp_sys_getppid ();           // get parent's process id
    for (i = sizeof buff; -- i >= 0;) {  // convert to decimal string
      buff[i] = (pid % 10) + '0';  // (sprintf uses all kinds of tls crapola)
      pid /= 10;
      if (pid == 0) break;
    }
    mtcp_sys_memcpy (buff, "/proc/", 6); // make /proc/<ppid>/cmdline string
    mtcp_sys_memmove (buff + 6, buff + i, sizeof buff - i);
    i = 6 + sizeof buff - i;
    mtcp_sys_memcpy (buff + i, "/cmdline", 9);
    j = mtcp_sys_open2 (buff, O_RDONLY); // open that file
    if (j >= 0) {
      i = mtcp_sys_read (j, buff, sizeof buff); // read parent's command line
      mtcp_sys_close (j);
      if (mtcp_sys_memcmp (buff, "gdb", 3) == 0) {  // see if it begins with gdb
        known = 1;                                  // if so, parent is gdb
      }
    }
  }

  if (known > 0) { // see if we know our parent is gdb
    asm volatile ("int3"); // if so, we do a breakpoint
  }
}
