/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

// TODO: Better way to do this. I think it was only a problem on dekaksi.
// Remove this, and see the compile error.
#define read _libc_read
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include  "../jalib/jassert.h"
#include  "../jalib/jfilesystem.h"
#include  "../jalib/jtimer.h"
#include "dmtcpmessagetypes.h"
#include "dmtcpworker.h"
#include "protectedfds.h"
#include "synchronizationlogging.h"
#include "syscallwrappers.h"
#include "virtualpidtable.h"
#undef read

#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
static void *thread_reaper(void *arg);
static pthread_t reaperThread;
static int reaper_thread_alive = 0;
static int signal_thread_alive = 0;
static volatile int reaper_thread_ready = 0;
static volatile int thread_create_destroy = 0;
static volatile pthread_t attributes_were_read = 0;
static volatile pthread_t arguments_were_decoded = 0;
static pthread_mutex_t attributes_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t arguments_decode_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t pthread_create_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t create_destroy_guard = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t read_mutex = PTHREAD_MUTEX_INITIALIZER;
static inline void memfence() {  asm volatile ("mfence" ::: "memory"); }
struct create_arg
{
  void *(*fn)(void *);
  void *thread_arg;
};
static int internal_pthread_mutex_lock(pthread_mutex_t *);
static int internal_pthread_mutex_unlock(pthread_mutex_t *);

/* Yanking this from mtcpinterface.cpp. It is needed by reapThread() to set up
   the pointer to the mtcp function delete_thread_on_pthread_join(). */
namespace
{
  static const char* REOPEN_MTCP = ( char* ) 0x1;

  static void* find_and_open_mtcp_so()
  {
    dmtcp::string mtcpso = jalib::Filesystem::FindHelperUtility ( "libmtcp.so" );
    void* handle = dlopen ( mtcpso.c_str(), RTLD_NOW );
    JASSERT ( handle != NULL ) ( mtcpso ).Text ( "failed to load libmtcp.so" );
    return handle;
  }

}

/* Had to rename it to avoid collision with the one defined in
   mtcpinterface.cpp */
extern "C" void* _my_get_mtcp_symbol ( const char* name )
{
  static void* theMtcpHandle = find_and_open_mtcp_so();
  if ( name == REOPEN_MTCP )
  {
    JTRACE ( "reopening libmtcp.so" ) ( theMtcpHandle );
    //must get ref count down to 0 so it is really unloaded
    for( int i=0; i<MAX_DLCLOSE_MTCP_CALLS; ++i){
      if(dlclose(theMtcpHandle) != 0){
        //failed call means it is unloaded
        JTRACE("dlclose(libmtcp.so) worked");
        break;
      }else{
        JTRACE("dlclose(libmtcp.so) decremented refcount");
      }
    }
    theMtcpHandle = find_and_open_mtcp_so();
    JTRACE ( "reopening libmtcp.so DONE" ) ( theMtcpHandle );
    return 0;
  }
  void* tmp = dlsym ( theMtcpHandle, name );
  JASSERT ( tmp != NULL ) ( name ).Text ( "failed to find libmtcp.so symbol" );
  //JTRACE("looking up libmtcp.so symbol")(name);
  return tmp;
}

#define ACQUIRE_THREAD_CREATE_DESTROY_LOCK() \
  int ready = 0;                                                \
  while (1) {                                                   \
    internal_pthread_mutex_lock(&create_destroy_guard);         \
    memfence();                                                 \
    if (thread_create_destroy == 0) {                           \
      ready = 1;                                                \
      thread_create_destroy = 1;                                \
    }                                                           \
    internal_pthread_mutex_unlock(&create_destroy_guard);       \
    if (ready) break;                                           \
    usleep(100);                                                \
  }

#define RELEASE_THREAD_CREATE_DESTROY_LOCK() \
  internal_pthread_mutex_lock(&create_destroy_guard);   \
  JASSERT ( thread_create_destroy == 1 );               \
  thread_create_destroy = 0;                            \
  internal_pthread_mutex_unlock(&create_destroy_guard); \

static void *start_wrapper(void *arg)
{
  /*
   This start function calls the user's start function. We need this so that we
   gain control immediately after the user's start function terminates, but
   before control goes back to libpthread. Libpthread will do some cleanup
   involving a free() call and some low level locks. Since we can't control the
   low level locks, we must implement our own lock: thread_transition_mutex.
  */
  struct create_arg *createArg = (struct create_arg *)arg;
  void *(*user_fnc) (void *) = createArg->fn;
  void *thread_arg = createArg->thread_arg;
  _real_pthread_mutex_lock(&arguments_decode_mutex);
  arguments_were_decoded = 1;
  _real_pthread_mutex_unlock(&arguments_decode_mutex);
  void *retval;
  retval = (*user_fnc)(thread_arg);
  JTRACE ( "User start function over." );
  ACQUIRE_THREAD_CREATE_DESTROY_LOCK(); // For thread destruction.
  reapThisThread();
  return retval;
}

/* 
   Create a thread stack via mmap() if one is not specified in the user
   attributes.
   
   Parameters:
   attr_out - (output) The final attributes caller should use.
   user_attr - User provided attributes; defer to these.
   dest - If non-NULL, force new stack to this location.
   size - If non-0, force new stack to this size.
*/
static void setupThreadStack(pthread_attr_t *attr_out, 
    const pthread_attr_t *user_attr, void *dest, size_t size)
{
  size_t stack_size;
  void *stack_addr;
  struct rlimit rl;
  int userStack = 0;
  // If the user's attributes have specified a stack size, use that.
  if (user_attr != NULL) {
    pthread_attr_getstack(user_attr, &stack_addr, &stack_size);
    if (stack_size != 0)
      userStack = 1;
    // Copy the user's attributes:
    *attr_out = *user_attr;
  }
  void *mmap_addr = dest == NULL ? NULL : dest;
  int mmap_flags  = dest == NULL ? MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK
    : MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK | MAP_FIXED;
  size_t mmap_size;
  if (userStack)
    mmap_size = stack_size;
  else
    mmap_size = size == 0 ? default_stack_size : size;
  void *s = mmap(mmap_addr, mmap_size, PROT_READ | PROT_WRITE,
      mmap_flags, -1, 0);
  if (s == MAP_FAILED)  {
    JTRACE ( "Failed to map thread stack." ) ( mmap_addr ) ( mmap_size )
      ( strerror(errno) ) ( log_entry_index );
    JASSERT ( false );
  }
  pthread_attr_setstack(attr_out, s, mmap_size);
}

static void teardownThreadStack(void *stack_addr, size_t stack_size)
{
  if (munmap(stack_addr, stack_size) == -1) {
    JASSERT ( false ) ( strerror(errno) ) ( stack_addr ) ( stack_size )
      .Text("Unable to munmap user thread stack.");
  }
}

/* Disable the pthread_CREATE_DETACHED flag if present in the given
   attributes. Returns the modified attributes. */
static void disableDetachState(pthread_attr_t *attr)
{
  // The opposite and only alternative to PTHREAD_CREATE_DETACHED is
  // PTHREAD_CREATE_JOINABLE.
  pthread_attr_setdetachstate(attr, PTHREAD_CREATE_JOINABLE);
}

/* Begin wrapper code */

/* Performs the _real version with log and replay. Does NOT check
   shouldSynchronize() and shouldn't be called directly unless you know what
   you're doing. */
static int internal_pthread_mutex_lock(pthread_mutex_t *mutex)
{
  int retval = 0;
  log_entry_t my_entry = create_pthread_mutex_lock_entry(my_clone_id, pthread_mutex_lock_event, 
      (unsigned long int)mutex);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &pthread_mutex_lock_turn_check);
    retval = _real_pthread_mutex_lock(mutex);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_pthread_mutex_lock(mutex);
  }
  return retval;
}

/* Performs the _real version with log and replay. Does NOT check
   shouldSynchronize() and shouldn't be called directly unless you know what
   you're doing. */
static int internal_pthread_mutex_unlock(pthread_mutex_t *mutex)
{
  int retval = 0;
  log_entry_t my_entry = create_pthread_mutex_unlock_entry(my_clone_id,
      pthread_mutex_unlock_event, (unsigned long int)mutex);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &pthread_mutex_unlock_turn_check);
    retval = _real_pthread_mutex_unlock(mutex);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_pthread_mutex_unlock(mutex);
  }
  return retval;
}

/* Performs the _real version with log and replay. Does NOT check
   shouldSynchronize() and shouldn't be called directly unless you know what
   you're doing. */
static int internal_pthread_cond_signal(pthread_cond_t *cond)
{
  int retval = 0;
  log_entry_t my_entry = create_pthread_cond_signal_entry(my_clone_id,
      pthread_cond_signal_event, (unsigned long int)cond);
  log_entry_t my_return_entry = create_pthread_cond_signal_entry(my_clone_id,
      pthread_cond_signal_event_return, (unsigned long int)cond);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &pthread_cond_signal_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &pthread_cond_signal_turn_check);
    retval = GET_COMMON(currentLogEntry,retval);
    getNextLogEntry();    
  } else  if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_pthread_cond_signal(cond);
    SET_COMMON(my_return_entry, retval);
    addNextLogEntry(my_return_entry);
  }
  return retval;
}

/* Performs the _real version with log and replay. Does NOT check
   shouldSynchronize() and shouldn't be called directly unless you know what
   you're doing. */
static int internal_pthread_cond_wait(pthread_cond_t *cond,
    pthread_mutex_t *mutex)
{
  int retval = 0;
  log_entry_t my_entry = create_pthread_cond_wait_entry(my_clone_id,
      pthread_cond_wait_event, 
      (unsigned long int)mutex, (unsigned long int)cond);
  log_entry_t my_return_entry = create_pthread_cond_wait_entry(my_clone_id,
      pthread_cond_wait_event_return, 
      (unsigned long int)mutex, (unsigned long int)cond);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &pthread_cond_wait_turn_check);
    _real_pthread_mutex_unlock(mutex);
    getNextLogEntry();
    waitForTurn(my_return_entry, &pthread_cond_wait_turn_check);
    retval = GET_COMMON(currentLogEntry,retval);
    _real_pthread_mutex_lock(mutex);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_pthread_cond_wait(cond, mutex);
    SET_COMMON(my_return_entry, retval);
    addNextLogEntry(my_return_entry);
  }
  return retval;
}

/* Performs the _real version with log and replay. Does NOT check
   shouldSynchronize() and shouldn't be called directly unless you know what
   you're doing. */
static int internal_pthread_create(pthread_t *thread,
    const pthread_attr_t *attr, void *(*start_routine)(void*), void *arg)
{
  int retval = 0;
  pthread_attr_t the_attr;
  size_t stack_size;
  void *stack_addr;
  struct create_arg createArg;
  createArg.fn = start_routine;
  createArg.thread_arg = arg;
  log_entry_t my_entry = create_pthread_create_entry(my_clone_id, 
      pthread_create_event, (unsigned long int)thread, 
      (unsigned long int)attr, (unsigned long int)start_routine,
      (unsigned long int)arg);
  log_entry_t my_return_entry = create_pthread_create_entry(my_clone_id, 
      pthread_create_event_return, (unsigned long int)thread,
      (unsigned long int)attr, (unsigned long int)start_routine,
      (unsigned long int)arg);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &pthread_create_turn_check);
    stack_addr = (void *)GET_FIELD(currentLogEntry, pthread_create, stack_addr);
    stack_size = GET_FIELD(currentLogEntry, pthread_create, stack_size);
    getNextLogEntry();
    ACQUIRE_THREAD_CREATE_DESTROY_LOCK();
    // Set up thread stacks to how they were at record time.
    pthread_attr_init(&the_attr);
    setupThreadStack(&the_attr, attr, stack_addr, stack_size);
    // Never let the user create a detached thread:
    disableDetachState(&the_attr);
    retval = _real_pthread_create(thread, &the_attr, 
                                  start_wrapper, (void *)&createArg);
    /* Wait for the newly created thread to decode his arguments (createArg).
       We must ensure that's been done before we return from this
       pthread_create wrapper and the createArg struct goes out of scope. */
    while (1) {
      _real_pthread_mutex_lock(&arguments_decode_mutex);
      if (arguments_were_decoded == 1) { 
        arguments_were_decoded = 0;
        _real_pthread_mutex_unlock(&arguments_decode_mutex);
        break;
      }
      _real_pthread_mutex_unlock(&arguments_decode_mutex);
      usleep(100);
    }
    RELEASE_THREAD_CREATE_DESTROY_LOCK();
    pthread_attr_destroy(&the_attr);
    waitForTurn(my_return_entry, &pthread_create_turn_check);
    getNextLogEntry();
  } else  if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    ACQUIRE_THREAD_CREATE_DESTROY_LOCK();
    pthread_attr_init(&the_attr);
    // start_wrapper() will unlock the mutex when it is done setup:
    // Possibly create a thread stack if the user has not provided one:
    setupThreadStack(&the_attr, attr, NULL, 0);
    // Never let the user create a detached thread:
    disableDetachState(&the_attr);
    retval = _real_pthread_create(thread, &the_attr,
                                  start_wrapper, (void *)&createArg);
    /* Wait for the newly created thread to decode his arguments (createArg).
       We must ensure that's been done before we return from this
       pthread_create wrapper and the createArg struct goes out of scope. */
    while (1) {
      _real_pthread_mutex_lock(&arguments_decode_mutex);
      if (arguments_were_decoded == 1) { 
        arguments_were_decoded = 0;
        _real_pthread_mutex_unlock(&arguments_decode_mutex);
        break;
      }
      _real_pthread_mutex_unlock(&arguments_decode_mutex);
      usleep(100);
    }
    RELEASE_THREAD_CREATE_DESTROY_LOCK();
    // Log whatever stack we ended up using:
    pthread_attr_getstack(&the_attr, &stack_addr, &stack_size);
    pthread_attr_destroy(&the_attr);
    SET_FIELD2(my_return_entry, pthread_create, stack_addr,
              (unsigned long int)stack_addr);
    SET_FIELD(my_return_entry, pthread_create, stack_size);
    addNextLogEntry(my_return_entry);
  }
  return retval;
}

extern "C" int pthread_mutex_lock(pthread_mutex_t *mutex)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    int retval = _real_pthread_mutex_lock(mutex);
    return retval;
  }
  /* NOTE: Don't call JTRACE (or anything that calls JTRACE) before
    this point. */
  int retval = internal_pthread_mutex_lock(mutex);
  return retval;
}

extern "C" int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    int retval = _real_pthread_mutex_trylock(mutex);
    return retval;
  }
  /* NOTE: Don't call JTRACE (or anything that calls JTRACE) before
    this point. */
  int retval = 0;
  log_entry_t my_entry = create_pthread_mutex_trylock_entry(my_clone_id, 
      pthread_mutex_trylock_event, (unsigned long int)mutex);
  log_entry_t my_return_entry = create_pthread_mutex_trylock_entry(my_clone_id, 
      pthread_mutex_trylock_event_return, (unsigned long int)mutex);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &pthread_mutex_trylock_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &pthread_mutex_trylock_turn_check);
    retval = _real_pthread_mutex_trylock(mutex);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_pthread_mutex_trylock(mutex);
    addNextLogEntry(my_return_entry);
  }
  return retval;
}

extern "C" int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    int retval = _real_pthread_mutex_unlock(mutex);
    return retval;
  }
  /* NOTE: Don't call JTRACE (or anything that calls JTRACE) before
    this point. */
  int retval = internal_pthread_mutex_unlock(mutex);
  return retval;
}

extern "C" int pthread_cond_signal(pthread_cond_t *cond)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    int retval = _real_pthread_cond_signal(cond);
    return retval;
  }
  int retval = internal_pthread_cond_signal(cond);
  return retval;
}

extern "C" int pthread_cond_broadcast(pthread_cond_t *cond)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    int retval = _real_pthread_cond_broadcast(cond);
    return retval;
  }
  int retval = 0;
  log_entry_t my_entry = create_pthread_cond_broadcast_entry(my_clone_id,
    pthread_cond_broadcast_event,
    (unsigned long int)cond);
  log_entry_t my_return_entry = create_pthread_cond_broadcast_entry(my_clone_id,
    pthread_cond_broadcast_event_return,
    (unsigned long int)cond);
  /* Hack for MySQL (or any program which does a lot of cond_broadcasts.
     Without this, the annotation algorithm for detecting anomalous broadcasts
     is unbearably slow for large logs. The hack is to call every broadcast
     anomalous until we can fix the annotation algorithm. */
  /*log_entry_t my_entry = create_pthread_cond_broadcast_entry(my_clone_id,
      pthread_cond_broadcast_anomalous_event, 
      (unsigned long int)cond);*/
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &pthread_cond_broadcast_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &pthread_cond_broadcast_turn_check);
    retval = GET_COMMON(currentLogEntry,retval);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_pthread_cond_broadcast(cond);
    SET_COMMON(my_return_entry, retval);
    addNextLogEntry(my_return_entry);
  }

  return retval;
}

extern "C" int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    int retval = _real_pthread_cond_wait(cond, mutex);
    return retval;
  }
  int retval = internal_pthread_cond_wait(cond, mutex);
  return retval;
}

extern "C" int pthread_cond_timedwait(pthread_cond_t *cond,
    pthread_mutex_t *mutex, const struct timespec *abstime)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    int retval = _real_pthread_cond_timedwait(cond, mutex, abstime);
    return retval;
  }
  int retval = 0;
  log_entry_t my_entry = create_pthread_cond_timedwait_entry(my_clone_id, 
      pthread_cond_timedwait_event, (unsigned long int)mutex,
      (unsigned long int)cond, (unsigned long int)abstime);
  log_entry_t my_return_entry = create_pthread_cond_timedwait_entry(my_clone_id,
      pthread_cond_timedwait_event_return, (unsigned long int)mutex,
      (unsigned long int)cond, (unsigned long int)abstime);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &pthread_cond_timedwait_turn_check);
    _real_pthread_mutex_unlock(mutex);
    getNextLogEntry();
    waitForTurn(my_return_entry, &pthread_cond_timedwait_turn_check);
    retval = GET_COMMON(currentLogEntry,retval);
    _real_pthread_mutex_lock(mutex);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_pthread_cond_timedwait(cond, mutex, abstime);
    SET_COMMON(my_return_entry, retval);
    // cond_timedwait does not set errno; on error, it returns an error #
    addNextLogEntry(my_return_entry);
  }
  return retval;
}


extern "C" int pthread_rwlock_unlock(pthread_rwlock_t *rwlock)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    int retval = _real_pthread_rwlock_unlock(rwlock);
    return retval;
  }
  int retval = 0;
  log_entry_t my_entry = create_pthread_rwlock_unlock_entry(my_clone_id,
      pthread_rwlock_unlock_event,
      (unsigned long int)rwlock);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &pthread_rwlock_unlock_turn_check);
    retval = _real_pthread_rwlock_unlock(rwlock);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_pthread_rwlock_unlock(rwlock);
  }
  return retval;
}

extern "C" int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    int retval = _real_pthread_rwlock_rdlock(rwlock);
    return retval;
  }
  int retval = 0;
  log_entry_t my_entry = create_pthread_rwlock_rdlock_entry(my_clone_id,
      pthread_rwlock_rdlock_event,
      (unsigned long int)rwlock);
  log_entry_t my_return_entry = create_pthread_rwlock_rdlock_entry(my_clone_id,
      pthread_rwlock_rdlock_event_return,
      (unsigned long int)rwlock);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &pthread_rwlock_rdlock_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &pthread_rwlock_rdlock_turn_check);
    retval = _real_pthread_rwlock_rdlock(rwlock);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_pthread_rwlock_rdlock(rwlock);
    addNextLogEntry(my_return_entry);
  }
  return retval;
}

extern "C" int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    int retval = _real_pthread_rwlock_wrlock(rwlock);
    return retval;
  }
  int retval = 0;
  log_entry_t my_entry = create_pthread_rwlock_wrlock_entry(my_clone_id,
      pthread_rwlock_wrlock_event,
      (unsigned long int)rwlock);
  log_entry_t my_return_entry = create_pthread_rwlock_wrlock_entry(my_clone_id,
      pthread_rwlock_wrlock_event_return,
      (unsigned long int)rwlock);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &pthread_rwlock_wrlock_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &pthread_rwlock_wrlock_turn_check);
    retval = _real_pthread_rwlock_wrlock(rwlock);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_pthread_rwlock_wrlock(rwlock);
    addNextLogEntry(my_return_entry);
  }
  return retval;
}

/* Function to perform cleanup tasks for a user thread exit. 
   Caller is responsible for acquiring reap_mutex. */
static void reapThread()
{
  typedef void ( *delete_thread_fnc_t ) ( pthread_t );
  static delete_thread_fnc_t delete_thread_fnc =
    (delete_thread_fnc_t) _my_get_mtcp_symbol("delete_thread_on_pthread_join");

  pthread_attr_t attr;
  pthread_join_retval_t join_retval;
  void *value_ptr = NULL;
  size_t stack_size;
  void *stack_addr;
  int retval = 0;
  pthread_getattr_np(thread_to_reap, &attr); // calls realloc().
  pthread_attr_getstack(&attr, &stack_addr, &stack_size);
  pthread_attr_destroy(&attr);
  _real_pthread_mutex_lock(&attributes_mutex);
  JASSERT ( attributes_were_read == 0 );
  attributes_were_read = thread_to_reap;
  _real_pthread_mutex_unlock(&attributes_mutex);
  retval = _real_pthread_join(thread_to_reap, &value_ptr);
  //_real_pthread_join(thread_to_reap, NULL);
  join_retval.my_errno = errno;
  join_retval.retval = retval;
  join_retval.value_ptr = value_ptr;
  pthread_join_retvals[thread_to_reap] = join_retval;
  teardownThreadStack(stack_addr, stack_size);
  delete_thread_fnc ( thread_to_reap );
  RELEASE_THREAD_CREATE_DESTROY_LOCK(); // End of thread destruction.
}

/* Thread to handle cleanup tasks associated with a user thread exiting.  Note
   we are not using the _real_ versions of pthread calls -- we want to
   synchronize these. */
static void *thread_reaper(void *arg)
{
  while (1) {
    /* Wait until there is a thread that needs to be reaped.  We call the
    internal_* versions here because we want them to be logged/replayed, but we
    want to skip the shouldSynchronize() function. That function will refuse to
    log/replay these because they are not coming from user code. */
    internal_pthread_mutex_lock(&reap_mutex);
    reaper_thread_ready = 1;
    internal_pthread_cond_wait(&reap_cv, &reap_mutex);
    reaper_thread_ready = 0;
    reapThread();
    internal_pthread_mutex_unlock(&reap_mutex);
  }
}

LIB_PRIVATE void reapThisThread()
{
  /*
    Called from two places:
     - pthread_exit() wrapper
     - end of start_wrapper() (which calls user's start function).

    We call the internal_* versions here because we want them to be
    logged/replayed, but we want to skip the shouldSynchronize() function. That
    function will refuse to log/replay these because they are not coming from
    user code.
  */
  // Make sure reaper thread has called cond_wait() before we signal:
  while (!reaper_thread_ready) usleep(100);
  internal_pthread_mutex_lock(&reap_mutex);
  thread_to_reap = pthread_self();
  internal_pthread_cond_signal(&reap_cv);
  internal_pthread_mutex_unlock(&reap_mutex);
  // Wait for reaper thread to read the thread attributes before we return,
  // letting the thread terminate.
  while (1) {
    _real_pthread_mutex_lock(&attributes_mutex);
    if (attributes_were_read == pthread_self()) break;
    _real_pthread_mutex_unlock(&attributes_mutex);
    usleep(100);
  }
  attributes_were_read = 0;
  _real_pthread_mutex_unlock(&attributes_mutex);
}

extern "C" int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
    void *(*start_routine)(void*), void *arg)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    int retval = _real_pthread_create(thread, attr, start_routine, arg);
    _real_pthread_mutex_unlock(&pthread_create_mutex);
    return retval;
  }
  if (__builtin_expect(reaper_thread_alive == 0, 0)) {
    // Create the reaper thread
    reaper_thread_alive = 1;
    internal_pthread_create(&reaperThread, NULL, thread_reaper, NULL);
  }
  int retval = internal_pthread_create(thread, attr, start_routine, arg);
  return retval;
}

extern "C" void pthread_exit(void *value_ptr)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    _real_pthread_exit(value_ptr);
  } else {
    log_entry_t my_entry = create_pthread_exit_entry(my_clone_id, 
        pthread_exit_event, (unsigned long int)value_ptr);

    if (SYNC_IS_REPLAY) {
      waitForTurn(my_entry, &pthread_exit_turn_check);
      getNextLogEntry();
      ACQUIRE_THREAD_CREATE_DESTROY_LOCK();
      reapThisThread();
      _real_pthread_exit(value_ptr);
    } else  if (SYNC_IS_LOG) {
      // Not restart; we should be logging.
      addNextLogEntry(my_entry);
      ACQUIRE_THREAD_CREATE_DESTROY_LOCK();
      reapThisThread();
      _real_pthread_exit(value_ptr);
    }
  }
}

extern "C" int pthread_detach(pthread_t thread)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    int retval = _real_pthread_detach(thread);
    return retval;
  }
  int retval = 0;
  log_entry_t my_entry = create_pthread_detach_entry(my_clone_id, pthread_detach_event,
      (unsigned long int)thread);
  log_entry_t my_return_entry = create_pthread_detach_entry(my_clone_id, pthread_detach_event_return,
      (unsigned long int)thread);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &pthread_detach_turn_check);
    getNextLogEntry();
    retval = 0;
    waitForTurn(my_return_entry, &pthread_detach_turn_check);
    getNextLogEntry();
  } else  if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = 0;
    addNextLogEntry(my_return_entry);
  }
  return retval;
}

static void *signal_thread(void *arg)
{
  int signal_sent_on = 0;
  while (1) {
    // Lock this so it doesn't change from underneath:
    _real_pthread_mutex_lock(&log_index_mutex);
    if (__builtin_expect(GET_COMMON(currentLogEntry,event) == signal_handler_event, 0)) {
      if (signal_sent_on != log_entry_index) {
        // Only send one signal per sig_handler entry.
        signal_sent_on = log_entry_index;
        _real_pthread_kill(clone_id_to_tid_table[GET_COMMON(currentLogEntry,clone_id)],
            GET_FIELD(currentLogEntry, signal_handler, sig));
      }
    }
    _real_pthread_mutex_unlock(&log_index_mutex);
    usleep(20);
  }
}

static void createSignalThread()
{
  pthread_t t;
  internal_pthread_create(&t, NULL, signal_thread, NULL);
}

extern "C" int pthread_kill(pthread_t thread, int sig)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    int retval = _real_pthread_kill(thread, sig);
    return retval;
  }
  if (__builtin_expect(signal_thread_alive == 0, 0)) {
    // Start the thread who will send signals (only on replay, but we need to
    // start it here so record has same behavior).
    signal_thread_alive = 1;
    createSignalThread();
  }
  int retval = 0;
  log_entry_t my_entry = create_pthread_kill_entry(my_clone_id, pthread_kill_event,
      (unsigned long int)thread, sig);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &pthread_kill_turn_check);
    getNextLogEntry();
    // TODO: Do something better than always returning success.
    retval = 0;//_real_pthread_kill(thread, sig);
  } else  if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_pthread_kill(thread, sig);
  }
  return retval;
}

extern "C" int select(int nfds, fd_set *readfds, fd_set *writefds, 
    fd_set *exceptfds, struct timeval *timeout)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    return _real_select(nfds, readfds, writefds, exceptfds, timeout);
  }
  int retval = 0;
  log_entry_t my_entry = create_select_entry(my_clone_id, select_event, 
      (unsigned long int)nfds, readfds, writefds, 
      (unsigned long int)exceptfds, (unsigned long int)timeout);
  log_entry_t my_return_entry = create_select_entry(my_clone_id,
      select_event_return, 
      (unsigned long int)nfds, readfds, writefds, 
      (unsigned long int)exceptfds, (unsigned long int)timeout);

  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &select_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &select_turn_check);
    copyFdSet(&GET_FIELD(currentLogEntry, select, readfds), readfds);
    copyFdSet(&GET_FIELD(currentLogEntry, select, writefds), writefds);
    retval = GET_COMMON(currentLogEntry, retval);
    if (retval == -1) {
      // Set retval and errno as they were, and return to user.
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_select(nfds, readfds, writefds, exceptfds, timeout);
    SET_COMMON(my_return_entry, retval);
    if (retval == -1) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    }
    // Note that we're logging the *changed* fd set, so on replay we can
    // just read that from the log, load it into user's location and return.
    copyFdSet(readfds, &GET_FIELD(my_return_entry, select, readfds));
    copyFdSet(writefds, &GET_FIELD(my_return_entry, select, writefds));
    addNextLogEntry(my_return_entry);
  }
  return retval;
}

extern "C" int read(int fd, void *buf, size_t count)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr) || dmtcp::ProtectedFDs::isProtected(fd)) {
    int retval = _real_read(fd, buf, count);
    return retval;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    // Don't log gdb's read calls (e.g. user commands)
    int retval = _real_read(fd, buf, count);
    return retval;
  }
  int retval = 0;
  log_entry_t my_entry = create_read_entry(my_clone_id, read_event, fd, 
      (unsigned long int)buf, count);
  log_entry_t my_data_entry = create_read_entry(my_clone_id, read_event_return, fd, 
      (unsigned long int)buf, count);

  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &read_turn_check);
    getNextLogEntry();
    // NOTE: We never actually call the user's _real_read. We don't
    // need to. We wait for the next event in the log that is the
    // READ_data_event, read from the read data log, and return the
    // corresponding value.
    waitForTurn(my_data_entry, &read_turn_check);
    if (__builtin_expect(read_data_fd == -1, 0)) {
      read_data_fd = open(SYNCHRONIZATION_READ_DATA_LOG_PATH, O_RDONLY);
    }
    JASSERT ( read_data_fd != -1 );
    lseek(read_data_fd, GET_FIELD(currentLogEntry,read,data_offset), SEEK_SET);
    // Only read however much was logged as the return value.
    if (GET_COMMON(currentLogEntry, retval) != -1) {
      readAll(read_data_fd, (char *)buf, GET_COMMON(currentLogEntry, retval));
    }
    // Set the errno to what was logged (e.g. EINTR).
    if (GET_COMMON(currentLogEntry, my_errno) != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    retval = GET_COMMON(currentLogEntry, retval);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    // Note we don't call readAll here. It should be the responsibility of
    // the user code to handle EINTR if needed.
    retval = _real_read(fd, buf, count);
    SET_COMMON(my_data_entry, retval);
    _real_pthread_mutex_lock(&read_data_mutex);
    if (retval == -1) {
      SET_COMMON2(my_data_entry, my_errno, errno);
    } else {
      SET_FIELD2(my_data_entry, read, data_offset, read_log_pos);
      logReadData(buf, retval);
    }
    _real_pthread_mutex_unlock(&read_data_mutex);
    addNextLogEntry(my_data_entry);
    // Be sure to not cover up the error with any intermediate calls
    // (like logReadData)
    if (retval == -1) errno = GET_COMMON(my_data_entry,my_errno);
  }
  return retval;
}

extern "C" ssize_t write(int fd, const void *buf, size_t count)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr) || dmtcp::ProtectedFDs::isProtected(fd)) {
    return _real_write(fd, buf, count);
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    return _real_write(fd, buf, count);
  }
  int retval = 0;
  log_entry_t my_entry = create_write_entry(my_clone_id, write_event, fd, 
      (unsigned long int)buf, count);
  log_entry_t my_return_entry = create_write_entry(my_clone_id, write_event_return, fd, 
      (unsigned long int)buf, count);

  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &write_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &write_turn_check);
#if 0
    if (GET_COMMON(currentLogEntry, retval) != -1) {
      // Write only # that was logged as return val.
      ssize_t cur_retval = writeAll(fd, buf, GET_COMMON(currentLogEntry, retval));
      if (cur_retval == -1) {
        if (errno == EBADF) {
          // If we weren't able to write, but on record we were, assume this is
          // a file descriptor that no longer exists on replay (e.g. an external
          // request from a socket in MySQL).
          // In that case, we just set the return val and errno to what they were
          // on record, and return to the user anyway.
          // (fall through to outside if block)
        } else {
          // We failed on replay with a different error.
          JASSERT ( false ) ( strerror(errno) )
            .Text("Unable to replay user's write() request.");
        }
      }
    }
#endif
    // Set the errno to what was logged (e.g. EINTR).
    if (GET_COMMON(currentLogEntry, my_errno) != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    retval = GET_COMMON(currentLogEntry, retval);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    // Note we don't call writeAll here. It should be the responsibility of
    // the user code to handle EINTR if needed.
    retval = _real_write(fd, buf, count);
    SET_COMMON(my_return_entry, retval);
    if (retval == -1) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    }
    addNextLogEntry(my_return_entry);
  }
  return retval;
}

extern "C" int rand(void)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    return _real_rand();
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    return _real_rand();
  }
  int retval = 0;
  log_entry_t my_entry = create_rand_entry(my_clone_id, rand_event);
  log_entry_t my_return_entry = create_rand_entry(my_clone_id, rand_event_return);

  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &rand_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &rand_turn_check);
    retval = GET_COMMON(currentLogEntry, retval);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_rand();
    SET_COMMON(my_return_entry, retval);
    addNextLogEntry(my_return_entry);
  }
  return retval;
}

// TODO: We can remove log/replay from srand() once we are confident that we
// have captured all random events used to provide the seed (e.g. time()).
extern "C" void srand(unsigned int seed)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    return _real_srand(seed);
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    return _real_srand(seed);
  }
  int retval = 0;
  log_entry_t my_entry = create_srand_entry(my_clone_id, srand_event, seed);
  log_entry_t my_return_entry = create_srand_entry(my_clone_id, srand_event_return, seed);

  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &srand_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &srand_turn_check);
    JASSERT ( seed == GET_FIELD(currentLogEntry, srand, seed) );
    _real_srand(seed);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    _real_srand(seed);
    addNextLogEntry(my_return_entry);
  }
}

extern "C" time_t time(time_t *tloc)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    return _real_time(tloc);
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    return _real_time(tloc);
  }
  time_t retval = 0;
  log_entry_t my_entry = create_time_entry(my_clone_id, time_event,
      (unsigned long int)tloc);
  log_entry_t my_return_entry = create_time_entry(my_clone_id, time_event_return,
      (unsigned long int)tloc);
  /*
  static int fd = -1;
  if (fd == -1) {
    fd = _real_open("/home/tyler/times", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
  }
  void *buffer[10];
  int nptrs;
  nptrs = backtrace (buffer, 10);
  char msg[128];
  sprintf(msg, "log_entry_index: %d\n", log_entry_index);
  write(fd, msg, strlen(msg));
  backtrace_symbols_fd ( buffer, nptrs, fd );
  */
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &time_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &time_turn_check);
    retval = GET_FIELD(currentLogEntry, time, time_retval);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_time(tloc);
    SET_FIELD2(my_return_entry, time, time_retval, retval);
    addNextLogEntry(my_return_entry);
  }
  return retval;
}

extern "C" int access(const char *pathname, int mode)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    return _real_access(pathname, mode);
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    return _real_access(pathname, mode);
  }
  int retval = 0;
  log_entry_t my_entry = create_access_entry(my_clone_id, access_event,
      (unsigned long int)pathname, mode);
  log_entry_t my_return_entry = create_access_entry(my_clone_id, access_event_return,
      (unsigned long int)pathname, mode);

  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &access_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &access_turn_check);
    retval = GET_COMMON(currentLogEntry, retval);
    if (retval != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_access(pathname, mode);
    SET_COMMON(my_return_entry, retval);
    if (retval != 0) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    }
    addNextLogEntry(my_return_entry);
  }
  return retval;
}

/*extern "C" int dup2(int oldfd, int newfd)
{
// TODO
}*/

extern "C" int dup(int oldfd)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    return _real_dup(oldfd);
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    return _real_dup(oldfd);
  }
  int retval = 0;
  log_entry_t my_entry = create_dup_entry(my_clone_id, dup_event, oldfd);
  log_entry_t my_return_entry = create_dup_entry(my_clone_id, dup_event_return,
      oldfd);

  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &dup_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &dup_turn_check);
    //retval = _real_dup(oldfd);
    retval = GET_COMMON(currentLogEntry, retval);
    if (retval != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_dup(oldfd);
    SET_COMMON(my_return_entry, retval);
    if (retval != 0) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    }
    addNextLogEntry(my_return_entry);
  }
  return retval;
}

extern "C" off_t lseek(int fd, off_t offset, int whence)
{
  BASIC_SYNC_WRAPPER(off_t, lseek, _real_lseek, fd, offset, whence);
}

extern "C" int unlink(const char *pathname)
{
  BASIC_SYNC_WRAPPER(int, unlink, _real_unlink, pathname);
}

extern "C" ssize_t pread(int fd, void *buf, size_t count, off_t offset)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr) || dmtcp::ProtectedFDs::isProtected(fd)) {
    int retval = _real_pread(fd, buf, count, offset);
    return retval;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    // Don't log gdb's pread calls (e.g. user commands)
    int retval = _real_pread(fd, buf, count, offset);
    return retval;
  }
  int retval = 0;
  log_entry_t my_entry = create_pread_entry(my_clone_id, pread_event, fd, 
      (unsigned long int)buf, count, offset);
  log_entry_t my_return_entry = create_pread_entry(my_clone_id,
      pread_event_return, fd, (unsigned long int)buf, count, offset);

  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &pread_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &pread_turn_check);
    if (__builtin_expect(read_data_fd == -1, 0)) {
      read_data_fd = open(SYNCHRONIZATION_READ_DATA_LOG_PATH, O_RDONLY);
    }
    JASSERT ( read_data_fd != -1 );
    lseek(read_data_fd, GET_FIELD(currentLogEntry, pread, data_offset), SEEK_SET);
    // Only pread however much was logged as the return value.
    if (GET_COMMON(currentLogEntry, retval) != -1) {
      readAll(read_data_fd, (char *)buf, GET_COMMON(currentLogEntry, retval));
    }
    // Set the errno to what was logged (e.g. EINTR).
    if (GET_COMMON(currentLogEntry, my_errno) != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    retval = GET_COMMON(currentLogEntry, retval);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_pread(fd, buf, count, offset);
    SET_COMMON(my_return_entry, retval);
    _real_pthread_mutex_lock(&read_data_mutex);
    if (retval == -1) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    } else {
      SET_FIELD2(my_return_entry, pread, data_offset, read_log_pos);
      logReadData(buf, retval);
    }
    _real_pthread_mutex_unlock(&read_data_mutex);
    addNextLogEntry(my_return_entry);
  }
  return retval;
}

extern "C" ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr) || dmtcp::ProtectedFDs::isProtected(fd)) {
    return _real_pwrite(fd, buf, count, offset);
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    return _real_pwrite(fd, buf, count, offset);
  }
  int retval = 0;
  log_entry_t my_entry = create_pwrite_entry(my_clone_id, pwrite_event, fd, 
      (unsigned long int)buf, count, offset);
  log_entry_t my_return_entry = create_pwrite_entry(my_clone_id,
      pwrite_event_return, fd, (unsigned long int)buf, count, offset);

  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &pwrite_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &pwrite_turn_check);
#if 0
    if (GET_COMMON(currentLogEntry, retval) != -1) {
      // write only # that was logged as return val.
      ssize_t cur_retval = pwriteAll(fd, buf, GET_COMMON(currentLogEntry, retval), offset);
      if (cur_retval == -1) {
        if (errno == EBADF) {
          // If we weren't able to pwrite, but on record we were, assume this is
          // a file descriptor that no longer exists on replay (e.g. an external
          // request from a socket in MySQL).
          // In that case, we just set the return val and errno to what they were
          // on record, and return to the user anyway.
          // (fall through to outside if block)
        } else {
          // We failed on replay with a different error.
          JASSERT ( false ) ( strerror(errno) )
            .Text("Unable to replay user's pwrite() request.");
        }
      }
    }
#endif
    // Set the errno to what was logged (e.g. EINTR).
    if (GET_COMMON(currentLogEntry, my_errno) != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    retval = GET_COMMON(currentLogEntry, retval);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    // Note we don't call pwriteAll here. It should be the responsibility of
    // the user code to handle EINTR if needed.
    retval = _real_pwrite(fd, buf, count, offset);
    SET_COMMON(my_return_entry, retval);
    if (retval == -1) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    }
    addNextLogEntry(my_return_entry);
  }
  return retval;
}

extern "C" int fdatasync(int fd)
{
  BASIC_SYNC_WRAPPER(int, fdatasync, _real_fdatasync, fd);
}

extern "C" int fsync(int fd)
{
  BASIC_SYNC_WRAPPER(int, fsync, _real_fsync, fd);
}

extern "C" int link(const char *oldpath, const char *newpath)
{
  BASIC_SYNC_WRAPPER(int, link, _real_link, oldpath, newpath);
}

extern "C" int rename(const char *oldpath, const char *newpath)
{
  BASIC_SYNC_WRAPPER(int, rename, _real_rename, oldpath, newpath);
}

extern "C" ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
  WRAPPER_HEADER(ssize_t, readlink, _real_readlink, path, buf, bufsiz);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &readlink_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &readlink_turn_check);
    retval = GET_COMMON(currentLogEntry, retval);
    if (GET_COMMON(currentLogEntry, my_errno) != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    } else {
      // Don't try to copy if error returned.
      strncpy(buf, GET_FIELD(my_return_entry, readlink, buf), retval);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    addNextLogEntry(my_entry);
    retval = _real_readlink(path, buf, bufsiz);
    JASSERT ( retval < READLINK_MAX_LENGTH );
    SET_COMMON(my_return_entry, retval);
    if (errno != 0) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    } else {
      // Don't try to copy if error returned.
      strncpy(GET_FIELD(my_return_entry, readlink, buf), buf, retval);
    }
    addNextLogEntry(my_return_entry);
  }
  return retval;  
}

extern "C" int rmdir(const char *pathname)
{
  BASIC_SYNC_WRAPPER(int, rmdir, _real_rmdir, pathname);
}

extern "C" int mkdir(const char *pathname, mode_t mode)
{
  BASIC_SYNC_WRAPPER(int, mkdir, _real_mkdir, pathname, mode);
}

extern "C" struct dirent *readdir(DIR *dirp)
{
  /* TODO: We should allocate space for retval on the heap so that we return
     a pointer to that area, instead of an area in the log. */
  WRAPPER_HEADER(struct dirent *, readdir, _real_readdir, dirp);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &readdir_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &readdir_turn_check);
    retval = &GET_FIELD(currentLogEntry, readdir, retval);
    if (GET_COMMON(currentLogEntry, my_errno) != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    addNextLogEntry(my_entry);
    retval = _real_readdir(dirp);
    SET_FIELD2(my_return_entry, readdir, retval, *retval);
    if (errno != 0) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    }
    addNextLogEntry(my_return_entry);
  }
  return retval;
}

extern "C" int readdir_r(DIR *dirp, struct dirent *entry,
    struct dirent **result)
{
  WRAPPER_HEADER(int, readdir_r, _real_readdir_r, dirp, entry, result);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &readdir_r_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &readdir_r_turn_check);
    retval = GET_COMMON(currentLogEntry, retval);
    *entry = GET_FIELD(currentLogEntry, readdir_r, entry);
    if (GET_FIELD(currentLogEntry, readdir_r, result) == 0) {
      *result = NULL;
    } else {
      *result = entry;
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    addNextLogEntry(my_entry);
    retval = _real_readdir_r(dirp, entry, result);
    SET_COMMON(my_return_entry, retval);
    SET_FIELD2(my_return_entry, readdir_r, entry, *entry);
    if (*result == NULL) {
      SET_FIELD2(my_return_entry, readdir_r, result, 0);
    } else {
      SET_FIELD2(my_return_entry, readdir_r, result, 1);
    }
    addNextLogEntry(my_return_entry);
  }
  return retval;
}

extern "C" int mkstemp(char *temp)
{
  BASIC_SYNC_WRAPPER(int, mkstemp, _real_mkstemp, temp);
}
/* End wrapper code */
#endif
