/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
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

#ifndef _GNU_SOURCE
# define _GNU_SOURCE /* for sake of mremap */
#endif
#include <stdarg.h>
#include <stdlib.h>
#include <vector>
#include <list>
#include <string>
#include <fcntl.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/version.h>
#include <limits.h>
#include "uniquepid.h"
#include "dmtcpworker.h"
#include "dmtcpmessagetypes.h"
#include "protectedfds.h"
#include "constants.h"
#include "connectionmanager.h"
#include "syscallwrappers.h"
#include "sysvipc.h"
#include "util.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jconvert.h"
#ifdef RECORD_REPLAY
#include "synchronizationlogging.h"
#include "log.h"
#include <sys/mman.h>
#include <sys/syscall.h>
#include <malloc.h>
#include <execinfo.h>
// TODO: hack to be able to compile this (fcntl wrapper).
#define open _libc_open
#define open64 _libc_open64
#include <fcntl.h>
#undef open
#undef open64
#endif

#ifdef ENABLE_MALLOC_WRAPPER
# ifdef ENABLE_DLOPEN
#  error "ENABLE_MALLOC_WRAPPER can't work with ENABLE_DLOPEN"
# endif

#ifdef RECORD_REPLAY
static pthread_mutex_t allocation_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mmap_lock = PTHREAD_MUTEX_INITIALIZER;

char progname[200] = {0};
#endif //RECORD_REPLAY

#ifdef RECORD_REPLAY
#if 0 // USED only for debug purposes.
static int initHook = 0;
static void my_init_hooks (void);
static void *my_malloc_hook (size_t, const void *);
static void *my_realloc_hook (void *, size_t, const void *);
static void *my_memalign_hook (size_t, size_t, const void *);
static void my_free_hook (void*, const void *);
static void *(*old_malloc_hook) (size_t, const void *);
static void *(*old_realloc_hook) (void *, size_t, const void *);
static void *(*old_memalign_hook) (size_t, size_t, const void *);
static void  (*old_free_hook) (void*, const void *);
//static void *_wrapped_malloc(size_t size);
//static void _wrapped_free(void *ptr);
/* Override initializing hook from the C library. */
//void (*__malloc_initialize_hook) (void) = my_init_hooks;

static pthread_mutex_t hook_lock = PTHREAD_MUTEX_INITIALIZER;

static void my_init_hooks(void)
{
  strncpy(progname, jalib::Filesystem::GetProgramName().c_str(), 200);
  /* Save old hook functions (from libc) and set them to our own hooks. */
  _real_pthread_mutex_lock(&hook_lock);
  if (!initHook) {
    old_malloc_hook = __malloc_hook;
    old_realloc_hook = __realloc_hook;
    old_memalign_hook = __memalign_hook;
    old_free_hook = __free_hook;
    __malloc_hook = my_malloc_hook;
    __realloc_hook = my_realloc_hook;
    __memalign_hook = my_memalign_hook;
    __free_hook = my_free_hook;
    initHook = 1;
  }
  _real_pthread_mutex_unlock(&hook_lock);
}

static void *my_malloc_hook (size_t size, const void *caller)
{
  _real_pthread_mutex_lock(&hook_lock);
  void *result;
  /* Restore all old hooks */
  __malloc_hook = old_malloc_hook;
  __realloc_hook = old_realloc_hook;
  __memalign_hook = old_memalign_hook;
  __free_hook = old_free_hook;
  result = _real_malloc (size);
  /* Save underlying hooks */
  old_malloc_hook = __malloc_hook;
  old_free_hook = __free_hook;
  if (log_all_allocs && strcmp(progname, "gdb") != 0) {
    static int tyler_pid = _real_getpid();
    printf ("<%d> malloc (%u) returns %p\n", tyler_pid, (unsigned int) size, result);
  }
  /*static int tyler_pid = _real_getpid();
  void *buffer[10];
  int nptrs;
  // NB: In order to use backtrace, you must disable log_all_allocs.
  // AND remove the locks around !log_all_allocs real_malloc in malloc wrapper
  nptrs = backtrace (buffer, 10);
  backtrace_symbols_fd ( buffer, nptrs, 1);
  printf ("<%d> malloc (%u) returns %p\n", tyler_pid, (unsigned int) size, result);*/
  /* Restore our own hooks */
  __malloc_hook = my_malloc_hook;
  __realloc_hook = my_realloc_hook;
  __memalign_hook = my_memalign_hook;
  __free_hook = my_free_hook;
  _real_pthread_mutex_unlock(&hook_lock);
  return result;
}

static void *my_realloc_hook (void *ptr, size_t size, const void *caller)
{
  _real_pthread_mutex_lock(&hook_lock);
  void *result;
  /* Restore all old hooks */
  __malloc_hook = old_malloc_hook;
  __realloc_hook = old_realloc_hook;
  __memalign_hook = old_memalign_hook;
  __free_hook = old_free_hook;
  result = _real_realloc (ptr, size);
  /* Save underlying hooks */
  old_malloc_hook = __malloc_hook;
  old_free_hook = __free_hook;
  if (log_all_allocs && strcmp(progname, "gdb") != 0) {
    static int tyler_pid = _real_getpid();
    printf ("<%d> realloc (%p,%u) returns %p\n", tyler_pid, ptr, (unsigned int) size, result);
  }
  __malloc_hook = my_malloc_hook;
  __realloc_hook = my_realloc_hook;
  __memalign_hook = my_memalign_hook;
  __free_hook = my_free_hook;
  _real_pthread_mutex_unlock(&hook_lock);
  return result;

}

static void *my_memalign_hook (size_t boundary, size_t size, const void *caller)
{
  _real_pthread_mutex_lock(&hook_lock);
  void *result;
  /* Restore all old hooks */
  __malloc_hook = old_malloc_hook;
  __realloc_hook = old_realloc_hook;
  __memalign_hook = old_memalign_hook;
  __free_hook = old_free_hook;
  result = _real_libc_memalign (boundary, size);
  /* Save underlying hooks */
  old_malloc_hook = __malloc_hook;
  old_free_hook = __free_hook;
  if (log_all_allocs && strcmp(progname, "gdb") != 0) {
    static int tyler_pid = _real_getpid();
    printf ("<%d> memalign (%u,%u) returns %p\n", tyler_pid,
        (unsigned int)boundary, (unsigned int) size, result);
  }
  __malloc_hook = my_malloc_hook;
  __realloc_hook = my_realloc_hook;
  __memalign_hook = my_memalign_hook;
  __free_hook = my_free_hook;
  _real_pthread_mutex_unlock(&hook_lock);
  return result;

}

static void my_free_hook (void *ptr, const void *caller)
{
  _real_pthread_mutex_lock(&hook_lock);
  /* Restore all old hooks */
  __malloc_hook = old_malloc_hook;
  __realloc_hook = old_realloc_hook;
  __memalign_hook = old_memalign_hook;
  __free_hook = old_free_hook;
  _real_free (ptr);
  /* Save underlying hooks */
  old_malloc_hook = __malloc_hook;
  old_free_hook = __free_hook;
  /* printf might call free, so protect it too. */
  if (log_all_allocs) {
    static int tyler_pid = _real_getpid();
    printf ("<%d> freed pointer %p\n", tyler_pid, ptr);
  }
  /* Restore our own hooks */
  __malloc_hook = my_malloc_hook;
  __realloc_hook = my_realloc_hook;
  __memalign_hook = my_memalign_hook;
  __free_hook = my_free_hook;
  _real_pthread_mutex_unlock(&hook_lock);
}
#endif

#define MMAP_WRAPPER_HEADER(name, ...)                                  \
  void *return_addr = GET_RETURN_ADDRESS();                             \
  if ((!shouldSynchronize(return_addr) && !log_all_allocs) ||           \
      jalib::Filesystem::GetProgramName() == "gdb") {                   \
    _real_pthread_mutex_lock(&mmap_lock);                               \
    void *retval = _real_ ## name (__VA_ARGS__);                        \
    _real_pthread_mutex_unlock(&mmap_lock);                             \
    UNSET_IN_MMAP_WRAPPER();                                            \
    WRAPPER_EXECUTION_ENABLE_CKPT();                                    \
    return retval;                                                      \
  }                                                                     \
  log_entry_t my_entry = create_ ## name ## _entry(my_clone_id,         \
      name ## _event,                                                   \
      __VA_ARGS__);                                                     \
  void *retval;

#define MMAP_WRAPPER_REPLAY_START(name)                                 \
  WRAPPER_REPLAY_START_TYPED(void*, name);                              \
  _real_pthread_mutex_lock(&mmap_lock);

#define MMAP_WRAPPER_REPLAY_END(name)                                   \
  _real_pthread_mutex_unlock(&mmap_lock);                               \
  WRAPPER_REPLAY_END(name);

#define MALLOC_FAMILY_WRAPPER_HEADER_TYPED(ret_type, name, ...)             \
  void *return_addr = GET_RETURN_ADDRESS();                                 \
  if ((!shouldSynchronize(return_addr) && !log_all_allocs) ||               \
      jalib::Filesystem::GetProgramName() == "gdb") {                       \
    _real_pthread_mutex_lock(&allocation_lock);                             \
    ret_type retval = _real_ ## name (__VA_ARGS__);                         \
    _real_pthread_mutex_unlock(&allocation_lock);                           \
    WRAPPER_EXECUTION_ENABLE_CKPT();                                        \
    return retval;                                                          \
  }                                                                         \
  log_entry_t my_entry = create_ ## name ## _entry(my_clone_id,             \
                                                   name ## _event,          \
                                                   __VA_ARGS__);            \
  ret_type retval;

#define MALLOC_FAMILY_WRAPPER_HEADER(name, ...)                             \
  MALLOC_FAMILY_WRAPPER_HEADER_TYPED(void*, name, __VA_ARGS__)

#define MALLOC_FAMILY_WRAPPER_REPLAY_START(name)                            \
  WRAPPER_REPLAY_START_TYPED(void*, name);                                  \
  _real_pthread_mutex_lock(&allocation_lock);

#define MALLOC_FAMILY_WRAPPER_REPLAY_END(name)                              \
  _real_pthread_mutex_unlock(&allocation_lock);                             \
  WRAPPER_REPLAY_END(name);

/* XXX Possible race here. It is not safe to call
 * MALLOC_FAMILY_WRAPPER_REPLAY_END before calling _real_XXX. The replay end
 * macro calls getNextLogEntry(), which we should not do before we have called
 * the _real_XXX function. We will need to fix this eventually. */
#define MALLOC_FAMILY_BASIC_SYNC_WRAPPER(ret_type, name, ...)               \
  MALLOC_FAMILY_WRAPPER_HEADER_TYPED(ret_type, name, __VA_ARGS__);          \
  do {                                                                      \
    if (SYNC_IS_REPLAY) {                                                   \
      MALLOC_FAMILY_WRAPPER_REPLAY_START(name);                             \
      void *savedRetval = GET_COMMON(currentLogEntry, retval);              \
      /* We have to end it immediately because there may be an mmap. */     \
      MALLOC_FAMILY_WRAPPER_REPLAY_END(name);                               \
      _real_pthread_mutex_lock(&allocation_lock);                           \
      retval = _real_ ## name(__VA_ARGS__);                                 \
      if (retval != savedRetval) {                                          \
        JTRACE ( #name " returned wrong address on replay" )                \
          ( retval ) ( GET_COMMON(currentLogEntry, retval) )                \
          (unified_log.currentEntryIndex());                                \
    while (1);                                                              \
      }                                                                     \
      _real_pthread_mutex_unlock(&allocation_lock);                         \
    } else if (SYNC_IS_RECORD) {                                            \
      _real_pthread_mutex_lock(&allocation_lock);                           \
      size_t savedOffset = my_log->dataSize();				    \
      /* We write the *alloc entry before calling _real_XXX because         \
	 they may be internally promoted to mmap by libc. This way,         \
	 the malloc entry appears before the mmap entry in the log and      \
	 replay can proceed as normal. On replay, _real_alloc will be       \
	 called again, and it will again be promoted to mmap. */            \
      WRAPPER_LOG_WRITE_ENTRY(my_entry);                                    \
      retval = _real_ ## name(__VA_ARGS__);                                 \
      SET_COMMON2(my_entry, retval, (void *)retval);			    \
      SET_COMMON2(my_entry, my_errno, errno);				    \
      /* Patch in the real return value. */				    \
      my_log->replaceEntryAtOffset(my_entry, savedOffset);		    \
      _real_pthread_mutex_unlock(&allocation_lock);                         \
    }                                                                       \
  } while(0)

#endif // RECORD_REPLAY


/* This buffer (wrapper_init_buf) is used to pass on to dlsym() while it is
 * initializing the dmtcp wrappers. See comments in syscallsreal.c for more
 * details.
 */
static char wrapper_init_buf[1024];
static bool mem_allocated_for_initializing_wrappers = false;

extern "C" void *calloc(size_t nmemb, size_t size)
{
  if (dmtcp_wrappers_initializing) {
    JASSERT(!mem_allocated_for_initializing_wrappers);
    memset(wrapper_init_buf, 0, sizeof (wrapper_init_buf));
    //void *ret = JALLOC_HELPER_MALLOC ( nmemb * size );
    mem_allocated_for_initializing_wrappers = true;
    return (void*) wrapper_init_buf;
  }
  WRAPPER_EXECUTION_DISABLE_CKPT();
#ifdef RECORD_REPLAY
  MALLOC_FAMILY_BASIC_SYNC_WRAPPER(void*, calloc, nmemb, size);
#else
  void *retval = _real_calloc ( nmemb, size );
#endif
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

extern "C" void *malloc(size_t size)
{
  if (dmtcp_wrappers_initializing) {
    return calloc(1, size);
  }
  WRAPPER_EXECUTION_DISABLE_CKPT();
#ifdef RECORD_REPLAY
  MALLOC_FAMILY_BASIC_SYNC_WRAPPER(void*, malloc, size);
#else
  void *retval = _real_malloc ( size );
#endif
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

extern "C" void *__libc_memalign(size_t boundary, size_t size)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
#ifdef RECORD_REPLAY
  JASSERT (my_clone_id != 0);
  MALLOC_FAMILY_BASIC_SYNC_WRAPPER(void*, libc_memalign, boundary, size);
#else
  void *retval = _real_libc_memalign(boundary, size);
#endif
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

extern "C" void *valloc(size_t size)
{
  return __libc_memalign(sysconf(_SC_PAGESIZE), size);
}

// FIXME:  Add wrapper for alloca(), posix_memalign(), etc.,
//    using WRAPPER_EXECUTION_DISABLE_CKPT(), etc.

extern "C" void free(void *ptr)
{
  if (dmtcp_wrappers_initializing) {
    JASSERT(mem_allocated_for_initializing_wrappers);
    JASSERT(ptr == wrapper_init_buf);
    return;
  }

  WRAPPER_EXECUTION_DISABLE_CKPT();
#ifdef RECORD_REPLAY
  void *return_addr = GET_RETURN_ADDRESS();
  if ((!shouldSynchronize(return_addr) && !log_all_allocs) ||
      ptr == NULL ||
      jalib::Filesystem::GetProgramName() == "gdb") {
    _real_pthread_mutex_lock(&allocation_lock);
    _real_free(ptr);
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return;
  }

  log_entry_t my_entry = create_free_entry(my_clone_id, free_event, ptr);
  void *retval = NULL;

  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &free_turn_check);
    _real_pthread_mutex_lock(&allocation_lock);
    _real_free(ptr);
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_REPLAY_END(free);
  } else if (SYNC_IS_RECORD) {
    // Not restart; we should be logging.
    _real_pthread_mutex_lock(&allocation_lock);
    _real_free(ptr);
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
    _real_pthread_mutex_unlock(&allocation_lock);
  }
#else
  _real_free ( ptr );
#endif
  WRAPPER_EXECUTION_ENABLE_CKPT();
}

extern "C" void *realloc(void *ptr, size_t size)
{
  JASSERT (!dmtcp_wrappers_initializing)
    .Text ("This is a rather unusual path. Please inform DMTCP developers");

  WRAPPER_EXECUTION_DISABLE_CKPT();
#ifdef RECORD_REPLAY
  MALLOC_FAMILY_BASIC_SYNC_WRAPPER(void*, realloc, ptr, size);
#else
  void *retval = _real_realloc ( ptr, size );
#endif
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

#ifdef RECORD_REPLAY
/* mmap/mmap64
 * TODO: Remove the PROT_WRITE flag on REPLAY phase if it was not part of
 *       original flags.
 * FIXME: MAP_SHARED areas are restored as MAP_PRIVATE, check for correctness.
 */
extern "C" void *mmap(void *addr, size_t length, int prot, int flags,
                      int fd, off_t offset)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  SET_IN_MMAP_WRAPPER();
  MMAP_WRAPPER_HEADER(mmap, addr, length, prot, flags, fd, offset);
  if (SYNC_IS_REPLAY) {
    bool mmap_read_from_readlog = false;
    MMAP_WRAPPER_REPLAY_START(mmap);
    JASSERT ( addr == NULL ).Text("Unimplemented to have non-null addr.");
    addr = GET_COMMON(currentLogEntry, retval);
    if (retval != MAP_FAILED && fd != -1 &&
        ((flags & MAP_PRIVATE) != 0 || (flags & MAP_SHARED) != 0)) {
      flags &= ~MAP_SHARED;
      flags |= MAP_PRIVATE;
      flags |= MAP_ANONYMOUS;
      fd = -1;
      offset = 0;
      size_t page_size = sysconf(_SC_PAGESIZE);
      size_t page_mask = ~(page_size - 1);
      length = (length + page_size - 1) & PAGE_MASK ;
      mmap_read_from_readlog = true;
    }
    flags |= MAP_FIXED;
    retval = _real_mmap (addr, length, prot | PROT_WRITE, flags, fd, offset);
    if (retval != GET_COMMON(currentLogEntry, retval)) sleep(20);
    JASSERT ( retval == GET_COMMON(currentLogEntry, retval) ) (retval)
      (GET_COMMON(currentLogEntry, retval)) (JASSERT_ERRNO);
    if (mmap_read_from_readlog) {
      WRAPPER_REPLAY_READ_FROM_READ_LOG(mmap, retval, length);
    }
    MMAP_WRAPPER_REPLAY_END(mmap);
  } else if (SYNC_IS_RECORD) {
    _real_pthread_mutex_lock(&mmap_lock);
    retval = _real_mmap (addr, length, prot, flags, fd, offset);
    if (retval != MAP_FAILED && fd != -1 &&
        ((flags & MAP_PRIVATE) != 0 || (flags & MAP_SHARED) != 0)) {
      WRAPPER_LOG_WRITE_INTO_READ_LOG(mmap, retval, length);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
    _real_pthread_mutex_unlock(&mmap_lock);
  }
  UNSET_IN_MMAP_WRAPPER();
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

extern "C" void *mmap64 (void *addr, size_t length, int prot, int flags,
                         int fd, off64_t offset)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  SET_IN_MMAP_WRAPPER();
  MMAP_WRAPPER_HEADER(mmap64, addr, length, prot, flags, fd, offset);
  if (SYNC_IS_REPLAY) {
    bool mmap_read_from_readlog = false;
    MMAP_WRAPPER_REPLAY_START(mmap64);
    JASSERT ( addr == NULL ).Text("Unimplemented to have non-null addr.");
    addr = GET_COMMON(currentLogEntry, retval);
    if (retval != MAP_FAILED && fd != -1 &&
        ((flags & MAP_PRIVATE) != 0 || (flags & MAP_SHARED) != 0)) {
      flags &= ~MAP_SHARED;
      flags |= MAP_PRIVATE;
      flags |= MAP_ANONYMOUS;
      fd = -1;
      offset = 0;
      size_t page_size = sysconf(_SC_PAGESIZE);
      size_t page_mask = ~(page_size - 1);
      length = (length + page_size - 1) & PAGE_MASK ;
      mmap_read_from_readlog = true;
    }
    flags |= MAP_FIXED;
    retval = _real_mmap64 (addr, length, prot | PROT_WRITE, flags, fd, offset);
    JASSERT ( retval == GET_COMMON(currentLogEntry, retval) );
    if (mmap_read_from_readlog) {
      WRAPPER_REPLAY_READ_FROM_READ_LOG(mmap64, retval, length);
    }
    MMAP_WRAPPER_REPLAY_END(mmap64);
  } else if (SYNC_IS_RECORD) {
    _real_pthread_mutex_lock(&mmap_lock);
    retval = _real_mmap64 (addr, length, prot, flags, fd, offset);
    if (retval != MAP_FAILED && fd != -1 &&
        ((flags & MAP_PRIVATE) != 0 || (flags & MAP_SHARED) != 0)) {
      WRAPPER_LOG_WRITE_INTO_READ_LOG(mmap64, retval, length);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
    _real_pthread_mutex_unlock(&mmap_lock);
  }
  UNSET_IN_MMAP_WRAPPER();
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

extern "C" int munmap(void *addr, size_t length)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  MALLOC_FAMILY_WRAPPER_HEADER_TYPED(int, munmap, addr, length);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(munmap);
    _real_pthread_mutex_lock(&allocation_lock);
    retval = _real_munmap (addr, length);
    JASSERT (retval == (int)(unsigned long)GET_COMMON(currentLogEntry, retval));
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_REPLAY_END(munmap);
  } else if (SYNC_IS_RECORD) {
    _real_pthread_mutex_lock(&mmap_lock);
    retval = _real_munmap (addr, length);
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
    _real_pthread_mutex_unlock(&mmap_lock);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

// When exactly did the declaration of /usr/include/sys/mman.h change?
// (The extra parameter was created for the sake of MREMAP_FIXED.)
# if __GLIBC_PREREQ (2,4)
extern "C" void *mremap(void *old_address, size_t old_size,
                        size_t new_size, int flags, ...)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  va_list ap;
  va_start( ap, flags );
  void *new_address = va_arg ( ap, void * );
  va_end ( ap );

  MALLOC_FAMILY_WRAPPER_HEADER(mremap, old_address, old_size, new_size, flags,
                               new_address);
  if (SYNC_IS_REPLAY) {
    MALLOC_FAMILY_WRAPPER_REPLAY_START(mremap);
    void *addr = GET_COMMON(currentLogEntry, retval);
    flags |= (MREMAP_MAYMOVE | MREMAP_FIXED);
    retval = _real_mremap (old_address, old_size, new_size, flags, addr);
    JASSERT ( retval == GET_COMMON(currentLogEntry, retval) );
    MALLOC_FAMILY_WRAPPER_REPLAY_END(mremap);
  } else if (SYNC_IS_RECORD) {
    _real_pthread_mutex_lock(&mmap_lock);
    retval = _real_mremap (old_address, old_size, new_size, flags, new_address);
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
    _real_pthread_mutex_unlock(&mmap_lock);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}
# else
extern "C" void *mremap(void *old_address, size_t old_size,
    size_t new_size, int flags)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  MALLOC_FAMILY_WRAPPER_HEADER(mremap, old_address, old_size, new_size, flags,
			       NULL);
  if (SYNC_IS_REPLAY) {
    MALLOC_FAMILY_WRAPPER_REPLAY_START(mremap);
    void *addr = GET_COMMON(currentLogEntry, retval);
    flags |= MREMAP_MAYMOVE;
    retval = _real_mremap (old_address, old_size, new_size, flags, addr);
    JASSERT ( retval == GET_COMMON(currentLogEntry, retval) );
    MALLOC_FAMILY_WRAPPER_REPLAY_END(mremap);
  } else if (SYNC_IS_RECORD) {
    _real_pthread_mutex_lock(&mmap_lock);
    retval = _real_mremap (old_address, old_size, new_size, flags, 0);
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
    _real_pthread_mutex_unlock(&mmap_lock);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}
# endif

/*
extern "C" void *mmap2(void *addr, size_t length, int prot,
    int flags, int fd, off_t pgoffset)
{

}
*/
#endif

#endif // ENABLE_MALLOC_WRAPPER
