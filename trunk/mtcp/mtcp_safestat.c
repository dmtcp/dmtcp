//+++2006-01-17
//    Copyright (C) 2006 - 2007  Mike Rieker, Beverly, MA USA
//    Modifications to make it 64-bit clean by Gene Cooperman
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
/*  No real reason for this one, except the linker barfs out because mtcp.so would try to access stat directly (although it 	*/
/*  access stuff like lseek, etc).  Go figure.											*/
/*																*/
/*  The only difference is we just return mtcp_sys_errno as the return code so we don't have to @#$@# with errno carpola.		*/
/*																*/
/********************************************************************************************************************************/

#include <sys/stat.h>
#include <sys/syscall.h>

#include "mtcp_internal.h"

// This file came from:
//      glibc-2.5/sysdeps/unix/sysv/linux/kernel_stat.h
#include "glibc_kernel_stat.h" // statbuf used by kernel (file was taken from glibc)
                               // it's not the same as in <sys/stat.h> -- wretched thing

int mtcp_safestat (char const *name, struct Stat *buf)

{
  int rc;
  struct kernel_stat kbuf;

  rc = mtcp_sys_kernel_stat(name, &kbuf);
  /* rc < -4096: Returning an address in high memory could appear as negative */
  if (rc >= 0 || rc < -4096) {
    buf -> st_mode = kbuf.st_mode;
    buf -> st_dev  = kbuf.st_dev;
    buf -> st_ino  = kbuf.st_ino;
  }
  return (rc);
}

int mtcp_safelstat (char const *name, struct Stat *buf)

{
  int rc;
  struct kernel_stat kbuf;

  rc = mtcp_sys_kernel_lstat(name, &kbuf);
  /* rc < -4096: Returning an address in high memory could appear as negative */
  if (rc >= 0 || rc < -4096) {
    buf -> st_mode = kbuf.st_mode;
    buf -> st_dev  = kbuf.st_dev;
    buf -> st_ino  = kbuf.st_ino;
  }
  return (rc);
}
