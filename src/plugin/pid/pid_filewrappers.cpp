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
#define open     open_always_inline
#define open64   open64_always_inline
#define readlink readlink_always_inline
#define realpath realpath_always_inline

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

#undef open
#undef open64
#undef readlink
#undef realpath

#include "jassert.h"
#include "jconvert.h"
#include "jfilesystem.h"
#include "dmtcp.h"
#include "pid.h"
#include "pidwrappers.h"
#include "util.h"
#include "virtualpidtable.h"

#define PROC_PREFIX "/proc/"

using namespace dmtcp;

// FIXME:  This function needs third argument newpathsize, or assume PATH_MAX
static void
updateProcPathVirtualToReal(const char *path, char **newpath)
{
  if (Util::strStartsWith(path, PROC_PREFIX)) {
    int index = strlen(PROC_PREFIX);
    char *rest;
    pid_t virtualPid = strtol(&path[index], &rest, 0);
    // WAS: if (realPid > 0 && *rest == '/')
    // Removing the "*rest ..." check to handle /proc/<pid>
    if (virtualPid > 0) {
      pid_t realPid = VIRTUAL_TO_REAL_PID(virtualPid);
      sprintf(*newpath, "/proc/%d%s", realPid, rest);
      return;
    }
  }
  *newpath = (char *)path;
}

// FIXME:  This function needs third argument newpathsize, or assume PATH_MAX
static void
updateProcPathRealToVirtual(const char *path, char **newpath)
{
  if (Util::strStartsWith(path, PROC_PREFIX)) {
    int index = strlen(PROC_PREFIX);
    char *rest;
    pid_t realPid = strtol(&path[index], &rest, 0);
    // WAS: if (realPid > 0 && *rest == '/')
    // Removing the "*rest ..." check to handle /proc/<pid>
    if (realPid > 0) {
      pid_t virtualPid = REAL_TO_VIRTUAL_PID(realPid);
      sprintf(*newpath, "/proc/%d%s", virtualPid, rest);
      return;
    }
  }
  *newpath = (char *)path;
}

/* Used by open() wrapper to do other tracking of open apart from
   synchronization stuff. */
extern "C" int
open(const char *path, int flags, ...)
{
  mode_t mode = 0;

  // Handling the variable number of arguments
  if (flags & O_CREAT) {
    va_list arg;
    va_start(arg, flags);
    mode = va_arg(arg, int);
    va_end(arg);
  }
  char tmpbuf[PATH_MAX];
  char *newpath = tmpbuf;
  updateProcPathVirtualToReal(path, &newpath);
  return _real_open(newpath, flags, mode);
}

// FIXME: Add the 'fn64' wrapper test cases to dmtcp test suite.
extern "C" int
open64(const char *path, int flags, ...)
{
  mode_t mode = 0;

  // Handling the variable number of arguments
  if (flags & O_CREAT) {
    va_list arg;
    va_start(arg, flags);
    mode = va_arg(arg, int);
    va_end(arg);
  }
  char tmpbuf[PATH_MAX];
  char *newpath = tmpbuf;
  updateProcPathVirtualToReal(path, &newpath);
  return _real_open64(newpath, flags, mode);
}

extern "C" FILE * fopen(const char *path, const char *mode)
{
  char tmpbuf[PATH_MAX];
  char *newpath = tmpbuf;

  updateProcPathVirtualToReal(path, &newpath);
  return _real_fopen(newpath, mode);
}

extern "C" FILE * fopen64(const char *path, const char *mode)
{
  char tmpbuf[PATH_MAX];
  char *newpath = tmpbuf;

  updateProcPathVirtualToReal(path, &newpath);
  return _real_fopen64(newpath, mode);
}

extern "C" int
fclose(FILE *fp)
{
  // This wrapper is needed to ensure that we call the "GLIBC_2.1" version in
  // 32-bit systems.  Ideally, this should be done only in the plugin that uses
  // fclose (e.g. File plugin), but doing it here will work as well.
  return _real_fclose(fp);
}

extern "C" DIR*
opendir(const char *name)
{
  char tmpbuf[PATH_MAX];
  char *newpath = tmpbuf;
  updateProcPathVirtualToReal(name, &newpath);
  return _real_opendir(newpath);
}

extern "C" int
__xstat(int vers, const char *path, struct stat *buf)
{
  char tmpbuf[PATH_MAX];
  char *newpath = tmpbuf;

  // See filewrapper.cpp:__xstat() for comments on this code.
  int retval = _real_xstat(vers, path, buf);

  if (retval == -1 && errno == EFAULT) {
    // We're done.  Return.
  } else {
    updateProcPathVirtualToReal(path, &newpath);
    if (newpath != path) {
      retval = _real_xstat(vers, newpath, buf);
    }
  }
  return retval;
}

extern "C" int
__xstat64(int vers, const char *path, struct stat64 *buf)
{
  char tmpbuf[PATH_MAX];
  char *newpath = tmpbuf;

  // See filewrapper.cpp:__xstat() for comments on this code.
  int retval = _real_xstat64(vers, path, buf);

  if (retval == -1 && errno == EFAULT) {
    // We're done.  Return.
  } else {
    updateProcPathVirtualToReal(path, &newpath);
    if (newpath != path) {
      retval = _real_xstat64(vers, newpath, buf);
    }
  }
  return retval;
}

#if 0
extern "C" int
__fxstat(int vers, int fd, struct stat *buf)
{
  int retval = _real_fxstat(vers, fd, buf);

  return retval;
}

extern "C" int
__fxstat64(int vers, int fd, struct stat64 *buf)
{
  int retval = _real_fxstat64(vers, fd, buf);

  return retval;
}
#endif // if 0

extern "C" int
__lxstat(int vers, const char *path, struct stat *buf)
{
  char tmpbuf[PATH_MAX];
  char *newpath = tmpbuf;

  // See filewrapper.cpp:__xstat() for comments on this code.
  int retval = _real_lxstat(vers, path, buf);

  if (retval == -1 && errno == EFAULT) {
    // We're done.  Return.
  } else {
    updateProcPathVirtualToReal(path, &newpath);
    if (newpath != path) {
      retval = _real_lxstat(vers, newpath, buf);
    }
  }
  return retval;
}

extern "C" int
__lxstat64(int vers, const char *path, struct stat64 *buf)
{
  char tmpbuf[PATH_MAX];
  char *newpath = tmpbuf;

  // See filewrapper.cpp:__xstat() for comments on this code.
  int retval = _real_lxstat64(vers, path, buf);

  if (retval == -1 && errno == EFAULT) {
    // We're done.  Return.
  } else {
    updateProcPathVirtualToReal(path, &newpath);
    if (newpath != path) {
      retval = _real_lxstat64(vers, newpath, buf);
    }
  }
  return retval;
}

extern "C" ssize_t
readlink(const char *path, char *buf, size_t bufsiz)
{
  char tmpbuf[PATH_MAX];
  char *newpath = tmpbuf;

  // FIXME:  Suppose the real path is longer than PATH_MAX.  Do we check?
  updateProcPathVirtualToReal(path, &newpath);
  return NEXT_FNC(readlink) (newpath, buf, bufsiz);

#if 0
  if (ret != -1) {
    JASSERT(ret < bufsiz)(ret)(bufsiz)(buf)(newpath);
    buf[ret] = '\0'; // glibc-2.13: readlink doesn't terminate buf w/ null char
    updateProcPathRealToVirtual(buf, newpath);
    JASSERT(strlen(newpath) < bufsiz)(newpath)(bufsiz);
    strcpy(buf, newpath);
  }
  return ret;
#endif // if 0
}

extern "C" char *realpath(const char *path, char *resolved_path)
{
  char tmpbuf[PATH_MAX];
  char *newpath = tmpbuf;

  updateProcPathVirtualToReal(path, &newpath);

  // Required for matlab-2012 and later; realpath is a versioned symbol.
  char *retval = NEXT_FNC(realpath) (newpath, resolved_path);
  if (retval != NULL) {
    updateProcPathRealToVirtual(retval, &newpath);
    strcpy(retval, newpath);
  }
  return retval;
}

extern "C" char *__realpath(const char *path, char *resolved_path)
{
  return realpath(path, resolved_path);
}

extern "C" char *__realpath_chk(const char *path, char *resolved_path,
                                size_t resolved_len)
{
  return realpath(path, resolved_path);
}

extern "C" char *canonicalize_file_name(const char *path)
{
  return realpath(path, NULL);
}

#include <unistd.h>
extern "C" int
access(const char *path, int mode)
{
  char tmpbuf[PATH_MAX];
  char *newpath = tmpbuf;

  updateProcPathVirtualToReal(path, &newpath);
  return NEXT_FNC(access) (newpath, mode);
}

// TODO:  ioctl must use virtualized pids for request = TIOCGPGRP / TIOCSPGRP
// These are synonyms for POSIX standard tcgetpgrp / tcsetpgrp
extern "C" {
int send_sigwinch = 0;
}


extern "C" int
ioctl(int d, unsigned long int request, ...)
{
  va_list ap;
  int retval;

  if (send_sigwinch && request == TIOCGWINSZ) {
    send_sigwinch = 0;
    va_list local_ap;
    va_copy(local_ap, ap);
    va_start(local_ap, request);
    struct winsize *win = va_arg(local_ap, struct winsize *);
    va_end(local_ap);
    retval = _real_ioctl(d, request, win);  // This fills in win
    win->ws_col--; // Lie to application, and force it to resize window,
                   // reset any scroll regions, etc.
    kill(getpid(), SIGWINCH); // Tell application to look up true winsize
                              // and resize again.
  } else {
    void *arg;
    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);
    retval = _real_ioctl(d, request, arg);
  }
  return retval;
}
