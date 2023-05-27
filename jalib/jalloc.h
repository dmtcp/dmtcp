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
#include <string.h>
#include <unistd.h>

#define JALIB_ALLOCATOR
#define OVERRIDE_GLOBAL_ALLOCATOR

// This is enabled by default for now to catch memory corruption bugs. We should
// remove it once we are more comfortable with the state of the code.
#define JALLOC_DEBUG

struct mallocHdr
{
  void *addr;
  size_t size;
};

static constexpr size_t headerSizeInBytes = sizeof(struct mallocHdr);
static constexpr size_t footerSizeInBytes = sizeof(size_t);
#ifdef JALLOC_DEBUG
static constexpr size_t headerFooterSizeInBytes = headerSizeInBytes + footerSizeInBytes;
#else
static constexpr size_t headerFooterSizeInBytes = headerSizeInBytes;
#endif // ifdef JALLOC_DEBUG

enum MallocType
{
  MallocType_Malloc,
  MallocType_Memalign,
  MallocType_Free,
  MallocType_Realloc
};

struct mallocInfo
{
  void *addr;
  unsigned size;
  MallocType type;
};

#define MALLOC_INFO_SIZE 10000000
extern size_t mallocInfoIdx;
extern struct mallocInfo mallocInfo[MALLOC_INFO_SIZE];
extern void *mmapHintAddrStart;
extern void *mmapHintAddr;

namespace jalib
{
class JAllocDispatcher
{
  private:
    static void initialize(void);
    static void *allocate(size_t n);
    static void deallocate(void *ptr, size_t n);

  public:
    static void print(void *addr)
    {
      for (size_t i = 0; i < mallocInfoIdx; i++) {
        if (mallocInfo[i].addr == addr) {
          char buf[256] = {0};
          sprintf(buf, "%lu: %u\n", i, mallocInfo[i].size);
          write(2, buf, strlen(buf) + 1);
        }
      }
    }

    static void record(void *addr, unsigned size, MallocType type)
    {
      int idx = mallocInfoIdx++;
      idx = idx % MALLOC_INFO_SIZE;
      mallocInfo[idx].addr = addr;
      mallocInfo[idx].size = size;
      mallocInfo[idx].type = type;
      if ((idx % 100000) == 0) print(addr);
    }

    static void *malloc(size_t nbytes)
    {
      size_t reqBytes = nbytes + headerFooterSizeInBytes;
      struct mallocHdr *header = (struct mallocHdr *)JAllocDispatcher::allocate(reqBytes);

      header->addr = header;
      header->size = reqBytes;

      void *ret = ((char*)header + headerSizeInBytes);

#ifdef JALLOC_DEBUG
      // Put a canary at the end of the allocated block. The canary is the value
      // of the starting address of the block. Note that the user only see the
      // block after the header.
      size_t *footerDebug = (size_t*) ((size_t) header->addr + header->size - footerSizeInBytes);
      *footerDebug = (size_t) header->addr;
#endif // ifdef JALLOC_DEBUG

      record(ret, nbytes, MallocType_Malloc);
      return ret;
    }

    static void *realloc(void *p, size_t size)
    {
      record(p, size, MallocType_Realloc);

      if (p == nullptr) {
        return malloc(size);
      }

      if (size == 0) {
        free(p);
        return NULL;
      }

      struct mallocHdr *header = (struct mallocHdr*)((char*)p - headerSizeInBytes);
      size_t nbytes = header->size - headerFooterSizeInBytes;

      if (size <= nbytes) {
        return p;
      }

      void *ret = malloc(size);
      memcpy(ret, p, nbytes);
      free(p);
      return ret;
    }

    static void *memalign (size_t alignment, size_t bytes)
    {
      /* Allocate with worst case padding to hit alignment. */
      size_t reqBytes = bytes + headerFooterSizeInBytes + alignment;

      size_t ptr = (size_t)JAllocDispatcher::allocate(reqBytes);
      size_t ret = ptr + headerSizeInBytes;

      if ((ret % alignment) != 0) {
        ret = ((ret + alignment) / alignment) * alignment;
      }

      if (ret >= ptr + bytes + footerSizeInBytes) {
      }

      struct mallocHdr *header = (struct mallocHdr *) (ret - sizeof(headerSizeInBytes));
      header->addr = (void*) ptr;
      header->size = reqBytes;

#ifdef JALLOC_DEBUG
      // Put a canary at the end of the allocated block. The canary is the value
      // of the starting address of the block. Note that the user only see the
      // block after the header.
      size_t *footerDebug = (size_t*) ((size_t) header->addr + header->size - footerSizeInBytes);
      *footerDebug = (size_t) header->addr;
#endif // ifdef JALLOC_DEBUG

      record((void*)ret, bytes, MallocType_Memalign);
      return (void*) ret;
    }

    static void free(void *p)
    {
      if (p == nullptr) {
        return;
      }

      if (p < mmapHintAddrStart || p > mmapHintAddr) {
        char msg[128];
        sprintf(msg, "Freeing non-arena memory: %p\n", p);
        write(2, msg, strlen(msg) + 1);
      }


      struct mallocHdr *header = (struct mallocHdr*)((char*)p - headerSizeInBytes);
      size_t nbytes = header->size;

#ifdef JALLOC_DEBUG
      // Check that the canary is intact. If not, then we have a memory corruption bug.
      size_t *footerDebug = (size_t*) ((size_t) header->addr + header->size - footerSizeInBytes);

      if (*footerDebug != (size_t) header->addr) {
        char msg[] = "***DMTCP INTERNAL ERROR: Memory corruption detected\n";
        int rc = write(2, msg, sizeof(msg));
        if (rc != sizeof(msg)) {
          perror("DMTCP(" __FILE__ "): write: ");
        }
        abort();
      }
#endif // ifdef JALLOC_DEBUG

      JAllocDispatcher::deallocate(header->addr, header->size);
      record(p, nbytes, MallocType_Free);
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
#define JALLOC_REALLOC(p, size) jalib::JAllocDispatcher::realloc(p, size)
#define JALLOC_MEMALIGN(alignment, nbytes) jalib::JAllocDispatcher::memalign(alignment, nbytes)
#endif // ifndef JALLOC_H
