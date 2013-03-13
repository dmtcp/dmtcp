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

#ifndef JALLOC_H
#define JALLOC_H

#define JALIB_ALLOCATOR

#include <stdlib.h>

namespace jalib
{

class JAllocDispatcher {
public:
  static void* allocate(size_t n);
  static void  deallocate(void* ptr, size_t n);
  static void* malloc(size_t nbytes)
  {
    size_t* p = (size_t*) jalib::JAllocDispatcher::allocate(nbytes+sizeof(size_t));
    *p = nbytes;
    p+=1;
    return p;
  }
  static void  free(void* p)
  {
    size_t* _p = (size_t*) p;
    _p-=1;
    jalib::JAllocDispatcher::deallocate(_p, *_p+sizeof(size_t));
  }
  static void lock();
  static void unlock();
  static void disable_locks();
  static void enable_locks();
  static void reset_on_fork();
};

}

#define JALLOC_HELPER_LOCK() jalib::JAllocDispatcher::lock();
#define JALLOC_HELPER_UNLOCK() jalib::JAllocDispatcher::unlock();
#define JALLOC_HELPER_DISABLE_LOCKS() jalib::JAllocDispatcher::disable_locks();
#define JALLOC_HELPER_ENABLE_LOCKS() jalib::JAllocDispatcher::enable_locks();

#define JALLOC_HELPER_RESET_ON_FORK() jalib::JAllocDispatcher::reset_on_fork();

#define JALLOC_HELPER_NEW(nbytes) return jalib::JAllocDispatcher::malloc(nbytes)
#define JALLOC_HELPER_DELETE(p) return jalib::JAllocDispatcher::free(p)

#define JALLOC_HELPER_MALLOC(nbytes) jalib::JAllocDispatcher::malloc(nbytes)
#define JALLOC_HELPER_FREE(p) jalib::JAllocDispatcher::free(p)

#endif
