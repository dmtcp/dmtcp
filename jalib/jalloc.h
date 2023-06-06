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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>

#define JALIB_ALLOCATOR

// This is enabled by default for now to catch memory corruption bugs. We should
// remove it once we are more comfortable with the state of the code.
#define JALLOC_DEBUG

struct mallocHdr
{
  uint32_t size;
  uint16_t extraBytes;
  int16_t offset;
};

static constexpr size_t headerSizeInBytes = sizeof(struct mallocHdr);
static constexpr size_t footerSizeInBytes = sizeof(size_t);
#ifdef JALLOC_DEBUG
static constexpr size_t headerFooterSizeInBytes = headerSizeInBytes + footerSizeInBytes;
#else
static constexpr size_t headerFooterSizeInBytes = headerSizeInBytes;
#endif // ifdef JALLOC_DEBUG

namespace jalib
{
class JAllocDispatcher
{
  private:
    static void initialize(void);
    static void *allocate(size_t n);
    static void deallocate(void *ptr, size_t n);

  public:
    enum MallocType
    {
      MallocType_Malloc,
      MallocType_Memalign,
      MallocType_Free,
      MallocType_Realloc
    };

    struct MallocRecord
    {
      void *addr;
      unsigned size;
      MallocType type;
    };

#ifdef ENABLE_MALLOC_RECORD
    static constexpr size_t MALLOC_INFO_SIZE = 10000000;
    static size_t mallocInfoIdx;
    static struct MallocRecord mallocRecords[MALLOC_INFO_SIZE];

    static void record(void *addr, unsigned size, MallocType type)
    {
      int idx = mallocInfoIdx++;
      idx = idx % MALLOC_INFO_SIZE;
      mallocRecords[idx].addr = addr;
      mallocRecords[idx].size = size;
      mallocRecords[idx].type = type;
    }
#else // #ifdef ENABLE_MALLOC_RECORD
    static void record(void *addr, unsigned size, MallocType type ){}
#endif

    static void *malloc(size_t nbytes)
    {
      size_t reqBytes = nbytes + headerFooterSizeInBytes;
      struct mallocHdr *header = (struct mallocHdr *)JAllocDispatcher::allocate(reqBytes);
      size_t ret = ((size_t)header + headerSizeInBytes);

      header->size = nbytes;
      header->extraBytes = headerFooterSizeInBytes;
      header->offset = ret - (size_t) header;

#ifdef JALLOC_DEBUG
      // Put a canary at the end of the allocated block. The canary is the value
      // of the starting address of the block. Note that the user only see the
      // block after the header.
      size_t *footerDebug = (size_t*) (ret + nbytes);
      *footerDebug = (size_t) header;
#endif // ifdef JALLOC_DEBUG

      record((void*) ret, nbytes, MallocType_Malloc);
      return (void*) ret;
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
      size_t nbytes = header->size;

      if (size <= nbytes) {
        return p;
      }

      void *ret = malloc(size);
      memcpy(ret, p, nbytes);
      free(p);
      return ret;
    }

    static void *memalign (size_t alignment, size_t nbytes)
    {
      /* Allocate with worst case padding to hit alignment. */
      size_t reqBytes = nbytes + headerFooterSizeInBytes + alignment;

      size_t ptr = (size_t)JAllocDispatcher::allocate(reqBytes);
      size_t ret = ptr + headerSizeInBytes;

      if ((ret % alignment) != 0) {
        ret = ((ret + alignment) / alignment) * alignment;
      }

      struct mallocHdr *header = (struct mallocHdr *) (ret - headerSizeInBytes);
      header->size = nbytes;
      header->extraBytes = headerFooterSizeInBytes + alignment;
      header->offset = ret - ptr;

#ifdef JALLOC_DEBUG
      // Put a canary at the end of the allocated block. The canary is the value
      // of the starting address of the block. Note that the user only sees the
      // block after the header.
      size_t *footerDebug = (size_t*) (ret + nbytes);
      *footerDebug = (size_t) header;
#endif // ifdef JALLOC_DEBUG

      record((void*)ret, nbytes, MallocType_Memalign);
      return (void*) ret;
    }

    static void free(void *p)
    {
      if (p == nullptr) {
        return;
      }

      struct mallocHdr *header = (struct mallocHdr*)((char*)p - headerSizeInBytes);
      size_t allocSize = header->size + header->extraBytes;
      size_t blockAddr = (size_t) p - (size_t) header->offset;

#ifdef JALLOC_DEBUG
      // Check that the canary is intact. If not, then we have a memory corruption bug.
      size_t *footerDebug = (size_t*) ((size_t) p + header->size);

      if (*footerDebug != (size_t) header) {
        char msg[] = "***DMTCP INTERNAL ERROR: Memory corruption detected\n";
        int rc = write(2, msg, sizeof(msg));
        if (rc != sizeof(msg)) {
          perror("DMTCP(" __FILE__ "): write: ");
        }
        abort();
      }
#endif // ifdef JALLOC_DEBUG

      record(p, header->size, MallocType_Free);
      JAllocDispatcher::deallocate((void*) blockAddr, allocSize);
    }

    static int numExpands();
    static void preExpand();
};
}

#define JALLOC_NEW(n)               return jalib::JAllocDispatcher::malloc(n)
#define JALLOC_DELETE(p)            jalib::JAllocDispatcher::free(p)

#define JALLOC_MALLOC(n)            jalib::JAllocDispatcher::malloc(n)
#define JALLOC_FREE(p)              jalib::JAllocDispatcher::free(p)
#define JALLOC_REALLOC(p, n)        jalib::JAllocDispatcher::realloc(p, size)
#define JALLOC_MEMALIGN(align, n)   jalib::JAllocDispatcher::memalign(align, n)

#define JALLOC_HELPER_NEW    JALLOC_NEW
#define JALLOC_HELPER_DELETE JALLOC_DELETE
#define JALLOC_HELPER_MALLOC JALLOC_MALLOC
#define JALLOC_HELPER_FREE   JALLOC_FREE

#endif // ifndef JALLOC_H
