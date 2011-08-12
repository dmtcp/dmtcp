/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef FRED_SYSCALLWRAPPERS_H
#define FRED_SYSCALLWRAPPERS_H

#include "syscallwrappers.h"

int _almost_real_close ( int fd );
int _almost_real_fclose(FILE *fp);
int _almost_real_open(const char *path, int flags, mode_t mode);
int _almost_real_open64(const char *path, int flags, mode_t mode);
FILE *_almost_real_fopen(const char *path, const char *mode);
FILE *_almost_real_fopen64(const char *path, const char *mode);
int _almost_real_xstat(int vers, const char *path, struct stat *buf);
int _almost_real_xstat64(int vers, const char *path, struct stat64 *buf);
int _almost_real_fxstat(int vers, int fd, struct stat *buf);
int _almost_real_fxstat64(int vers, int fd, struct stat64 *buf);
int _almost_real_lxstat(int vers, const char *path, struct stat *buf);
int _almost_real_lxstat64(int vers, const char *path, struct stat64 *buf);
READLINK_RET_TYPE _almost_real_readlink(const char *path, char *buf,
                                        size_t bufsiz);
int _almost_real_ioctl(int d,  unsigned long int request, ...);
int _almost_real_socket(int domain, int type, int protocol);
int _almost_real_connect(int sockfd, const struct sockaddr *serv_addr,
                         socklen_t addrlen);
int _almost_real_bind (int sockfd, const struct sockaddr *my_addr,
                       socklen_t addrlen);
int _almost_real_listen ( int sockfd, int backlog );
int _almost_real_accept(int sockfd, struct sockaddr *addr,
                        socklen_t *addrlen);
int _almost_real_accept4 ( int sockfd, struct sockaddr *addr,
                           socklen_t *addrlen, int flags );
int _almost_real_setsockopt(int sockfd, int level, int optname,
                            const void *optval, socklen_t optlen);
int _almost_real_getsockopt(int sockfd, int level, int optname,
                                   void *optval, socklen_t *optlen);
sighandler_t _almost_real_signal(int signum, sighandler_t handler);
int _almost_real_sigaction(int signum, const struct sigaction *act,
                           struct sigaction *oldact);
int _almost_real_sigwait(const sigset_t *set, int *sig);
void *_almost_real_calloc(size_t nmemb, size_t size);
void *_almost_real_malloc(size_t size);
void *_almost_real_libc_memalign(size_t boundary, size_t size);
void *_almost_real_valloc(size_t size);
void _almost_real_free(void *ptr);
void *_almost_real_realloc(void *ptr, size_t size);
void *_almost_real_mmap(void *addr, size_t length, int prot, int flags,
                        int fd, off_t offset);
void *_almost_real_mmap64 (void *addr, size_t length, int prot, int flags,
                         int fd, off64_t offset);
int _almost_real_munmap(void *addr, size_t length);

# if __GLIBC_PREREQ (2,4)
void *_almost_real_mremap(void *old_address, size_t old_size,
                          size_t new_size, int flags, ...);
#else
void *_almost_real_mremap(void *old_address, size_t old_size,
                          size_t new_size, int flags);
#endif

int _almost_real_epoll_create(int size);
int _almost_real_epoll_create1(int flags);
int _almost_real_pthread_join (pthread_t thread, void **value_ptr);
int _almost_real_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                void *(*start_routine)(void*), void *arg);
#endif
