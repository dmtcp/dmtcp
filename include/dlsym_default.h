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

#include <stdio.h>
#include <pthread.h>

#ifndef __USE_GNU
# define __USE_GNU_NOT_SET
# define __USE_GNU
#endif
#include <dlfcn.h>  /* for NEXT_FNC() */
#ifdef __USE_GNU_NOT_SET
# undef __USE_GNU_NOT_SET
# undef __USE_GNU
#endif

// #define DLSYM_DEFAULT_DO_DEBUG

#ifdef DLSYM_DEFAULT_DO_DEBUG
# define DLSYM_DEFAULT_DEBUG(handle,symbol,info) \
    JNOTE("dlsym_default (RTLD_NEXT==-1l)")(symbol)(handle) \
         (info.dli_fname)(info.dli_saddr)
#else
# define DLSYM_DEFAULT_DEBUG(handle,symbol,info)
#endif

#ifdef __cplusplus
extern "C"
{
#else
#endif
 void *dlsym_default(void *handle, const char *symbol);
#ifdef __cplusplus
}
#else
#endif

#ifndef STANDALONE
// This implementation mirrors dmtcp.h:NEXT_FNC() for DMTCP.
// It uses dlsym_default to get default version, in case of symbol versioning
# define NEXT_FNC_DEFAULT(func)                                             \
  ({                                                                        \
     static __typeof__(&func) _real_##func = (__typeof__(&func)) -1;        \
     if (_real_##func == (__typeof__(&func)) -1) {                          \
       if (dmtcp_initialize) dmtcp_initialize();                            \
       _real_##func = (__typeof__(&func)) dlsym_default(RTLD_NEXT, #func);  \
     }                                                                      \
   _real_##func;})
#endif

#ifdef STANDALONE
// For standalone testing.
// Copy this .h file to tmp.c file for standalone testing, and:
//   gcc -DSTANDALONE ../src/dlsym_default.c tmp.c -ldl
int main() {
  void *fnc;
  printf("pthread_cond_broadcast (via normal linker): %p\n",
         pthread_cond_broadcast);

  printf("================ dlsym ================\n");
  fnc = dlsym(RTLD_DEFAULT, "pthread_cond_broadcast");
  printf("pthread_cond_broadcast (via RTLD_DEFAULT): %p\n", fnc);
  fnc = dlsym(RTLD_NEXT, "pthread_cond_broadcast");
  printf("pthread_cond_broadcast (via RTLD_NEXT): %p\n", fnc);

  printf("================ dlsym_default ================\n");
  // NOTE: RTLD_DEFAULT would try to use this a.out, and fail to find a library
  // fnc = dlsym_default(RTLD_DEFAULT, "pthread_cond_broadcast");
  // printf("pthread_cond_broadcast (via RTLD_DEFAULT): %p\n", fnc);
  fnc = dlsym_default(RTLD_NEXT, "pthread_cond_broadcast");
  printf("pthread_cond_broadcast (via RTLD_NEXT): %p\n", fnc);

  return 0;
}
#endif
