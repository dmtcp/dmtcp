/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel                                 *
 *   jansel@csail.mit.edu                                                   *
 *                                                                          *
 *   This file is part of the JALIB module of DMTCP (DMTCP:dmtcp/jalib).    *
 *                                                                          *
 *  DMTCP:dmtcp/jalib is free software: you can redistribute it and/or      *
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

#include "jalib.h"
#include "jconvert.h"
#include "jfilesystem.h"
#include <algorithm>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#ifdef __aarch64__

// FIXME:  We should use SYS_getdents64, and not SYS_getdents for all arch's.
// SYS_getdents not supported in aarch64.
# undef SYS_getdents
# define SYS_getdents       SYS_getdents64
#endif // ifdef __aarch64__

#define DELETED_FILE_SUFFIX " (deleted)"

namespace
{
// In Red Hat Enterprise Linux Server 5.4 (Linux kernel 2.6.18)
// For tests like dmtcp5, forkexec (tests with child processes),
// the child process may have a name "NAME (deleted)".
// This is arguably a bug in the kernel.
dmtcp::string
_GetProgramExe()
{
  dmtcp::string exe = "/proc/self/exe";

  // NOTE: ResolveSymlink is returning string on stack.  Hopefully
  // C++ dmtcp::string is smart enough to copy it.
  dmtcp::string exeRes = jalib::Filesystem::ResolveSymlink(exe);

  JASSERT(exe != exeRes) (exe).Text("problem with /proc/self/exe");

  // Bug fix for Linux 2.6.19
  if (jalib::strEndsWith(exeRes.c_str(), DELETED_FILE_SUFFIX)) {
    exeRes.erase(exeRes.length() - strlen(DELETED_FILE_SUFFIX));
  }

  return exeRes;
}

// Set buf, and return length read (including all null characters)
int
_GetProgramCmdline(char *buf, int size)
{
  int fd = jalib::open("/proc/self/cmdline", O_RDONLY, 0);
  int rc;

  JASSERT(fd >= 0);

  // rc == 0 means EOF, or else it means buf is full (size chars read)
  rc = jalib::readAll(fd, buf, size);
  jalib::close(fd);
  return rc;
}
}

dmtcp::string
jalib::Filesystem::GetCWD()
{
  dmtcp::string cwd;
  char buf[PATH_MAX];

  JASSERT(getcwd(buf, PATH_MAX) == buf)
  .Text("Pathname too long");
  cwd = buf;
  return cwd;
}

dmtcp::string
jalib::Filesystem::BaseName(const dmtcp::string &str)
{
  size_t len = str.length();

  if (str == "/" || str == "." || str == ".." || str.empty()) {
    return str;
  }

  // Remove trailing slashes
  while (len > 0 && str[len - 1] == '/') {
    len--;
  }

  size_t lastSlash = str.find_last_of('/', len);

  if (lastSlash == dmtcp::string::npos) {
    return str.substr(0, len);
  }

  return str.substr(lastSlash + 1, len - lastSlash);
}

dmtcp::string
jalib::Filesystem::DirName(const dmtcp::string &str)
{
  size_t len = str.length();

  if (str == "/" || str == "." || str.empty()) {
    return str;
  }

  if (str == "..") {
    return ".";
  }

  // Remove trailing slashes
  while (len > 0 && str[len - 1] == '/') {
    len--;
  }

  size_t lastSlash = str.find_last_of('/', len);

  if (lastSlash == dmtcp::string::npos) {
    return ".";
  }

  if (lastSlash == 0) {
    return "/";
  }

  return str.substr(0, lastSlash);
}

int
jalib::Filesystem::mkdir_r(const dmtcp::string &dir, mode_t mode)
{
  struct stat buf;
  int ret = stat(dir.c_str(), &buf);

  JTRACE("Create dir")(dir);

  if (ret && errno != ENOENT) {
    JTRACE("Cannot create directory path")(dir)(errno)(strerror(errno));
    return ret;
  }

  if (ret && errno == ENOENT) {
    dmtcp::string pdir = DirName(dir);
    JTRACE("Create parent dir")(pdir);
    mkdir_r(pdir, mode);
    return mkdir(dir.c_str(), mode);
  }

  JTRACE("Directory already exists")(dir);
  return 0;
}

dmtcp::string
jalib::Filesystem::GetProgramDir()
{
  static dmtcp::string *value = NULL;
  if (value == NULL) {
    // Technically, this is a memory leak, but value is static and so it happens
    // only once.
    value = new dmtcp::string(DirName(GetProgramPath()));
  }
  return *value;
}

dmtcp::string
jalib::Filesystem::GetProgramName()
{
  static dmtcp::string *value = NULL;

  if (value == NULL) {
    size_t len;
    char cmdline[1024];
    value = new dmtcp::string(BaseName ( GetProgramPath() )); // uses /proc/self/exe
    // We may rewrite "a.out" to "/lib/ld-linux.so.2 a.out".  If so, find cmd.
    if (!value->empty()
        && jalib::elfInterpreter() != NULL
        && *value == ResolveSymlink(jalib::elfInterpreter()) // e.g. /lib/ld-linux.so.2
        && (len = _GetProgramCmdline(cmdline, sizeof(cmdline))) > 0
        && len > strlen(cmdline) + 1 // more than one word in cmdline
        && *(cmdline + strlen(cmdline) + 1) != '-') { // second word not a flag
      *value = BaseName(cmdline + strlen(cmdline) + 1); // find second word
    }
  }
  return *value;
}

dmtcp::string
jalib::Filesystem::GetProgramPath()
{
  static dmtcp::string *value = NULL;
  if (value == NULL) {
    // Technically, this is a memory leak, but value is static and so it happens
    // only once.
    value = new dmtcp::string(_GetProgramExe());
  }
  return *value;
}

// NOTE: ResolveSymlink returns a string, buf, allocated on the stack.
// While this is dangerous, it avoids the use of malloc or 'new'.
// Finish using the result before you call a different function.
dmtcp::string
jalib::Filesystem::ResolveSymlink(const dmtcp::string &path)
{
  struct stat statBuf;

  // If path is not a symbolic link, just return it.
  if (lstat(path.c_str(), &statBuf) == 0
      && !S_ISLNK(statBuf.st_mode)) {
    return path;
  }
  char buf[PATH_MAX];  // This could be passed on via call to readlink()
  memset(buf, 0, sizeof buf);
  int len = readlink(path.c_str(), buf, sizeof(buf) - 1);
  if (len <= 0) {
    return "";
  } else if (len > 0 && buf[0] != '/' &&
             path.find("/proc/") != 0) {
    // Handle links of type "file -> dir/file2"
    dmtcp::string res = DirName(path).append("/").append(buf);
    return res;
  }
  return buf;
}

dmtcp::string
jalib::Filesystem::GetDeviceName(int fd)
{
  return ResolveSymlink("/proc/self/fd/" + jalib::XToString(fd));
}

bool
jalib::Filesystem::FileExists(const dmtcp::string &str)
{
  struct stat st;

  if (!stat(str.c_str(), &st)) {
    return true;
  } else {
    return false;
  }
}

dmtcp::vector<dmtcp::string>
jalib::Filesystem::GetProgramArgs()
{
  static dmtcp::vector<dmtcp::string> *rv = NULL;
  if (rv == NULL) {
    // Technically, this is a memory leak, but rv is static and so it happens
    // only once.
    rv = new dmtcp::vector<dmtcp::string>();
  }

  if (rv->empty()) {
    dmtcp::string path = "/proc/self/cmdline";

    // FIXME: Replace fopen with open.
    FILE *args = jalib::fopen(path.c_str(), "r");

    JASSERT(args != NULL) (path).Text("failed to open command line");

    size_t len = 4095;

    // getdelim will auto-grow this buffer using realloc which would fail with
    // bad pointer.
    // We should replace getdelim with our own version
    char *lineptr = (char *)JALLOC_HELPER_MALLOC(len + 1);
    while (getdelim(&lineptr, &len, '\0', args) >= 0) {
      rv->push_back(lineptr);
    }

    JALLOC_HELPER_FREE(lineptr);
    jalib::fclose(args);
  }

  return *rv;
}

dmtcp::vector<int>
jalib::Filesystem::ListOpenFds()
{
  int fd = jalib::open("/proc/self/fd", O_RDONLY | O_NDELAY |
                       O_LARGEFILE | O_DIRECTORY, 0);

  JASSERT(fd >= 0);

  const size_t allocation = (4 * BUFSIZ < sizeof(struct dirent64)
                             ? sizeof(struct dirent64) : 4 * BUFSIZ);
  char *buf = (char *)JALLOC_HELPER_MALLOC(allocation);

  dmtcp::vector<int> fdVec;

  while (true) {
    int nread = jalib::syscall(SYS_getdents, fd, buf, allocation);
    if (nread == 0) {
      break;
    }
    JASSERT(nread > 0);
    for (int pos = 0; pos < nread;) {
      struct linux_dirent *d = (struct linux_dirent *)(&buf[pos]);
      if (d->d_ino > 0) {
        char *ch;
        int fdnum = strtol(d->d_name, &ch, 10);
        if (*ch == 0 && fdnum >= 0 && fdnum != fd) {
          fdVec.push_back(fdnum);
        }
      }
      pos += d->d_reclen;
    }
  }

  jalib::close(fd);

  std::sort(fdVec.begin(), fdVec.end());
  JALLOC_HELPER_FREE(buf);
  return fdVec;
}

dmtcp::string
jalib::Filesystem::GetCurrentHostname()
{
  struct utsname tmp;

  memset(&tmp, 0, sizeof(tmp));
  JASSERT(uname(&tmp) != -1) (JASSERT_ERRNO);
  dmtcp::string name = "unknown";
  if (strlen(tmp.nodename) != 0) {
    name = tmp.nodename;
  }

  // #ifdef _GNU_SOURCE
  // if(tmp.domainname != 0)
  // name += dmtcp::string(".") + tmp.domainname;
  // #endif
  return name;
}

dmtcp::string
jalib::Filesystem::GetControllingTerm(pid_t pid /* = -1*/)
{
  char sbuf[1024];
  char ttyName[64];
  char procPath[64];
  char *tmp;
  char *S;
  char state;
  int ppid, pgrp, session, tty, tpgid;

  int fd, num_read;

  if (pid == -1) {
    strcpy(procPath, "/proc/self/stat");
  } else {
    sprintf(procPath, "/proc/%d/stat", pid);
  }
  fd = jalib::open(procPath, O_RDONLY, 0);

  JASSERT(fd >= 0) (strerror(errno))
  .Text("Unable to open /proc/self/stat\n");

  num_read = read(fd, sbuf, sizeof sbuf - 1);
  close(fd);
  if (num_read <= 0) {
    return NULL;
  }
  sbuf[num_read] = '\0';

  S = strchr(sbuf, '(') + 1;
  tmp = strrchr(S, ')');
  S = tmp + 2;                 // skip ") "

  sscanf(S,
         "%c "
         "%d %d %d %d %d ",
         &state,
         &ppid, &pgrp, &session, &tty, &tpgid
         );

  int maj = ((unsigned)(tty) >> 8u) & 0xfffu;
  int min = ((unsigned)(tty) & 0xffu) |
    (((unsigned)(tty) & 0xfff00000u) >> 12u);

  /* /dev/pts/ * has major numbers in the range 136 - 143 */
  if (maj >= 136 && maj <= 143) {
    sprintf(ttyName, "/dev/pts/%d", min + (maj - 136) * 256);
  } else {
    ttyName[0] = '\0';
  }

  return ttyName;
}
