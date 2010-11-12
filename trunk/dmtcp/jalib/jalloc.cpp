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

#include "jalloc.h"
#include <pthread.h>
#include <stdio.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

static pthread_mutex_t allocateLock = PTHREAD_MUTEX_INITIALIZER;

void jalib::JAllocDispatcher::reset_on_fork()
{
  pthread_mutex_t tmpLock = PTHREAD_MUTEX_INITIALIZER;
  allocateLock = tmpLock;
}

void jalib::JAllocDispatcher::lock()
{
  if(pthread_mutex_lock(&allocateLock) != 0)
    perror("JGlobalAlloc::ckptThreadAcquireLock");
}

void jalib::JAllocDispatcher::unlock()
{
  if(pthread_mutex_unlock(&allocateLock) != 0)
    perror("JGlobalAlloc::ckptThreadReleaseLock");
}


#ifdef JALIB_ALLOCATOR

#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/user.h>


namespace jalib
{

inline void* _alloc_raw(size_t n) {
#ifdef JALIB_USE_MALLOC
  return malloc(n);
#else

//#define USE_DMTCP_ALLOC_ARENA
# ifdef USE_DMTCP_ALLOC_ARENA
#ifndef __x86_64__
#error "USE_DMTCP_ALLOC_ARENA can't be used with 32-bit binaries"
#endif
  static void *mmapHintAddr = (void*) 0x1f000000000;
  if (n % PAGE_SIZE != 0) {
    n = (n + PAGE_SIZE) - (n % PAGE_SIZE);
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

inline void _dealloc_raw(void* ptr, size_t n) {
#ifdef JALIB_USE_MALLOC
  free(ptr);
#else
  if(ptr==0 || n==0) return;
  int rv = munmap(ptr, n);
  if(rv!=0)
    perror("_dealloc_raw: ");
#endif
}

template < size_t _N, size_t BLOCKSIZE>
class JFixedAllocStack {
public:
  enum { N=_N };
  JFixedAllocStack() : _root(NULL) {}

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
    FreeItem* item = static_cast<FreeItem*>(ptr);
    item->next = _root;
    _root = item;
  }
protected:
  //allocate more raw memory when stack is empty
  void expand() {
    FreeItem* bufs = static_cast<FreeItem*>(_alloc_raw(BLOCKSIZE));
    int count=BLOCKSIZE / sizeof(FreeItem);
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
  char padding[128];
};

// FIXME: Do we really need this class now?     --Kapil
template < typename Alloc >
class JGlobalAlloc {
public:
  enum { N = Alloc::N };

  static void* allocate(){
#if 0
    if(pthread_mutex_lock(theMutex()) != 0)
      perror("JGlobalAlloc::allocate");
#endif
   
    void* ptr = theAlloc().allocate();

#if 0
    if(pthread_mutex_unlock(theMutex()) != 0)
      perror("JGlobalAlloc::allocate");
#endif

    return ptr;
  }

  //deallocate a chunk of size N
  static void deallocate(void* ptr){
#if 0
    if(pthread_mutex_lock(theMutex()) != 0)
      perror("JGlobalAlloc::allocate");
#endif
   
    theAlloc().deallocate(ptr);

#if 0
    if(pthread_mutex_unlock(theMutex()) != 0)
      perror("JGlobalAlloc::allocate");
#endif
  }

private:
  static pthread_mutex_t* theMutex() {
    static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    return &m;
  }
  static Alloc& theAlloc() {
    static Alloc a;
    return a;
  }
};

typedef JGlobalAlloc< JFixedAllocStack<64 ,  1024*16 > > lvl1;
typedef JGlobalAlloc< JFixedAllocStack<256,  1024*16 > > lvl2;
typedef JGlobalAlloc< JFixedAllocStack<1024, 1024*16 > > lvl3;

void* JAllocDispatcher::allocate(size_t n) {
  lock();
  void *retVal;
  if(n <= lvl1::N) retVal = lvl1::allocate(); else
  if(n <= lvl2::N) retVal = lvl2::allocate(); else
  if(n <= lvl3::N) retVal = lvl3::allocate(); else
  retVal = _alloc_raw(n);
  unlock();
  return retVal;
}
void JAllocDispatcher::deallocate(void* ptr, size_t n){
  lock();
  if(n <= lvl1::N) lvl1::deallocate(ptr); else
  if(n <= lvl2::N) lvl2::deallocate(ptr); else
  if(n <= lvl3::N) lvl3::deallocate(ptr); else
  _dealloc_raw(ptr, n);
  unlock();
}

} // namespace jalib

#else

#include <stdlib.h>

void* jalib::JAllocDispatcher::allocate(size_t n) {
  lock();
  void* p = malloc(n);
  unlock();
  return p;
}
void jalib::JAllocDispatcher::deallocate(void* ptr, size_t){
  lock();
  free(ptr);
  unlock();
}

#endif

#ifdef OVERRIDE_GLOBAL_ALLOCATOR
#  ifndef JALIB_ALLOCATOR
#    error "JALIB_ALLOCATOR must be #defined in dmtcp/jalib/jalloc.h for --enable-allocator to work"
#  endif
void* operator new(size_t nbytes){
  size_t* p = (size_t*) jalib::JAllocDispatcher::allocate(nbytes+sizeof(size_t));
  *p = nbytes;
  p+=1;
  return p;
}

void operator delete(void* _p){
  size_t* p = (size_t*) _p;
  p-=1;
  jalib::JAllocDispatcher::deallocate(p, *p+sizeof(size_t));
}
#endif
