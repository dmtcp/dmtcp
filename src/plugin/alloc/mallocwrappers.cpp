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
#include "dmtcp.h"
// This plugin is built with only include/ and jalib/ on its path.
#include "../../threadinfo.h"
#include "../../workerstate.h"

EXTERNC int
dmtcp_alloc_enabled() { return 1; }

// glibc frees the cached stacks of exited threads in an exiting detached
// thread while holding its stack cache lock, which pthread_create() also
// takes inside DMTCP's wrapper.  If a checkpoint request waits for that
// pthread_create() to leave the wrapper, an exiting thread waiting here for
// the wrapper lock deadlocks all three.  An exiting thread needs no lock: the
// checkpoint thread does not suspend it, and waits for it to exit only after
// releasing the lock.
static bool
isExitingThread()
{
  // As in ThreadSync::wrapperExecutionLockLock(): while DMTCP initializes,
  // dmtcp_get_current_thread() may create the thread list, so don't call it.
  if (dmtcp::WorkerState::currentState() == dmtcp::WorkerState::UNKNOWN) {
    return false;
  }
  return dmtcp_get_current_thread()->exiting;
}

extern "C" void *calloc(size_t nmemb, size_t size)
{
  if (isExitingThread()) {
    return _real_calloc(nmemb, size);
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  void *retval = _real_calloc(nmemb, size);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

extern "C" void *malloc(size_t size)
{
  if (isExitingThread()) {
    return _real_malloc(size);
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  void *retval = _real_malloc(size);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

extern "C" void *memalign(size_t boundary, size_t size)
{
  if (isExitingThread()) {
    return _real_memalign(boundary, size);
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  void *retval = _real_memalign(boundary, size);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

extern "C" int
posix_memalign(void **memptr, size_t alignment, size_t size)
{
  if (isExitingThread()) {
    return _real_posix_memalign(memptr, alignment, size);
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  int retval = _real_posix_memalign(memptr, alignment, size);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

extern "C" void *valloc(size_t size)
{
  if (isExitingThread()) {
    return _real_valloc(size);
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  void *retval = _real_valloc(size);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

extern "C" void
free(void *ptr)
{
  // Add for performance.
  if (ptr == NULL) {
    return;
  }
  if (isExitingThread()) {
    _real_free(ptr);
    return;
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  _real_free(ptr);
  DMTCP_PLUGIN_ENABLE_CKPT();
}

extern "C" void *realloc(void *ptr, size_t size)
{
  if (isExitingThread()) {
    return _real_realloc(ptr, size);
  }
  DMTCP_PLUGIN_DISABLE_CKPT();
  void *retval = _real_realloc(ptr, size);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}
