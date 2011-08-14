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

#ifndef JALIB_H
#define JALIB_H

#include <sys/types.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <fstream>

namespace jalib {
  typedef struct JalibFuncPtrs {
    const char* (*dmtcp_get_tmpdir)();
    const char* (*dmtcp_get_uniquepid_str)();

    int   (*open)(const char *pathname, int flags, ...);
    FILE* (*fopen)(const char *path, const char *mode);
    int   (*close)(int fd);
    int   (*fclose)(FILE *fp);

    long int (*syscall)(long int sys_num, ...);

    ssize_t (*read)(int fd, void *buf, size_t count);
    ssize_t (*write)(int fd, const void *buf, size_t count);
    int   (*select)(int nfds, fd_set *readfds, fd_set *writefds,
                    fd_set *exceptfds, struct timeval *timeout);

    int   (*socket)(int domain, int type, int protocol);
    int   (*connect)(int sockfd, const struct sockaddr *saddr, socklen_t addrlen);
    int   (*bind)(int sockfd, const struct sockaddr *my_addr, socklen_t addrlen);
    int   (*listen)(int sockfd, int backlog);
    int   (*accept)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
    int   (*pthread_mutex_lock)(pthread_mutex_t *mutex);
    int   (*pthread_mutex_trylock)(pthread_mutex_t *mutex);
    int   (*pthread_mutex_unlock)(pthread_mutex_t *mutex);

    ssize_t (*writeAll)(int fd, const void *buf, size_t count);
    ssize_t (*readAll)(int fd, void *buf, size_t count);
  } JalibFuncPtrs;

  extern JalibFuncPtrs jalibFuncPtrs;
  extern int jalib_funcptrs_initialized;
  extern int stderrFd;
  extern int logFd;

  const char* dmtcp_get_tmpdir();
  const char* dmtcp_get_uniquepid_str();

  int open(const char *pathname, int flags, ...);
  FILE* fopen(const char *path, const char *mode);
  int close(int fd);
  int fclose(FILE *fp);

  long int syscall(long int sys_num, ...);

  ssize_t read(int fd, void *buf, size_t count);
  ssize_t write(int fd, const void *buf, size_t count);
  int select(int nfds, fd_set *readfds, fd_set *writefds,
             fd_set *exceptfds, struct timeval *timeout);

  int socket(int domain, int type, int protocol);
  int connect(int sockfd, const struct sockaddr *serv_addr, socklen_t addrlen);
  int bind(int sockfd, const struct sockaddr *my_addr, socklen_t addrlen);
  int listen(int sockfd, int backlog);
  int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
  int pthread_mutex_lock(pthread_mutex_t *mutex);
  int pthread_mutex_trylock(pthread_mutex_t *mutex);
  int pthread_mutex_unlock(pthread_mutex_t *mutex);

  ssize_t writeAll(int fd, const void *buf, size_t count);
  ssize_t readAll(int fd, void *buf, size_t count);

  bool strEndsWith(const char *str, const char *pattern);
}

extern "C" void jalib_init(jalib::JalibFuncPtrs jalibFuncPtrs,
                           int stderrFd,
                           int jassertLogFd);

#endif
