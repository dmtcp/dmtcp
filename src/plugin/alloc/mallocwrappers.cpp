/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include <stdlib.h>
#include <unistd.h>
#include "dmtcp.h"
#include "alloc.h"

EXTERNC int dmtcp_alloc_enabled() { return 1; }

extern "C" void *calloc(size_t nmemb, size_t size)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  void *retval = _real_calloc ( nmemb, size );
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

extern "C" void *malloc(size_t size)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  void *retval = _real_malloc ( size );
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

extern "C" void *__libc_memalign(size_t boundary, size_t size)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  void *retval = _real_libc_memalign(boundary, size);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

extern "C" void *valloc(size_t size)
{
  return __libc_memalign(sysconf(_SC_PAGESIZE), size);
}

// FIXME:  Add wrapper for alloca(), posix_memalign(), etc.,
//    using DMTCP_PLUGIN_DISABLE_CKPT(), etc.

extern "C" void free(void *ptr)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  _real_free ( ptr );
  DMTCP_PLUGIN_ENABLE_CKPT();
}

extern "C" void *realloc(void *ptr, size_t size)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  void *retval = _real_realloc ( ptr, size );
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

