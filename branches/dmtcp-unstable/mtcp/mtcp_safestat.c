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
