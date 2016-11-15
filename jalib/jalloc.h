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
class JAllocDispatcher
{
  private:
    static void initialize(void);

  public:
    static void *allocate(size_t n);
    static void deallocate(void *ptr, size_t n);
    static void *malloc(size_t nbytes)
    {
      size_t *p = (size_t *)JAllocDispatcher::allocate(nbytes + sizeof(size_t));

      *p = nbytes;
      p += 1;
      return p;
    }

    static void free(void *p)
    {
      size_t *_p = (size_t *)p;

      _p -= 1;
      JAllocDispatcher::deallocate(_p, *_p + sizeof(size_t));
    }

    static int numExpands();
    static void preExpand();
};

class JAlloc
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p)
    {
      return p;
    }

    static void *operator new(size_t nbytes)
    {
      return JAllocDispatcher::malloc(nbytes);
    }

    static void *operator new[](size_t nbytes)
    {
      return JAllocDispatcher::malloc(nbytes);
    }

    static void operator delete(void *p)
    {
      return JAllocDispatcher::free(p);
    }

    static void operator delete[](void *p)
    {
      return JAllocDispatcher::free(p);
    }
#endif // ifdef JALIB_ALLOCATOR
};
}

#define JALLOC_HELPER_NEW(nbytes)                                            \
                                     return jalib::JAllocDispatcher::malloc( \
    nbytes)
#define JALLOC_HELPER_DELETE(p)      jalib::JAllocDispatcher::free(p)

#define JALLOC_HELPER_MALLOC(nbytes) jalib::JAllocDispatcher::malloc(nbytes)
#define JALLOC_HELPER_FREE(p)        jalib::JAllocDispatcher::free(p)

#define JALLOC_NEW    JALLOC_HELPER_NEW
#define JALLOC_DELETE JALLOC_HELPER_DELETE
#define JALLOC_MALLOC JALLOC_HELPER_MALLOC
#define JALLOC_FREE   JALLOC_HELPER_FREE
#endif // ifndef JALLOC_H
