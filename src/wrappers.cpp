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

#include <fcntl.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#undef open
#undef open64
#undef openat
#undef openat64

#include "dmtcp.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "pluginmanager.h"
#include "threadsync.h"
#include "util.h"
#include "syscallwrappers.h"


namespace dmtcp
{

static bool
isValidAddress(const char *path)
{
  struct stat buf;
  int retval = _real___xstat(0, path, &buf);
  if (retval == -1 && errno == EFAULT) {
    return false;
  }

  return true;
}

static void
processOpenFd(int fd, const char *path, int flags, mode_t mode)
{
  if (!dmtcp_is_running_state()) {
    return;
  }

  DmtcpEventData_t data;
  data.openFd.fd = fd;
  data.openFd.path = path;
  data.openFd.flags = flags;
  data.openFd.mode = mode;

  PluginManager::eventHook(DMTCP_EVENT_OPEN_FD, &data);
}

static void
processReopenFd(int fd, const char *path, int flags)
{
  if (!dmtcp_is_running_state()) {
    return;
  }

  DmtcpEventData_t data;
  data.reopenFd.fd = fd;
  data.reopenFd.path = path;
  data.reopenFd.flags = flags;

  PluginManager::eventHook(DMTCP_EVENT_REOPEN_FD, &data);
}

static void
processCloseFd(int fd)
{
  if (!dmtcp_is_running_state()) {
    return;
  }

  DmtcpEventData_t data;
  data.closeFd.fd = fd;

  PluginManager::eventHook(DMTCP_EVENT_CLOSE_FD, &data);
}

static void
processDupFd(int oldFd, int newFd)
{
  if (!dmtcp_is_running_state()) {
    return;
  }

  DmtcpEventData_t data;
  data.dupFd.oldFd = oldFd;
  data.dupFd.newFd = newFd;

  PluginManager::eventHook(DMTCP_EVENT_DUP_FD, &data);
}

static const char*
virtualToRealPath(const char *virtualPath, char *realPath)
{
  // We want to first validate path to make sure it's in our address space.
  // We do this using a preliminary call to _real_xstat(). If path or buf is
  // invalid, return with calling translation functions. Otherwise we proceed to
  // translate the path.

  if (!isValidAddress(virtualPath)) {
    return virtualPath;
  }

  strncpy(realPath, virtualPath, PATH_MAX);
  realPath[PATH_MAX - 1] = 0;

  DmtcpEventData_t data;
  data.virtualToRealPath.path = realPath;

  PluginManager::eventHook(DMTCP_EVENT_VIRTUAL_TO_REAL_PATH, &data);

  return realPath;
}

extern "C" char *
realToVirtualPath(char *path)
{
  // No need to validate valid address for path. The address returned by the
  // underlying syscall is valid on a successful return.

  DmtcpEventData_t data;
  data.realToVirtualPath.path = path;

  PluginManager::eventHook(DMTCP_EVENT_REAL_TO_VIRTUAL_PATH, &data);

  return path;
}

static int
dmtcp_openat(int dirfd, const char *path, int flags, mode_t mode)
{
  WrapperLock wrapperLock;

  char realPath[PATH_MAX] = { 0 };

  int fd = _real_openat(dirfd, virtualToRealPath(path, realPath), flags, mode);

  if (fd != -1) {
    processOpenFd(fd, path, flags, mode);
  }

  return fd;
}

extern "C" int
open(const char *path, int flags, ...)
{
  mode_t mode = 0;

  if (flags & O_CREAT) {
    va_list arg;
    va_start(arg, flags);
    mode = va_arg(arg, int);
    va_end(arg);
  }

  return dmtcp_openat(AT_FDCWD, path, flags, mode);
}


extern "C" int
__open_2(const char *path, int flags)
{
  return dmtcp_openat(AT_FDCWD, path, flags, 0);
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

  return dmtcp_openat(AT_FDCWD, path, flags | O_LARGEFILE, mode);
}


extern "C" int
__open64_2(const char *path, int flags)
{
  return dmtcp_openat(AT_FDCWD, path, flags | O_LARGEFILE, 0);
}


extern "C" FILE *
fopen(const char *path, const char *mode)
{
  WrapperLock wrapperLock;

  char realPath[PATH_MAX] = { 0 };

  FILE *fp = _real_fopen(virtualToRealPath(path, realPath), mode);

  if (fp != NULL) {
    processOpenFd(fileno(fp), path, -1, -1);
  }

  return fp;
}


extern "C" FILE *
fopen64(const char *path, const char *mode)
{
  WrapperLock wrapperLock;

  char realPath[PATH_MAX] = { 0 };

  FILE *fp = _real_fopen64(virtualToRealPath(path, realPath), mode);

  if (fp != NULL) {
    processOpenFd(fileno(fp), path, -1, -1);
  }

  return fp;
}


extern "C" FILE *
freopen(const char *path, const char *mode, FILE * stream)
{
  WrapperLock wrapperLock;

  char realPath[PATH_MAX] = { 0 };

  FILE *fp = _real_freopen(virtualToRealPath(path, realPath), mode, stream);

  if (fp != NULL) {
    processReopenFd(fileno(fp), path, -1);
  }

  return fp;
}


extern "C" FILE *
freopen64(const char *path, const char *mode, FILE * stream)
{
  WrapperLock wrapperLock;

  char realPath[PATH_MAX] = { 0 };

  FILE *fp = _real_freopen64(virtualToRealPath(path, realPath), mode, stream);

  if (fp != NULL) {
    processReopenFd(fileno(fp), path, -1);
  }

  return fp;
}


extern "C" int
openat(int dirfd, const char *path, int flags, ...)
{
  va_list arg;

  va_start(arg, flags);
  mode_t mode = va_arg(arg, int);
  va_end(arg);

  return dmtcp_openat(dirfd, path, flags, mode);
}


extern "C" int
openat_2(int dirfd, const char *path, int flags)
{
  return dmtcp_openat(dirfd, path, flags, 0);
}


extern "C" int
__openat_2(int dirfd, const char *path, int flags)
{
  return dmtcp_openat(dirfd, path, flags, 0);
}


extern "C" int
openat64(int dirfd, const char *path, int flags, ...)
{
  va_list arg;

  va_start(arg, flags);
  mode_t mode = va_arg(arg, int);
  va_end(arg);

  return dmtcp_openat(dirfd, path, flags | O_LARGEFILE, mode);
}


extern "C" int
openat64_2(int dirfd, const char *path, int flags)
{
  return dmtcp_openat(dirfd, path, flags | O_LARGEFILE, 0);
}


extern "C" int
__openat64_2(int dirfd, const char *path, int flags)
{
  return dmtcp_openat(dirfd, path, flags | O_LARGEFILE, 0);
}


extern "C" int
creat(const char *path, mode_t mode)
{
  // creat() is equivalent to open() with flags equal to
  // O_CREAT|O_WRONLY|O_TRUNC
  int flags = O_CREAT | O_WRONLY | O_TRUNC;

  return dmtcp_openat(AT_FDCWD, path, flags, mode);
}


extern "C" int
creat64(const char *path, mode_t mode)
{
  int flags = O_CREAT | O_WRONLY | O_TRUNC;
  return dmtcp_openat(AT_FDCWD, path, flags | O_LARGEFILE, mode);
}

extern "C" int
close(int fd)
{
  WrapperLock wrapperLock;

  if (dmtcp_is_protected_fd(fd)) {
    JTRACE("blocked attempt to close protected fd") (fd);
    errno = EBADF;
    return -1;
  }

  int ret = _real_close(fd);
  if (ret != -1) {
    processCloseFd(fd);
  }

  return ret;
}

extern "C" int
fclose(FILE *fp)
{
  WrapperLock wrapperLock;

  int fd = fileno(fp);

  if (dmtcp_is_protected_fd(fd)) {
    JTRACE("blocked attempt to fclose protected fd") (fd);
    errno = EBADF;
    return -1;
  }

  int ret = _real_fclose(fp);
  if (ret != -1) {
    processCloseFd(fd);
  }

  return ret;
}

extern "C" int
closedir(DIR *dir)
{
  WrapperLock wrapperLock;

  int fd = dirfd(dir);

  if (dmtcp_is_protected_fd(fd)) {
    JTRACE("blocked attempt to closedir protected fd") (fd);
    errno = EBADF;
    return -1;
  }

  int ret = _real_closedir(dir);
  if (ret != -1) {
    processCloseFd(fd);
  }

  return ret;
}

extern "C" int
dup(int oldfd)
{
  WrapperLock wrapperLock;

  int ret = _real_dup(oldfd);
  if (ret != -1) {
    processDupFd(oldfd, ret);
  }

  return ret;
}

extern "C" int
dup2(int oldfd, int newfd)
{
  WrapperLock wrapperLock;

  int ret = _real_dup2(oldfd, newfd);
  if (ret != -1) {
    processDupFd(oldfd, ret);
  }

  return ret;
}

// dup3 appeared in Linux 2.6.27
extern "C" int
dup3(int oldfd, int newfd, int flags)
{
  WrapperLock wrapperLock;

  int ret = _real_dup3(oldfd, newfd, flags);
  if (ret != -1) {
    processDupFd(oldfd, ret);
  }

  return ret;
}

#ifndef F_DUPFD_CLOEXEC
# define F_DUPFD_CLOEXEC 0
#endif


extern "C" int
fcntl(int fd, int cmd, ...)
{
  WrapperLock wrapperLock;

  void *arg = NULL;
  va_list ap;

  va_start(ap, cmd);
  arg = va_arg(ap, void *);
  va_end(ap);

  int res = _real_fcntl(fd, cmd, arg);

  if (res != -1 && (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC)) {
    processDupFd(fd, res);
  }

  return res;
}

extern "C" FILE *
tmpfile()
{
  WrapperLock wrapperLock;
  return _real_tmpfile();

  FILE *fp = _real_tmpfile();
  if (fp != NULL) {
    string device = jalib::Filesystem::GetDeviceName(fileno(fp));
    processOpenFd(fileno(fp), device.c_str(), O_RDWR, 0600);
  }

  return fp;
}

extern "C" int
mkstemp(char *ttemplate)
{
  WrapperLock wrapperLock;

  int fd = _real_mkostemps(ttemplate, 0, 0);

  if (fd != -1) {
    string device = jalib::Filesystem::GetDeviceName(fd);
    processOpenFd(fd, device.c_str(), O_RDWR, 0600);
  }

  return fd;
}

extern "C" int
mkostemp(char *ttemplate, int flags)
{
  WrapperLock wrapperLock;

  int fd = _real_mkostemps(ttemplate, 0, flags);
  if (fd != -1) {
    string device = jalib::Filesystem::GetDeviceName(fd);
    processOpenFd(fd, device.c_str(), flags, 0600);
  }

  return fd;
}

extern "C" int
mkstemps(char *ttemplate, int suffixlen)
{
  WrapperLock wrapperLock;

  int fd = _real_mkostemps(ttemplate, suffixlen, 0);
  if (fd != -1) {
    string device = jalib::Filesystem::GetDeviceName(fd);
    processOpenFd(fd, device.c_str(), O_RDWR, 0600);
  }

  return fd;
}

extern "C" int
mkostemps(char *ttemplate, int suffixlen, int flags)
{
  WrapperLock wrapperLock;

  int fd = _real_mkostemps(ttemplate, suffixlen, flags);
  if (fd != -1) {
    string device = jalib::Filesystem::GetDeviceName(fd);
    processOpenFd(fd, device.c_str(), flags, 0600);
  }

  return fd;
}

extern "C" DIR * opendir(const char *name)
{
  WrapperLock wrapperLock;
  char realPath[PATH_MAX] = {0};

  DIR *d = _real_opendir(virtualToRealPath(name, realPath));

  if (d != NULL) {
    processOpenFd(dirfd(d), name, O_RDWR, 0);
  }

  return d;
}

//
// PTY Wrappers
//

extern "C" int
__xstat(int vers, const char *path, struct stat *buf)
{
  WrapperLock wrapperLock;

  char realPath[PATH_MAX] = {0};
  return _real___xstat(vers, virtualToRealPath(path, realPath), buf);
}

extern "C" int
__xstat64(int vers, const char *path, struct stat64 *buf)
{
  WrapperLock wrapperLock;

  char realPath[PATH_MAX] = {0};
  return _real___xstat64(vers, virtualToRealPath(path, realPath), buf);
}

extern "C" int
__lxstat(int vers, const char *path, struct stat *buf)
{
  WrapperLock wrapperLock;

  char realPath[PATH_MAX] = {0};
  return _real___lxstat(vers, virtualToRealPath(path, realPath), buf);
}

extern "C" int
__lxstat64(int vers, const char *path, struct stat64 *buf)
{
  WrapperLock wrapperLock;

  char realPath[PATH_MAX] = {0};
  return _real___lxstat64(vers, virtualToRealPath(path, realPath), buf);
}

static ssize_t
readlink_work(const char *path, char *buf, size_t bufsiz)
{
  WrapperLock wrapperLock;

  char realPath[PATH_MAX] = { 0 };
  char resPath[PATH_MAX] = { 0 };

  ssize_t ret =
    _real_readlink(virtualToRealPath(path, realPath), resPath, sizeof(resPath));
  if (ret == -1) {
    return ret;
  }

  realToVirtualPath(resPath);

  ret = MIN(bufsiz, strlen(resPath));
  strncpy(buf, resPath, ret);

  return ret;
}

extern "C" ssize_t
readlink(const char *path, char *buf, size_t bufsiz)
{
  return readlink_work(path, buf, bufsiz);
}

extern "C" ssize_t
__readlink_chk(const char *path, char *buf, size_t bufsiz, size_t buflen)
{
  return readlink_work(path, buf, bufsiz);
}

static char *
realpath_work(const char *path, char *resolved_path)
{
  WrapperLock wrapperLock;

  char realPath[PATH_MAX] = { 0 };
  char resPath[PATH_MAX] = { 0 };

  char *ret = _real_realpath(virtualToRealPath(path, realPath), resPath);
  if (ret == NULL) {
    return ret;
  }

  realToVirtualPath(resPath);

  if (!resolved_path) {
    // TODO: Replace with libc::malloc.
    resolved_path = (char*) malloc(strlen(resPath) + 1);
  }

  strcpy(resolved_path, resPath);
  return resolved_path;
}

extern "C" char *realpath(const char *path, char *resolved_path)
{
  return realpath_work(path, resolved_path);
}

extern "C" char *__realpath(const char *path, char *resolved_path)
{
  return realpath_work(path, resolved_path);
}

extern "C" char *__realpath_chk(const char *path, char *resolved_path,
                                size_t resolved_len)
{
  return realpath_work(path, resolved_path);
}

extern "C" char *canonicalize_file_name(const char *path)
{
  return realpath_work(path, NULL);
}

extern "C" int
access(const char *path, int mode)
{
  WrapperLock wrapperLock;

  char realPath[PATH_MAX] = { 0 };

  return _real_access(virtualToRealPath(path, realPath), mode);
}

static int
ptsname_r_work(int fd, char *buf, size_t buflen)
{
  WrapperLock wrapperLock;

  char resPath[PATH_MAX] = { 0 };

  int ret = _real_ptsname_r(fd, resPath, sizeof(resPath));

  if (ret == 0) {
    realToVirtualPath(resPath);
    strncpy(buf, resPath, buflen);
  }

  return ret;
}

extern "C" int
ptsname_r(int fd, char *buf, size_t buflen)
{
  return ptsname_r_work(fd, buf, buflen);
}

extern "C" char *ptsname(int fd)
{
  /* No need to acquire Wrapper Protection lock since it will be done in
     ptsname_r */
  JTRACE("ptsname() promoted to ptsname_r()");
  static char path[PATH_MAX];

  if (ptsname_r_work(fd, path, sizeof(path)) != 0) {
    return NULL;
  }

  return path;
}

extern "C" int
__ptsname_r_chk(int fd, char *buf, size_t buflen, size_t nreal)
{
  JASSERT(buflen <= nreal) (buflen) (nreal).Text("Buffer Overflow detected!");

  return ptsname_r_work(fd, buf, buflen);
}

static int
ttyname_r_work(int fd, char *buf, size_t buflen)
{
  WrapperLock wrapperLock;
  char resPath[PATH_MAX] = { 0 };
  int res = _real_ttyname_r(fd, resPath, sizeof(resPath));
  if (res == -1) {
    return res;
  }

  realToVirtualPath(resPath);
  strncpy(buf, resPath, buflen);

  return 0;
}

extern "C" int
ttyname_r(int fd, char *buf, size_t buflen)
{
  return ttyname_r_work(fd, buf, buflen);
}

extern "C" char *ttyname(int fd)
{
  static char resPath[64];

  if (ttyname_r_work(fd, resPath, sizeof(resPath)) != 0) {
    return NULL;
  }

  return resPath;
}

extern "C" int
__ttyname_r_chk(int fd, char *buf, size_t buflen, size_t nreal)
{
  return ttyname_r_work(fd, buf, buflen);
}

extern "C" int
getpt()
{
  WrapperLock wrapperLock;
  return dmtcp_openat(AT_FDCWD, "/dev/ptmx", O_RDWR | O_NOCTTY, 0);
}

extern "C" int
posix_openpt(int flags)
{
  WrapperLock wrapperLock;
  return dmtcp_openat(AT_FDCWD, "/dev/ptmx", flags, 0);
}

}
