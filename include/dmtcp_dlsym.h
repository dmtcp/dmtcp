/****************************************************************************
 *   Copyright (C) 2014 by Gene Cooperman                                   *
 *   gene@ccs.neu.edu                                                       *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <stddef.h>
#include <stdio.h>
#include <pthread.h>

#include "dmtcp.h"

#ifndef __USE_GNU
# define __USE_GNU_NOT_SET
# define __USE_GNU
#endif
#include <dlfcn.h>  /* for NEXT_FNC() */
#ifdef __USE_GNU_NOT_SET
# undef __USE_GNU_NOT_SET
# undef __USE_GNU
#endif

EXTERNC void *dmtcp_dlsym(void *handle, const char *symbol);
EXTERNC void *dmtcp_dlvsym(void *handle, char *symbol, const char *version);
EXTERNC void *dmtcp_dlsym_lib(const char *libname, const char *symbol);
/*
 * Returns the offset of the given function within the given shared library
 * or -1 if the function does not exist in the library
 */
EXTERNC ptrdiff_t dmtcp_dlsym_lib_fnc_offset(const char *libname,
                                             const char *symbol);

#ifndef STANDALONE
// This implementation mirrors dmtcp.h:NEXT_FNC() for DMTCP.
// It uses dmtcp_dlsym to get default version, in case of symbol versioning
# define NEXT_FNC_DEFAULT(func)                                             \
  ({                                                                        \
     static __typeof__(&func) _real_##func = (__typeof__(&func)) -1;        \
     if (_real_##func == (__typeof__(&func)) -1) {                          \
       if (dmtcp_initialize) dmtcp_initialize();                            \
       _real_##func = (__typeof__(&func)) dmtcp_dlsym(RTLD_NEXT, #func);    \
     }                                                                      \
   _real_##func;})

/*
 * It uses dmtcp_dlvsym to get the function with the specified version in the
 * next library in the library-search order.
 */
# define NEXT_FNC_DEFAULTV(func, ver)                                          \
  ({                                                                           \
     static __typeof__(&func) _real_##func = (__typeof__(&func)) -1;           \
     if (_real_##func == (__typeof__(&func)) -1) {                             \
       if (dmtcp_initialize) dmtcp_initialize();                               \
       _real_##func = (__typeof__(&func)) dmtcp_dlsym(RTLD_NEXT, #func, ver);  \
     }                                                                         \
   _real_##func;})

/*
 * It uses dmtcp_dlsym to get the default function (in case of symbol
 * versioning) in the library with the given name.
 *
 * One possible usecase could be for bypassing the plugin layers and directly
 * jumping to a symbol in libc.
 */
# define NEXT_FNC_DEFAULT_LIB(lib, func)                                       \
  ({                                                                           \
    static __typeof__(&func) _real_##func = (__typeof__(&func)) -1;            \
    if (_real_##func == (__typeof__(&func)) -1) {                              \
      if (dmtcp_initialize) {                                                  \
        dmtcp_initialize();                                                    \
      }                                                                        \
      _real_##func = (__typeof__(&func)) dmtcp_dlsym_lib(lib,  #func);         \
    }                                                                          \
    _real_##func;                                                              \
  })
#endif // ifndef STANDALONE

#ifdef STANDALONE
// For standalone testing.
// Copy this .h file to tmp.c file for standalone testing, and:
//   g++ -DSTANDALONE ../src/dmtcp_dlsym.cpp tmp.c -ldl
int main() {
  void *fnc;
  printf("pthread_cond_broadcast (via normal linker): %p\n",
         pthread_cond_broadcast);

  printf("================ dlsym ================\n");
  fnc = dlsym(RTLD_DEFAULT, "pthread_cond_broadcast");
  printf("pthread_cond_broadcast (via RTLD_DEFAULT): %p\n", fnc);
  fnc = dlsym(RTLD_NEXT, "pthread_cond_broadcast");
  printf("pthread_cond_broadcast (via RTLD_NEXT): %p\n", fnc);

  printf("================ dmtcp_dlsym ================\n");
  // NOTE: RTLD_DEFAULT would try to use this a.out, and fail to find a library
  // fnc = dmtcp_dlsym(RTLD_DEFAULT, "pthread_cond_broadcast");
  // printf("pthread_cond_broadcast (via RTLD_DEFAULT): %p\n", fnc);
  fnc = dmtcp_dlsym(RTLD_NEXT, "pthread_cond_broadcast");
  printf("pthread_cond_broadcast (via RTLD_NEXT): %p\n", fnc);

  return 0;
}
#endif
