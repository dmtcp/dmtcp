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
/*  If parent is gdb, do a breakpoint, else nop											*/
/*																*/
/********************************************************************************************************************************/

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

  if (known < 0) {                                  // see if it is known if we are child of gdb
    known = 0;                                      // not known, assume that we aren't child of gdb
    pid = mtcp_sys_getppid ();                         // get parent's process id
    for (i = sizeof buff; -- i >= 0;) {             // convert to decimal string
      buff[i] = (pid % 10) + '0';                     // (sprintf uses all kinds of tls crapola)
      pid /= 10;
      if (pid == 0) break;
    }
    mtcp_sys_memcpy (buff, "/proc/", 6);             // make /proc/<ppid>/cmdline string
    mtcp_sys_memmove (buff + 6, buff + i, sizeof buff - i);
    i = 6 + sizeof buff - i;
    mtcp_sys_memcpy (buff + i, "/cmdline", 9);
    j = mtcp_sys_open2 (buff, O_RDONLY);                // open that file
    if (j >= 0) {
      i = mtcp_sys_read (j, buff, sizeof buff);      // read parent's command line
      mtcp_sys_close (j);
      if (mtcp_sys_memcmp (buff, "gdb", 3) == 0) {   // see if it begins with gdb
        known = 1;                                  // if so, parent is gdb
      }
    }
  }

  if (known > 0) {                                  // see if we know our parent is gdb
    asm volatile ("int3");                          // if so, we do a breakpoint
  }
}
