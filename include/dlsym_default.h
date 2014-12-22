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

/************************************************************************
 * IMPORTANT CAVEATS:
 *   DLSYM_DEFAULT() is effective when called from a library, but not when
 *     called from within the base executable.
 *   Don't use dlsym_default_internal() outside of this macro.
 *   This must be a macro because dlsym() looks one level up in the stack
 *     to decide what library the caller of dlsym() is located in.
 *   RTLD_DEFAULT does not work with this macro.
 ************************************************************************/

// #define DLSYM_DEFAULT_DO_DEBUG

#ifdef DLSYM_DEFAULT_DO_DEBUG
# define DLSYM_DEFAULT_DEBUG(handle,symbol,info) \
    JNOTE("DLSYM_DEFAULT (RTLD_NEXT==-1l)")(symbol)(handle) \
         (info.dli_fname)(info.dli_saddr)
#else
# define DLSYM_DEFAULT_DEBUG(handle,symbol,info)
#endif

#define DLSYM_DEFAULT(handle,symbol)                                          \
  ({ Dl_info info;                                                            \
     void *handle2 = handle;                                                  \
     if (handle == RTLD_DEFAULT || handle == RTLD_NEXT) {                     \
       /* Hack: use dlsym()/dlopen() only to get the lib handle */            \
       /* MUST BE MACRO:  dlsym uses stack to find curr. lib for RTLD_NEXT */ \
       /* Logic for dlsym_fnptr is shadowing logic for dmtcp.h:NEXT_FNC() */  \
       __typeof__(&dlsym) dlsym_fnptr;                                        \
       dlsym_fnptr = (__typeof__(&dlsym)) dmtcp_get_libc_dlsym_addr();        \
       void *tmp_fnc = (*dlsym_fnptr) (handle2, symbol);                      \
       dladdr(tmp_fnc, &info);                                                \
       DLSYM_DEFAULT_DEBUG(handle,symbol,info);                               \
       /* Found handle of RTLD_NEXT or RTLD_DEFAULT */                        \
       handle2 = dlopen(info.dli_fname, RTLD_NOLOAD | RTLD_LAZY);             \
     }                                                                        \
     /* Same signature as dlsym(): */                                         \
     void *symbol_ptr = dlsym_default_internal(handle2, symbol);              \
     if (handle == RTLD_DEFAULT || handle == RTLD_NEXT) {                     \
       dlclose(handle2); /* Should verify that this returns 9. */             \
     }                                                                        \
     symbol_ptr; })

#ifdef __cplusplus
extern "C"
{
#else
#endif
  void *dlsym_default_internal(void *handle, const char *symbol);
#ifdef __cplusplus
}
#else
#endif

#ifndef STANDALONE
// FIXME: DLSYM_DEFAULT() and dlsym_default_internal() should probably
//    both use dmtcp_get_libc_dlsym_addr() instead of dlsym()
//    when used with DMTCP.
//    Or do they need to?

// This implementation mirrors dmtcp.h:NEXT_FNC() for DMTCP.
// It uses DLSYM_DEFAULT to get default version, in case of symbol versioning
# define NEXT_FNC_DEFAULT(func)                                             \
  ({                                                                        \
     static __typeof__(&func) _real_##func = (__typeof__(&func)) -1;        \
     if (_real_##func == (__typeof__(&func)) -1) {                          \
       if (dmtcp_prepare_wrappers) dmtcp_prepare_wrappers();                \
       _real_##func = (__typeof__(&func)) DLSYM_DEFAULT(RTLD_NEXT, #func);  \
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
  // fnc = DLSYM_DEFAULT(RTLD_DEFAULT, "pthread_cond_broadcast");
  // printf("pthread_cond_broadcast (via RTLD_DEFAULT): %p\n", fnc);
  fnc = DLSYM_DEFAULT(RTLD_NEXT, "pthread_cond_broadcast");
  printf("pthread_cond_broadcast (via RTLD_NEXT): %p\n", fnc);

  return 0;
}
#endif
