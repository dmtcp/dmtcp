/****************************************************************************
 *  Copyright (C) 2012-2014 by Artem Y. Polyakov <artpol84@gmail.com>       *
 *                                                                          *
 *  This file is part of the RM plugin for DMTCP                            *
 *                                                                          *
 *  RM plugin is free software: you can redistribute it and/or              *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  RM plugin is distributed in the hope that it will be useful,            *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include "rm_pmi.h"
#include <linux/limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <list>
#include <string>
#include <vector>
#include "jalib.h"
#include "jassert.h"
#include "jconvert.h"
#include "jfilesystem.h"
#include "procmapsarea.h"
#include "rm_main.h"
#include "rm_utils.h"
#include "util.h"

#define PMI_SUCCESS                0
#define PMI_FAIL                   -1
#define PMI_ERR_INIT               1
#define PMI_ERR_NOMEM              2
#define PMI_ERR_INVALID_ARG        3
#define PMI_ERR_INVALID_KEY        4
#define PMI_ERR_INVALID_KEY_LENGTH 5
#define PMI_ERR_INVALID_VAL        6
#define PMI_ERR_INVALID_VAL_LENGTH 7
#define PMI_ERR_INVALID_LENGTH     8
#define PMI_ERR_INVALID_NUM_ARGS   9
#define PMI_ERR_INVALID_ARGS       10
#define PMI_ERR_INVALID_NUM_PARSED 11
#define PMI_ERR_INVALID_KEYVALP    12
#define PMI_ERR_INVALID_SIZE       13
#define PMI_ERR_INVALID_KVS        14

using namespace dmtcp;

static pthread_mutex_t _lock_lib, _lock_flag;
static void
do_lock_lib()
{
  JASSERT(pthread_mutex_lock(&_lock_lib) == 0);
}

static void
do_unlock_lib()
{
  JASSERT(pthread_mutex_unlock(&_lock_lib) == 0);
}

static void
do_lock_flag()
{
  JASSERT(pthread_mutex_lock(&_lock_flag) == 0);
}

static void
do_unlock_flag()
{
  JASSERT(pthread_mutex_unlock(&_lock_flag) == 0);
}

static void *handle = NULL;
typedef int (*_PMI_Init_t)(int *t);
typedef int (*_PMI_Fini_t)();
typedef int (*_PMI_Barrier_t)();
typedef int PMI_BOOL;
#define PMI_TRUE  1
#define PMI_FALSE 0
typedef int (*_PMI_Initialized_t)(PMI_BOOL *);
static _PMI_Init_t _real_PMI_Init = NULL;
static _PMI_Fini_t _real_PMI_Fini = NULL;
static _PMI_Barrier_t _real_PMI_Barrier = NULL;
static _PMI_Initialized_t _real_PMI_Initialized = NULL;

static bool pmi_is_used = false;
static bool explicit_srun = false;

static bool
want_pmi_shutdown()
{
  return pmi_is_used && (_get_rmgr_type() == slurm && !explicit_srun);
}

void
rm_init_pmi()
{
  do_lock_lib();
  if (!handle) {
    string pattern = "libpmi";
    string libpath;
    if (findLib_byname(pattern, libpath) != 0) {
      JASSERT(findLib_byfunc("PMI_Init", libpath) == 0);
    }
    JTRACE("")(libpath);
    handle = dlopen(libpath.c_str(), RTLD_LAZY);
    JASSERT(handle != NULL);
    _real_PMI_Init = (_PMI_Init_t)dlsym(handle, "PMI_Init");
    JASSERT(_real_PMI_Init != NULL);
    _real_PMI_Fini = (_PMI_Fini_t)dlsym(handle, "PMI_Finalize");
    JASSERT(_real_PMI_Fini != NULL);
    _real_PMI_Barrier = (_PMI_Barrier_t)dlsym(handle, "PMI_Barrier");
    JASSERT(_real_PMI_Barrier != NULL);
    _real_PMI_Initialized =
      (_PMI_Initialized_t)dlsym(handle, "PMI_Initialized");
    if (_real_PMI_Initialized == NULL) {
      // eventually smpd of MPICH2 and Intel-MPI uses iPMI_Initialized function
      _real_PMI_Initialized = (_PMI_Initialized_t)dlsym(handle,
                                                        "iPMI_Initialized");
    }
    JASSERT(_real_PMI_Initialized != NULL);
    if (getenv(ENV_VAR_EXPLICIT_SRUN) != NULL) {
      explicit_srun = true;
    }
  }
  do_unlock_lib();
  JTRACE("")(handle);
}

extern "C" int
PMI_Init(int *spawned)
{
  if (!_real_PMI_Init) {
    rm_init_pmi();
  }

  if (!pmi_is_used) {
    do_lock_flag();
    pmi_is_used = true;
    do_unlock_flag();
  }
  int ret = _real_PMI_Init(spawned);
  JTRACE("")(_real_PMI_Init)(ret);
  return ret;
}

int
rm_shutdown_pmi()
{
  int ret = 0;

  JTRACE("Start, internal pmi capable");
  if (want_pmi_shutdown()) {
    JTRACE("Perform shutdown");

    PMI_BOOL en;
    if (!_real_PMI_Fini || !_real_PMI_Initialized) {
      rm_init_pmi();
    }
    JASSERT(_real_PMI_Initialized(&en) == PMI_SUCCESS);
    if (en == PMI_TRUE) {
      JASSERT(_real_PMI_Fini() == PMI_SUCCESS);
    }
    JTRACE("Shutdown PMI connection before checkpoint:")(ret);
  }
  return ret;
}

int
rm_restore_pmi()
{
  int ret = 0;

  JTRACE("Start, internal pmi capable");
  if (want_pmi_shutdown()) {
    JTRACE("Perform restore");
    if (!_real_PMI_Init || !_real_PMI_Initialized) {
      rm_init_pmi();
    }
    PMI_BOOL en;
    int spawned;
    JASSERT(_real_PMI_Initialized(&en) == PMI_SUCCESS);
    if (en == PMI_FALSE) {
      JASSERT(_real_PMI_Init(&spawned) == PMI_SUCCESS);
    }
    JTRACE("Restore PMI connection:")(ret);
    JASSERT(_real_PMI_Barrier() == PMI_SUCCESS);
    JTRACE("After PMI_Barrier()")(ret);
  }
  return ret;
}
