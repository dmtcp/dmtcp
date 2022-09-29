#ifndef __DMTCP_BUFFER_H__
#define __DMTCP_BUFFER_H__

#include <limits.h>  // for PATH_MAX
#include <sys/mman.h>
#include "jalloc.h"
#include "jassert.h"

namespace dmtcp
{
class PathBuffer
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR

    // Delete copy constructor and assignment operator.
    PathBuffer(const PathBuffer&) = delete;
    PathBuffer& operator=(const PathBuffer&) = delete;

    PathBuffer()
    {
      // TODO: Use a pool to avoid mmap/munmap calls.
      addr = mmap(0, PATH_MAX, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      JASSERT(addr != MAP_FAILED);
    }

    ~PathBuffer()
    {
      if (addr != MAP_FAILED && addr != nullptr) {
        JASSERT(munmap(addr, PATH_MAX) == 0);
      }
    }

    size_t size() { return PATH_MAX; }

    void *ptr() { return addr; }

    char *str() { return (char*) addr; }

  private:
    void *addr = nullptr;
};
}
#endif // #ifndef __DMTCP_BUFFER_H__
