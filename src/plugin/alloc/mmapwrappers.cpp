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
#include <errno.h>
#include <sys/mman.h>
#include "alloc.h"
#include "builtinplugins.h"
#include "wrapperevents.h"
#include "wrapperlock.h"

using namespace dmtcp;

// #define ENABLE_MMAP_WRAPPERS
#ifdef ENABLE_MMAP_WRAPPERS
extern "C" void *mmap(void *addr, size_t length, int prot, int flags,
                      int fd, off_t offset)
{
  if (!builtinPluginEnabled(BUILTIN_PLUGIN_ALLOC)) {
    return _real_mmap(addr, length, prot, flags, fd, offset);
  }

  ensureAllocWrapperHooksRegistered();
  WrapperLock wrapperLock;
  MmapWrapperCtx ctx = { addr, length, prot, flags, fd, offset, NULL, errno };
  dispatchWrapperPre(WRAPPER_EVENT_MMAP_PRE, &ctx);
  errno = ctx.savedErrno;
  ctx.result = _real_mmap(ctx.addr, ctx.length, ctx.prot, ctx.flags, ctx.fd,
                          ctx.offset);
  ctx.savedErrno = errno;
  dispatchWrapperPost(WRAPPER_EVENT_MMAP_POST, &ctx);
  errno = ctx.savedErrno;
  return ctx.result;
}

extern "C" void *mmap64(void *addr, size_t length, int prot, int flags,
                        int fd, off64_t offset)
{
  if (!builtinPluginEnabled(BUILTIN_PLUGIN_ALLOC)) {
    return _real_mmap64(addr, length, prot, flags, fd, offset);
  }

  ensureAllocWrapperHooksRegistered();
  WrapperLock wrapperLock;
  Mmap64WrapperCtx ctx = { addr, length, prot, flags, fd, offset, NULL, errno };
  dispatchWrapperPre(WRAPPER_EVENT_MMAP64_PRE, &ctx);
  errno = ctx.savedErrno;
  ctx.result = _real_mmap64(ctx.addr, ctx.length, ctx.prot, ctx.flags, ctx.fd,
                            ctx.offset);
  ctx.savedErrno = errno;
  dispatchWrapperPost(WRAPPER_EVENT_MMAP64_POST, &ctx);
  errno = ctx.savedErrno;
  return ctx.result;
}

extern "C" int
munmap(void *addr, size_t length)
{
  if (!builtinPluginEnabled(BUILTIN_PLUGIN_ALLOC)) {
    return _real_munmap(addr, length);
  }

  ensureAllocWrapperHooksRegistered();
  WrapperLock wrapperLock;
  MunmapWrapperCtx ctx = { addr, length, -1, errno };
  dispatchWrapperPre(WRAPPER_EVENT_MUNMAP_PRE, &ctx);
  errno = ctx.savedErrno;
  ctx.result = _real_munmap(ctx.addr, ctx.length);
  ctx.savedErrno = errno;
  dispatchWrapperPost(WRAPPER_EVENT_MUNMAP_POST, &ctx);
  errno = ctx.savedErrno;
  return ctx.result;
}

# if __GLIBC_PREREQ(2, 4)
extern "C" void *mremap(void *old_address, size_t old_size,
                        size_t new_size, int flags, ...)
{
  if (!builtinPluginEnabled(BUILTIN_PLUGIN_ALLOC)) {
    if (flags & MREMAP_FIXED) {
      va_list ap;
      va_start(ap, flags);
      void *new_address = va_arg(ap, void *);
      va_end(ap);
      return _real_mremap(old_address, old_size, new_size, flags, new_address);
    }
    return _real_mremap(old_address, old_size, new_size, flags);
  }

  ensureAllocWrapperHooksRegistered();
  WrapperLock wrapperLock;
  MremapWrapperCtx ctx = {
    old_address, old_size, new_size, flags, NULL, false, NULL, errno
  };
  if (flags & MREMAP_FIXED) {
    va_list ap;
    va_start(ap, flags);
    ctx.newAddress = va_arg(ap, void *);
    ctx.hasNewAddress = true;
    va_end(ap);
  }
  dispatchWrapperPre(WRAPPER_EVENT_MREMAP_PRE, &ctx);
  errno = ctx.savedErrno;
  if (ctx.hasNewAddress) {
    ctx.result = _real_mremap(ctx.oldAddress, ctx.oldSize, ctx.newSize,
                              ctx.flags, ctx.newAddress);
  } else {
    ctx.result = _real_mremap(ctx.oldAddress, ctx.oldSize, ctx.newSize,
                              ctx.flags);
  }
  ctx.savedErrno = errno;
  dispatchWrapperPost(WRAPPER_EVENT_MREMAP_POST, &ctx);
  errno = ctx.savedErrno;
  return ctx.result;
}
# else // if __GLIBC_PREREQ(2, 4)
extern "C" void *mremap(void *old_address, size_t old_size,
                        size_t new_size, int flags)
{
  if (!builtinPluginEnabled(BUILTIN_PLUGIN_ALLOC)) {
    return _real_mremap(old_address, old_size, new_size, flags);
  }

  ensureAllocWrapperHooksRegistered();
  WrapperLock wrapperLock;
  MremapWrapperCtx ctx = {
    old_address, old_size, new_size, flags, NULL, false, NULL, errno
  };
  dispatchWrapperPre(WRAPPER_EVENT_MREMAP_PRE, &ctx);
  errno = ctx.savedErrno;
  ctx.result = _real_mremap(ctx.oldAddress, ctx.oldSize, ctx.newSize,
                            ctx.flags);
  ctx.savedErrno = errno;
  dispatchWrapperPost(WRAPPER_EVENT_MREMAP_POST, &ctx);
  errno = ctx.savedErrno;
  return ctx.result;
}
# endif // if __GLIBC_PREREQ(2, 4)
#endif // ENABLE_MMAP_WRAPPERS
