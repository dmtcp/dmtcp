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

#pragma once
#ifndef FILE_WRAPPERS_H
#define FILE_WRAPPERS_H

#include "dmtcp.h"

#define _real_open NEXT_FNC(open)
#define _real_open64 NEXT_FNC(open64)
#define _real_fopen NEXT_FNC(fopen)
#define _real_fopen64 NEXT_FNC(fopen64)
#define _real_freopen NEXT_FNC(freopen)
#define _real_openat NEXT_FNC(openat)
#define _real_openat64 NEXT_FNC(openat64)
#define _real_opendir NEXT_FNC(opendir)
#define _real_tmpfile NEXT_FNC(tmpfile)
#define _real_mkstemp NEXT_FNC(mkstemp)
#define _real_mkostemp NEXT_FNC(mkostemp)
#define _real_mkstemps NEXT_FNC(mkstemps)
#define _real_mkostemps NEXT_FNC(mkostemps)
#define _real_close NEXT_FNC(close)
#define _real_fclose NEXT_FNC(fclose)
#define _real_closedir NEXT_FNC(closedir)
#define _real_lseek NEXT_FNC(lseek)
#define _real_dup NEXT_FNC(dup)
#define _real_dup2 NEXT_FNC(dup2)
#define _real_dup3 NEXT_FNC(dup3)
#define _real_xstat NEXT_FNC(__xstat)
#define _real_xstat64 NEXT_FNC(__xstat64)
#define _real_lxstat NEXT_FNC(__lxstat)
#define _real_lxstat64 NEXT_FNC(__lxstat64)
#define _real_readlink NEXT_FNC(readlink)
#define _real_exit NEXT_FNC(exit)
#define _real_syscall NEXT_FNC(syscall)
#define _real_unsetenv NEXT_FNC(unsetenv)
#define _real_ptsname_r NEXT_FNC(ptsname_r)
#define _real_ttyname_r NEXT_FNC(ttyname_r)
#define _real_getpt NEXT_FNC(getpt)
#define _real_posix_openpt NEXT_FNC(posix_openpt)
#define _real_openlog NEXT_FNC(openlog)
#define _real_closelog NEXT_FNC(closelog)
#define _real_mq_open NEXT_FNC(mq_open)
#define _real_mq_close NEXT_FNC(mq_close)
#define _real_mq_send NEXT_FNC(mq_send)
#define _real_mq_receive NEXT_FNC(mq_receive)
#define _real_mq_timedsend NEXT_FNC(mq_timedsend)
#define _real_mq_timedreceive NEXT_FNC(mq_timedreceive)
#define _real_mq_notify NEXT_FNC(mq_notify)
#define _real_fcntl NEXT_FNC(fcntl)

#define _real_system NEXT_FNC(system)
#define _real_mmap NEXT_FNC(mmap)
#define _real_munmap NEXT_FNC(munmap)
#define _real_access NEXT_FNC(access)
#define _real_access NEXT_FNC(access)
// NOTE:  realpath is a versioned symbol, and we should be using
//   NEXT_FNC_DEFAULT.  But that interferes with libdl.so (e.g., dlopen).
//   and other functions that use gettid() -> __tls_get_addr()
//   for some unknown reason.
#define _real_realpath NEXT_FNC(realpath)

#endif // FILE_WRAPPERS_H
