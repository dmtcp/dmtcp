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
#include "log.h"
#include "syscallwrappers.h"
#include "virtualpidtable.h"

#ifdef RECORD_REPLAY
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
static pthread_mutex_t create_destroy_guard = PTHREAD_MUTEX_INITIALIZER;
//static pthread_mutex_t read_mutex = PTHREAD_MUTEX_INITIALIZER;
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
   size - If non-0, force new stack to this size.
*/
static void setupThreadStack(pthread_attr_t *attr_out, 
    const pthread_attr_t *user_attr, size_t size)
{
  size_t stack_size;
  void *stack_addr;
  int userStack = 0;
  // If the user's attributes have specified a stack size, use that.
  if (user_attr != NULL) {
    pthread_attr_getstack(user_attr, &stack_addr, &stack_size);
    if (stack_size != 0)
      userStack = 1;
    // Copy the user's attributes:
    *attr_out = *user_attr;
  }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
  int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK;
#else
  int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS;
#endif
  size_t mmap_size;
  if (userStack) {
    mmap_size = stack_size;
  } else {
    mmap_size = size == 0 ? default_stack_size : size;
  }
  // mmap() wrapper handles forcing it to the same place on replay.
  void *s = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, mmap_flags, -1, 0);
  if (s == MAP_FAILED)  {
    JTRACE ( "Failed to map thread stack." ) ( mmap_size )
      ( strerror(errno) ) (unified_log.currentEntryIndex());
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
  log_entry_t my_entry = create_pthread_mutex_lock_entry(my_clone_id,
                                                         pthread_mutex_lock_event, 
                                                         mutex);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(pthread_mutex_lock);
    if (retval == 0) {
      *mutex = GET_FIELD(currentLogEntry, pthread_mutex_lock, mutex);
    }
    WRAPPER_REPLAY_END(pthread_mutex_lock);
  } else if (SYNC_IS_RECORD) {
    retval = _real_pthread_mutex_lock(mutex);
    if (retval == 0) {
      SET_FIELD2(my_entry, pthread_mutex_lock, mutex, *mutex);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
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
                                                           pthread_mutex_unlock_event,
                                                           mutex);

  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(pthread_mutex_unlock);
    if (retval == 0) {
      *mutex = GET_FIELD(currentLogEntry, pthread_mutex_lock, mutex);
    }
    WRAPPER_REPLAY_END(pthread_mutex_unlock);
  } else if (SYNC_IS_RECORD) {
    WRAPPER_LOG_SET_LOG_ID(my_entry);
    retval = _real_pthread_mutex_unlock(mutex);
    if (retval == 0) {
      SET_FIELD2(my_entry, pthread_mutex_unlock, mutex, *mutex);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
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
                                                          pthread_cond_signal_event,
                                                          cond);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(pthread_cond_signal);
    if (retval == 0) {
      *cond = GET_FIELD(my_entry, pthread_cond_signal, cond);
    }
    WRAPPER_REPLAY_END(pthread_cond_signal);
  } else  if (SYNC_IS_RECORD) {
    retval = _real_pthread_cond_signal(cond);
    if (retval == 0) {
      SET_FIELD2(my_entry, pthread_cond_signal, cond, *cond);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
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
                                                        cond, mutex);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(pthread_cond_wait);
    if (retval == 0) {
      *cond = GET_FIELD(my_entry, pthread_cond_wait, cond);
      *mutex = GET_FIELD(my_entry, pthread_cond_wait, mutex);
    }
    WRAPPER_REPLAY_END(pthread_cond_wait);
  } else if (SYNC_IS_RECORD) {
    retval = _real_pthread_cond_wait(cond, mutex);
    if (retval == 0) {
      SET_FIELD2(my_entry, pthread_cond_wait, cond, *cond);
      SET_FIELD2(my_entry, pthread_cond_wait, mutex, *mutex);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

static inline void waitForChildThreadToInitialize()
{
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
                                                     pthread_create_event,
                                                     thread, attr,
                                                     start_routine, arg);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(pthread_create);
    stack_addr = (void *)GET_FIELD(currentLogEntry, pthread_create, stack_addr);
    stack_size = GET_FIELD(currentLogEntry, pthread_create, stack_size);
    WRAPPER_REPLAY_END(pthread_create);

    ACQUIRE_THREAD_CREATE_DESTROY_LOCK();
    // Set up thread stacks to how they were at record time.
    pthread_attr_init(&the_attr);

    setupThreadStack(&the_attr, attr, stack_size);
    // Never let the user create a detached thread:
    disableDetachState(&the_attr);
    retval = _real_pthread_create(thread, &the_attr, 
                                  start_wrapper, (void *)&createArg);
    waitForChildThreadToInitialize();

    RELEASE_THREAD_CREATE_DESTROY_LOCK();
    pthread_attr_destroy(&the_attr);

  } else  if (SYNC_IS_RECORD) {
    // Log annotation on the fly.
    size_t savedOffset = my_log->dataSize();
    WRAPPER_LOG_WRITE_ENTRY(my_entry);

    ACQUIRE_THREAD_CREATE_DESTROY_LOCK();
    pthread_attr_init(&the_attr);
    // Possibly create a thread stack if the user has not provided one:
    setupThreadStack(&the_attr, attr, 0);
    // Never let the user create a detached thread:
    disableDetachState(&the_attr);

    retval = _real_pthread_create(thread, &the_attr,
                                  start_wrapper, (void *)&createArg);
    SET_COMMON2(my_entry, retval, (void*)retval);
    SET_COMMON2(my_entry, my_errno, errno);

    waitForChildThreadToInitialize();

    RELEASE_THREAD_CREATE_DESTROY_LOCK();
    // Log whatever stack we ended up using:
    pthread_attr_getstack(&the_attr, &stack_addr, &stack_size);
    pthread_attr_destroy(&the_attr);
    SET_FIELD(my_entry, pthread_create, stack_addr);
    SET_FIELD(my_entry, pthread_create, stack_size);
    // Log annotation on the fly.
    my_log->replaceEntryAtOffset(my_entry, savedOffset);
  }
  return retval;
}

extern "C" int pthread_mutex_lock(pthread_mutex_t *mutex)
{
  WRAPPER_HEADER_RAW(int, pthread_mutex_lock, _real_pthread_mutex_lock,
                     mutex);

  /* NOTE: Don't call JTRACE (or anything that calls JTRACE) before
    this point. */
  int retval = internal_pthread_mutex_lock(mutex);
  return retval;
}

extern "C" int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
  WRAPPER_HEADER(int, pthread_mutex_trylock, _real_pthread_mutex_trylock,
                 mutex);
  /* NOTE: Don't call JTRACE (or anything that calls JTRACE) before
    this point. */
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(pthread_mutex_trylock);
    if (retval == 0) {
      *mutex = GET_FIELD(my_entry, pthread_mutex_trylock, mutex);
    }
    WRAPPER_REPLAY_END(pthread_mutex_trylock);
  } else if (SYNC_IS_RECORD) {
    retval = _real_pthread_mutex_trylock(mutex);
    if (retval == 0) {
      SET_FIELD2(my_entry, pthread_mutex_trylock, mutex, *mutex);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
  WRAPPER_HEADER_RAW(int, pthread_mutex_unlock, _real_pthread_mutex_unlock,
                     mutex);
  /* NOTE: Don't call JTRACE (or anything that calls JTRACE) before
    this point. */
  int retval = internal_pthread_mutex_unlock(mutex);
  return retval;
}

extern "C" int pthread_cond_signal(pthread_cond_t *cond)
{
  WRAPPER_HEADER_RAW(int, pthread_cond_signal, _real_pthread_cond_signal,
                     cond);
  int retval = internal_pthread_cond_signal(cond);
  return retval;
}

extern "C" int pthread_cond_broadcast(pthread_cond_t *cond)
{
  WRAPPER_HEADER(int, pthread_cond_broadcast, _real_pthread_cond_broadcast,
                 cond);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(pthread_cond_broadcast);
    if (retval == 0) {
      *cond = GET_FIELD(my_entry, pthread_cond_broadcast, cond);
    }
    WRAPPER_REPLAY_END(pthread_cond_broadcast);
  } else if (SYNC_IS_RECORD) {
    retval = _real_pthread_cond_broadcast(cond);
    if (retval == 0) {
      SET_FIELD2(my_entry, pthread_cond_broadcast, cond, *cond);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
  WRAPPER_HEADER_RAW(int, pthread_cond_wait, _real_pthread_cond_wait,
                     cond, mutex);
  int retval = internal_pthread_cond_wait(cond, mutex);
  return retval;
}

extern "C" int pthread_cond_timedwait(pthread_cond_t *cond,
    pthread_mutex_t *mutex, const struct timespec *abstime)
{
  JASSERT(abstime != NULL)
    .Text("We just want to know what happens if abstime is NULL :-)");
  // FIXME: Why are pthread_cond_wait and pthread_cond_timedwait handled differently?
  WRAPPER_HEADER(int, pthread_cond_timedwait, _real_pthread_cond_timedwait,
                 cond, mutex, abstime);

  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(pthread_cond_timedwait);
    if (retval == 0) {
      *cond = GET_FIELD(my_entry, pthread_cond_timedwait, cond);
      *mutex = GET_FIELD(my_entry, pthread_cond_timedwait, mutex);
    }
    WRAPPER_REPLAY_END(pthread_cond_timedwait);
  } else if (SYNC_IS_RECORD) {
    retval = _real_pthread_cond_timedwait(cond, mutex, abstime);
    if (retval == 0) {
      SET_FIELD2(my_entry, pthread_cond_timedwait, cond, *cond);
      SET_FIELD2(my_entry, pthread_cond_timedwait, mutex, *mutex);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" int pthread_rwlock_unlock(pthread_rwlock_t *rwlock)
{
  WRAPPER_HEADER(int, pthread_rwlock_unlock, _real_pthread_rwlock_unlock,
                 rwlock);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(pthread_rwlock_unlock);
    if (retval == 0) {
      *rwlock = GET_FIELD(my_entry, pthread_rwlock_unlock, rwlock);
    }
    WRAPPER_REPLAY_START(pthread_rwlock_unlock);
  } else if (SYNC_IS_RECORD) {
    WRAPPER_LOG_SET_LOG_ID(my_entry);
    retval = _real_pthread_rwlock_unlock(rwlock);
    if (retval == 0) {
      SET_FIELD2(my_entry, pthread_rwlock_unlock, rwlock, *rwlock);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
  WRAPPER_HEADER(int, pthread_rwlock_rdlock, _real_pthread_rwlock_rdlock,
                 rwlock);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(pthread_rwlock_rdlock);
    if (retval == 0) {
      *rwlock = GET_FIELD(my_entry, pthread_rwlock_rdlock, rwlock);
    }
    WRAPPER_REPLAY_START(pthread_rwlock_rdlock);
  } else if (SYNC_IS_RECORD) {
    retval = _real_pthread_rwlock_rdlock(rwlock);
    if (retval == 0) {
      SET_FIELD2(my_entry, pthread_rwlock_rdlock, rwlock, *rwlock);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock)
{
  WRAPPER_HEADER(int, pthread_rwlock_wrlock, _real_pthread_rwlock_wrlock,
                 rwlock);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(pthread_rwlock_wrlock);
    if (retval == 0) {
      *rwlock = GET_FIELD(my_entry, pthread_rwlock_wrlock, rwlock);
    }
    WRAPPER_REPLAY_START(pthread_rwlock_wrlock);
  } else if (SYNC_IS_RECORD) {
    retval = _real_pthread_rwlock_wrlock(rwlock);
    if (retval == 0) {
      SET_FIELD2(my_entry, pthread_rwlock_wrlock, rwlock, *rwlock);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
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
  {
    clone_id_t clone_id = tid_to_clone_id_table[thread_to_reap];
    dmtcp::SynchronizationLog *log = clone_id_to_log_table[clone_id];
    JASSERT(log != NULL);
    log->destroy();
    clone_id_to_log_table.erase(clone_id);
    tid_to_clone_id_table.erase(thread_to_reap);
  }
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
  JASSERT(false) .Text("Unreachable");
  return NULL;
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
  WRAPPER_HEADER_RAW(int, pthread_create, _real_pthread_create,
                     thread, attr, start_routine, arg);
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
  WRAPPER_HEADER_NO_RETURN(pthread_exit, _real_pthread_exit, value_ptr);

  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &pthread_exit_turn_check);
    getNextLogEntry();
    ACQUIRE_THREAD_CREATE_DESTROY_LOCK();
    reapThisThread();
    _real_pthread_exit(value_ptr);
  } else  if (SYNC_IS_RECORD) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    ACQUIRE_THREAD_CREATE_DESTROY_LOCK();
    reapThisThread();
    _real_pthread_exit(value_ptr);
  }
  while(1); // to suppress compiler warning about 'noreturn' function returning
}

extern "C" int pthread_detach(pthread_t thread)
{
  WRAPPER_HEADER(int, pthread_detach, _real_pthread_detach, thread);
  //FIXME: We don't need to log/replay this event.
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &pthread_detach_turn_check);
    getNextLogEntry();
    retval = 0;
  } else  if (SYNC_IS_RECORD) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = 0;
  }
  return retval;
}

static void *signal_thread(void *arg)
{
  size_t signal_sent_on = 0;
  while (1) {
    // Lock this so it doesn't change from underneath:
    _real_pthread_mutex_lock(&log_index_mutex);
    if (__builtin_expect(GET_COMMON(currentLogEntry,event) == signal_handler_event, 0)) {
      if (signal_sent_on != unified_log.currentEntryIndex()) {
        // Only send one signal per sig_handler entry.
        signal_sent_on = unified_log.currentEntryIndex();
        _real_pthread_kill(clone_id_to_tid_table[GET_COMMON(currentLogEntry,clone_id)],
            GET_FIELD(currentLogEntry, signal_handler, sig));
      }
    }
    _real_pthread_mutex_unlock(&log_index_mutex);
    usleep(20);
  }
  JASSERT(false) .Text("Unreachable");
  return NULL;
}

static void createSignalThread()
{
  pthread_t t;
  internal_pthread_create(&t, NULL, signal_thread, NULL);
}

extern "C" int pthread_kill(pthread_t thread, int sig)
{
  WRAPPER_HEADER(int, pthread_kill, _real_pthread_kill, thread, sig);

  if (__builtin_expect(signal_thread_alive == 0, 0)) {
    // Start the thread who will send signals (only on replay, but we need to
    // start it here so record has same behavior).
    signal_thread_alive = 1;
    createSignalThread();
  }
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &pthread_kill_turn_check);
    getNextLogEntry();
    // TODO: Do something better than always returning success.
    retval = 0;//_real_pthread_kill(thread, sig);
  } else  if (SYNC_IS_RECORD) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_pthread_kill(thread, sig);
  }
  return retval;
}

extern "C" int rand()
{
  WRAPPER_HEADER_NO_ARGS(int, rand, _real_rand);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY(rand);
  } else if (SYNC_IS_RECORD) {
    retval = _real_rand();
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

// TODO: We can remove log/replay from srand() once we are confident that we
// have captured all random events used to provide the seed (e.g. time()).
extern "C" void srand(unsigned int seed)
{
  BASIC_SYNC_WRAPPER_VOID(srand, _real_srand, seed);
}

extern "C" time_t time(time_t *tloc)
{
  WRAPPER_HEADER(time_t, time, _real_time, tloc);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(time);
    if (retval != (time_t) -1 && tloc != NULL) {
      *tloc = GET_FIELD(my_entry, time, time_retval);
    }
    WRAPPER_REPLAY_END(time);
  } else if (SYNC_IS_RECORD) {
    retval = _real_time(tloc);
    SET_FIELD2(my_entry, time, time_retval, retval);
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" int gettimeofday(struct timeval *tv, struct timezone *tz)
{
  WRAPPER_HEADER(int, gettimeofday, _real_gettimeofday, tv, tz);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(gettimeofday);
    if (retval == 0 && tv != NULL) {
      *tv = GET_FIELD(currentLogEntry, gettimeofday, tv_val);
    }
    if (retval == 0 && tz != NULL) {
      *tz = GET_FIELD(currentLogEntry, gettimeofday, tz_val);
    }
    WRAPPER_REPLAY_END(time);
  } else if (SYNC_IS_RECORD) {
    retval = _real_gettimeofday(tv, tz);
    if (retval == 0 && tv != NULL) {
      SET_FIELD2(my_entry, gettimeofday, tv_val, *tv);
    }
    if (retval == 0 && tz != NULL) {
      SET_FIELD2(my_entry, gettimeofday, tz_val, *tz);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}
/* End wrapper code */
#endif
