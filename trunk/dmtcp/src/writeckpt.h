/*****************************************************************************
 *   Copyright (C) 2006-2009 by Michael Rieker, Jason Ansel, Kapil Arya, and *
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

#ifndef _MTCP_H
#define _MTCP_H

#include <sys/types.h>

// MTCP_PAGE_SIZE must be page-aligned:  multiple of sysconf(_SC_PAGESIZE).
#define MTCP_PAGE_SIZE 4096
#define MTCP_PAGE_MASK (~(MTCP_PAGE_SIZE-1))
#define MTCP_PAGE_OFFSET_MASK (MTCP_PAGE_SIZE-1)
#define FILENAMESIZE 1024

#ifndef HIGHEST_VA
// If 32-bit process in 64-bit Linux, then Makefile overrides this address,
// with correct address for that case.
# ifdef __x86_64__
 /* There's a segment, 7fbfffb000-7fc0000000 rw-p 7fbfffb000 00:00 0;
  * What is it?  It's busy (EBUSY) when we try to unmap it.
  */
// #  define HIGHEST_VA ((VA)0xFFFFFF8000000000)
// #  define HIGHEST_VA ((VA)0x8000000000)
#  define HIGHEST_VA ((VA)0x7f00000000)
# else
#  define HIGHEST_VA ((VA)0xC0000000)
# endif
#endif

// magic number at beginning of uncompressed ckpt file
#ifdef __x86_64__
# define MAGIC "MTCP64-V1.0"
# define MAGIC_LEN 12
#else
# define MAGIC "MTCP-V1.0"
# define MAGIC_LEN 10
#endif

#define NSCD_MMAP_STR1 "/var/run/nscd/"   /* OpenSUSE*/
#define NSCD_MMAP_STR2 "/var/cache/nscd"  /* Debian / Ubuntu*/
#define NSCD_MMAP_STR3 "/var/db/nscd"     /* RedHat / Fedora*/
#define DEV_ZERO_DELETED_STR "/dev/zero (deleted)"
#define DEV_NULL_DELETED_STR "/dev/null (deleted)"
#define SYS_V_SHMEM_FILE "/SYSV"
#ifdef IBV
# define INFINIBAND_SHMEM_FILE "/dev/infiniband/uverbs"
#endif

/* Shared memory regions for Direct Rendering Infrastructure */
#define DEV_DRI_SHMEM "/dev/dri/card"

#define DELETED_FILE_SUFFIX " (deleted)"

/* Let MTCP_PROT_ZERO_PAGE be a unique bit mask
 * This assumes: PROT_READ == 0x1, PROT_WRITE == 0x2, and PROT_EXEC == 0x4
 */
#define MTCP_PROT_ZERO_PAGE (PROT_EXEC << 1)

typedef char * VA; /* VA = virtual address */

typedef union Area {
  struct {
  int type; // Content type (CS_XXX
  char *addr;   // args required for mmap to restore memory area
  size_t size;
  off_t filesize;
  int prot;
  int flags;
  off_t offset;
  char name[FILENAMESIZE];
  };
  char _padding[4096];
} Area;

//typedef struct DeviceInfo {
//  unsigned int long devmajor;
//  unsigned int long devminor;
//  unsigned int long inodenum;
//} DeviceInfo;

#endif
