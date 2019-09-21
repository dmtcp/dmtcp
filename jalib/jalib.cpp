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

#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "jalib.h"
#include "jassert.h"
#include <fstream>

static JalibFuncPtrs jalibFuncPtrs;
static int jalib_funcptrs_initialized = 0;

static struct {
  const char *elfInterpreter;
  int stderrFd;
  int logFd;
  int dmtcp_fail_rc;
} dmtcpInfo = { NULL, -1, -1, -1 };

extern "C" void initializeJalib();

extern "C" void
jalib_init(JalibFuncPtrs _jalibFuncPtrs,
           const char *elfInterpreter,
           int stderrFd,
           int jassertLogFd,
           int dmtcp_fail_rc)
{
  dmtcpInfo.elfInterpreter = elfInterpreter;
  dmtcpInfo.stderrFd = stderrFd;
  dmtcpInfo.logFd = jassertLogFd;
  dmtcpInfo.dmtcp_fail_rc = dmtcp_fail_rc;

  jalibFuncPtrs = _jalibFuncPtrs;
  jalib_funcptrs_initialized = 1;

  JASSERT_INIT();
}

const char *
jalib::elfInterpreter()
{
  return dmtcpInfo.elfInterpreter;
}

int
jalib::stderrFd()
{
  return dmtcpInfo.stderrFd;
}

int
jalib::logFd()
{
  return dmtcpInfo.logFd;
}

int
jalib::dmtcp_fail_rc()
{
  return dmtcpInfo.dmtcp_fail_rc;
}

#define REAL_FUNC_PASSTHROUGH(type, name) \
  if (!jalib_funcptrs_initialized) {      \
    initializeJalib();                    \
  }                                       \
  return (*jalibFuncPtrs.name)

namespace jalib
{
int
open(const char *pathname, int flags, ...)
{
  mode_t mode = 0;

  // Handling the variable number of arguments
  if (flags & O_CREAT) {
    va_list arg;
    va_start(arg, flags);
    mode = va_arg(arg, int);
    va_end(arg);
  }
  REAL_FUNC_PASSTHROUGH(int, open) (pathname, flags, mode);
}

FILE *
fopen(const char *path, const char *mode)
{
  REAL_FUNC_PASSTHROUGH(FILE *, fopen) (path, mode);
}

int
close(int fd)
{
  REAL_FUNC_PASSTHROUGH(int, close) (fd);
}

int
fclose(FILE *fp)
{
  REAL_FUNC_PASSTHROUGH(int, fclose) (fp);
}

int
dup(int oldfd)
{
  REAL_FUNC_PASSTHROUGH(int, dup) (oldfd);
}

int dup2(int oldfd, int newfd) {
  struct rlimit file_limit;
  getrlimit(RLIMIT_NOFILE, &file_limit);
  JASSERT ( (unsigned int)newfd < file_limit.rlim_cur )
          (newfd)(file_limit.rlim_cur)
          .Text("dup2: newfd is >= current limit on number of files");
  REAL_FUNC_PASSTHROUGH(int, dup2) (oldfd, newfd);
}

ssize_t
readlink(const char *path, char *buf, size_t bufsiz)
{
  REAL_FUNC_PASSTHROUGH(ssize_t, readlink) (path, buf, bufsiz);
}

long
syscall(long sys_num, ...)
{
  int i;
  void *arg[7];
  va_list ap;

  va_start(ap, sys_num);
  for (i = 0; i < 7; i++) {
    arg[i] = va_arg(ap, void *);
  }
  va_end(ap);

  ///usr/include/unistd.h says syscall returns long int (contrary to man
  // page)
  REAL_FUNC_PASSTHROUGH(long, syscall) (sys_num, arg[0], arg[1], arg[2],
                                        arg[3], arg[4], arg[5], arg[6]);
}

int
socket(int domain, int type, int protocol)
{
  REAL_FUNC_PASSTHROUGH(int, socket) (domain, type, protocol);
}

int
connect(int sockfd, const struct sockaddr *serv_addr, socklen_t addrlen)
{
  REAL_FUNC_PASSTHROUGH(int, connect) (sockfd, serv_addr, addrlen);
}

int
bind(int sockfd, const struct sockaddr *my_addr, socklen_t addrlen)
{
  REAL_FUNC_PASSTHROUGH(int, bind) (sockfd, my_addr, addrlen);
}

int
listen(int sockfd, int backlog)
{
  REAL_FUNC_PASSTHROUGH(int, listen) (sockfd, backlog);
}

int
accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
  REAL_FUNC_PASSTHROUGH(int, accept) (sockfd, addr, addrlen);
}

int
setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen)
{
  REAL_FUNC_PASSTHROUGH(int, setsockopt) (s, level, optname, optval, optlen);
}

ssize_t
writeAll(int fd, const void *buf, size_t count)
{
  REAL_FUNC_PASSTHROUGH(ssize_t, writeAll) (fd, buf, count);
}

ssize_t
readAll(int fd, void *buf, size_t count)
{
  REAL_FUNC_PASSTHROUGH(ssize_t, readAll) (fd, buf, count);
}

bool
strEndsWith(const char *str, const char *pattern)
{
  if (str != NULL && pattern != NULL) {
    int len1 = strlen(str);
    int len2 = strlen(pattern);
    if (len1 >= len2) {
      size_t idx = len1 - len2;
      return strncmp(str + idx, pattern, len2) == 0;
    }
  }
  return false;
}
}
