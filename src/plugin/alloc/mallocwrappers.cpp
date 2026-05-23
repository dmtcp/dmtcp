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

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include "alloc.h"
#include "builtinplugins.h"
#include "jassert.h"
#include "wrapperevents.h"
#include "wrapperlock.h"

using namespace dmtcp;

static pthread_once_t registerAllocWrapperHooksOnceControl =
  PTHREAD_ONCE_INIT;

static void
allocWrapperNoopHook(WrapperEvent event, void *ctx)
{
  (void)event;
  (void)ctx;
}

static void
registerAllocWrapperHooksOnce()
{
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC, WRAPPER_EVENT_MALLOC_PRE,
                             allocWrapperNoopHook);
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC, WRAPPER_EVENT_MALLOC_POST,
                             allocWrapperNoopHook);
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC, WRAPPER_EVENT_CALLOC_PRE,
                             allocWrapperNoopHook);
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC, WRAPPER_EVENT_CALLOC_POST,
                             allocWrapperNoopHook);
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC, WRAPPER_EVENT_REALLOC_PRE,
                             allocWrapperNoopHook);
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC, WRAPPER_EVENT_REALLOC_POST,
                             allocWrapperNoopHook);
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC, WRAPPER_EVENT_FREE_PRE,
                             allocWrapperNoopHook);
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC, WRAPPER_EVENT_FREE_POST,
                             allocWrapperNoopHook);
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC, WRAPPER_EVENT_MEMALIGN_PRE,
                             allocWrapperNoopHook);
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC, WRAPPER_EVENT_MEMALIGN_POST,
                             allocWrapperNoopHook);
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC,
                             WRAPPER_EVENT_POSIX_MEMALIGN_PRE,
                             allocWrapperNoopHook);
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC,
                             WRAPPER_EVENT_POSIX_MEMALIGN_POST,
                             allocWrapperNoopHook);
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC, WRAPPER_EVENT_VALLOC_PRE,
                             allocWrapperNoopHook);
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC, WRAPPER_EVENT_VALLOC_POST,
                             allocWrapperNoopHook);
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC, WRAPPER_EVENT_MMAP_PRE,
                             allocWrapperNoopHook);
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC, WRAPPER_EVENT_MMAP_POST,
                             allocWrapperNoopHook);
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC, WRAPPER_EVENT_MMAP64_PRE,
                             allocWrapperNoopHook);
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC, WRAPPER_EVENT_MMAP64_POST,
                             allocWrapperNoopHook);
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC, WRAPPER_EVENT_MUNMAP_PRE,
                             allocWrapperNoopHook);
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC, WRAPPER_EVENT_MUNMAP_POST,
                             allocWrapperNoopHook);
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC, WRAPPER_EVENT_MREMAP_PRE,
                             allocWrapperNoopHook);
  registerBuiltinWrapperHook(BUILTIN_PLUGIN_ALLOC, WRAPPER_EVENT_MREMAP_POST,
                             allocWrapperNoopHook);
}

namespace dmtcp
{
void
ensureAllocWrapperHooksRegistered()
{
  int rc = pthread_once(&registerAllocWrapperHooksOnceControl,
                        registerAllocWrapperHooksOnce);
  JASSERT(rc == 0) (rc).Text("Failed to register allocation wrapper hooks.");
}
}

EXTERNC int
dmtcp_alloc_enabled()
{
  return builtinPluginEnabled(BUILTIN_PLUGIN_ALLOC) ? 1 : 0;
}

extern "C" void *calloc(size_t nmemb, size_t size)
{
  if (!builtinPluginEnabled(BUILTIN_PLUGIN_ALLOC)) {
    return _real_calloc(nmemb, size);
  }

  ensureAllocWrapperHooksRegistered();
  WrapperLock wrapperLock;
  CallocWrapperCtx ctx = { nmemb, size, NULL, errno };
  dispatchWrapperPre(WRAPPER_EVENT_CALLOC_PRE, &ctx);
  errno = ctx.savedErrno;
  ctx.result = _real_calloc(ctx.nmemb, ctx.size);
  ctx.savedErrno = errno;
  dispatchWrapperPost(WRAPPER_EVENT_CALLOC_POST, &ctx);
  errno = ctx.savedErrno;
  return ctx.result;
}

extern "C" void *malloc(size_t size)
{
  if (!builtinPluginEnabled(BUILTIN_PLUGIN_ALLOC)) {
    return _real_malloc(size);
  }

  ensureAllocWrapperHooksRegistered();
  WrapperLock wrapperLock;
  MallocWrapperCtx ctx = { size, NULL, errno };
  dispatchWrapperPre(WRAPPER_EVENT_MALLOC_PRE, &ctx);
  errno = ctx.savedErrno;
  ctx.result = _real_malloc(ctx.size);
  ctx.savedErrno = errno;
  dispatchWrapperPost(WRAPPER_EVENT_MALLOC_POST, &ctx);
  errno = ctx.savedErrno;
  return ctx.result;
}

extern "C" void *memalign(size_t boundary, size_t size)
{
  if (!builtinPluginEnabled(BUILTIN_PLUGIN_ALLOC)) {
    return _real_memalign(boundary, size);
  }

  ensureAllocWrapperHooksRegistered();
  WrapperLock wrapperLock;
  MemalignWrapperCtx ctx = { boundary, size, NULL, errno };
  dispatchWrapperPre(WRAPPER_EVENT_MEMALIGN_PRE, &ctx);
  errno = ctx.savedErrno;
  ctx.result = _real_memalign(ctx.boundary, ctx.size);
  ctx.savedErrno = errno;
  dispatchWrapperPost(WRAPPER_EVENT_MEMALIGN_POST, &ctx);
  errno = ctx.savedErrno;
  return ctx.result;
}

extern "C" int
posix_memalign(void **memptr, size_t alignment, size_t size)
{
  if (!builtinPluginEnabled(BUILTIN_PLUGIN_ALLOC)) {
    return _real_posix_memalign(memptr, alignment, size);
  }

  ensureAllocWrapperHooksRegistered();
  WrapperLock wrapperLock;
  PosixMemalignWrapperCtx ctx = { memptr, alignment, size, 0, errno };
  dispatchWrapperPre(WRAPPER_EVENT_POSIX_MEMALIGN_PRE, &ctx);
  errno = ctx.savedErrno;
  ctx.result = _real_posix_memalign(ctx.memptr, ctx.alignment, ctx.size);
  dispatchWrapperPost(WRAPPER_EVENT_POSIX_MEMALIGN_POST, &ctx);
  errno = ctx.savedErrno;
  return ctx.result;
}

extern "C" void *valloc(size_t size)
{
  if (!builtinPluginEnabled(BUILTIN_PLUGIN_ALLOC)) {
    return _real_valloc(size);
  }

  ensureAllocWrapperHooksRegistered();
  WrapperLock wrapperLock;
  VallocWrapperCtx ctx = { size, NULL, errno };
  dispatchWrapperPre(WRAPPER_EVENT_VALLOC_PRE, &ctx);
  errno = ctx.savedErrno;
  ctx.result = _real_valloc(ctx.size);
  ctx.savedErrno = errno;
  dispatchWrapperPost(WRAPPER_EVENT_VALLOC_POST, &ctx);
  errno = ctx.savedErrno;
  return ctx.result;
}

extern "C" void
free(void *ptr)
{
  if (!builtinPluginEnabled(BUILTIN_PLUGIN_ALLOC)) {
    _real_free(ptr);
    return;
  }

  ensureAllocWrapperHooksRegistered();
  WrapperLock wrapperLock;
  FreeWrapperCtx ctx = { ptr, errno };
  dispatchWrapperPre(WRAPPER_EVENT_FREE_PRE, &ctx);
  errno = ctx.savedErrno;
  _real_free(ctx.ptr);
  dispatchWrapperPost(WRAPPER_EVENT_FREE_POST, &ctx);
  errno = ctx.savedErrno;
}

extern "C" void *realloc(void *ptr, size_t size)
{
  if (!builtinPluginEnabled(BUILTIN_PLUGIN_ALLOC)) {
    return _real_realloc(ptr, size);
  }

  ensureAllocWrapperHooksRegistered();
  WrapperLock wrapperLock;
  ReallocWrapperCtx ctx = { ptr, size, NULL, errno };
  dispatchWrapperPre(WRAPPER_EVENT_REALLOC_PRE, &ctx);
  errno = ctx.savedErrno;
  ctx.result = _real_realloc(ctx.ptr, ctx.size);
  ctx.savedErrno = errno;
  dispatchWrapperPost(WRAPPER_EVENT_REALLOC_POST, &ctx);
  errno = ctx.savedErrno;
  return ctx.result;
}
