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

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include "jalib.h"
#include "jalloc.h"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

extern "C" int fred_record_replay_enabled() __attribute__ ((weak));
static pthread_mutex_t allocateLock = PTHREAD_MUTEX_INITIALIZER;
static bool _enable_locks = true;
static bool _initialized = false;

void jalib::JAllocDispatcher::reset_on_fork()
{
  pthread_mutex_t tmpLock = PTHREAD_MUTEX_INITIALIZER;
  allocateLock = tmpLock;
}

void jalib::JAllocDispatcher::lock()
{
  if(_enable_locks && jalib::pthread_mutex_lock(&allocateLock) != 0)
    perror("JGlobalAlloc::ckptThreadAcquireLock");
}

void jalib::JAllocDispatcher::unlock()
{
  if(_enable_locks && jalib::pthread_mutex_unlock(&allocateLock) != 0)
    perror("JGlobalAlloc::ckptThreadReleaseLock");
}

void jalib::JAllocDispatcher::disable_locks()
{
  _enable_locks = false;
}

void jalib::JAllocDispatcher::enable_locks()
{
  _enable_locks = true;
}

#ifdef JALIB_ALLOCATOR

#include <sys/mman.h>
#include <stdlib.h>

namespace jalib
{

inline void* _alloc_raw(size_t n)
{
#ifdef JALIB_USE_MALLOC
  return malloc(n);
#else

//#define USE_DMTCP_ALLOC_ARENA
# ifdef USE_DMTCP_ALLOC_ARENA
#ifndef __x86_64__
#error "USE_DMTCP_ALLOC_ARENA can't be used with 32-bit binaries"
#endif
  static void *mmapHintAddr = (void*) 0x1f000000000;
  if (n % sysconf(_SC_PAGESIZE) != 0) {
    n = (n + sysconf(_SC_PAGESIZE) - (n % sysconf(_SC_PAGESIZE)));
  }
  void* p = mmap(mmapHintAddr, n, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
  if (p!= MAP_FAILED)
    mmapHintAddr = p + n;
# else
  void* p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
# endif

  if(p==MAP_FAILED)
    perror("_alloc_raw: ");
  return p;
#endif
}

inline void _dealloc_raw(void* ptr, size_t n)
{
#ifdef JALIB_USE_MALLOC
  free(ptr);
#else
  if(ptr==0 || n==0) return;
  int rv = munmap(ptr, n);
  if(rv!=0)
    perror("_dealloc_raw: ");
#endif
}

template <size_t _N>
class JFixedAllocStack {
public:
  enum { N=_N };
  JFixedAllocStack() {
    if (_blockSize == 0) {
      _blockSize = 10*1024;
      _root = NULL;
    }
  }

  void initialize(int blockSize) {
    _blockSize = blockSize;
  }

  size_t chunkSize() { return N; }

  //allocate a chunk of size N
  void* allocate() {
    if(_root == NULL) expand();
    FreeItem* item = _root;
    _root = item->next;
    item->next = NULL;
    return item;
  }

  //deallocate a chunk of size N
  void deallocate(void* ptr) {
    if (ptr == NULL) return;
    FreeItem* item = static_cast<FreeItem*>(ptr);
    item->next = _root;
    _root = item;
  }
protected:
  //allocate more raw memory when stack is empty
  void expand() {
    if (_root != NULL &&
        fred_record_replay_enabled && fred_record_replay_enabled()) {
      // TODO: why is expand being called? If you see this message, raise lvl2
      // allocation level.
      char expand_msg[] = "\n\n\n******* EXPAND IS CALLED *******\n\n\n";
      jalib::write(2, expand_msg, sizeof(expand_msg));
      //jalib::fflush(stderr);
      abort();
    }
    FreeItem* bufs = static_cast<FreeItem*>(_alloc_raw(_blockSize));
    int count= _blockSize / sizeof(FreeItem);
    for(int i=0; i<count-1; ++i){
      bufs[i].next=bufs+i+1;
    }
    bufs[count-1].next = _root;
    _root=bufs;
  }
protected:
  struct FreeItem {
    union {
      FreeItem* next;
      char buf[N];
    };
  };
private:
  FreeItem* _root;
  size_t _blockSize;
  char padding[128];
};

} // namespace jalib

jalib::JFixedAllocStack<64>   lvl1;
jalib::JFixedAllocStack<256>  lvl2;
jalib::JFixedAllocStack<1024> lvl3;
jalib::JFixedAllocStack<2048> lvl4;

void jalib::JAllocDispatcher::initialize(void)
{
  if (fred_record_replay_enabled != 0 && fred_record_replay_enabled()) {
    /* We need a greater arena size to eliminate mmap() calls that could happen
       at different times for record vs. replay. */
    lvl1.initialize(1024*1024*16);
    lvl2.initialize(1024*1024*16);
    lvl3.initialize(1024*32*16);
    lvl4.initialize(1024*32*16);
  } else {
    lvl1.initialize(1024*16);
    lvl2.initialize(1024*16);
    lvl3.initialize(1024*32);
    lvl4.initialize(1024*32);
  }
  _initialized = true;
}
void* jalib::JAllocDispatcher::allocate(size_t n)
{
  lock();
  if (!_initialized) {
    initialize();
  }
  void *retVal;
  if(n <= lvl1.chunkSize()) retVal = lvl1.allocate(); else
  if(n <= lvl2.chunkSize()) retVal = lvl2.allocate(); else
  if(n <= lvl3.chunkSize()) retVal = lvl3.allocate(); else
  if(n <= lvl4.chunkSize()) retVal = lvl4.allocate(); else
  retVal = _alloc_raw(n);
  unlock();
  return retVal;
}
void jalib::JAllocDispatcher::deallocate(void* ptr, size_t n)
{
  lock();
  if (!_initialized) {
    initialize();
  }
  if(n <= lvl1.N) lvl1.deallocate(ptr); else
  if(n <= lvl2.N) lvl2.deallocate(ptr); else
  if(n <= lvl3.N) lvl3.deallocate(ptr); else
  if(n <= lvl4.N) lvl4.deallocate(ptr); else
  _dealloc_raw(ptr, n);
  unlock();
}

#else

#include <stdlib.h>

void* jalib::JAllocDispatcher::allocate(size_t n)
{
  lock();
  void* p = ::malloc(n);
  unlock();
  return p;
}
void jalib::JAllocDispatcher::deallocate(void* ptr, size_t)
{
  lock();
  ::free(ptr);
  unlock();
}

#endif

#ifdef OVERRIDE_GLOBAL_ALLOCATOR
#  ifndef JALIB_ALLOCATOR
#    error "JALIB_ALLOCATOR must be #defined in dmtcp/jalib/jalloc.h for --enable-allocator to work"
#  endif
void* operator new(size_t nbytes)
{
  if (fred_record_replay_enabled && fred_record_replay_enabled()) {
    fprintf(stderr, "*** DMTCP Internal Error: OVERRIDE_GLOBAL_ALLOCATOR not"
            " supported with FReD\n\n");
    abort();
  }
  size_t* p = (size_t*) jalib::JAllocDispatcher::allocate(nbytes+sizeof(size_t));
  *p = nbytes;
  p+=1;
  return p;
}

void operator delete(void* _p)
{
  if (fred_record_replay_enabled && fred_record_replay_enabled()) {
    fprintf(stderr, "*** DMTCP Internal Error: OVERRIDE_GLOBAL_ALLOCATOR not"
            " supported with FReD\n\n");
    abort();
  }
  size_t* p = (size_t*) _p;
  p-=1;
  jalib::JAllocDispatcher::deallocate(p, *p+sizeof(size_t));
}
#endif
