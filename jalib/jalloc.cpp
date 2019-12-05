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

#include "jalib.h"
#include "jalloc.h"
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

// Make highest chunk size large; avoid a raw_alloc calling mmap()
// during /proc/self/maps
#define MAX_CHUNKSIZE (4 * 1024)

using namespace jalib;

extern "C" int fred_record_replay_enabled() __attribute__((weak));
static bool _initialized = false;

#ifdef JALIB_ALLOCATOR

# include <stdlib.h>
# include <sys/mman.h>

namespace jalib
{

inline void *
_alloc_raw(size_t n)
{
# ifdef JALIB_USE_MALLOC
  return malloc(n);

# else // ifdef JALIB_USE_MALLOC

  // #define USE_DMTCP_ALLOC_ARENA
#  ifdef USE_DMTCP_ALLOC_ARENA
#   ifndef __x86_64__
#    error "USE_DMTCP_ALLOC_ARENA can't be used with 32-bit binaries"
#   endif // ifndef __x86_64__
  static void *mmapHintAddr = (void *)0x1f000000000;
  if (n % sysconf(_SC_PAGESIZE) != 0) {
    n = (n + sysconf(_SC_PAGESIZE) - (n % sysconf(_SC_PAGESIZE)));
  }
  void *p = mmap(mmapHintAddr, n, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
  if (p != MAP_FAILED) {
    mmapHintAddr = p + n;
  }
#  else // ifdef USE_DMTCP_ALLOC_ARENA
  void *p = mmap(NULL,
                 n,
                 PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS,
                 -1,
                 0);
#  endif // ifdef USE_DMTCP_ALLOC_ARENA

  if (p == MAP_FAILED) {
    perror("DMTCP(" __FILE__ "): _alloc_raw: ");
  }
  return p;
# endif // ifdef JALIB_USE_MALLOC
}

inline void
_dealloc_raw(void *ptr, size_t n)
{
# ifdef JALIB_USE_MALLOC
  free(ptr);
# else // ifdef JALIB_USE_MALLOC
  if (ptr == 0 || n == 0) {
    return;
  }
  int rv = munmap(ptr, n);
  if (rv != 0) {
    perror("DMTCP(" __FILE__ "): _dealloc_raw: ");
  }
# endif // ifdef JALIB_USE_MALLOC
}

template<size_t _N>
class JFixedAllocStack
{
  public:
    enum { N = _N };
    JFixedAllocStack()
    {
      if (_blockSize == 0) {
        _blockSize = 4 * MAX_CHUNKSIZE;
      }
      _root = NULL;
      _numExpands = 0;
    }

    void initialize(int blockSize)
    {
      _blockSize = blockSize;
    }

    size_t chunkSize() { return N; }

    // allocate a chunk of size N
    void *allocate()
    {
      FreeItem *item = NULL;

      do {
        if (_root == NULL) {
          expand();
        }

        // NOTE: _root could still be NULL (if other threads consumed all
        // blocks that were made available from expand().  In such case, we
        // loop once again.

        /* Atomically does the following operation:
         *   item = _root;
         *   _root = item->next;
         */
        item = _root;
      } while (!item ||
               !__sync_bool_compare_and_swap(&_root, item, item->next));

      item->next = NULL;
      return item;
    }

    // deallocate a chunk of size N
    void deallocate(void *ptr)
    {
      if (ptr == NULL) { return; }
      FreeItem *item = static_cast<FreeItem *>(ptr);
      do {
        /* Atomically does the following operation:
         *   item->next = _root;
         *   _root = item;
         */
        item->next = _root;
      } while (!__sync_bool_compare_and_swap(&_root, item->next, item));
    }

    int numExpands()
    {
      return _numExpands;
    }

    void preExpand()
    {
      // Force at least numChunks chunks to become free.
      const int numAllocs = 10;
      void *allocatedItem[numAllocs];

      for (int i = 0; i < numAllocs; i++) {
        allocatedItem[i] = allocate();
      }

      // if (_root == NULL) { expand(); }
      for (int i = 0; i < numAllocs; i++) {
        deallocate(allocatedItem[i]);
      }
    }

  protected:
    // allocate more raw memory when stack is empty
    void expand()
    {
      _numExpands++;
      if (_root != NULL &&
          fred_record_replay_enabled && fred_record_replay_enabled()) {
        // TODO: why is expand being called? If you see this message, raise lvl2
        // allocation level.
        char expand_msg[] = "\n\n\n******* EXPAND IS CALLED *******\n\n\n";
        write(2, expand_msg, sizeof(expand_msg));

        // jalib::fflush(stderr);
        abort();
      }
      FreeItem *bufs = static_cast<FreeItem *>(_alloc_raw(_blockSize));
      int count = _blockSize / sizeof(FreeItem);
      for (int i = 0; i < count - 1; ++i) {
        bufs[i].next = bufs + i + 1;
      }

      do {
        /* Atomically does the following operation:
         *   bufs[count-1].next = _root;
         *   _root = bufs;
         */
        bufs[count - 1].next = _root;
      } while (!__sync_bool_compare_and_swap(&_root, bufs[count - 1].next,
                                             bufs));
    }

  protected:
    struct FreeItem {
      union {
        FreeItem *next;
        char buf[N];
      };
    };

  private:
    FreeItem *volatile _root;
    size_t _blockSize = 0;
    char padding[128];
    int volatile _numExpands;
};
} // namespace jalib

jalib::JFixedAllocStack<64>lvl1;
jalib::JFixedAllocStack<256>lvl2;
jalib::JFixedAllocStack<1024>lvl3;
# if MAX_CHUNKSIZE <= 1024
#  error MAX_CHUNKSIZE must be larger
# endif // if MAX_CHUNKSIZE <= 1024
jalib::JFixedAllocStack<MAX_CHUNKSIZE>lvl4;

void
jalib::JAllocDispatcher::initialize(void)
{
  if (fred_record_replay_enabled != 0 && fred_record_replay_enabled()) {
    /* We need a greater arena size to eliminate mmap() calls that could happen
       at different times for record vs. replay. */
    lvl1.initialize(1024 * 1024 * 16);
    lvl2.initialize(1024 * 1024 * 16);
    lvl3.initialize(1024 * 32 * 16);
    lvl4.initialize(1024 * 32 * 16);
  } else {
    lvl1.initialize(1024 * 16);
    lvl2.initialize(1024 * 16);
    lvl3.initialize(1024 * 32);
    lvl4.initialize(1024 * 32);
  }
  _initialized = true;
}

void *
jalib::JAllocDispatcher::allocate(size_t n)
{
  if (!_initialized) {
    initialize();
  }
  void *retVal;
  if (n <= lvl1.chunkSize()) {
    retVal = lvl1.allocate();
  } else if (n <= lvl2.chunkSize()) {
    retVal = lvl2.allocate();
  } else if (n <= lvl3.chunkSize()) {
    retVal = lvl3.allocate();
  } else if (n <= lvl4.chunkSize()) {
    retVal = lvl4.allocate();
  } else {
    retVal = _alloc_raw(n);
  }
  return retVal;
}

void
jalib::JAllocDispatcher::deallocate(void *ptr, size_t n)
{
  if (!_initialized) {
    char msg[] = "***DMTCP INTERNAL ERROR: Free called before init\n";
    write(2, msg, sizeof(msg));
    abort();
  }
  if (n <= lvl1.N) {
    lvl1.deallocate(ptr);
  } else if (n <= lvl2.N) {
    lvl2.deallocate(ptr);
  } else if (n <= lvl3.N) {
    lvl3.deallocate(ptr);
  } else if (n <= lvl4.N) {
    lvl4.deallocate(ptr);
  } else {
    _dealloc_raw(ptr, n);
  }
}

int
jalib::JAllocDispatcher::numExpands()
{
  return lvl1.numExpands() + lvl2.numExpands() +
         lvl3.numExpands() + lvl4.numExpands();
}

void
jalib::JAllocDispatcher::preExpand()
{
  lvl1.preExpand();
  lvl2.preExpand();
  lvl3.preExpand();
  lvl4.preExpand();
}

#else // ifdef JALIB_ALLOCATOR

# include <stdlib.h>

void *
jalib::JAllocDispatcher::allocate(size_t n)
{
  void *p = ::malloc(n);

  return p;
}

void
jalib::JAllocDispatcher::deallocate(void *ptr, size_t)
{
  ::free(ptr);
}
#endif // ifdef JALIB_ALLOCATOR

#ifdef OVERRIDE_GLOBAL_ALLOCATOR
# ifndef JALIB_ALLOCATOR
#  error \
  "JALIB_ALLOCATOR must be #defined in dmtcp/jalib/jalloc.h for --enable-allocator to work"
# endif // ifndef JALIB_ALLOCATOR
void *
operator new(size_t nbytes)
{
  if (fred_record_replay_enabled && fred_record_replay_enabled()) {
    fprintf(stderr, "*** DMTCP Internal Error: OVERRIDE_GLOBAL_ALLOCATOR not"
                    " supported with FReD\n\n");
    abort();
  }
  size_t *p = (size_t *)jalib::JAllocDispatcher::allocate(
      nbytes + sizeof(size_t));
  *p = nbytes;
  p += 1;
  return p;
}

void
operator delete(void *_p)
{
  if (fred_record_replay_enabled && fred_record_replay_enabled()) {
    fprintf(stderr, "*** DMTCP Internal Error: OVERRIDE_GLOBAL_ALLOCATOR not"
                    " supported with FReD\n\n");
    abort();
  }
  size_t *p = (size_t *)_p;
  p -= 1;
  jalib::JAllocDispatcher::deallocate(p, *p + sizeof(size_t));
}
#endif // ifdef OVERRIDE_GLOBAL_ALLOCATOR
