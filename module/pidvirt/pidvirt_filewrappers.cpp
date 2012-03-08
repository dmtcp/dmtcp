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

#include <unistd.h>
#include <stdlib.h>
#include <string>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/personality.h>
#include <linux/version.h>
#include <string.h>

#include "jassert.h"
#include "jfilesystem.h"
#include "jconvert.h"
#include "pidwrappers.h"
#include "util.h"
#include "virtualpidtable.h"
#include "dmtcpmodule.h"
#include "pidvirt.h"
#include "sysvipc.h"

static void updateProcPath(const char *path, char *newpath)
{
  if (path == NULL || strlen(path) == 0) {
    strcpy(newpath, "");
    return;
  }

  if (dmtcp::Util::strStartsWith(path, "/proc/")) {
    int index = 6;
    char *rest;
    pid_t virtualPid = strtol(&path[index], &rest, 0);
    if (virtualPid > 0 && *rest == '/') {
      pid_t realPid = VIRTUAL_TO_REAL_PID(virtualPid);
      sprintf(newpath, "/proc/%d%s", realPid, rest);
    } else {
      strcpy(newpath, path);
    }
  } else {
    strcpy(newpath, path);
  }
  return;
}

/* Used by open() wrapper to do other tracking of open apart from
   synchronization stuff. */
extern "C" int open (const char *path, int flags, ... )
{
  mode_t mode = 0;
  // Handling the variable number of arguments
  if (flags & O_CREAT) {
    va_list arg;
    va_start (arg, flags);
    mode = va_arg (arg, int);
    va_end (arg);
  }
  char newpath[PATH_MAX];
  updateProcPath(path, newpath);
  return _real_open(newpath, flags, mode);
}

// FIXME: Add the 'fn64' wrapper test cases to dmtcp test suite.
extern "C" int open64 (const char *path, int flags, ... )
{
  mode_t mode;
  // Handling the variable number of arguments
  if (flags & O_CREAT) {
    va_list arg;
    va_start (arg, flags);
    mode = va_arg (arg, int);
    va_end (arg);
  }
  char newpath[PATH_MAX];
  updateProcPath(path, newpath);
  return _real_open64(newpath, flags, mode);
}

extern "C" FILE *fopen (const char* path, const char* mode)
{
  char newpath[PATH_MAX];
  updateProcPath(path, newpath);
  return _real_fopen(newpath, mode);
}

extern "C" FILE *fopen64 (const char* path, const char* mode)
{
  char newpath[PATH_MAX];
  updateProcPath(path, newpath);
  return _real_fopen64(newpath, mode);
}

extern "C" int __xstat(int vers, const char *path, struct stat *buf)
{
  char newpath [ PATH_MAX ] = {0} ;
  updateProcPath(path, newpath);
  int retval = _real_xstat( vers, newpath, buf );
  return retval;
}

extern "C" int __xstat64(int vers, const char *path, struct stat64 *buf)
{
  char newpath [ PATH_MAX ] = {0};
  updateProcPath(path, newpath);
  int retval = _real_xstat64( vers, newpath, buf );
  return retval;
}

#if 0
extern "C" int __fxstat(int vers, int fd, struct stat *buf)
{
  int retval = _real_fxstat(vers, fd, buf);
  return retval;
}

extern "C" int __fxstat64(int vers, int fd, struct stat64 *buf)
{
  int retval = _real_fxstat64(vers, fd, buf);
  return retval;
}
#endif

extern "C" int __lxstat(int vers, const char *path, struct stat *buf)
{
  char newpath [ PATH_MAX ] = {0} ;
  updateProcPath(path, newpath);
  int retval = _real_lxstat( vers, newpath, buf );
  return retval;
}

extern "C" int __lxstat64(int vers, const char *path, struct stat64 *buf)
{
  char newpath [ PATH_MAX ] = {0} ;
  updateProcPath(path, newpath);
  int retval = _real_lxstat64( vers, newpath, buf );
  return retval;
}

extern "C" READLINK_RET_TYPE readlink(const char *path, char *buf,
                                      size_t bufsiz)
{
  char newpath [ PATH_MAX ] = {0} ;
  READLINK_RET_TYPE retval;
  updateProcPath(path, newpath);
  retval = _real_readlink(newpath, buf, bufsiz);
  return retval;
}


// TODO:  ioctl must use virtualized pids for request = TIOCGPGRP / TIOCSPGRP
// These are synonyms for POSIX standard tcgetpgrp / tcsetpgrp
extern "C" {
int send_sigwinch = 0;
}


extern "C" int ioctl(int d,  unsigned long int request, ...)
{
  va_list ap;
  int retval;

  if (send_sigwinch && request == TIOCGWINSZ) {
    send_sigwinch = 0;
    va_list local_ap;
    va_copy(local_ap, ap);
    va_start(local_ap, request);
    struct winsize * win = va_arg(local_ap, struct winsize *);
    va_end(local_ap);
    retval = _real_ioctl(d, request, win);  // This fills in win
    win->ws_col--; // Lie to application, and force it to resize window,
		   //  reset any scroll regions, etc.
    kill(getpid(), SIGWINCH); // Tell application to look up true winsize
			      // and resize again.
  } else {
    void * arg;
    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);
    retval = _real_ioctl(d, request, arg);
  }
  return retval;
}
