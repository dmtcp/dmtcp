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

/* ptsname_r is defined with "always_inline" attribute. GCC>=4.7 disallows us
 * to define the ptsname_r wrapper if compiled with -O0. Here we are renaming
 * ptsname_r so that later code does not see the declaration of ptsname_r as
 * inline. Normal user code from other files will continue to invoke ptsname_r
 * as inline as an inline function calling __ptsname_r_chk. Later in this file
 * we define __ptsname_r_chk to call the original ptsname_r symbol.
 * Similarly, for ttyname_r, etc.
 *
 * Also, on some machines (e.g. SLES 10), readlink has conflicting return types
 * (ssize_t and int).
 *     In general, we rename the functions below, since any type declarations
 * may vary on different systems, and so we ignore these type declarations.
*/
#define open     open_always_inline
#define open64   open64_always_inline
#define openat   openat_always_inline
#define openat64 openat64_always_inline

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/version.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <list>
#include <string>
#include <vector>

#undef open
#undef open64
#undef openat
#undef openat64

#include "jassert.h"
#include "jconvert.h"
#include "jfilesystem.h"
#include "dmtcp.h"
#include "shareddata.h"
#include "util.h"

#include "fileconnection.h"
#include "fileconnlist.h"
#include "filewrappers.h"
#include "ptyconnection.h"
#include "ptyconnlist.h"

using namespace dmtcp;

static void
processConnection(int fd, const char *path, int flags, mode_t mode)
{
  string device = jalib::Filesystem::ResolveSymlink(path);

  if (device == "") {
    device = path;
  }

  if (Util::isPseudoTty(device.c_str())) {
    PtyConnList::instance().processPtyConnection(fd, path, flags, mode);
  } else {
    FileConnList::instance().processFileConnection(fd, path, flags, mode);
  }
}

static int
_open_open64_work(int (*fn)(const char *path,
                            int flags,
                            ...), const char *path, int flags, mode_t mode)
{
  char currPtsDevName[32];
  const char *newpath = path;

  DMTCP_PLUGIN_DISABLE_CKPT();

  if (Util::strStartsWith(path, VIRT_PTS_PREFIX_STR)) {
    SharedData::getRealPtyName(path, currPtsDevName, sizeof(currPtsDevName));
    newpath = currPtsDevName;
  }

  int fd = (*fn)(newpath, flags, mode);

  if (fd >= 0 && dmtcp_is_running_state()) {
    processConnection(fd, newpath, flags, mode);
  }

  DMTCP_PLUGIN_ENABLE_CKPT();

  return fd;
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
  return _open_open64_work(_real_open, path, flags, mode);
}

extern "C" int
__open_2(const char *path, int flags)
{
  return _open_open64_work(_real_open, path, flags, 0);
}

// FIXME: The 'fn64' version of functions is defined only when within
// __USE_LARGEFILE64 is #defined. The wrappers in this file need to consider
// this fact. The problem can occur, for example, when DMTCP is not compiled
// with __USE_LARGEFILE64 whereas the user-binary is. In that case the open64()
// call from user will come to DMTCP and DMTCP might fail to execute it
// properly.

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
  return _open_open64_work(_real_open64, path, flags, mode);
}

extern "C" int
__open64_2(const char *path, int flags)
{
  return _open_open64_work(_real_open64, path, flags, 0);
}

static FILE *
_fopen_fopen64_work(FILE *(*fn)(const char *path,
                                const char *mode), const char *path,
                    const char *mode)
{
  char currPtsDevName[32];
  const char *newpath = path;

  DMTCP_PLUGIN_DISABLE_CKPT();

  if (Util::strStartsWith(path, VIRT_PTS_PREFIX_STR)) {
    SharedData::getRealPtyName(path, currPtsDevName, sizeof(currPtsDevName));
    newpath = currPtsDevName;
  }

  FILE *file = (*fn)(newpath, mode);

  if (file != NULL && dmtcp_is_running_state()) {
    processConnection(fileno(file), newpath, -1, -1);
  }

  DMTCP_PLUGIN_ENABLE_CKPT();
  return file;
}

extern "C" FILE * fopen(const char *path, const char *mode)
{
  return _fopen_fopen64_work(_real_fopen, path, mode);
}

extern "C" FILE * fopen64(const char *path, const char *mode)
{
  return _fopen_fopen64_work(_real_fopen64, path, mode);
}

extern "C" FILE * freopen(const char *path, const char *mode, FILE * stream)
{
  char currPtsDevName[32];
  const char *newpath = path;

  DMTCP_PLUGIN_DISABLE_CKPT();

  if (Util::strStartsWith(path, VIRT_PTS_PREFIX_STR)) {
    SharedData::getRealPtyName(path, currPtsDevName,
                               sizeof(currPtsDevName));
    newpath = currPtsDevName;
  }

  FILE *file = _real_freopen(newpath, mode, stream);

  if (file != NULL && dmtcp_is_running_state()) {
    processConnection(fileno(file), newpath, -1, -1);
  }

  DMTCP_PLUGIN_ENABLE_CKPT();
  return file;
}

extern "C" int
openat(int dirfd, const char *path, int flags, ...)
{
  va_list arg;

  va_start(arg, flags);
  mode_t mode = va_arg(arg, int);
  va_end(arg);
  DMTCP_PLUGIN_DISABLE_CKPT();
  int fd = _real_openat(dirfd, path, flags, mode);
  if (fd >= 0 && dmtcp_is_running_state()) {
    string procpath = "/proc/self/fd/" + jalib::XToString(fd);
    string device = jalib::Filesystem::ResolveSymlink(procpath);
    processConnection(fd, device.c_str(), flags, mode);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return fd;
}

extern "C" int
openat_2(int dirfd, const char *path, int flags)
{
  return openat(dirfd, path, flags, 0);
}

extern "C" int
__openat_2(int dirfd, const char *path, int flags)
{
  return openat(dirfd, path, flags, 0);
}

extern "C" int
openat64(int dirfd, const char *path, int flags, ...)
{
  va_list arg;

  va_start(arg, flags);
  mode_t mode = va_arg(arg, int);
  va_end(arg);
  DMTCP_PLUGIN_DISABLE_CKPT();
  int fd = _real_openat64(dirfd, path, flags, mode);
  if (fd >= 0 && dmtcp_is_running_state()) {
    string procpath = "/proc/self/fd/" + jalib::XToString(fd);
    string device = jalib::Filesystem::ResolveSymlink(procpath);
    processConnection(fd, device.c_str(), flags, mode);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return fd;
}

extern "C" int
openat64_2(int dirfd, const char *path, int flags)
{
  return openat64(dirfd, path, flags, 0);
}

extern "C" int
__openat64_2(int dirfd, const char *path, int flags)
{
  return openat64(dirfd, path, flags, 0);
}

extern "C" int
creat(const char *path, mode_t mode)
{
  // creat() is equivalent to open() with flags equal to
  // O_CREAT|O_WRONLY|O_TRUNC
  return _open_open64_work(_real_open, path, O_CREAT | O_WRONLY | O_TRUNC,
                           mode);
}

extern "C" int
creat64(const char *path, mode_t mode)
{
  // creat() is equivalent to open() with flags equal to
  // O_CREAT|O_WRONLY|O_TRUNC
  return _open_open64_work(_real_open64,
                           path,
                           O_CREAT | O_WRONLY | O_TRUNC,
                           mode);
}

extern "C" void process_fd_event(int event, int arg1, int arg2 = -1);
extern "C" int
fcntl(int fd, int cmd, ...)
{
  void *arg = NULL;
  va_list ap;

  va_start(ap, cmd);
  arg = va_arg(ap, void *);
  va_end(ap);

  DMTCP_PLUGIN_DISABLE_CKPT();

  int res = _real_fcntl(fd, cmd, arg);
  if (res != -1 &&
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
      (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC) &&
#else // if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
      (cmd == F_DUPFD) &&
#endif // if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
      dmtcp_is_running_state()) {
    process_fd_event(SYS_dup, fd, res);
  }

  DMTCP_PLUGIN_ENABLE_CKPT();
  return res;
}

#if 0

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
#endif // if 0
