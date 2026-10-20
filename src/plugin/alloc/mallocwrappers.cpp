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

#include <stdlib.h>
#include <unistd.h>
#include "alloc.h"
#include "pluginmanager.h"
#include "threadinfo.h"
#include "wrapperlock.h"

using namespace dmtcp;

EXTERNC int
dmtcp_alloc_enabled()
{
  static const int enabled =
    internalPluginEnabled(INTERNAL_PLUGIN_ALLOC) ? 1 : 0;
  return enabled;
}

// Deadlock happens when one thread has called pthread_create, and a
// second thread is exiting, and then a checkpoint request arrives.
// Note that glibc contains a cache of stacks so that when it creates
// a new thread, it can reuse an old stack instead of doing an allocation.
// glibc maintains a lock for the cached stacks GL(dl_stack_cache_lock).
// DMTCP maintains a wrapper lock.
// 
// Next, the glibc cache of stacks is limited to a certain maximum.
// When a thread is exiting and the cache of stacks is already at the
// limit, glibc will evict the oldest stack in the cache, and places
// the stack of the exiting thread into the cache.
// 
// So, the thread being created holds read access to the DMTCP wrapper
// lock, and wants glibc's cache-of-stacks lock in order to complete
// the real pthread_create.  The exiting thread holds glibc's
// cache-of-stacks lock, but wants read access to the DMTCP wrapper
// lock in order to evict a stack from the cache-of-stacks.  The exiting
// thread is waiting on the checkpoint thread because the DMTCP wrapper
// lock is writer-prefered, and the checkpoint thread wants write
// access to the DMTCP wrapper lock. The checkpoint thread can't gain
// write access to the DMTCP wrapper lock because the thread being
// created holds read access to the DMTCP wrapper lock.
// 
// This happens often when using OpenMP (libgomp), which rapidly creates
// and destroys threads.
//
// If isExistingThread() returns true, we can skip acquiring read access
// to the DMTCP wrapper lock in DMTCP's memory allocation wrappers.
static bool
isExitingThread()
{
  // Read curThread directly: this check runs on every allocation, and
  // dmtcp_get_current_thread() may create the thread list.  A thread with no
  // Thread yet is not exiting.
  Thread *thread = curThread;
  return thread != nullptr && thread->exiting;
}

extern "C" void *calloc(size_t nmemb, size_t size)
{
  if (!dmtcp_alloc_enabled() || isExitingThread()) {
    return _real_calloc(nmemb, size);
  }

  WrapperLock wrapperLock;
  return _real_calloc(nmemb, size);
}

extern "C" void *malloc(size_t size)
{
  if (!dmtcp_alloc_enabled() || isExitingThread()) {
    return _real_malloc(size);
  }

  WrapperLock wrapperLock;
  return _real_malloc(size);
}

extern "C" void *memalign(size_t boundary, size_t size)
{
  if (!dmtcp_alloc_enabled() || isExitingThread()) {
    return _real_memalign(boundary, size);
  }

  WrapperLock wrapperLock;
  return _real_memalign(boundary, size);
}

extern "C" int
posix_memalign(void **memptr, size_t alignment, size_t size)
{
  if (!dmtcp_alloc_enabled() || isExitingThread()) {
    return _real_posix_memalign(memptr, alignment, size);
  }

  WrapperLock wrapperLock;
  return _real_posix_memalign(memptr, alignment, size);
}

extern "C" void *valloc(size_t size)
{
  if (!dmtcp_alloc_enabled() || isExitingThread()) {
    return _real_valloc(size);
  }

  WrapperLock wrapperLock;
  return _real_valloc(size);
}

extern "C" void
free(void *ptr)
{
  // Add for performance.
  if (ptr == NULL) {
    return;
  }
  if (!dmtcp_alloc_enabled() || isExitingThread()) {
    _real_free(ptr);
    return;
  }

  WrapperLock wrapperLock;
  _real_free(ptr);
}

extern "C" void *realloc(void *ptr, size_t size)
{
  if (!dmtcp_alloc_enabled() || isExitingThread()) {
    return _real_realloc(ptr, size);
  }

  WrapperLock wrapperLock;
  return _real_realloc(ptr, size);
}
