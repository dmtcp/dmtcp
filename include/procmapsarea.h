/****************************************************************************
 *   Copyright (C) 2012-2014 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef PROCMAPSAREA_H
#define PROCMAPSAREA_H
#include <stdint.h>
#include <sys/types.h>

// MTCP_PAGE_SIZE must be page-aligned:  multiple of sysconf(_SC_PAGESIZE).
#define MTCP_PAGE_SIZE        4096
#define MTCP_PAGE_MASK        (~(MTCP_PAGE_SIZE - 1))
#define MTCP_PAGE_OFFSET_MASK (MTCP_PAGE_SIZE - 1)
#define FILENAMESIZE          1024

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
# else // ifdef __x86_64__
#  define HIGHEST_VA ((VA)0xC0000000)
# endif // ifdef __x86_64__
#endif // ifndef HIGHEST_VA

#define DELETED_FILE_SUFFIX " (deleted)"

#define FILENAMESIZE        1024

typedef char *VA;  /* VA = virtual address */

typedef enum ProcMapsAreaProperties {
  DMTCP_ZERO_PAGE = 0x0001,
  DMTCP_SKIP_WRITING_TEXT_SEGMENTS = 0x0002
} ProcMapsAreaProperties;

typedef union ProcMapsArea {
  struct {
    union {
      VA addr;   // args required for mmap to restore memory area
      uint64_t __addr;
    };
    union {
      VA endAddr;   // args required for mmap to restore memory area
      uint64_t __endAddr;
    };
    union {
      size_t size;
      uint64_t __size;
    };
    union {
      off_t offset;
      uint64_t __offset;
    };
    union {
      int prot;
      uint64_t __prot;
    };
    union {
      int flags;
      uint64_t __flags;
    };
    union {
      unsigned int long devmajor;
      uint64_t __devmajor;
    };
    union {
      unsigned int long devminor;
      uint64_t __devminor;
    };
    union {
      ino_t inodenum;
      uint64_t __inodenum;
    };

    uint64_t properties;

    char name[FILENAMESIZE];
  };
  char _padding[4096];
} ProcMapsArea;

typedef ProcMapsArea Area;
#endif // ifndef PROCMAPSAREA_H
