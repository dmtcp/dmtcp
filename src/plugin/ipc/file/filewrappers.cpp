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
*/
#define ptsname_r ptsname_r_always_inline
#define ttyname_r ttyname_r_always_inline
#define open open_always_inline
#define open64 open64_always_inline
#define openat openat_always_inline
#define openat64 openat64_always_inline
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

#include "dmtcp.h"
#include "shareddata.h"
#include "util.h"
#include "jassert.h"
#include "jconvert.h"
#include "jfilesystem.h"

#include "fileconnlist.h"
#include "fileconnection.h"
#include "filewrappers.h"

#undef ptsname_r
#undef ttyname_r
#undef open
#undef open64
#undef openat
#undef openat64
#undef readlink
#undef __readlink_chk
#undef realpath

using namespace dmtcp;
#if 0
extern "C" int close(int fd)
{
  if (dmtcp_is_protected_fd(fd)) {
    JTRACE("blocked attempt to close protected fd") (fd);
    errno = EBADF;
    return -1;
  }

  DMTCP_PLUGIN_DISABLE_CKPT();
  int rv = _real_close(fd);
  if (rv == 0 && dmtcp_is_running_state()) {
    FileConnList::instance().processClose(fd);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return rv;
}

extern "C" int fclose(FILE *fp)
{
  int fd = fileno(fp);
  if (dmtcp_is_protected_fd(fd)) {
    JTRACE("blocked attempt to fclose protected fd") (fd);
    errno = EBADF;
    return -1;
  }

  DMTCP_PLUGIN_DISABLE_CKPT();
  int rv = _real_fclose(fp);
  if (rv == 0 && dmtcp_is_running_state()) {
    FileConnList::instance().processClose(fd);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();

  return rv;
}

extern "C" int closedir(DIR *dir)
{
  int fd = dirfd(dir);
  if (dmtcp_is_protected_fd(fd)) {
    JTRACE("blocked attempt to closedir protected fd") (fd);
    errno = EBADF;
    return -1;
  }

  DMTCP_PLUGIN_DISABLE_CKPT();
  int rv = _real_closedir(dir);
  if (rv == 0 && dmtcp_is_running_state()) {
    FileConnList::instance().processClose(fd);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();

  return rv;
}

extern "C" int dup(int oldfd)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int newfd = _real_dup(oldfd);
  if (newfd != -1 && dmtcp_is_running_state()) {
    dmtcp::FileConnList::instance().processDup(oldfd, newfd);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return newfd;
}

extern "C" int dup2(int oldfd, int newfd)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int res = _real_dup2(oldfd, newfd);
  if (res != -1 && newfd != oldfd && dmtcp_is_running_state()) {
    dmtcp::FileConnList::instance().processDup(oldfd, newfd);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return newfd;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)) && __GLIBC_PREREQ(2,9)
// dup3 appeared in Linux 2.6.27
extern "C" int dup3(int oldfd, int newfd, int flags)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int res = _real_dup3(oldfd, newfd, flags);
  if (res != -1 && newfd != oldfd && dmtcp_is_running_state()) {
    dmtcp::FileConnList::instance().processDup(oldfd, newfd);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return newfd;
}
#endif
#endif

static int ptsname_r_work(int fd, char * buf, size_t buflen)
{
  JTRACE("Calling ptsname_r");

  dmtcp::Connection* c = dmtcp::FileConnList::instance().getConnection(fd);
  dmtcp::PtyConnection* ptyCon =(dmtcp::PtyConnection*) c;

  dmtcp::string virtPtsName = ptyCon->virtPtsName();

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
    Connection* c = dmtcp::FileConnList::instance().getConnection(fd);
    JASSERT(c != NULL) (fd) (tmpbuf);
    dmtcp::PtyConnection* ptyCon =(dmtcp::PtyConnection*) c;
    dmtcp::string virtPtsName = ptyCon->virtPtsName();

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

extern "C" FILE *tmpfile()
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  FILE *fp = _real_tmpfile();
  if (fp  != NULL && dmtcp_is_running_state()) {
    FileConnList::instance().processFileConnection(fileno(fp), NULL, O_RDWR, 0600);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return fp;
}

extern "C" int mkstemp(char *ttemplate)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int fd = _real_mkstemp(ttemplate);
  if (fd >= 0 && dmtcp_is_running_state()) {
    FileConnList::instance().processFileConnection(fd, NULL, O_RDWR, 0600);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return fd;
}

extern "C" int mkostemp(char *ttemplate, int flags)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int fd = _real_mkostemp(ttemplate, flags);
  if (fd >= 0 && dmtcp_is_running_state()) {
    FileConnList::instance().processFileConnection(fd, NULL, flags, 0600);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return fd;
}

extern "C" int mkstemps(char *ttemplate, int suffixlen)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int fd = _real_mkstemps(ttemplate, suffixlen);
  if (fd >= 0 && dmtcp_is_running_state()) {
    FileConnList::instance().processFileConnection(fd, NULL, O_RDWR, 0600);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return fd;
}

extern "C" int mkostemps(char *ttemplate, int suffixlen, int flags)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int fd = _real_mkostemps(ttemplate, suffixlen, flags);
  if (fd >= 0 && dmtcp_is_running_state()) {
    FileConnList::instance().processFileConnection(fd, NULL, flags, 0600);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return fd;
}

static int _open_open64_work(int(*fn) (const char *path, int flags, ...),
                             const char *path, int flags, mode_t mode)
{
  char currPtsDevName[32];
  const char *newpath = path;

  DMTCP_PLUGIN_DISABLE_CKPT();

  if (dmtcp::Util::strStartsWith(path, VIRT_PTS_PREFIX_STR)) {
    dmtcp::SharedData::getRealPtyName(path, currPtsDevName,
                                      sizeof(currPtsDevName));
    newpath = currPtsDevName;
  }

  int fd =(*fn) (newpath, flags, mode);

  if (fd >= 0 && dmtcp_is_running_state()) {
    FileConnList::instance().processFileConnection(fd, newpath, flags, mode);
  }

  DMTCP_PLUGIN_ENABLE_CKPT();

  return fd;
}

/* Used by open() wrapper to do other tracking of open apart from
   synchronization stuff. */
extern "C" int open(const char *path, int flags, ...)
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

extern "C" int __open_2(const char *path, int flags)
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
extern "C" int open64(const char *path, int flags, ...)
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

extern "C" int __open64_2(const char *path, int flags)
{
  return _open_open64_work(_real_open64, path, flags, 0);
}

extern "C" int creat(const char *path, mode_t mode)
{
  //creat() is equivalent to open() with flags equal to O_CREAT|O_WRONLY|O_TRUNC
  return _open_open64_work(_real_open, path, O_CREAT|O_WRONLY|O_TRUNC, mode);
}

extern "C" int creat64(const char *path, mode_t mode)
{
  //creat() is equivalent to open() with flags equal to O_CREAT|O_WRONLY|O_TRUNC
  return _open_open64_work(_real_open64, path, O_CREAT|O_WRONLY|O_TRUNC, mode);
}

static FILE *_fopen_fopen64_work(FILE*(*fn) (const char *path, const char *mode),
                                 const char *path, const char *mode)
{
  char currPtsDevName[32];
  const char *newpath = path;

  DMTCP_PLUGIN_DISABLE_CKPT();

  if (dmtcp::Util::strStartsWith(path, VIRT_PTS_PREFIX_STR)) {
    dmtcp::SharedData::getRealPtyName(path, currPtsDevName,
                                      sizeof(currPtsDevName));
    newpath = currPtsDevName;
  }

  FILE *file =(*fn) (newpath, mode);

  if (file != NULL && dmtcp_is_running_state()) {
    FileConnList::instance().processFileConnection(fileno(file), newpath,
                                                     -1, -1);
  }

  DMTCP_PLUGIN_ENABLE_CKPT();
  return file;
}

extern "C" FILE *fopen(const char* path, const char* mode)
{
  return _fopen_fopen64_work(_real_fopen, path, mode);
}

extern "C" FILE *fopen64(const char* path, const char* mode)
{
  return _fopen_fopen64_work(_real_fopen64, path, mode);
}

extern "C" FILE *freopen(const char *path, const char *mode, FILE *stream)
{
  char currPtsDevName[32];
  const char *newpath = path;

  DMTCP_PLUGIN_DISABLE_CKPT();

  if (dmtcp::Util::strStartsWith(path, VIRT_PTS_PREFIX_STR)) {
    dmtcp::SharedData::getRealPtyName(path, currPtsDevName,
                                      sizeof(currPtsDevName));
    newpath = currPtsDevName;
  }

  FILE *file = _real_freopen(newpath, mode, stream);

  if (file != NULL && dmtcp_is_running_state()) {
    FileConnList::instance().processFileConnection(fileno(file), newpath,
                                                   -1, -1);
  }

  DMTCP_PLUGIN_ENABLE_CKPT();
  return file;
}

extern "C" int openat(int dirfd, const char *path, int flags, ...)
{
  va_list arg;
  va_start(arg, flags);
  mode_t mode = va_arg(arg, int);
  va_end(arg);
  DMTCP_PLUGIN_DISABLE_CKPT();
  int fd = _real_openat(dirfd, path, flags, mode);
  if (fd >= 0 && dmtcp_is_running_state()) {
    dmtcp::string procpath = "/proc/self/fd/" + jalib::XToString(fd);
    dmtcp::string device = jalib::Filesystem::ResolveSymlink(procpath);
    FileConnList::instance().processFileConnection(fd, device.c_str(),
                                                     flags, mode);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return fd;
}

extern "C" int openat_2(int dirfd, const char *path, int flags)
{
  return openat(dirfd, path, flags, 0);
}

extern "C" int __openat_2(int dirfd, const char *path, int flags)
{
  return openat(dirfd, path, flags, 0);
}

extern "C" int openat64(int dirfd, const char *path, int flags, ...)
{
  va_list arg;
  va_start(arg, flags);
  mode_t mode = va_arg(arg, int);
  va_end(arg);
  DMTCP_PLUGIN_DISABLE_CKPT();
  int fd = _real_openat64(dirfd, path, flags, mode);
  if (fd >= 0 && dmtcp_is_running_state()) {
    dmtcp::string procpath = "/proc/self/fd/" + jalib::XToString(fd);
    dmtcp::string device = jalib::Filesystem::ResolveSymlink(procpath);
    FileConnList::instance().processFileConnection(fd, device.c_str(),
                                                     flags, mode);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return fd;
}

extern "C" int openat64_2(int dirfd, const char *path, int flags)
{
  return openat64(dirfd, path, flags, 0);
}

extern "C" int __openat64_2(int dirfd, const char *path, int flags)
{
  return openat64(dirfd, path, flags, 0);
}

extern "C" DIR *opendir(const char *name)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  DIR *dir = _real_opendir(name);
  if (dir != NULL && dmtcp_is_running_state()) {
    FileConnList::instance().processFileConnection(dirfd(dir), name, -1, -1);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return dir;
}

static void updateStatPath(const char *path, char **newpath)
{
  if (dmtcp::Util::strStartsWith(path, VIRT_PTS_PREFIX_STR)) {
    char currPtsDevName[32];
    dmtcp::SharedData::getRealPtyName(path, currPtsDevName,
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
  updateStatPath(path, &newpath);
  int retval = _real_xstat(vers, newpath, buf);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

extern "C" int __xstat64(int vers, const char *path, struct stat64 *buf)
{
  char tmpbuf [ PATH_MAX ] = {0};
  char *newpath = tmpbuf;
  DMTCP_PLUGIN_DISABLE_CKPT();
  updateStatPath(path, &newpath);
  int retval = _real_xstat64(vers, newpath, buf);
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
  updateStatPath(path, &newpath);
  int retval = _real_lxstat(vers, newpath, buf);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

extern "C" int __lxstat64(int vers, const char *path, struct stat64 *buf)
{
  char tmpbuf [ PATH_MAX ] = {0} ;
  char *newpath = tmpbuf;
  DMTCP_PLUGIN_DISABLE_CKPT();
  updateStatPath(path, &newpath);
  int retval = _real_lxstat64(vers, newpath, buf);
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

extern "C" int fcntl(int fd, int cmd, ...)
{
  void *arg = NULL;
  va_list ap;
  va_start(ap, cmd);
  arg = va_arg(ap, void *);
  va_end(ap);

  DMTCP_PLUGIN_DISABLE_CKPT();

  int res = _real_fcntl(fd, cmd, arg);
  if (res != -1 &&
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
      (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC) &&
#else
      (cmd == F_DUPFD) &&
#endif
      dmtcp_is_running_state()) {
    dmtcp::FileConnList::instance().processDup(fd, res);
  }

  DMTCP_PLUGIN_ENABLE_CKPT();
  return res;
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

#if 0
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
#endif
