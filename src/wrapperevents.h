#ifndef DMTCP_WRAPPEREVENTS_H
#define DMTCP_WRAPPEREVENTS_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "builtinplugins.h"

namespace dmtcp
{
enum WrapperEvent {
  WRAPPER_EVENT_MALLOC_PRE = 0,
  WRAPPER_EVENT_MALLOC_POST,
  WRAPPER_EVENT_CALLOC_PRE,
  WRAPPER_EVENT_CALLOC_POST,
  WRAPPER_EVENT_REALLOC_PRE,
  WRAPPER_EVENT_REALLOC_POST,
  WRAPPER_EVENT_FREE_PRE,
  WRAPPER_EVENT_FREE_POST,
  WRAPPER_EVENT_MEMALIGN_PRE,
  WRAPPER_EVENT_MEMALIGN_POST,
  WRAPPER_EVENT_POSIX_MEMALIGN_PRE,
  WRAPPER_EVENT_POSIX_MEMALIGN_POST,
  WRAPPER_EVENT_VALLOC_PRE,
  WRAPPER_EVENT_VALLOC_POST,
  WRAPPER_EVENT_MMAP_PRE,
  WRAPPER_EVENT_MMAP_POST,
  WRAPPER_EVENT_MMAP64_PRE,
  WRAPPER_EVENT_MMAP64_POST,
  WRAPPER_EVENT_MUNMAP_PRE,
  WRAPPER_EVENT_MUNMAP_POST,
  WRAPPER_EVENT_MREMAP_PRE,
  WRAPPER_EVENT_MREMAP_POST,
  WRAPPER_EVENT_DLOPEN_PRE,
  WRAPPER_EVENT_DLOPEN_POST,
  WRAPPER_EVENT_COUNT
};

struct MallocWrapperCtx {
  size_t size;
  void *result;
  int savedErrno;
};

struct CallocWrapperCtx {
  size_t nmemb;
  size_t size;
  void *result;
  int savedErrno;
};

struct ReallocWrapperCtx {
  void *ptr;
  size_t size;
  void *result;
  int savedErrno;
};

struct FreeWrapperCtx {
  void *ptr;
  int savedErrno;
};

struct MemalignWrapperCtx {
  size_t boundary;
  size_t size;
  void *result;
  int savedErrno;
};

struct PosixMemalignWrapperCtx {
  void **memptr;
  size_t alignment;
  size_t size;
  int result;
  int savedErrno;
};

struct VallocWrapperCtx {
  size_t size;
  void *result;
  int savedErrno;
};

struct MmapWrapperCtx {
  void *addr;
  size_t length;
  int prot;
  int flags;
  int fd;
  off_t offset;
  void *result;
  int savedErrno;
};

struct Mmap64WrapperCtx {
  void *addr;
  size_t length;
  int prot;
  int flags;
  int fd;
  int64_t offset;
  void *result;
  int savedErrno;
};

struct MunmapWrapperCtx {
  void *addr;
  size_t length;
  int result;
  int savedErrno;
};

struct MremapWrapperCtx {
  void *oldAddress;
  size_t oldSize;
  size_t newSize;
  int flags;
  void *newAddress;
  bool hasNewAddress;
  void *result;
  int savedErrno;
};

struct DlopenWrapperCtx {
  const char *filename;
  int flags;
  void *result;
  int savedErrno;
};

typedef void (*WrapperHook)(WrapperEvent event, void *ctx);

void registerBuiltinWrapperHook(BuiltinPluginId owner,
                                WrapperEvent event,
                                WrapperHook hook);
void dispatchWrapperPre(WrapperEvent event, void *ctx);
void dispatchWrapperPost(WrapperEvent event, void *ctx);
}

#endif
