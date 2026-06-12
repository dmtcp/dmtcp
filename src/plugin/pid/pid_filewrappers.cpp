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

/* realpath is defined with "always_inline" attribute. GCC>=4.7 disallows us
 * to define the realpath wrapper if compiled with -O0. Here we are renaming
 * realpath so that later code does not see the declaration of realpath as
 * inline. Normal user code from other files will continue to invoke realpath
 * as an inline function calling __ptsname_r_chk. Later in this file
 * we define __ptsname_r_chk to call the original realpath symbol.
 * Similarly, for ttyname_r, etc.
 *
 * Also, on some machines (e.g. SLES 10), readlink has conflicting return types
 * (ssize_t and int).
 *     In general, we rename the functions below, since any type declarations
 * may vary on different systems, and so we ignore these type declarations.
 */
#define readlink readlink_always_inline
#define realpath realpath_always_inline

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

#undef readlink
#undef realpath

#include "jconvert.h"
#include "jfilesystem.h"
#include "dmtcp.h"
#include "pid.h"
#include "pidwrappers.h"
#include "util.h"
#include "virtualpidtable.h"

using namespace dmtcp;

// TODO:  ioctl must use virtualized pids for request = TIOCGPGRP / TIOCSPGRP
// These are synonyms for POSIX standard tcgetpgrp / tcsetpgrp
extern "C" {
int send_sigwinch = 0;
}

static bool
ioctlRequestTakesNoArg(unsigned long int request)
{
#ifdef FIOCLEX
  if (request == FIOCLEX) {
    return true;
  }
#endif
#ifdef FIONCLEX
  if (request == FIONCLEX) {
    return true;
  }
#endif
#ifdef TIOCEXCL
  if (request == TIOCEXCL) {
    return true;
  }
#endif
#ifdef TIOCNXCL
  if (request == TIOCNXCL) {
    return true;
  }
#endif
#ifdef TIOCNOTTY
  if (request == TIOCNOTTY) {
    return true;
  }
#endif
  return false;
}

static bool
ioctlRequestTakesIntArg(unsigned long int request)
{
#ifdef TCSBRK
  if (request == TCSBRK) {
    return true;
  }
#endif
#ifdef TCSBRKP
  if (request == TCSBRKP) {
    return true;
  }
#endif
#ifdef TCFLSH
  if (request == TCFLSH) {
    return true;
  }
#endif
#ifdef TCXONC
  if (request == TCXONC) {
    return true;
  }
#endif
#ifdef TIOCSCTTY
  if (request == TIOCSCTTY) {
    return true;
  }
#endif
  return false;
}


extern "C" int
ioctl(int d, unsigned long int request, ...)
{
  va_list ap;
  int retval;

  if (send_sigwinch && request == TIOCGWINSZ) {
    send_sigwinch = 0;
    va_start(ap, request);
    struct winsize *win = va_arg(ap, struct winsize *);
    va_end(ap);
    retval = _real_ioctl(d, request, win);  // This fills in win
    win->ws_col--; // Lie to application, and force it to resize window,
                   // reset any scroll regions, etc.
    kill(getpid(), SIGWINCH); // Tell application to look up true winsize
                              // and resize again.
  } else if (ioctlRequestTakesNoArg(request)) {
    retval = _real_ioctl(d, request);
  } else if (ioctlRequestTakesIntArg(request)) {
    va_start(ap, request);
    int arg = va_arg(ap, int);
    va_end(ap);
    retval = _real_ioctl(d, request, arg);
  } else {
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    retval = _real_ioctl(d, request, arg);
  }
  return retval;
}
