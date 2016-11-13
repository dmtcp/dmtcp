/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <stdarg.h>
#include <sys/mman.h>
#include "alloc.h"
#include "dmtcp.h"

// #define ENABLE_MMAP_WRAPPERS
#ifdef ENABLE_MMAP_WRAPPERS
extern "C" void *mmap(void *addr, size_t length, int prot, int flags,
                      int fd, off_t offset)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  void *retval = _real_mmap(addr, length, prot, flags, fd, offset);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

extern "C" void *mmap64(void *addr, size_t length, int prot, int flags,
                        int fd, off64_t offset)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  void *retval = _real_mmap64(addr, length, prot, flags, fd, offset);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

extern "C" int
munmap(void *addr, size_t length)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int retval = _real_munmap(addr, length);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

# if __GLIBC_PREREQ(2, 4)
extern "C" void *mremap(void *old_address, size_t old_size,
                        size_t new_size, int flags, ...)
{
  void *retval;

  DMTCP_PLUGIN_DISABLE_CKPT();
  if (flags == MREMAP_FIXED) {
    va_list ap;
    va_start(ap, flags);
    void *new_address = va_arg(ap, void *);
    va_end(ap);
    retval = _real_mremap(old_address, old_size, new_size, flags, new_address);
  } else {
    retval = _real_mremap(old_address, old_size, new_size, flags);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}
# else // if __GLIBC_PREREQ(2, 4)
extern "C" void *mremap(void *old_address, size_t old_size,
                        size_t new_size, int flags)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  void *retval = _real_mremap(old_address, old_size, new_size, flags);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}
# endif // if __GLIBC_PREREQ(2, 4)
#endif // ENABLE_MMAP_WRAPPERS
