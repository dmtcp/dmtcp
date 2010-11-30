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
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
#include "synchronizationlogging.h"
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

#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
// Limit at which we promote malloc() -> mmap() (in bytes):
#define SYNCHRONIZATION_MALLOC_LIMIT 1024
/* Signature used to identify regions of memory we allocated.  TODO: The fact
   that this is unique is purely probabilistic. It's possible that we could get
   very unlucky, and this pattern happened to be in memory in front of the
   target region. */
#define ALLOC_SIGNATURE 123456789
struct alloc_header {
  unsigned int is_mmap : 1;
# ifdef x86_64
  unsigned int chunk_size : 63;
# else
  unsigned int chunk_size : 31;
# endif
  int signature;
};
#define ALLOC_HEADER_SIZE sizeof(struct alloc_header)
#define REAL_ADDR(addr) ((char *)(addr) - ALLOC_HEADER_SIZE)
#define REAL_SIZE(size) ((size) + ALLOC_HEADER_SIZE)
#define USER_ADDR(addr) ((char *)(addr) + ALLOC_HEADER_SIZE)
#define USER_SIZE(size) ((size) - ALLOC_HEADER_SIZE)
static dmtcp::map<void *, struct alloc_header> memaligned_regions;

static int initHook = 0;
static void my_init_hooks (void);
static void *my_malloc_hook (size_t, const void *);
static void my_free_hook (void*, const void *);
static void *(*old_malloc_hook) (size_t, const void *);
static void  (*old_free_hook) (void*, const void *);
//static void *_wrapped_malloc(size_t size);
//static void _wrapped_free(void *ptr);
/* Override initializing hook from the C library. */
//void (*__malloc_initialize_hook) (void) = my_init_hooks;

static pthread_mutex_t hook_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t allocation_lock = PTHREAD_MUTEX_INITIALIZER;
#endif //SYNCHRONIZATION_LOG_AND_REPLAY

#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
/* 
We define several distinct layers of operation for the *alloc() family
wrappers.

Since large malloc()s are internally promoted to mmap() calls by libc, we want
to do our own promotion to mmap() so that we can control the addresses on
replay. The promotion threshold is SYNCHRONIZATION_MALLOC_LIMIT.

Since we do our own promotion, we need to remember which pointers we called
mmap() for, and which we called _real_malloc(). For example, user code calls
malloc(2000). This is above the limit, and so instead of calling
_real_malloc(2000), we mmap(). But, all the user knows is that they called
malloc(). This means that the user will eventually pass the pointer returned by
our mmap() to his own free() call. So, internally at that point we will need to
call munmap() instead of free().

We insert a header (struct alloc_header) at the beginning of each memory area
we allocate for the user that contains the size, and also if it was mmapped.

WRAPPER LEVEL
-------------
These are called directly from user's code, and as such only worry about user
addresses.

INTERNAL_ level
---------------
These are called from wrapper code. Externally they always deal in USER terms.
They consume and return USER addresses but know how to access and manipulate
the internal header ("REAL" addresses), and also make the choice of malloc()
vs. mmap().
*/

static void insertAllocHeader(struct alloc_header *header, void *dest)
{
  header->signature = ALLOC_SIGNATURE;
  memcpy(dest, header, ALLOC_HEADER_SIZE);
}

static void getAllocHeader(struct alloc_header *header, void *p)
{
  memcpy(header, REAL_ADDR(p), ALLOC_HEADER_SIZE);
}

/* NOTE: Always consumes/returns USER addresses. Given a size and destination,
   will allocate the space and insert our own header at the beginning. Non-null
   destination forces allocation at that address. */
static void *internal_malloc(size_t size, void *dest)
{
  JASSERT ( size != 0 ).Text("0 should be not be passed to internal_malloc()");
  void *retval;
  struct alloc_header header;
  void *mmap_addr = (dest == NULL) ? NULL : REAL_ADDR(dest);
  int mmap_flags = (dest == NULL) ? (MAP_PRIVATE | MAP_ANONYMOUS)
                                  : (MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED);
  if (size >= SYNCHRONIZATION_MALLOC_LIMIT) {
    header.is_mmap = 1;
    retval = mmap ( mmap_addr, REAL_SIZE(size), 
        PROT_READ | PROT_WRITE, mmap_flags, -1, 0 );
    JASSERT ( retval != (void *)-1 ) ( retval ) ( errno );
  } else {
    header.is_mmap = 0;
    retval = _real_malloc ( REAL_SIZE(size) );
  }
  header.chunk_size = size;
  // Insert our header in the beginning:
  insertAllocHeader(&header, retval);
  return USER_ADDR(retval);
}

/* TODO: Need a better way to handle aligned memory. The current strategy is to
keep an extra list of memory regions allocated with memalign so that we know
they don't have the header when we go to free() them.

We can't simply insert the header at the beginning, because then the region
returned to the user would not be aligned with their boundary.

A proper implementation here might perform its own alignment, and not rely on
the alignment in _real_libc_memalign.

If a program is heavy on memalign+free, the list strategy could create a lot of
performance penalties. */
static void *internal_libc_memalign(size_t boundary, size_t size, void *dest)
{
  void *retval;
  struct alloc_header header;
  void *mmap_addr = dest;
  int mmap_flags = (dest == NULL) ? (MAP_PRIVATE | MAP_ANONYMOUS)
                                  : (MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED);
  if (size >= SYNCHRONIZATION_MALLOC_LIMIT) {
    header.is_mmap = 1;
    retval = mmap ( mmap_addr, size, 
        PROT_READ | PROT_WRITE, mmap_flags, -1, 0 );
    JASSERT ( retval != (void *)-1 ) ( retval ) ( errno );
  } else {
    header.is_mmap = 0;
    retval = _real_libc_memalign ( boundary, size );
  }
  header.chunk_size = size;
  /* Instead of inserting the header in the memory region, keep track of it
     using a separate list. */
  memaligned_regions[retval] = header;
  return retval;
}

/* NOTE: Always consumes/returns USER addresses. Given a size and destination,
   will allocate the space and insert our own header at the beginning. Non-null
   destination forces allocation at that address. */
static void *internal_calloc(size_t nmemb, size_t size, void *dest)
{
  // internal_malloc() returns USER address.
  void *retval = internal_malloc(nmemb*size, dest);
  memset(retval, 0, nmemb*size);
  return retval;
}

/* NOTE: Always consumes USER addresses. Frees the memory at the given address,
   calling munmap() or _real_free() as needed. */
static void internal_free(void *ptr)
{
  struct alloc_header header;
  if (memaligned_regions.find(ptr) != memaligned_regions.end()) {
    /* It was a memaligned region -- no header at the front, so handle it
       specially. */
    header = memaligned_regions[ptr];
    if (header.is_mmap) {
      int retval = munmap(ptr, header.chunk_size);
      JASSERT ( retval != -1 );
    } else {
      _real_free(ptr);
    }
    memaligned_regions.erase(ptr);
    return;
  }
  getAllocHeader(&header, ptr);
  if (header.signature != ALLOC_SIGNATURE) {
    JASSERT ( false ).Text("This should be handled by free() wrapper.");
    _real_free(ptr);
    return;
  }
  if (header.is_mmap) {
    int retval = munmap(REAL_ADDR(ptr), REAL_SIZE(header.chunk_size));
    JASSERT ( retval != -1 );
  } else {
    _real_free ( REAL_ADDR(ptr) );
  }
}

/* NOTE: Always consumes/returns USER addresses. Given a pointer, size and
   destination, will reallocate the space (copying old data in process) and
   insert our own header at the beginning. Non-null destination forces
   allocation at that address. */
static void *internal_realloc(void *ptr, size_t size, void *dest)
{
  struct alloc_header header;
  getAllocHeader(&header, ptr);
  void *retval = internal_malloc(size, dest);
  if (size < header.chunk_size) {
    memcpy(retval, ptr, size);
  } else {
    memcpy(retval, ptr, header.chunk_size);
  }
  internal_free(ptr);
  return retval;
}

static void my_init_hooks(void)
{
  /* Save old hook functions (from libc) and set them to our own hooks. */
  old_malloc_hook = __malloc_hook;
  old_free_hook = __free_hook;
  __malloc_hook = my_malloc_hook;
  __free_hook = my_free_hook;
}

static void *my_malloc_hook (size_t size, const void *caller)
{
  _real_pthread_mutex_lock(&hook_lock);
  void *result;
  /* Restore all old hooks */
  __malloc_hook = old_malloc_hook;
  __free_hook = old_free_hook;
  result = _real_malloc (size);
  /* Save underlying hooks */
  old_malloc_hook = __malloc_hook;
  old_free_hook = __free_hook;
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
  __free_hook = my_free_hook;
  _real_pthread_mutex_unlock(&hook_lock);
  return result;
}

static void my_free_hook (void *ptr, const void *caller)
{
  _real_pthread_mutex_lock(&hook_lock);
  /* Restore all old hooks */
  __malloc_hook = old_malloc_hook;
  __free_hook = old_free_hook;
  _real_free (ptr);
  /* Save underlying hooks */
  old_malloc_hook = __malloc_hook;
  old_free_hook = __free_hook;
  /* printf might call free, so protect it too. */
  /*static int tyler_pid = _real_getpid();
  printf ("<%d> freed pointer %p\n", tyler_pid, ptr);*/
  /* Restore our own hooks */
  __malloc_hook = my_malloc_hook;
  __free_hook = my_free_hook;
  _real_pthread_mutex_unlock(&hook_lock);
}
#endif // SYNCHRONIZATION_LOG_AND_REPLAY

extern "C" void *calloc(size_t nmemb, size_t size)
{
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr) && !log_all_allocs) {
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_calloc ( nmemb, size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    // Don't log gdb's read calls (e.g. user commands)
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_calloc ( nmemb, size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  if (nmemb == 0 || size == 0) {
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_calloc ( nmemb, size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  void *retval;
  log_entry_t my_entry = create_calloc_entry(my_clone_id, calloc_event, nmemb, size);
  log_entry_t my_return_entry = create_calloc_entry(my_clone_id, calloc_event_return, nmemb, size);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &calloc_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &calloc_turn_check);
    _real_pthread_mutex_lock(&allocation_lock);
    retval = internal_calloc(nmemb, size, 
               (void *)currentLogEntry.log_event_t.log_event_calloc.return_ptr);
    JASSERT ( (unsigned long int)retval ==
              currentLogEntry.log_event_t.log_event_calloc.return_ptr ) 
              ( retval )
              ( (void*)currentLogEntry.log_event_t.log_event_calloc.return_ptr );
    _real_pthread_mutex_unlock(&allocation_lock);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    _real_pthread_mutex_lock(&allocation_lock);
    addNextLogEntry(my_entry);
    retval = internal_calloc(nmemb, size, NULL);
    my_return_entry.log_event_t.log_event_calloc.return_ptr =
      (unsigned long int)retval;
    addNextLogEntry(my_return_entry);
    _real_pthread_mutex_unlock(&allocation_lock);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
#else
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *retVal = _real_calloc ( nmemb, size );
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retVal;
#endif
}
extern "C" void *malloc(size_t size)
{
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr) && !log_all_allocs) {
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_malloc ( size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    // Don't log gdb's read calls (e.g. user commands)
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_malloc ( size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  /*if ((logEntryIndex >= 900000) && !initHook) {
    my_init_hooks();
    initHook = 1;
  }*/
  if (size == 0) {
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_malloc ( size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  void *retval;
  log_entry_t my_entry = create_malloc_entry(my_clone_id, malloc_event, size);
  log_entry_t my_return_entry = create_malloc_entry(my_clone_id, malloc_event_return, size);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &malloc_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &malloc_turn_check);
    // Force allocation at same location as record:
    _real_pthread_mutex_lock(&allocation_lock);
    retval = internal_malloc(size, 
               (void *)currentLogEntry.log_event_t.log_event_malloc.return_ptr);
    if ((unsigned long int)retval !=
        currentLogEntry.log_event_t.log_event_malloc.return_ptr) {
      JTRACE ( "tyler" ) ( retval ) 
             ( (void*) currentLogEntry.log_event_t.log_event_malloc.return_ptr )
	     ( log_entry_index );
      kill(getpid(), SIGSEGV);
    }
    _real_pthread_mutex_unlock(&allocation_lock);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    _real_pthread_mutex_lock(&allocation_lock);
    addNextLogEntry(my_entry);
    retval = internal_malloc(size, NULL);
    my_return_entry.log_event_t.log_event_malloc.return_ptr =
      (unsigned long int)retval;
    addNextLogEntry(my_return_entry);
    _real_pthread_mutex_unlock(&allocation_lock);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
#else
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *retVal = _real_malloc ( size );
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retVal;
#endif
}

#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
extern "C" void *__libc_memalign(size_t boundary, size_t size)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr) && !log_all_allocs) {
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_libc_memalign ( boundary, size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    // Don't log gdb's read calls (e.g. user commands)
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_libc_memalign ( boundary, size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  if (size == 0) {
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_libc_memalign ( boundary, size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  void *retval;
  while (my_clone_id == 0) sleep(0.1);  
  log_entry_t my_entry = create_libc_memalign_entry(my_clone_id, libc_memalign_event, boundary, size);
  log_entry_t my_return_entry = create_libc_memalign_entry(my_clone_id, libc_memalign_event_return, boundary, size);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &libc_memalign_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &libc_memalign_turn_check);
    // Force allocation at same location as record:
    _real_pthread_mutex_lock(&allocation_lock);
    retval = internal_libc_memalign(boundary, size,
               (void *)currentLogEntry.log_event_t.log_event_libc_memalign.return_ptr);
    if ((unsigned long int)retval !=
        currentLogEntry.log_event_t.log_event_libc_memalign.return_ptr) {
      JTRACE ( "tyler" ) ( retval ) 
             ( (void*) currentLogEntry.log_event_t.log_event_libc_memalign.return_ptr )
	     ( log_entry_index );
      kill(getpid(), SIGSEGV);
    }
    _real_pthread_mutex_unlock(&allocation_lock);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    _real_pthread_mutex_lock(&allocation_lock);
    addNextLogEntry(my_entry);
    retval = internal_libc_memalign(boundary, size, NULL);
    my_return_entry.log_event_t.log_event_libc_memalign.return_ptr =
      (unsigned long int)retval;
    addNextLogEntry(my_return_entry);
    _real_pthread_mutex_unlock(&allocation_lock);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

/* TODO: fix me */
extern "C" void *valloc(size_t size) 
{
  return __libc_memalign(sysconf(_SC_PAGESIZE), size);
}

#endif

extern "C" void free(void *ptr)
{
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr) && !log_all_allocs) {
    _real_pthread_mutex_lock(&allocation_lock);
    _real_free ( ptr );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
  } else if (jalib::Filesystem::GetProgramName() == "gdb") {
    // Don't log gdb's read calls (e.g. user commands)
    _real_pthread_mutex_lock(&allocation_lock);
    _real_free ( ptr );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
  } else if (ptr == NULL) {
    // free(NULL) is allowed, but has no effect.
    // We can safely skip record/replay without compromising determinism.
    _real_pthread_mutex_lock(&allocation_lock);
    _real_free ( ptr );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
  } else {
    struct alloc_header header;
    getAllocHeader(&header, ptr);
    if (header.signature != ALLOC_SIGNATURE) {
      if (memaligned_regions.find(ptr) != memaligned_regions.end()) {
        /* It was a memaligned region -- no header at the front, so let
           internal_free below handle it specially. We still want to log/replay
           this.*/
      } else {
        /* No proper signature, and it wasn't memaligned. This means somebody
           allocated memory outside of our wrappers. We ignore this, and don't
           log/replay it. */
        _real_free(ptr);
        return;
      }
    }

    log_entry_t my_entry = create_free_entry(my_clone_id, free_event, (unsigned long int)ptr);
    log_entry_t my_return_entry = create_free_entry(my_clone_id, free_event_return, (unsigned long int)ptr);
    if (SYNC_IS_REPLAY) {
      waitForTurn(my_entry, &free_turn_check);
      getNextLogEntry();
      waitForTurn(my_return_entry, &free_turn_check);
      _real_pthread_mutex_lock(&allocation_lock);
      internal_free(ptr);
      _real_pthread_mutex_unlock(&allocation_lock);
      getNextLogEntry();
    } else if (SYNC_IS_LOG) {
      // Not restart; we should be logging.
      _real_pthread_mutex_lock(&allocation_lock);
      addNextLogEntry(my_entry);
      internal_free(ptr);
      addNextLogEntry(my_return_entry);
      _real_pthread_mutex_unlock(&allocation_lock);
    }
    WRAPPER_EXECUTION_ENABLE_CKPT();
  }
#else
  WRAPPER_EXECUTION_DISABLE_CKPT();
  _real_free ( ptr );
  WRAPPER_EXECUTION_ENABLE_CKPT();
#endif
}

extern "C" void *realloc(void *ptr, size_t size)
{
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr) && !log_all_allocs) {
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_realloc ( ptr, size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    // Don't log gdb's calls (e.g. user commands)
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_realloc ( ptr, size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  if (ptr == NULL) {
    // See man page for details.
    return malloc(size);
  }
  void *retval;
  log_entry_t my_entry = create_realloc_entry(my_clone_id, realloc_event, (unsigned long int)ptr, size);
  log_entry_t my_return_entry = create_realloc_entry(my_clone_id, realloc_event_return, (unsigned long int)ptr, size);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &realloc_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &realloc_turn_check);
    _real_pthread_mutex_lock(&allocation_lock);
    retval = internal_realloc(ptr, size,
               (void *)currentLogEntry.log_event_t.log_event_realloc.return_ptr);
    JASSERT ( (unsigned long int)retval ==
              currentLogEntry.log_event_t.log_event_realloc.return_ptr ) 
              ( (unsigned long int)retval )
              ( currentLogEntry.log_event_t.log_event_realloc.return_ptr );
    _real_pthread_mutex_unlock(&allocation_lock);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    _real_pthread_mutex_lock(&allocation_lock);
    addNextLogEntry(my_entry);
    retval = internal_realloc(ptr, size, NULL);
    my_return_entry.log_event_t.log_event_realloc.return_ptr =
      (unsigned long int)retval;
    addNextLogEntry(my_return_entry);
    _real_pthread_mutex_unlock(&allocation_lock);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
#else
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *retVal = _real_realloc ( ptr, size );
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retVal;
#endif
}
#endif // ENABLE_MALLOC_WRAPPER
