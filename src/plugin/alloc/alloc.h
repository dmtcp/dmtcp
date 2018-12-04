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

#ifndef ALLOC_H
#define ALLOC_H

#include "dmtcp.h"

extern "C" void *__libc_memalign(size_t boundary, size_t size);

#define _real_malloc         NEXT_FNC(malloc)
#define _real_calloc         NEXT_FNC(calloc)
#define _real_valloc         NEXT_FNC(valloc)
#define _real_realloc        NEXT_FNC(realloc)
#define _real_free           NEXT_FNC(free)
#define _real_memalign       NEXT_FNC(memalign)
#define _real_posix_memalign NEXT_FNC(posix_memalign)
#define _real_libc_memalign  NEXT_FNC(__libc_memalign)

#define _real_mmap           NEXT_FNC(mmap)
#define _real_mmap64         NEXT_FNC(mmap64)
#define _real_munmap         NEXT_FNC(munmap)
#define _real_mremap         NEXT_FNC(mremap)
#endif // ALLOC_H
