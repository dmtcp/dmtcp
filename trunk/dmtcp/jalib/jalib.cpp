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

#include <sys/socket.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdarg.h>
#include <dlfcn.h>

#include <fstream>
#include "jalib.h"

jalib::JalibFuncPtrs jalib::jalibFuncPtrs;
int jalib::jalib_funcptrs_initialized = 0;
int jalib::stderrFd = -1;
int jalib::logFd = -1;
int jalib::dmtcp_fail_rc = -1;

extern "C" void jalib_init(jalib::JalibFuncPtrs jalibFuncPtrs,
                      int stderrFd, int jassertLogFd, int dmtcp_fail_rc)
{
  jalib::jalibFuncPtrs = jalibFuncPtrs;
  jalib::stderrFd = stderrFd;
  jalib::logFd = jassertLogFd;
  jalib::jalib_funcptrs_initialized = 1;
  jalib::dmtcp_fail_rc = dmtcp_fail_rc;
}

#define REAL_FUNC_PASSTHROUGH(type,name) \
  if (!jalib_funcptrs_initialized) { \
    jalibFuncPtrs.name = ::name; \
  } \
  return (*jalibFuncPtrs.name)

static const char *dmtcp_get_tmpdir() {
  fprintf(stderr, "DMTCP: Internal Error: Not Implemented\n");
  abort();
  return "/tmp";
}

static const char *dmtcp_get_uniquepid_str() {
  fprintf(stderr, "DMTCP: Internal Error: Not Implemented\n");
  abort();
  return "DUMMY_UNIQUE_PID";
}

ssize_t writeAll(int fd, const void *buf, size_t count) {
  fprintf(stderr, "DMTCP: Internal Error: Not Implemented\n");
  abort();
  return write(fd, buf, count);
}
ssize_t readAll(int fd, void *buf, size_t count) {
  fprintf(stderr, "DMTCP: Internal Error: Not Implemented\n");
  abort();
  return read(fd, buf, count);
}


namespace jalib {

  const char* dmtcp_get_tmpdir() {
    REAL_FUNC_PASSTHROUGH(const char *, dmtcp_get_tmpdir) ();
  }

  const char* dmtcp_get_uniquepid_str() {
    REAL_FUNC_PASSTHROUGH(const char *, dmtcp_get_uniquepid_str) ();
  }

  int open(const char *pathname, int flags, ...) {
    mode_t mode = 0;
    // Handling the variable number of arguments
    if (flags & O_CREAT) {
      va_list arg;
      va_start (arg, flags);
      mode = va_arg (arg, int);
      va_end (arg);
    }
    REAL_FUNC_PASSTHROUGH(int, open) (pathname, flags, mode);
  }

  FILE* fopen(const char *path, const char *mode) {
    REAL_FUNC_PASSTHROUGH(FILE*, fopen) (path, mode);
  }

  int close(int fd) {
    REAL_FUNC_PASSTHROUGH(int, close) (fd);
  }

  int fclose(FILE *fp) {
    REAL_FUNC_PASSTHROUGH(int, fclose) (fp);
  }

  long int syscall(long int sys_num, ...) {
    int i;
    void * arg[7];
    va_list ap;

    va_start(ap, sys_num);
    for (i = 0; i < 7; i++)
      arg[i] = va_arg(ap, void *);
    va_end(ap);

    // /usr/include/unistd.h says syscall returns long int (contrary to man
    // page)
    REAL_FUNC_PASSTHROUGH(long int, syscall) (sys_num, arg[0], arg[1], arg[2],
                                              arg[3], arg[4], arg[5], arg[6]);
  }

  void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    REAL_FUNC_PASSTHROUGH(void*, mmap) (addr, length, prot, flags, fd, offset);
  }

  int munmap(void *addr, size_t length) {
    REAL_FUNC_PASSTHROUGH(int, munmap) (addr, length);
  }

  ssize_t read(int fd, void *buf, size_t count) {
    REAL_FUNC_PASSTHROUGH(ssize_t, read) (fd, buf, count);
  }

  ssize_t write(int fd, const void *buf, size_t count) {
    REAL_FUNC_PASSTHROUGH(ssize_t, write) (fd, buf, count);
  }

  int select(int nfds, fd_set *readfds, fd_set *writefds,
             fd_set *exceptfds, struct timeval *timeout) {
    REAL_FUNC_PASSTHROUGH(int, select) (nfds, readfds, writefds, exceptfds,
                                        timeout);
  }

  int socket(int domain, int type, int protocol) {
    REAL_FUNC_PASSTHROUGH(int, socket) (domain, type, protocol);
  }

  int connect(int sockfd, const struct sockaddr *serv_addr, socklen_t addrlen) {
    REAL_FUNC_PASSTHROUGH(int, connect) (sockfd, serv_addr, addrlen);
  }

  int bind(int sockfd, const struct sockaddr *my_addr, socklen_t addrlen) {
    REAL_FUNC_PASSTHROUGH(int, bind) (sockfd, my_addr, addrlen);
  }

  int listen(int sockfd, int backlog) {
    REAL_FUNC_PASSTHROUGH(int, listen) (sockfd, backlog);
  }

  int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    REAL_FUNC_PASSTHROUGH(int, accept) (sockfd, addr, addrlen);
  }

  int pthread_mutex_lock(pthread_mutex_t *mutex) {
    REAL_FUNC_PASSTHROUGH(int, pthread_mutex_lock) (mutex);
  }

  int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    REAL_FUNC_PASSTHROUGH(int, pthread_mutex_trylock) (mutex);
  }

  int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    REAL_FUNC_PASSTHROUGH(int, pthread_mutex_unlock) (mutex);
  }

  ssize_t writeAll(int fd, const void *buf, size_t count) {
    REAL_FUNC_PASSTHROUGH(ssize_t, writeAll) (fd, buf, count);
  }

  ssize_t readAll(int fd, void *buf, size_t count) {
    REAL_FUNC_PASSTHROUGH(ssize_t, readAll) (fd, buf, count);
  }

  bool strEndsWith(const char *str, const char *pattern)
  {
    if (str != NULL && pattern != NULL) {
      int len1 = strlen(str);
      int len2 = strlen(pattern);
      if (len1 >= len2) {
        size_t idx = len1 - len2;
        return strncmp(str+idx, pattern, len2) == 0;
      }
    }
    return false;
  }
}

