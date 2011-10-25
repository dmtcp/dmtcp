/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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


#define _GNU_SOURCE
#define _XOPEN_SOURCE 500

#include <malloc.h>
#include <pthread.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#include "ptracewrappers.h"

typedef int ( *funcptr_t ) ();
typedef pid_t ( *funcptr_pid_t ) ();
typedef funcptr_t ( *signal_funcptr_t ) ();

#define REAL_FUNC_PASSTHROUGH_TYPED(type,name) \
  static type (*fn)() = NULL; \
  if (fn == NULL) { \
    fn = _real_dlsym(RTLD_NEXT, #name); \
    if (fn == NULL) { \
      fprintf(stderr, "*** DMTCP: Error: lookup failed for %s.\n" \
                      "           The symbol wasn't found in current library" \
                      " loading sequence.\n" \
                      "    Aborting.\n", #name); \
      abort(); \
    } \
  } \
  return (*fn)

typedef void* (*dlsym_fnptr_t) (void *handle, const char *symbol);
void *dmtcp_get_libc_dlsym_addr();

LIB_PRIVATE
void *_real_dlsym ( void *handle, const char *symbol ) {
  static dlsym_fnptr_t _libc_dlsym_fnptr = NULL;
  if (_libc_dlsym_fnptr == NULL) {
    _libc_dlsym_fnptr = (dlsym_fnptr_t) dmtcp_get_libc_dlsym_addr();
  }

  return (void*) (*_libc_dlsym_fnptr) ( handle, symbol );
}

LIB_PRIVATE
pid_t _real_waitpid(pid_t pid, int *stat_loc, int options) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, waitpid ) ( pid, stat_loc, options );
}

LIB_PRIVATE
long _real_ptrace(enum __ptrace_request request, pid_t pid, void *addr,
                  void *data) {
  REAL_FUNC_PASSTHROUGH_TYPED ( long, ptrace ) ( request, pid, addr, data );
}
