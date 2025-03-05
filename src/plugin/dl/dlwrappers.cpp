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

#include <elf.h>
#include <link.h>
#include <stdlib.h>
#include <unistd.h>

#include "../jalib/jassert.h"
#include "../jalib/jfilesystem.h"
#include "dmtcp.h"
#include "wrapperlock.h"
#include "tokenize.h"
#include "util.h"

#define _real_dlopen  NEXT_FNC(dlopen)
#define _real_dlclose NEXT_FNC(dlclose)

extern "C" int dmtcp_libdlLockLock();
extern "C" void dmtcp_libdlLockUnlock();

using namespace dmtcp;

void
getRpathRunPath(void *caller, char *rpathStr, char *runpathStr)
{
  Dl_info info;
  struct link_map *map;

  ASSERT_NOT_NULL(rpathStr);
  ASSERT_NOT_NULL(runpathStr);

  rpathStr[0] = '\0';
  runpathStr[0] = '\0';

  // Retrieve the link_map for the library given by addr
  int ret = dladdr1(caller, &info, (void **)&map, RTLD_DL_LINKMAP);
  ASSERT_NE(0, ret);

  char dirname[4096];
  jalib::Filesystem::DirName(dirname, info.dli_fname);
  
  const ElfW(Dyn) *dyn = map->l_ld;
  const ElfW(Dyn) *rpath = NULL;
  const ElfW(Dyn) *runpath = NULL;
  const char *strtab = NULL;
  for (; dyn->d_tag != DT_NULL; ++dyn) {
    if (dyn->d_tag == DT_RPATH) {
      rpath = dyn;
    } else if (dyn->d_tag == DT_RUNPATH) {
      runpath = dyn;
    } else if (dyn->d_tag == DT_STRTAB) {
      strtab = (const char *)dyn->d_un.d_val;
    }
  }

  ASSERT_NOT_NULL(strtab);

  if (rpath != NULL) {
    strcpy(rpathStr, strtab + rpath->d_un.d_val);
    Util::replace(rpathStr, "$ORIGIN", dirname);
    Util::replace(rpathStr, "${ORIGIN}", dirname);

    ASSERT_NULL(strstr(rpathStr, "$LIB")) (rpathStr);
    ASSERT_NULL(strstr(rpathStr, "${LIB}")) (rpathStr);
    ASSERT_NULL(strstr(rpathStr, "$PLATFORM")) (rpathStr);
    ASSERT_NULL(strstr(rpathStr, "${PLATFORM}")) (rpathStr);
  }

  if (runpath != NULL) {
    strcpy(rpathStr, strtab + runpath->d_un.d_val);
    Util::replace(runpathStr, "$ORIGIN", dirname);
    Util::replace(runpathStr, "${ORIGIN}", dirname);

    ASSERT_NULL(strstr(runpathStr, "$LIB")) (runpathStr);
    ASSERT_NULL(strstr(runpathStr, "${LIB}")) (runpathStr);
    ASSERT_NULL(strstr(runpathStr, "$PLATFORM")) (runpathStr);
    ASSERT_NULL(strstr(runpathStr, "${PLATFORM}")) (runpathStr);
  }
}

static void *
dlopen_try_paths(const char *filename, int flag, const char *paths)
{
  // Iterate over each path in the colon separated list of paths.
  // Try to dlopen the file using the path.
  // If successful, return the handle.
  // If unsuccessful, continue to the next path.
  // If all paths fail, return NULL.

  char path[4096];
  strncpy(path, paths, sizeof(path));
  path[sizeof(path) - 1] = '\0';

  char *saveptr;
  char *pathToken = strtok_r(path, ":", &saveptr);
  while (pathToken != NULL) {
    char newFilename[4096];
    snprintf(newFilename, sizeof(newFilename), "%s/%s", pathToken, filename);
    void *handle = _real_dlopen(newFilename, flag);
    if (handle != NULL) {
      return handle;
    }
    pathToken = strtok_r(NULL, ":", &saveptr);
  }

  return NULL;
}

/* Reason for using thread_performing_dlopen_dlsym:
 *
 * dlsym/dlopen/dlclose make a call to calloc() internally. We do not want to
 * checkpoint while we are in the midst of dlopen etc. as it can lead to
 * undesired behavior. To do so, we use WRAPPER_EXECUTION_DISABLE_CKPT() at the
 * beginning of the function. However, if a checkpoint request is received right
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
  LibDlWrapperLock wrapperLock;

  // TODO(kapil): Replace with scoped lock once we have it.
  char rpath[4096];
  char runpath[4096];
  void *ret = NULL;

  // If filename is null. The user just wants a handle to the main program.
  if (filename == NULL || strlen(filename) == 0) {
    return _real_dlopen(filename, flag);
  }

  getRpathRunPath(__builtin_return_address(0), rpath, runpath);

  // 1. We call libc dlopen:
  //    * if RPATH exists. We let libc handle RPATH when locating the file.
  if (strlen(rpath) != 0) {
    ret = _real_dlopen(filename, flag);
  }

  // 2. Attempt dlopen using LD_LIBRARY_PATH.
  // TODO(kapil): We should use a cached copy of LD_LIBRARY_PATH as it existed
  // at the time of program start since the manpage explicitly states it:
  //    If, at the time that the program was started, the environment variable
  //    LD_LIBRARY_PATH was defined to contain a colon-separated list of
  //    directories, then these are searched.
  const char *ldLibraryPath = getenv("LD_LIBRARY_PATH");
  if (ret == NULL && ldLibraryPath != NULL) {
    ret = dlopen_try_paths(filename, flag, ldLibraryPath);
  }

  // 3. Attempt dlopen using RUNPATH.
  if (ret == NULL && strlen(runpath) != 0) {
    ret = dlopen_try_paths(filename, flag, runpath);
  }

  // 4. Finally try dlopen using filename as is.
  if (ret == NULL) {
    ret = _real_dlopen(filename, flag);
  }

  if (ret == NULL) {
    JTRACE("dlopen failed")(filename)(flag)(rpath)(ldLibraryPath)(runpath);
  }

  return ret;
}

extern "C"
int
dlclose(void *handle)
{
  LibDlWrapperLock wrapperLock;
  return _real_dlclose(handle);
}
