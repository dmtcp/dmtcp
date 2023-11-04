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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define JALIB_ALLOCATOR

// This is enabled by default for now to catch memory corruption bugs. We should
// remove it once we are more comfortable with the state of the code.
#define JALLOC_DEBUG

static constexpr size_t headerSizeInBytes = sizeof(size_t);
static constexpr size_t footerSizeInBytes = sizeof(size_t);
#ifdef JALLOC_DEBUG
static constexpr size_t headerFooterSizeInBytes = headerSizeInBytes + footerSizeInBytes;
#else
static constexpr size_t headerFooterSizeInBytes = headerSizeInBytes;
#endif // ifdef JALLOC_DEBUG

namespace jalib
{
#define MAX_ARENAS 1024
struct JAllocArena
{
  void *startAddr;
  void *endAddr;
};

class JAllocDispatcher
{
  private:
    static void initialize(void);
    static void *allocate(size_t n);
    static void deallocate(void *ptr, size_t n);

  public:
    static void *malloc(size_t nbytes)
    {
      size_t *p = (size_t *)JAllocDispatcher::allocate(nbytes + headerFooterSizeInBytes);

      *p = nbytes;
      p += 1;

#ifdef JALLOC_DEBUG
      // Put a canary at the end of the allocated block. The canary is the value
      // of the starting address of the block. Note that the user only see the
      // block after the header.
      size_t *footerDebug = (size_t*) ((char *)(p) + nbytes);
      *footerDebug = (size_t)(p - 1);
#endif // ifdef JALLOC_DEBUG

      return p;
    }

    static void free(void *p)
    {
      if (p == nullptr) {
        return;
      }

      size_t *_p = (size_t*)((char*)p - headerSizeInBytes);
      size_t nbytes = *_p;

#ifdef JALLOC_DEBUG
      // Check that the canary is intact. If not, then we have a memory corruption bug.
      size_t *footerDebug = (size_t*)((char*)p + nbytes);

      if (*footerDebug != (size_t) _p) {
        char msg[] = "***DMTCP INTERNAL ERROR: Memory corruption detected\n";
        int rc = write(2, msg, sizeof(msg));
        if (rc != sizeof(msg)) {
          perror("DMTCP(" __FILE__ "): write: ");
        }
        abort();
      }
#endif // ifdef JALLOC_DEBUG

      JAllocDispatcher::deallocate(_p, nbytes + headerFooterSizeInBytes);
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

    static void getAllocArenas(JAllocArena **arenas, int *numArenas);
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
