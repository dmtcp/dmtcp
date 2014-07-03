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
#include "dmtcp.h"

#define _real_dlopen  NEXT_FNC(dlopen)
#define _real_dlclose NEXT_FNC(dlclose)

EXTERNC int dmtcp_dlopen_enabled() { return 1; }

extern "C" int dmtcp_libdlLockLock();
extern "C" void dmtcp_libdlLockUnlock();

/* Reason for using thread_performing_dlopen_dlsym:
 *
 * dlsym/dlopen/dlclose make a call to calloc() internally. We do not want to
 * checkpoint while we are in the midst of dlopen etc. as it can lead to
 * undesired behavior. To do so, we use WRAPPER_EXECUTION_DISABLE_CKPT() at the
 * beginning of the funtion. However, if a checkpoint request is received right
 * after WRAPPER_EXECUTION_DISABLE_CKPT(), the ckpt-thread is queued for wrlock
 * on the pthread-rwlock and any subsequent request for rdlock by other threads
 * will have to wait until the ckpt-thread releases the lock. However, in this
 * scenario, dlopen calls calloc, which then calls
 * WRAPPER_EXECUTION_DISABLE_CKPT() and hence resulting in a deadlock.
 *
 * We set this variable to true, once we are inside the dlopen/dlsym/dlerror
 * wrapper, so that the calling thread won't try to acquire the lock later on.
 *
 * EDIT: Instead of acquiring wrapperExecutionLock, we acquire libdlLock.
 * libdlLock is a higher priority lock than wrapperExectionLock i.e. during
 * checkpointing this lock is acquired before wrapperExecutionLock by the
 * ckpt-thread.
 * Rationale behind not using wrapperExecutionLock and creating an extra lock:
 *   When loading a shared library, dlopen will initialize the static objects
 *   in the shared library by calling their corresponding constructors.
 *   Further, the constructor might call fork/exec to create new
 *   process/program. Finally, fork/exec require the wrapperExecutionLock in
 *   exclusive mode (writer lock). However, if dlopen wrapper acquires the
 *   wrapperExecutionLock, the fork wrapper will deadlock when trying to get
 *   writer lock.
 *
 * EDIT: The dlopen() wrappers causes the problems with the semantics of RPATH
 * associated with the caller library. In future, we can work without this
 * plugin by detecting if we are in the middle of a dlopen by looking up the
 * stack frames.
 */

extern "C"
void *dlopen(const char *filename, int flag)
{
  bool lockAcquired = dmtcp_libdlLockLock();
  void *ret = _real_dlopen(filename, flag);
  if (lockAcquired) {
    dmtcp_libdlLockUnlock();
  }
  return ret;
}

extern "C"
int dlclose(void *handle)
{
  bool lockAcquired = dmtcp_libdlLockLock();
  int ret = _real_dlclose(handle);
  if (lockAcquired) {
    dmtcp_libdlLockUnlock();
  }
  return ret;
}

