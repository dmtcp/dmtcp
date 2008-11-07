/*****************************************************************************
 *   Copyright (C) 2006-2008 by Michael Rieker, Jason Ansel, Kapil Arya, and *
 *                                                            Gene Cooperman *
 *   mrieker@nii.net, jansel@csail.mit.edu, kapil@ccs.neu.edu, and           *
 *                                                          gene@ccs.neu.edu *
 *                                                                           *
 *   This file is part of the MTCP module of DMTCP (DMTCP:mtcp).             *
 *                                                                           *
 *  DMTCP:mtcp is free software: you can redistribute it and/or              *
 *  modify it under the terms of the GNU Lesser General Public License as    *
 *  published by the Free Software Foundation, either version 3 of the       *
 *  License, or (at your option) any later version.                          *
 *                                                                           *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,       *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *  GNU Lesser General Public License for more details.                      *
 *                                                                           *
 *  You should have received a copy of the GNU Lesser General Public         *
 *  License along with DMTCP:dmtcp/src.  If not, see                         *
 *  <http://www.gnu.org/licenses/>.                                          *
 *****************************************************************************/

#ifndef _MTCP_H
#define _MTCP_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MTCP_DEFAULT_SIGNAL SIGUSR2

void mtcp_init (char const *checkpointfilename, int interval, int clonenabledefault);
int mtcp_wrapper_clone (int (*fn) (void *arg), void *child_stack, int flags, void *arg);
int mtcp_ok (void);
int mtcp_no (void);

__attribute__ ((visibility ("hidden"))) void * mtcp_safemmap (void *start, size_t length, int prot, int flags, int fd, off_t offset);

void mtcp_set_callbacks(void (*sleep_between_ckpt)(int sec),
                        void (*pre_ckpt)(),
                        void (*post_ckpt)(int is_restarting),
                        int  (*ckpt_fd)(int fd),
                        void (*write_ckpt_prefix)(int fd));

#ifdef __cplusplus
}
#endif

#endif
