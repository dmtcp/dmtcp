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
#include <link.h>

#include "jassert.h"
#include "dmtcp.h"

#define ENABLE_DLSYM_WRAPPER
#ifdef ENABLE_DLSYM_WRAPPER

/* NOTE:  'dlsym' is used in DMTCP in two different ways.
 *   CASE A:  Internally, in libdmtcp*.so, we have wrappers around
 *     functions in libc.so.  In order to implement that easily, we use
 *     _real_dlsym(),  and we do _not_ use dlsym().  See below, as an
 *     example of how _real_dlsym() is defined as a macro.  The macro for
 *     _real_dlsym() expands via the NEXT_FNC() macro (see include/dmtcp.h)
 *     to a call to dmtcp_dlsym().  And 'dmtcp_dlsym' is a function pointer
 *     that is initialized by DMTCP to point to 'libc:dlsym'.  This avoids
 *     the 'dlsym' symbol entirely.
 *   CASE B:  Externally, it is possible that the target application
 *     will call dlsym().  Ideally, DMTCP would not define 'dlsym'
 *     at all, and the target application would call libc:dlsym()
 *     in the usual manner.  However, there is a problem when the target
 *     application directly modifies its own global offset table for
 *     a symbol that DMTCP is trying to wrap.  See the comments on
 *     Open MPI 2.x and libc:shmdt() below.  Apparently, Open MPI uses
 *     dlsym to install its own hooks.  For the current Open MPI-4.1,
 *     see: ./opal/mca/memory/patcher/memory_patcher_component.c
 *     and: ./opal/mca/patcher/linux/patcher_linux_module.c .
 *     I'm guessing that their _intercept_shmdt() wants to make a kernel
 *     call via syscall, and the DMTCP logic interferes.  The details
 *     are unclear.  But, Open MPI was using 'dlsym' to patch 'shmdt'
 *     (probably patching libc.so). It seems that when Open MPI called
 *     "dlsym", it ended up patching the DMTCP wrapper for shmdt.
 *         It's not clear if we still want to add a dlsym wrapper.
 *     For example, if this is only for Open MPI, then we now prefer
 *     the split-process approach of MANA to support MPI.  Further, by
 *     moving the dlsym wrapper to libdmtcp.so instead of
 *     libdmtcp_svipc.so, the call to this dlsym wrapper will call
 *     RTLD)_NEXT beginning with libdmtcp.so instead of the earlier
 *     libdmtcp_svipc.so.  In the case of "shmdt", that probably makes
 *     no difference.
 *         In any case, if we want to keep the dlsym wrapper, then this
 *     code will do the right thing in the case that the target application
 *     uses 'dlsym' _only_ for a single level of wrappers, and assuming
 *     that the target application calls 'dlsym' from inside the wrapper
 *     function.  There are techniques to extend this, and one could even
 *     imitate the logic in glibc.  But for simplicity, we should avoid
 *     this for now.
 */

using namespace dmtcp;

// Open MPI 2.x uses dlsym() to locate the address of certain functions
// in order to install its own hooks. For us, shmdt() is the only interesting
// one. Instead of giving the address of our wrapper to the hook library, we
// want to return the address in libc. See PR #472 and PR #657 for details.
static int dlsym_addr_instance = 0;
static const char *dlsym_symbol = NULL;
static void *dlsym_retval;
// extern "C"
static int
callback(struct dl_phdr_info *info, size_t size, void *data) {
  void *handle = dlopen(info->dlpi_name, RTLD_LAZY);
  dlerror(); // Clear any old errors; will return non-null only on new error
  void *address = dlsym(handle, dlsym_symbol);
  if (address == NULL && dlerror() != NULL) {
    dlsym_retval = dlsym(handle, dlsym_symbol); // Set dlerror to error string
    dlclose(handle);
    return 0;
  } else {
    dlclose(handle);
  }
  // Turn on JTRACE for an overview of this logic in action.
  if (address != NULL) {
    dlsym_addr_instance++;
    JTRACE("Symbol dlsym_symbol in info->dlpi_name; found at address")
          (dlsym_symbol) (info->dlpi_name) (address);
  }
  if (dlsym_addr_instance == 1) {
    JTRACE("Caller of dlsym_symbol() in info->dlpi_name; wrapper fnc. address")
          (dlsym_symbol) (info->dlpi_name) (address);
  }
  if (dlsym_addr_instance == 2) {
    JTRACE("RTLD_NEXT") (address);
    dlsym_retval = address;
  }
  return 0;
}

// extern "C"
static void *
dlsym_with_rtld_next(const char *symbol) {
  dlsym_symbol = symbol;
  dlsym_addr_instance = 0;
  dlsym_retval = NULL;
  dl_iterate_phdr(callback, NULL);
  return dlsym_retval;
}

//   We need to wrap dlsym to disable and re-enable ckpt.  But then,
// dlsym(RTLD_NEXT, ...) no longer works, since glibc:dlsym() looks up
// the stack to find the caller's library, and discovers a DMTCP library
// instead of the original target library.
//   It was originally placed in the svipc plugin so that RTLD_NEXT would
// point to glibc.  But some targets want RTLD_NEXT to point to a library
// before glibc.  This happened when ompt_start_tool called dlsym()
// to see if there was a preferred definition later in the search path.
// By default, dlsym() should return NULL.  But now, dlsym is called by
// DMTCP, and so dlsysm discovers the Intel OpenMP library (libiomp5.so)
// later in the search path.
// FIXME:  This code searches from the beginning of the link map, using
//         dl_iterate_phdr().  It assumes that the first match of symbol
//         is the wrapper function, and that RTLD_NEXT should assume that
//         the caller starts with that library, in order to find the next
//         match.  This does not have to be the case.
extern "C"
void *
dlsym(void *handle, const char *symbol)
{
  void *ret = NULL;
  if (handle == RTLD_NEXT) {
    ret = dlsym_with_rtld_next(symbol);

    // If RTLD_NEXT failed, let's call libc-dlsym so that a subsequent dlerror()
    // will return the right error message.
    if (ret != NULL) {
      return ret;
    }
  }

  DMTCP_PLUGIN_DISABLE_CKPT();
  // FIXME:  _real_dlsym is currently in src/plugin/pid/pid_syscallsreal.c
  //         Should we move it to src/syscallsreal.c ?
  ret = NEXT_FNC(dlsym)(handle, symbol);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}
#endif // #ifdef ENABLE_DLSYM_WRAPPER
