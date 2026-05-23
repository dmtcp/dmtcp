#ifndef DMTCP_WRAPPEREVENTS_H
#define DMTCP_WRAPPEREVENTS_H

#include <stddef.h>
#include <sys/types.h>

#include "builtinplugins.h"

namespace dmtcp
{
enum WrapperEvent {
  WRAPPER_EVENT_MALLOC_PRE = 0,
  WRAPPER_EVENT_MALLOC_POST,
  WRAPPER_EVENT_MMAP_PRE,
  WRAPPER_EVENT_MMAP_POST,
  WRAPPER_EVENT_DLOPEN_PRE,
  WRAPPER_EVENT_DLOPEN_POST,
  WRAPPER_EVENT_COUNT
};

struct MallocWrapperCtx {
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
