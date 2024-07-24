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

#include <atomic>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "jalib.h"
#include "jalloc.h"

#define ATOMIC_SHARED volatile __attribute((aligned))

using namespace jalib;

static bool _initialized = false;
JAllocArena allocArenas[MAX_ARENAS];
static int numAllocArenas;

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
  void *p =
    mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#  endif // ifdef USE_DMTCP_ALLOC_ARENA

  if (p == MAP_FAILED) {
    perror("DMTCP(" __FILE__ "): _alloc_raw: ");
  }

#ifdef JALLOC_DEBUG_STATS
  if (p != MAP_FAILED) {
    int idx = __sync_fetch_and_add(&numAllocArenas, 1);
    if (idx < MAX_ARENAS) {
      allocArenas[idx].startAddr = p;
      allocArenas[idx].endAddr = (char*)p + n;
    }
  }
#endif // ifdef JALLOC_DEBUG_STATS

  return p;
# endif // ifdef JALIB_USE_MALLOC
}

void jalib::JAlloc::getAllocArenas(JAllocArena **arenas, int *numArenas)
{
  *arenas = allocArenas;
  int idx = numAllocArenas;
  *numArenas = idx > MAX_ARENAS ? MAX_ARENAS : idx;
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

#ifdef JALLOC_DEBUG_STATS
  for (int i = 0; i < numAllocArenas; i++) {
    if (allocArenas[i].startAddr == ptr) {
      allocArenas[i].startAddr = NULL;
      allocArenas[i].endAddr = NULL;
      break;
    }
  }
#endif // #ifdef JALLOC_DEBUG_STATS

  int rv = munmap(ptr, n);
  if (rv != 0) {
    perror("DMTCP(" __FILE__ "): _dealloc_raw: ");
  }

# endif // ifdef JALIB_USE_MALLOC
}

/*
 * Atomically compares the word at the given 'oldValue' address with the
 * word at the given 'dst' address. If the two words are equal, the word
 * at 'dst' address is changed to the word at 'newValue' address.
 * The function returns true if the exchange was successful.
 * If the exchange is not successfully, the function returns false.
 */
static inline bool
bool_atomic_dwcas(void volatile *dst, void *oldValue, void *newValue)
{
  bool result = false;
#ifdef __x86_64__
  typedef unsigned __int128 uint128_t;
  // This requires compiling with -mcx16
  result = __sync_bool_compare_and_swap((uint128_t volatile *)dst,
                                        *(uint128_t*)oldValue,
                                        *(uint128_t*)newValue);
#elif __arm__ || __i386__
  // FIXME: Consider replacing __sync_bool_compare_and_swap by
  //        __atomic_compare_exchange for improved performance.
  //        But for x86_64, this already uses cmpxchg16.  So,
  //        __atomic_* has no advantage, and adds a dependency on libatomic.so
  result = __sync_bool_compare_and_swap((uint64_t volatile *)dst,
                                        *(uint64_t*)oldValue,
                                        *(uint64_t*)newValue);
#elif __aarch64__
  // This requires libatomic.so
  typedef unsigned __int128 uint128_t;
  result = __atomic_compare_exchange((uint128_t*)dst,
                                     (uint128_t*)oldValue,
                                     (uint128_t*)newValue, 0,
                                      __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#endif /* if __x86_64__ */
  return result;
}

template<size_t _N>
class JFixedAllocStack
{
  public:
    enum { N = _N };
    JFixedAllocStack() = delete;

    JFixedAllocStack(size_t blockSize)
    : _blockSize(blockSize)
    {
      memset(&_top, 0, sizeof(_top));
      _numExpands = 0;
    }

    size_t chunkSize() { return N; }

    // allocate a chunk of size N
    void *allocate()
    {
      StackHead origHead = {0};
      StackHead newHead = {0};

      do {
        origHead = _top;
        if (origHead.node == NULL) {
          expand();
          origHead = _top;
        }

        // NOTE: _root could still be NULL (if other threads consumed all
        // blocks that were made available from expand().  In such case, we
        // loop once again.

        /* Atomically does the following operation:
         *   origHead = _top;
         *   _top = origHead.node->next;
         */
        // origHead.node is guaranteed to be a valid value if we get here
        newHead.node = origHead.node->next;
        newHead.counter = origHead.counter + 1;
      } while (!origHead.node ||
               !bool_atomic_dwcas(&_top, &origHead, &newHead));

      origHead.node->next = NULL;
      return origHead.node;
    }

    // deallocate a chunk of size N
    void deallocate(void *ptr)
    {
      if (ptr == NULL) { return; }
      FreeItem *item = static_cast<FreeItem *>(ptr);
      StackHead origHead = {0};
      StackHead newHead = {0};
      do {
        /* Atomically does the following operation:
         *   item->next = _top.node;
         *   _top = newHead;
         */
        origHead = _top;
        item->next = origHead.node;
        newHead.counter = origHead.counter + 1;
        newHead.node = item;
      } while (!bool_atomic_dwcas(&_top, &origHead, &newHead));
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
      StackHead origHead = {0};
      StackHead newHead = {0};
      _numExpands++;
      FreeItem *bufs =
        static_cast<FreeItem *>(JAllocDispatcher::allocate(_blockSize));
      int count = _blockSize / sizeof(FreeItem);
      for (int i = 0; i < count - 1; ++i) {
        bufs[i].next = bufs + i + 1;
      }

      do {
        /* Atomically does the following operation:
         *   bufs[count - 1].next = _top.node;
         *   _top = bufs;
         */
        origHead = _top;
        bufs[count - 1].next = origHead.node;
        newHead.node = bufs;
        newHead.counter = origHead.counter + 1;
      } while (!bool_atomic_dwcas(&_top, &origHead, &newHead));
    }

  protected:
    struct FreeItem {
      union {
        FreeItem *next;
        char buf[N];
      };
    };
    struct StackHead {
      uintptr_t counter;
      FreeItem* node;
    };

  private:
    StackHead _top;
    size_t _blockSize = 0;
    char padding[128];
    ATOMIC_SHARED int _numExpands;
};
} // namespace jalib

jalib::JFixedAllocStack<64> *lvl1;
jalib::JFixedAllocStack<256> *lvl2;
jalib::JFixedAllocStack<1024> *lvl3;
jalib::JFixedAllocStack<4096> *lvl4;
jalib::JFixedAllocStack<4 * 4096> *lvl5;

static char lvl1StorageBuffer[sizeof(jalib::JFixedAllocStack<64>)];
static char lvl2StorageBuffer[sizeof(jalib::JFixedAllocStack<256>)];
static char lvl3StorageBuffer[sizeof(jalib::JFixedAllocStack<1024>)];
static char lvl4StorageBuffer[sizeof(jalib::JFixedAllocStack<4096>)];
static char lvl5StorageBuffer[sizeof(jalib::JFixedAllocStack<4 * 4096>)];

void
jalib::JAllocDispatcher::initialize(void)
{
  // The template parameter specifies the largest allocation request a
  // particular alloc-arena can handle. For example, lvl1 will handle all
  // requests up to 64 bytes. Larger requests go to the next arena and so on.
  // Lvl5 handles requests up to 16 KB; anything larger results in a call to
  // alloc_raw (which in turn calls mmap).
  constexpr int blockSize = 4 * 4096;
  lvl1 = new (lvl1StorageBuffer) jalib::JFixedAllocStack<64>(blockSize);
  lvl2 = new (lvl2StorageBuffer) jalib::JFixedAllocStack<256>(blockSize);
  lvl3 = new (lvl3StorageBuffer) jalib::JFixedAllocStack<1024>(blockSize);
  lvl4 = new (lvl4StorageBuffer) jalib::JFixedAllocStack<4096>(blockSize);
  lvl5 = new (lvl5StorageBuffer) jalib::JFixedAllocStack<4 * 4096>(1024 * 1024 * 16);
  _initialized = true;
}

void *
jalib::JAllocDispatcher::allocate(size_t n)
{
  if (!_initialized) {
    initialize();
  }
  void *retVal;
  if (n <= lvl1->chunkSize()) {
    retVal = lvl1->allocate();
  } else if (n <= lvl2->chunkSize()) {
    retVal = lvl2->allocate();
  } else if (n <= lvl3->chunkSize()) {
    retVal = lvl3->allocate();
  } else if (n <= lvl4->chunkSize()) {
    retVal = lvl4->allocate();
  } else if (n <= lvl5->chunkSize()) {
    retVal = lvl5->allocate();
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
    int rc = write(2, msg, sizeof(msg));
    if (rc != sizeof(msg)) {
      perror("DMTCP(" __FILE__ "): write: ");
    }
    abort();
  }
  if (n <= lvl1->N) {
    lvl1->deallocate(ptr);
  } else if (n <= lvl2->N) {
    lvl2->deallocate(ptr);
  } else if (n <= lvl3->N) {
    lvl3->deallocate(ptr);
  } else if (n <= lvl4->N) {
    lvl4->deallocate(ptr);
  } else if (n <= lvl5->N) {
    lvl5->deallocate(ptr);
  } else {
    _dealloc_raw(ptr, n);
  }
}

int
jalib::JAllocDispatcher::numExpands()
{
  return lvl1->numExpands() + lvl2->numExpands() + lvl3->numExpands() +
         lvl4->numExpands() + lvl5->numExpands();
}

void
jalib::JAllocDispatcher::preExpand()
{
  lvl1->preExpand();
  lvl2->preExpand();
  lvl3->preExpand();
  lvl4->preExpand();
  lvl5->preExpand();
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
  size_t *p = (size_t *)jalib::JAllocDispatcher::allocate(
      nbytes + sizeof(size_t));
  *p = nbytes;
  p += 1;
  return p;
}

void
operator delete(void *_p)
{
  size_t *p = (size_t *)_p;
  p -= 1;
  jalib::JAllocDispatcher::deallocate(p, *p + sizeof(size_t));
}
#endif // ifdef OVERRIDE_GLOBAL_ALLOCATOR
