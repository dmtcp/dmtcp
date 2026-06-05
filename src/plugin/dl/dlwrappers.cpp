/****************************************************************************
 *   Copyright (C) 2006-2022 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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
# define _GNU_SOURCE
#endif
#include <elf.h>
#include <errno.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "dmtcp.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "pluginmanager.h"
#include "util.h"
#include "util_assert.h"
#include "wrapperlock.h"

#define _real_dlopen  NEXT_FNC(dlopen)
#define _real_dlclose NEXT_FNC(dlclose)

using namespace dmtcp;

extern "C" int
dmtcp_dl_enabled()
{
  static const int enabled =
    internalPluginEnabled(INTERNAL_PLUGIN_DL) ? 1 : 0;
  return enabled;
}

static void
getRpathRunPath(void *caller, char *rpathStr, char *runpathStr)
{
  Dl_info info;
  struct link_map *map;

  ASSERT_NOT_NULL(rpathStr);
  ASSERT_NOT_NULL(runpathStr);

  rpathStr[0] = '\0';
  runpathStr[0] = '\0';

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

    ASSERT_NULL_MSG(strstr(rpathStr, "$LIB"), "rpath={}", rpathStr);
    ASSERT_NULL_MSG(strstr(rpathStr, "${LIB}"), "rpath={}", rpathStr);
    ASSERT_NULL_MSG(strstr(rpathStr, "$PLATFORM"), "rpath={}", rpathStr);
    ASSERT_NULL_MSG(strstr(rpathStr, "${PLATFORM}"), "rpath={}", rpathStr);
  }

  if (runpath != NULL) {
    strcpy(runpathStr, strtab + runpath->d_un.d_val);
    Util::replace(runpathStr, "$ORIGIN", dirname);
    Util::replace(runpathStr, "${ORIGIN}", dirname);

    ASSERT_NULL_MSG(strstr(runpathStr, "$LIB"), "runpath={}", runpathStr);
    ASSERT_NULL_MSG(strstr(runpathStr, "${LIB}"), "runpath={}", runpathStr);
    ASSERT_NULL_MSG(strstr(runpathStr, "$PLATFORM"), "runpath={}",
                    runpathStr);
    ASSERT_NULL_MSG(strstr(runpathStr, "${PLATFORM}"), "runpath={}",
                    runpathStr);
  }
}

static void *
dlopen_try_paths(const char *filename, int flags, const char *paths)
{
  char path[4096];
  strncpy(path, paths, sizeof(path));
  path[sizeof(path) - 1] = '\0';

  char *saveptr;
  char *pathToken = strtok_r(path, ":", &saveptr);
  while (pathToken != NULL) {
    char newFilename[4096];
    snprintf(newFilename, sizeof(newFilename), "%s/%s", pathToken, filename);
    void *handle = _real_dlopen(newFilename, flags);
    if (handle != NULL) {
      return handle;
    }
    pathToken = strtok_r(NULL, ":", &saveptr);
  }

  return NULL;
}

static void *
dmtcp_dlopen_with_search_policy(const char *filename, int flags, void *caller)
{
  if (filename == NULL || strlen(filename) == 0) {
    return _real_dlopen(filename, flags);
  }

  char rpath[4096];
  char runpath[4096];
  void *ret = NULL;

  getRpathRunPath(caller, rpath, runpath);

  if (strlen(rpath) != 0) {
    ret = dlopen_try_paths(filename, flags, rpath);
  }

  const char *ldLibraryPath = getenv("LD_LIBRARY_PATH");
  if (ret == NULL && ldLibraryPath != NULL) {
    ret = dlopen_try_paths(filename, flags, ldLibraryPath);
  }

  if (ret == NULL && strlen(runpath) != 0) {
    ret = dlopen_try_paths(filename, flags, runpath);
  }

  if (ret == NULL) {
    ret = _real_dlopen(filename, flags);
  }

  return ret;
}

extern "C"
void *
dlopen(const char *filename, int flags)
{
  if (!dmtcp_dl_enabled()) {
    return _real_dlopen(filename, flags);
  }

  LibDlWrapperLock wrapperLock;
  return dmtcp_dlopen_with_search_policy(filename, flags,
                                         __builtin_return_address(0));
}

extern "C"
int
dlclose(void *handle)
{
  if (!dmtcp_dl_enabled()) {
    return _real_dlclose(handle);
  }

  LibDlWrapperLock wrapperLock;
  return _real_dlclose(handle);
}
