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
#include "wrapperlock.h"

using namespace dmtcp;

EXTERNC int
dmtcp_alloc_enabled()
{
  // We used to cache this in a function-local `static const int enabled`, but a
  // function-local static with a runtime initializer compiles to a C++ guard
  // (__cxa_guard_acquire/release), which ThreadSanitizer intercepts.  This
  // wrapper can run during TSAN's own constructor (TSAN may call malloc while
  // installing its interceptors), when TSAN's shadow is not yet initialized, so
  // the intercepted guard misbehaves -- the same class of early-init crash that
  // bit pthread_once in initializeInternalPluginState().
  //
  // Use a constant-initialized atomic flag instead: no C++ guard, and the
  // atomic ops are compiler intrinsics rather than TSAN-intercepted calls.
  // internalPluginEnabled() is idempotent, so a benign double-init under
  // contention is harmless.
  static int enabled = -1;  // -1 = not yet computed
  int value = __atomic_load_n(&enabled, __ATOMIC_ACQUIRE);
  if (value < 0) {
    value = internalPluginEnabled(INTERNAL_PLUGIN_ALLOC) ? 1 : 0;
    __atomic_store_n(&enabled, value, __ATOMIC_RELEASE);
  }
  return value;
}

extern "C" void *calloc(size_t nmemb, size_t size)
{
  if (!dmtcp_alloc_enabled()) {
    return _real_calloc(nmemb, size);
  }

  WrapperLock wrapperLock;
  return _real_calloc(nmemb, size);
}

extern "C" void *malloc(size_t size)
{
  if (!dmtcp_alloc_enabled()) {
    return _real_malloc(size);
  }

  WrapperLock wrapperLock;
  return _real_malloc(size);
}

extern "C" void *memalign(size_t boundary, size_t size)
{
  if (!dmtcp_alloc_enabled()) {
    return _real_memalign(boundary, size);
  }

  WrapperLock wrapperLock;
  return _real_memalign(boundary, size);
}

extern "C" int
posix_memalign(void **memptr, size_t alignment, size_t size)
{
  if (!dmtcp_alloc_enabled()) {
    return _real_posix_memalign(memptr, alignment, size);
  }

  WrapperLock wrapperLock;
  return _real_posix_memalign(memptr, alignment, size);
}

extern "C" void *valloc(size_t size)
{
  if (!dmtcp_alloc_enabled()) {
    return _real_valloc(size);
  }

  WrapperLock wrapperLock;
  return _real_valloc(size);
}

extern "C" void
free(void *ptr)
{
  if (!dmtcp_alloc_enabled()) {
    _real_free(ptr);
    return;
  }

  WrapperLock wrapperLock;
  _real_free(ptr);
}

extern "C" void *realloc(void *ptr, size_t size)
{
  if (!dmtcp_alloc_enabled()) {
    return _real_realloc(ptr, size);
  }

  WrapperLock wrapperLock;
  return _real_realloc(ptr, size);
}
