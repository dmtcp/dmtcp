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
#endif // RECORD_REPLAY

extern "C" void *calloc(size_t nmemb, size_t size)
{
#ifdef RECORD_REPLAY
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
    _real_pthread_mutex_lock(&allocation_lock);
    retval = _real_calloc(nmemb, size);
    waitForTurn(my_return_entry, &calloc_turn_check);
    JASSERT ( (unsigned long int)retval == GET_FIELD(currentLogEntry, calloc, return_ptr) )
      ( retval )
      ( (void*)GET_FIELD(currentLogEntry, calloc, return_ptr ) );
    _real_pthread_mutex_unlock(&allocation_lock);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    _real_pthread_mutex_lock(&allocation_lock);
    addNextLogEntry(my_entry);
    retval = _real_calloc(nmemb, size);
    SET_FIELD2(my_return_entry, calloc, return_ptr, (unsigned long int)retval);
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
#ifdef RECORD_REPLAY
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
    _real_pthread_mutex_lock(&allocation_lock);
    retval = _real_malloc(size);
    waitForTurn(my_return_entry, &malloc_turn_check);
    if ((unsigned long int)retval !=
        GET_FIELD(currentLogEntry, malloc, return_ptr)) {
      JTRACE ( "malloc wrong address" ) ( retval ) 
        ( (void*) GET_FIELD(currentLogEntry, malloc, return_ptr) )
        (unified_log.currentEntryIndex());
      kill(getpid(), SIGSEGV);
    }
    _real_pthread_mutex_unlock(&allocation_lock);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    _real_pthread_mutex_lock(&allocation_lock);
    addNextLogEntry(my_entry);
    retval = _real_malloc(size);
    SET_FIELD2(my_return_entry, malloc, return_ptr, (unsigned long int)retval);
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

#ifdef RECORD_REPLAY
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
    _real_pthread_mutex_lock(&allocation_lock);
    retval = _real_libc_memalign(boundary, size);
    waitForTurn(my_return_entry, &libc_memalign_turn_check);
    if ((unsigned long int)retval !=
        GET_FIELD(currentLogEntry, libc_memalign, return_ptr)) {
      JTRACE ( "tyler" ) ( retval ) 
        ( (void*) GET_FIELD(currentLogEntry, libc_memalign, return_ptr) )
        (unified_log.currentEntryIndex());
      kill(getpid(), SIGSEGV);
    }
    _real_pthread_mutex_unlock(&allocation_lock);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    _real_pthread_mutex_lock(&allocation_lock);
    addNextLogEntry(my_entry);
    retval = _real_libc_memalign(boundary, size);
    SET_FIELD2(my_return_entry, libc_memalign, return_ptr, (unsigned long int)retval);
    addNextLogEntry(my_return_entry);
    _real_pthread_mutex_unlock(&allocation_lock);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

extern "C" void *valloc(size_t size) 
{
  return __libc_memalign(sysconf(_SC_PAGESIZE), size);
}
#endif

extern "C" void free(void *ptr)
{
#ifdef RECORD_REPLAY
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
    log_entry_t my_entry = create_free_entry(my_clone_id, free_event, (unsigned long int)ptr);
    log_entry_t my_return_entry = create_free_entry(my_clone_id, free_event_return, (unsigned long int)ptr);
    if (SYNC_IS_REPLAY) {
      waitForTurn(my_entry, &free_turn_check);
      getNextLogEntry();
      _real_pthread_mutex_lock(&allocation_lock);
      _real_free(ptr);
      _real_pthread_mutex_unlock(&allocation_lock);
      waitForTurn(my_return_entry, &free_turn_check);
      getNextLogEntry();
    } else if (SYNC_IS_LOG) {
      // Not restart; we should be logging.
      _real_pthread_mutex_lock(&allocation_lock);
      addNextLogEntry(my_entry);
      _real_free(ptr);
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
#ifdef RECORD_REPLAY
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
    _real_pthread_mutex_lock(&allocation_lock);
    retval = _real_realloc(ptr, size);
    waitForTurn(my_return_entry, &realloc_turn_check);
    JASSERT ( (unsigned long int)retval ==
        GET_FIELD(currentLogEntry, realloc, return_ptr) ) 
      ( (unsigned long int)retval )
      ( GET_FIELD(currentLogEntry, realloc, return_ptr) );
    _real_pthread_mutex_unlock(&allocation_lock);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    _real_pthread_mutex_lock(&allocation_lock);
    addNextLogEntry(my_entry);
    retval = _real_realloc(ptr, size);
    SET_FIELD2(my_return_entry, realloc, return_ptr, (unsigned long int)retval);
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

#ifdef RECORD_REPLAY
extern "C" void *mmap(void *addr, size_t length, int prot, int flags,
    int fd, off_t offset)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  SET_IN_MMAP_WRAPPER();
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr) && !log_all_allocs) {
    _real_pthread_mutex_lock(&mmap_lock);
    void *retval = _real_mmap (addr, length, prot, flags, fd, offset);
    _real_pthread_mutex_unlock(&mmap_lock);
    UNSET_IN_MMAP_WRAPPER();
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retval;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    _real_pthread_mutex_lock(&mmap_lock);
    void *retval = _real_mmap (addr, length, prot, flags, fd, offset);
    _real_pthread_mutex_unlock(&mmap_lock);
    UNSET_IN_MMAP_WRAPPER();
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retval;
  }
  void *retval;
  log_entry_t my_entry = create_mmap_entry(my_clone_id, mmap_event,
      addr, length, prot, flags, fd, offset);
  log_entry_t my_return_entry = create_mmap_entry(my_clone_id, mmap_event_return, 
      addr, length, prot, flags, fd, offset);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &mmap_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &mmap_turn_check);
    _real_pthread_mutex_lock(&mmap_lock);
    JASSERT ( addr == NULL ).Text("Unimplemented to have non-null addr.");
    addr = (void *)GET_FIELD(currentLogEntry, mmap, retval);
    flags |= MAP_FIXED;
    retval = _real_mmap (addr, length, prot, flags, fd, offset);
    JASSERT ( retval == (void *)GET_FIELD(currentLogEntry, mmap, retval) );
    _real_pthread_mutex_unlock(&mmap_lock);
    if (retval == MAP_FAILED) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    _real_pthread_mutex_lock(&mmap_lock);
    addNextLogEntry(my_entry);
    retval = _real_mmap (addr, length, prot, flags, fd, offset);
    SET_FIELD2(my_return_entry, mmap, retval, (unsigned long int)retval);
    addNextLogEntry(my_return_entry);
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
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr) && !log_all_allocs) {
    _real_pthread_mutex_lock(&mmap_lock);
    void *retval = _real_mmap64 (addr, length, prot, flags, fd, offset);
    _real_pthread_mutex_unlock(&mmap_lock);
    UNSET_IN_MMAP_WRAPPER();
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retval;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    _real_pthread_mutex_lock(&mmap_lock);
    void *retval = _real_mmap64 (addr, length, prot, flags, fd, offset);
    _real_pthread_mutex_unlock(&mmap_lock);
    UNSET_IN_MMAP_WRAPPER();
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retval;
  }
  void *retval;
  log_entry_t my_entry = create_mmap64_entry(my_clone_id, mmap64_event,
      addr, length, prot, flags, fd, offset);
  log_entry_t my_return_entry = create_mmap64_entry(my_clone_id, mmap64_event_return, 
      addr, length, prot, flags, fd, offset);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &mmap64_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &mmap64_turn_check);
    _real_pthread_mutex_lock(&mmap_lock);
    JASSERT ( addr == NULL ).Text("Unimplemented to have non-null addr.");
    addr = (void *)GET_FIELD(currentLogEntry, mmap64, retval);
    flags |= MAP_FIXED;
    retval = _real_mmap64 (addr, length, prot, flags, fd, offset);
    JASSERT ( retval == (void *)GET_FIELD(currentLogEntry, mmap64, retval) );
    _real_pthread_mutex_unlock(&mmap_lock);
    if (retval == MAP_FAILED) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    _real_pthread_mutex_lock(&mmap_lock);
    addNextLogEntry(my_entry);
    retval = _real_mmap64 (addr, length, prot, flags, fd, offset);
    SET_FIELD2(my_return_entry, mmap64, retval, (unsigned long int)retval);
    addNextLogEntry(my_return_entry);
    _real_pthread_mutex_unlock(&mmap_lock);
  }
  UNSET_IN_MMAP_WRAPPER();
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

extern "C" int munmap(void *addr, size_t length)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr) && !log_all_allocs) {
    _real_pthread_mutex_lock(&mmap_lock);
    int retval = _real_munmap (addr, length);
    _real_pthread_mutex_unlock(&mmap_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retval;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    _real_pthread_mutex_lock(&mmap_lock);
    int retval = _real_munmap (addr, length);
    _real_pthread_mutex_unlock(&mmap_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retval;
  }
  int retval;
  log_entry_t my_entry = create_munmap_entry(my_clone_id, munmap_event,
      addr, length);
  log_entry_t my_return_entry = create_munmap_entry(my_clone_id, munmap_event_return, 
      addr, length);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &munmap_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &munmap_turn_check);
    _real_pthread_mutex_lock(&mmap_lock);
    retval = _real_munmap (addr, length);
    JASSERT ( retval == GET_COMMON(currentLogEntry, retval) );
    _real_pthread_mutex_unlock(&mmap_lock);
    if (retval == -1) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    _real_pthread_mutex_lock(&mmap_lock);
    addNextLogEntry(my_entry);
    retval = _real_munmap (addr, length);
    SET_COMMON(my_return_entry, retval);
    addNextLogEntry(my_return_entry);
    _real_pthread_mutex_unlock(&mmap_lock);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

extern "C" void *mremap(void *old_address, size_t old_size,
    size_t new_size, int flags, ...)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  va_list ap;
  va_start( ap, flags );
  void *new_address = va_arg ( ap, void * );
  va_end ( ap );

  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr) && !log_all_allocs) {
    _real_pthread_mutex_lock(&mmap_lock);
    void *retval = _real_mremap (old_address, old_size, new_size, flags,
                                 new_address);
    _real_pthread_mutex_unlock(&mmap_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retval;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    _real_pthread_mutex_lock(&mmap_lock);
    void *retval = _real_mremap (old_address, old_size, new_size, flags,
                                 new_address);
    _real_pthread_mutex_unlock(&mmap_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retval;
  }
  void *retval;
  log_entry_t my_entry = create_mremap_entry(my_clone_id, mremap_event,
      old_address, old_size, new_size, flags);
  log_entry_t my_return_entry = create_mremap_entry(my_clone_id,
      mremap_event_return, old_address, old_size, new_size, flags);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &mremap_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &mremap_turn_check);
    _real_pthread_mutex_lock(&mmap_lock);
    void *addr = (void *)GET_FIELD(currentLogEntry, mremap, retval);
    flags |= (MREMAP_MAYMOVE | MREMAP_FIXED);
    retval = _real_mremap (old_address, old_size, new_size, flags, addr);
    JASSERT ( retval == (void *)GET_FIELD(currentLogEntry, mremap, retval) );
    _real_pthread_mutex_unlock(&mmap_lock);
    if (retval == MAP_FAILED) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    _real_pthread_mutex_lock(&mmap_lock);
    addNextLogEntry(my_entry);
    retval = _real_mremap (old_address, old_size, new_size, flags, new_address);
    SET_FIELD2(my_return_entry, mremap, retval, (unsigned long int)retval);
    addNextLogEntry(my_return_entry);
    _real_pthread_mutex_unlock(&mmap_lock);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

/*
extern "C" void *mmap2(void *addr, size_t length, int prot,
    int flags, int fd, off_t pgoffset)
{

}
*/
#endif

#endif // ENABLE_MALLOC_WRAPPER
