/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef _GNU_SOURCE
# define _GNU_SOURCE /* for sake of mremap */
#endif
#include <stdarg.h>
#include <stdlib.h>
#include <vector>
#include <list>
#include <string>
#include <fcntl.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/version.h>
#include <limits.h>
#include "uniquepid.h"
#include "dmtcpworker.h"
#include "dmtcpmessagetypes.h"
#include "protectedfds.h"
#include "constants.h"
#include "connectionmanager.h"
#include "syscallwrappers.h"
#include "sysvipc.h"
#include "util.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jconvert.h"

#ifdef ENABLE_MALLOC_WRAPPER
# ifdef ENABLE_DLOPEN
#  error "ENABLE_MALLOC_WRAPPER can't work with ENABLE_DLOPEN"
# endif


/* This buffer (wrapper_init_buf) is used to pass on to dlsym() while it is
 * initializing the dmtcp wrappers. See comments in syscallsreal.c for more
 * details.
 */
static char wrapper_init_buf[1024];
static bool mem_allocated_for_initializing_wrappers = false;

#ifdef RECORD_REPLAY
void *_almost_real_calloc(size_t nmemb, size_t size)
#else
extern "C" void *calloc(size_t nmemb, size_t size)
#endif
{
  if (dmtcp_wrappers_initializing) {
    JASSERT(!mem_allocated_for_initializing_wrappers);
    memset(wrapper_init_buf, 0, sizeof (wrapper_init_buf));
    //void *ret = JALLOC_HELPER_MALLOC ( nmemb * size );
    mem_allocated_for_initializing_wrappers = true;
    return (void*) wrapper_init_buf;
  }
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *retval = _real_calloc ( nmemb, size );
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

#ifdef RECORD_REPLAY
void *_almost_real_malloc(size_t size)
#else
extern "C" void *malloc(size_t size)
#endif
{
  if (dmtcp_wrappers_initializing) {
    return calloc(1, size);
  }
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *retval = _real_malloc ( size );
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

#ifdef RECORD_REPLAY
void *_almost_real_libc_memalign(size_t boundary, size_t size)
#else
extern "C" void *__libc_memalign(size_t boundary, size_t size)
#endif
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *retval = _real_libc_memalign(boundary, size);
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

#ifdef RECORD_REPLAY
extern "C" void *__libc_memalign(size_t boundary, size_t size);
void *_almost_real_valloc(size_t size)
#else
extern "C" void *valloc(size_t size)
#endif
{
  return __libc_memalign(sysconf(_SC_PAGESIZE), size);
}

// FIXME:  Add wrapper for alloca(), posix_memalign(), etc.,
//    using WRAPPER_EXECUTION_DISABLE_CKPT(), etc.

#ifdef RECORD_REPLAY
void _almost_real_free(void *ptr)
#else
extern "C" void free(void *ptr)
#endif
{
  if (dmtcp_wrappers_initializing) {
    JASSERT(mem_allocated_for_initializing_wrappers);
    JASSERT(ptr == wrapper_init_buf);
    return;
  }

  WRAPPER_EXECUTION_DISABLE_CKPT();
  _real_free ( ptr );
  WRAPPER_EXECUTION_ENABLE_CKPT();
}

#ifdef RECORD_REPLAY
void *_almost_real_realloc(void *ptr, size_t size)
#else
extern "C" void *realloc(void *ptr, size_t size)
#endif
{
  JASSERT (!dmtcp_wrappers_initializing)
    .Text ("This is a rather unusual path. Please inform DMTCP developers");

  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *retval = _real_realloc ( ptr, size );
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}
#endif

#ifdef RECORD_REPLAY
void *_almost_real_mmap(void *addr, size_t length, int prot, int flags,
                        int fd, off_t offset)
#else
extern "C" void *mmap(void *addr, size_t length, int prot, int flags,
                      int fd, off_t offset)
#endif
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *retval = _real_mmap(addr, length, prot, flags, fd, offset);
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

#ifdef RECORD_REPLAY
void *_almost_real_mmap64 (void *addr, size_t length, int prot, int flags,
                         int fd, off64_t offset)
#else
extern "C" void *mmap64 (void *addr, size_t length, int prot, int flags,
                         int fd, off64_t offset)
#endif
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *retval = _real_mmap64(addr, length, prot, flags, fd, offset);
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

#ifdef RECORD_REPLAY
int _almost_real_munmap(void *addr, size_t length)
#else
extern "C" int munmap(void *addr, size_t length)
#endif
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  int retval = _real_munmap(addr, length);
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

# if __GLIBC_PREREQ (2,4)
#ifdef RECORD_REPLAY
void *_almost_real_mremap(void *old_address, size_t old_size,
                          size_t new_size, int flags, ...)
#else
extern "C" void *mremap(void *old_address, size_t old_size,
                        size_t new_size, int flags, ...)
#endif
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  va_list ap;
  va_start( ap, flags );
  void *new_addr = va_arg ( ap, void * );
  va_end ( ap );
  void *retval = _real_mremap(old_address, old_size, new_size, flags, new_addr);
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}
# else
#ifdef RECORD_REPLAY
void *_almost_real_mremap(void *old_address, size_t old_size,
                          size_t new_size, int flags)
#else
extern "C" void *mremap(void *old_address, size_t old_size,
                        size_t new_size, int flags)
#endif
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *retval = _real_mremap(old_address, old_size, new_size, flags);
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}
#endif
