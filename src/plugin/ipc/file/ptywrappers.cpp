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
#define ptsname_r ptsname_r_always_inline
#define ttyname_r ttyname_r_always_inline
#define readlink readlink_always_inline
#define __readlink_chk _ret__readlink_chk
#define realpath realpath_always_inline

#include <stdarg.h>
#include <stdlib.h>
#include <vector>
#include <list>
#include <string>
#include <fcntl.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/syscall.h>
#include <linux/version.h>
#include <limits.h>

#undef ptsname_r
#undef ttyname_r
#undef readlink
#undef __readlink_chk
#undef realpath

#include "dmtcp.h"
#include "shareddata.h"
#include "util.h"
#include "jassert.h"

#include "fileconnlist.h"
#include "ptywrappers.h"

using namespace dmtcp;

static void updateStatPath(const char *path, char **newpath)
{
  if (Util::strStartsWith(path, VIRT_PTS_PREFIX_STR)) {
    char currPtsDevName[32];
    SharedData::getRealPtyName(path, currPtsDevName,
                                      sizeof(currPtsDevName));
    strcpy(*newpath, currPtsDevName);
  } else {
    *newpath = (char*) path;
  }
}

extern "C" int __xstat(int vers, const char *path, struct stat *buf)
{
  char tmpbuf [ PATH_MAX ] = {0} ;
  char *newpath = tmpbuf;
  DMTCP_PLUGIN_DISABLE_CKPT();
  // We want to call updateStatPath(). But if path is an invalid memory address,
  //   then updateStatPath() will crash.  So, do a preliminary call to
  //   _real_xstat().  If path or buf is invalid, return with the erro.
  //   If path is a valid memory address, but not a valid filename,
  //   there is no harm done, since xstat has no side effects outside of buf.
  int retval = _real_xstat(vers, path, buf);
  if (retval == -1 && errno == EFAULT) {
    // EFAULT means path or buf was a bad address.  So, we're done.  Return.
    // And don't call updateStatPath().  If path is bad, it will crash.
  } else {
    updateStatPath(path, &newpath);
    if (newpath != path) {
      retval = _real_xstat(vers, newpath, buf); // Re-do it with correct path.
    } // else use answer from previous call to _real_xstat(), and save time.
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

extern "C" int __xstat64(int vers, const char *path, struct stat64 *buf)
{
  char tmpbuf [ PATH_MAX ] = {0};
  char *newpath = tmpbuf;
  DMTCP_PLUGIN_DISABLE_CKPT();
  // See filewrapper.cpp:__xstat() for comments on this code.
  int retval = _real_xstat64(vers, path, buf);
  if (retval == -1 && errno == EFAULT) {
    // We're done.  Return.
  } else {
    updateStatPath(path, &newpath);
    if (newpath != path) {
      retval = _real_xstat64(vers, newpath, buf);
    }
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

#if 0
extern "C" int __fxstat(int vers, int fd, struct stat *buf)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int retval = _real_fxstat(vers, fd, buf);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

extern "C" int __fxstat64(int vers, int fd, struct stat64 *buf)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int retval = _real_fxstat64(vers, fd, buf);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}
#endif

extern "C" int __lxstat(int vers, const char *path, struct stat *buf)
{
  char tmpbuf [ PATH_MAX ] = {0} ;
  char *newpath = tmpbuf;
  DMTCP_PLUGIN_DISABLE_CKPT();
  // See filewrapper.cpp:__xstat() for comments on this code.
  int retval = _real_lxstat(vers, path, buf);
  if (retval == -1 && errno == EFAULT) {
    // We're done.  Return.
  } else {
    updateStatPath(path, &newpath);
    if (newpath != path) {
      retval = _real_lxstat(vers, newpath, buf);
    }
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

extern "C" int __lxstat64(int vers, const char *path, struct stat64 *buf)
{
  char tmpbuf [ PATH_MAX ] = {0} ;
  char *newpath = tmpbuf;
  DMTCP_PLUGIN_DISABLE_CKPT();
  // See filewrapper.cpp:__xstat() for comments on this code.
  int retval = _real_lxstat64(vers, path, buf);
  if (retval == -1 && errno == EFAULT) {
    // We're done.  Return.
  } else {
    updateStatPath(path, &newpath);
    if (newpath != path) {
      retval = _real_lxstat64(vers, newpath, buf);
    }
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

//FIXME: Add wrapper for readlinkat
// NOTE:  If you see a compiler error: "declaration of C function ... conflicts
//   with ... unistd.h", then consider changing ssize_t to int
//   A user has reported this was needed for Linux SLES10.
extern "C" ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
  char tmpbuf [ PATH_MAX ] = {0} ;
  char *newpath = tmpbuf;
  DMTCP_PLUGIN_DISABLE_CKPT();
  ssize_t retval;
  if (path != NULL && strcmp(path, "/proc/self/exe") == 0) {
    const char *procSelfExe = dmtcp_get_executable_path();
    strncpy(buf, procSelfExe, bufsiz);
    retval = bufsiz > strlen(procSelfExe) ? strlen(procSelfExe) : bufsiz;
  } else {
    updateStatPath(path, &newpath);
    retval = _real_readlink(newpath, buf, bufsiz);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

extern "C" ssize_t __readlink_chk(const char *path, char *buf,
                                  size_t bufsiz, size_t buflen)
{
  return readlink(path, buf, bufsiz);
}

extern "C" char *realpath(const char *path, char *resolved_path)
{
  char *ret;
  if (Util::strStartsWith(path, "/dev/pts")) {
    JASSERT(strlen(path) < PATH_MAX);
    if (resolved_path == NULL) {
      ret = (char*) malloc(strlen(path) + 1);
    } else {
      ret = resolved_path;
    }
    strcpy(ret, path);
  } else {
    ret = _real_realpath(path, resolved_path);
  }
  return ret;
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

extern "C" int access(const char *path, int mode)
{
  if (Util::strStartsWith(path, "/dev/pts")) {
    char currPtsDevName[32];
    DMTCP_PLUGIN_DISABLE_CKPT();
    SharedData::getRealPtyName(path, currPtsDevName, sizeof(currPtsDevName));
    int ret = _real_access(currPtsDevName, mode);
    DMTCP_PLUGIN_ENABLE_CKPT();
    return ret;
  }
  return _real_access(path, mode);
}

static int ptsname_r_work(int fd, char * buf, size_t buflen)
{
  JTRACE("Calling ptsname_r");

  Connection* c = FileConnList::instance().getConnection(fd);
  PtyConnection* ptyCon =(PtyConnection*) c;

  string virtPtsName = ptyCon->virtPtsName();

  JTRACE("ptsname_r") (virtPtsName);

  if (virtPtsName.length() >= buflen)
  {
    JWARNING(false) (virtPtsName) (virtPtsName.length()) (buflen)
      .Text("fake ptsname() too long for user buffer");
    errno = ERANGE;
    return -1;
  }

  strcpy(buf, virtPtsName.c_str());

  return 0;
}

extern "C" int ptsname_r(int fd, char * buf, size_t buflen)
{
  DMTCP_PLUGIN_DISABLE_CKPT();

  int retVal = ptsname_r_work(fd, buf, buflen);

  DMTCP_PLUGIN_ENABLE_CKPT();

  return retVal;
}

extern "C" char *ptsname(int fd)
{
  /* No need to acquire Wrapper Protection lock since it will be done in ptsname_r */
  JTRACE("ptsname() promoted to ptsname_r()");
  static char tmpbuf[PATH_MAX];

  if (ptsname_r(fd, tmpbuf, sizeof(tmpbuf)) != 0)
  {
    return NULL;
  }

  return tmpbuf;
}

extern "C" int __ptsname_r_chk(int fd, char * buf, size_t buflen, size_t nreal)
{
  DMTCP_PLUGIN_DISABLE_CKPT();

  JASSERT(buflen <= nreal) (buflen) (nreal) .Text("Buffer Overflow detected!");

  int retVal = ptsname_r_work(fd, buf, buflen);

  DMTCP_PLUGIN_ENABLE_CKPT();

  return retVal;
}

extern "C" int ttyname_r(int fd, char *buf, size_t buflen)
{
  char tmpbuf[64];
  DMTCP_PLUGIN_DISABLE_CKPT();
  int ret = _real_ttyname_r(fd, tmpbuf, sizeof(tmpbuf));

  if (ret == 0 && strcmp(tmpbuf, "/dev/tty") != 0) {
    Connection* c = FileConnList::instance().getConnection(fd);
    JASSERT(c != NULL) (fd) (tmpbuf);
    PtyConnection* ptyCon =(PtyConnection*) c;
    string virtPtsName = ptyCon->virtPtsName();

    if (virtPtsName.length() >= buflen) {
      JWARNING(false) (virtPtsName) (virtPtsName.length()) (buflen)
        .Text("fake ptsname() too long for user buffer");
      errno = ERANGE;
      ret = -1;
    } else {
      strncpy(buf, virtPtsName.c_str(), buflen);
    }
  }
  DMTCP_PLUGIN_ENABLE_CKPT();

  return ret;
}

extern "C" char *ttyname(int fd)
{
  static char tmpbuf[64];

  if (ttyname_r(fd, tmpbuf, sizeof(tmpbuf)) != 0) {
    return NULL;
  }
  return tmpbuf;
}

extern "C" int __ttyname_r_chk(int fd, char *buf, size_t buflen, size_t nreal)
{
  return ttyname_r(fd, buf, buflen);
}

extern "C" int getpt()
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int fd = _real_getpt();
  if (fd >= 0 && dmtcp_is_running_state()) {
    FileConnList::instance().processFileConnection(fd, "/dev/ptmx",
                                                     O_RDWR | O_NOCTTY, -1);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return fd;
}

extern "C" int posix_openpt(int flags)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int fd = _real_posix_openpt(flags);
  if (fd >= 0 && dmtcp_is_running_state()) {
    FileConnList::instance().processFileConnection(fd, "/dev/ptmx",
                                                     flags, -1);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return fd;
}
