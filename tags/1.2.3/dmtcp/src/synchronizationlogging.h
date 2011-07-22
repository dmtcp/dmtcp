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

#ifndef SYNCHRONIZATION_LOGGING_H
#define SYNCHRONIZATION_LOGGING_H

#ifdef RECORD_REPLAY

// Needed for getpeername() etc.
#include <sys/socket.h>
// Needed for *xstat() to store 'struct stat' fields.
#include <sys/stat.h>
// Needed for readdir:
#include <sys/types.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/select.h>
#include "dmtcpalloc.h"
// Needed for ioctl:
#include <sys/ioctl.h>
#include <net/if.h>

// 'long int' IS 32 bits ON 32-bit ARCH AND 64 bits ON A 64-bit ARCH.
// 'sizeof(long long int)==sizeof(long int)' on 64-bit arch. 
// SHOULDN'T WE JUST MAKE THESE TYPES ALWAYS 'long int', AND
//   SIMPLIFY PRINTING THEM IN printf (USING "%ld")?  - Gene
#ifdef __x86_64__
typedef long long int clone_id_t;
typedef unsigned long long int log_id_t;
#else
typedef long int clone_id_t;
typedef unsigned long int log_id_t;
#endif

namespace dmtcp { class SynchronizationLog; }

static pthread_mutex_t read_data_mutex = PTHREAD_MUTEX_INITIALIZER;

#define LIB_PRIVATE __attribute__ ((visibility ("hidden")))

#define MAX_LOG_LENGTH ((size_t)50 * 1024 * 1024) // = 4096*4096. For what reason?
#define MAX_PATCH_LIST_LENGTH MAX_LOG_LENGTH
#define READLINK_MAX_LENGTH 256
#define WAKE_ALL_THREADS -1
#define SYNC_NOOP   0
#define SYNC_RECORD 1
#define SYNC_REPLAY 2
#define SYNC_IS_REPLAY    (sync_logging_branch == SYNC_REPLAY)
#define SYNC_IS_RECORD    (sync_logging_branch == SYNC_RECORD)
#define SYNC_IS_NOOP      (sync_logging_branch == SYNC_NOOP)
#define GET_RETURN_ADDRESS() __builtin_return_address(0)
#define SET_IN_MMAP_WRAPPER()   (in_mmap_wrapper = 1)
#define UNSET_IN_MMAP_WRAPPER() (in_mmap_wrapper = 0)
#define IN_MMAP_WRAPPER         (in_mmap_wrapper == 1)

#define TURN_CHECK_P(name) int name(log_entry_t *e1, log_entry_t *e2)

#define SYNC_TIMINGS

#ifdef SYNC_TIMINGS
/* To be used when the timer is started and finished in the same function. */
#define SYNC_TIMER_START(name)                  \
  struct timeval name##_start;                  \
  gettimeofday(&name##_start, NULL);

/* To be used when the timer is started in one function and finished
 * in another. The struct timeval should be declared in this file. */
#define SYNC_TIMER_START_GLOBAL(name)           \
  gettimeofday(&name##_start, NULL);

#define SYNC_TIMER_STOP(name)                                           \
  struct timeval name##_end;                                            \
  gettimeofday(&name##_end, NULL);                                      \
  double name##_sec = name##_end.tv_sec - name##_start.tv_sec;          \
  name##_sec += (name##_end.tv_usec - name##_start.tv_usec)/1000000.0;  \
  JNOTE ( "Timer " #name ) ( name##_sec );
#else
#define SYNC_TIMER_START(name)
#define SYNC_TIMER_STOP(name)
#endif

#define WRAPPER_HEADER_VOID_RAW(name, real_func, ...)                   \
  void *return_addr = GET_RETURN_ADDRESS();                             \
  do {                                                                  \
    if (!shouldSynchronize(return_addr) ||                              \
        jalib::Filesystem::GetProgramName() == "gdb") {                 \
      real_func(__VA_ARGS__);                                           \
      return;                                                           \
    }                                                                   \
  } while(0)

#define WRAPPER_HEADER_RAW(ret_type, name, real_func, ...)              \
  void *return_addr = GET_RETURN_ADDRESS();                             \
  do {                                                                  \
    if (!shouldSynchronize(return_addr) ||                              \
        jalib::Filesystem::GetProgramName() == "gdb") {                 \
      return real_func(__VA_ARGS__);                                    \
    }                                                                   \
  } while(0)

#define WRAPPER_HEADER_NO_ARGS(ret_type, name, real_func)                  \
  void *return_addr = GET_RETURN_ADDRESS();                             \
  if (!shouldSynchronize(return_addr) ||                                \
      jalib::Filesystem::GetProgramName() == "gdb") {                   \
    return real_func();                                             \
  }                                                                     \
  ret_type retval;                                                      \
  log_entry_t my_entry = create_##name##_entry(my_clone_id,             \
      name##_event);

#define WRAPPER_HEADER_NO_RETURN(name, real_func, ...)                  \
  void *return_addr = GET_RETURN_ADDRESS();                             \
  if (!shouldSynchronize(return_addr) ||                                \
      jalib::Filesystem::GetProgramName() == "gdb") {                   \
    real_func(__VA_ARGS__);                                             \
  }                                                                     \
  log_entry_t my_entry = create_##name##_entry(my_clone_id,             \
      name##_event, __VA_ARGS__);

#define WRAPPER_HEADER(ret_type, name, real_func, ...)                  \
  WRAPPER_HEADER_RAW(ret_type, name, real_func, __VA_ARGS__);           \
  ret_type retval;                                                      \
  log_entry_t my_entry = create_##name##_entry(my_clone_id,             \
      name##_event, __VA_ARGS__);

#define WRAPPER_HEADER_CKPT_DISABLED(ret_type, name, real_func, ...)    \
  void *return_addr = GET_RETURN_ADDRESS();                             \
  ret_type retval;                                                      \
  if (!shouldSynchronize(return_addr) ||                                \
      jalib::Filesystem::GetProgramName() == "gdb") {                   \
    retval = real_func(__VA_ARGS__);                                    \
    WRAPPER_EXECUTION_ENABLE_CKPT();                                    \
    return retval;                                                      \
    }                                                                   \
    log_entry_t my_entry = create_##name##_entry(my_clone_id,           \
      name##_event, __VA_ARGS__);

#define WRAPPER_HEADER_VOID(name, real_func, ...)                     \
  WRAPPER_HEADER_VOID_RAW(name, real_func, __VA_ARGS__);              \
  log_entry_t my_entry = create_##name##_entry(my_clone_id,           \
      name##_event, __VA_ARGS__);

#define WRAPPER_REPLAY_START_TYPED(ret_type, name)                                    \
  do {                                                                \
    waitForTurn(my_entry, &name##_turn_check);                        \
    retval = (ret_type) (unsigned long) GET_COMMON(currentLogEntry,   \
                                                   retval);           \
  } while (0)

#define WRAPPER_REPLAY_START(name)                                    \
  WRAPPER_REPLAY_START_TYPED(int, name)

#define WRAPPER_REPLAY_END(name)                                      \
  do {                                                                \
    int saved_errno = GET_COMMON(currentLogEntry, my_errno);          \
    getNextLogEntry();                                              \
    if (saved_errno != 0) {                                         \
      errno = saved_errno;                                          \
    }                                                               \
  } while (0)


#define WRAPPER_REPLAY_TYPED(ret_type, name)                        \
  do {                                                              \
    WRAPPER_REPLAY_START_TYPED(ret_type, name);                     \
    WRAPPER_REPLAY_END(name);                                       \
  } while (0)

#define WRAPPER_REPLAY(name) WRAPPER_REPLAY_TYPED(int, name)

#define WRAPPER_REPLAY_VOID(name)                                   \
  do {                                                              \
    waitForTurn(my_entry, &name##_turn_check);                      \
    int saved_errno = GET_COMMON(currentLogEntry, my_errno);        \
    getNextLogEntry();                                              \
    if (saved_errno != 0) {                                         \
      errno = saved_errno;                                          \
    }                                                               \
  } while (0)

#define WRAPPER_REPLAY_READ_FROM_READ_LOG(name, ptr, len)           \
  do {                                                              \
    if (__builtin_expect(read_data_fd == -1, 0)) {                  \
      read_data_fd = _real_open(RECORD_READ_DATA_LOG_PATH,          \
                                O_RDONLY, 0);                       \
    }                                                               \
    JASSERT ( read_data_fd != -1 );                                 \
    lseek(read_data_fd,                                             \
          GET_FIELD(currentLogEntry, name, data_offset), SEEK_SET); \
    dmtcp::Util::readAll(read_data_fd, ptr, len);                          \
  } while (0)

#define WRAPPER_LOG_WRITE_INTO_READ_LOG(name, ptr, len)             \
  do {                                                              \
    int saved_errno = errno;                                        \
    _real_pthread_mutex_lock(&read_data_mutex);                     \
    SET_FIELD2(my_entry, name, data_offset, read_log_pos);          \
    logReadData(ptr, len);                                          \
    _real_pthread_mutex_unlock(&read_data_mutex);                   \
    errno = saved_errno;                                            \
  } while (0)

#define WRAPPER_LOG_SET_LOG_ID(my_entry)                            \
  do {                                                              \
    SET_COMMON2(my_entry, log_id, -1);                              \
    prepareNextLogEntry(my_entry);                                  \
  } while(0)

#define WRAPPER_LOG_WRITE_ENTRY(my_entry)                           \
  do {                                                              \
    SET_COMMON2(my_entry, retval, (void*)retval);                   \
    SET_COMMON2(my_entry, my_errno, errno);                         \
    SET_COMMON2(my_entry, isOptional, isOptionalEvent);             \
    addNextLogEntry(my_entry);                                      \
    errno = GET_COMMON(my_entry, my_errno);                         \
  } while (0)

#define WRAPPER_LOG(real_func, ...)                                 \
  do {                                                              \
    isOptionalEvent = true;                                         \
    retval = real_func(__VA_ARGS__);                                \
    isOptionalEvent = false;                                        \
    WRAPPER_LOG_WRITE_ENTRY(my_entry);                              \
  } while (0)

#define WRAPPER_LOG_VOID(real_func, ...)                            \
  do {                                                              \
    real_func(__VA_ARGS__);                                         \
    SET_COMMON2(my_entry, my_errno, errno);                         \
    addNextLogEntry(my_entry);                                      \
    errno = GET_COMMON(my_entry, my_errno);                         \
  } while (0)


/* Your basic record wrapper template. Does not call _real_func on
   replay, but restores the return value and errno from the log. Also, the
   create_func_entry() function must handle the variable arguments and casting
   to correct types. */

#define BASIC_SYNC_WRAPPER_WITH_CKPT_LOCK(ret_type, name, real_func, ...)\
  WRAPPER_EXECUTION_DISABLE_CKPT();                                 \
  WRAPPER_HEADER_CKPT_DISABLED(ret_type, name, real_func,           \
                               __VA_ARGS__);                        \
  if (SYNC_IS_REPLAY) {                                             \
    WRAPPER_REPLAY_TYPED(ret_type, name);                           \
  } else if (SYNC_IS_RECORD) {                                         \
    WRAPPER_LOG(real_func, __VA_ARGS__);                            \
  }                                                                 \
  WRAPPER_EXECUTION_ENABLE_CKPT();                                  \
  return retval;

#define BASIC_SYNC_WRAPPER(ret_type, name, real_func, ...)          \
  WRAPPER_HEADER(ret_type, name, real_func, __VA_ARGS__);           \
  if (SYNC_IS_REPLAY) {                                             \
    WRAPPER_REPLAY_TYPED(ret_type, name);                           \
  } else if (SYNC_IS_RECORD) {                                         \
    WRAPPER_LOG(real_func, __VA_ARGS__);                            \
  }                                                                 \
  return retval;

#define BASIC_SYNC_WRAPPER_NO_RETURN(ret_type, name, real_func, ...)          \
  WRAPPER_HEADER(ret_type, name, real_func, __VA_ARGS__);           \
  if (SYNC_IS_REPLAY) {                                             \
    WRAPPER_REPLAY_TYPED(ret_type, name);                           \
  } else if (SYNC_IS_RECORD) {                                         \
    WRAPPER_LOG(real_func, __VA_ARGS__);                            \
  }                                                                 \

#define BASIC_SYNC_WRAPPER_VOID(name, real_func, ...)               \
  WRAPPER_HEADER_VOID(name, real_func, __VA_ARGS__);                \
  if (SYNC_IS_REPLAY) {                                             \
    WRAPPER_REPLAY_VOID(name);                                      \
  } else if (SYNC_IS_RECORD) {                                         \
    WRAPPER_LOG_VOID(real_func, __VA_ARGS__);                       \
  }

#define FOREACH_NAME(MACRO, ...)                                               \
  do {                                                                         \
    MACRO(accept, __VA_ARGS__);                                                \
    MACRO(accept4, __VA_ARGS__);                                                \
    MACRO(access, __VA_ARGS__);                                                \
    MACRO(bind, __VA_ARGS__);                                                  \
    MACRO(calloc, __VA_ARGS__);                                                \
    MACRO(close, __VA_ARGS__);                                                 \
    MACRO(closedir, __VA_ARGS__);                                              \
    MACRO(connect, __VA_ARGS__);                                               \
    MACRO(dup, __VA_ARGS__);                                                   \
    MACRO(exec_barrier, __VA_ARGS__);                                          \
    MACRO(fclose, __VA_ARGS__);                                                \
    MACRO(fcntl, __VA_ARGS__);                                                 \
    MACRO(fdatasync, __VA_ARGS__);                                             \
    MACRO(fdopen, __VA_ARGS__);                                                \
    MACRO(fgets, __VA_ARGS__);                                                 \
    MACRO(fflush, __VA_ARGS__);                                                \
    MACRO(fopen, __VA_ARGS__);                                                 \
    MACRO(fopen64, __VA_ARGS__);                                               \
    MACRO(fprintf, __VA_ARGS__);                                               \
    MACRO(fscanf, __VA_ARGS__);                                                \
    MACRO(fputs, __VA_ARGS__);                                                 \
    MACRO(free, __VA_ARGS__);                                                  \
    MACRO(fsync, __VA_ARGS__);                                                 \
    MACRO(ftell, __VA_ARGS__);                                                 \
    MACRO(fwrite, __VA_ARGS__);                                                \
    MACRO(fxstat, __VA_ARGS__);                                                \
    MACRO(fxstat64, __VA_ARGS__);                                              \
    MACRO(getc, __VA_ARGS__);                                                  \
    MACRO(getsockopt, __VA_ARGS__);                                            \
    MACRO(gettimeofday, __VA_ARGS__);                                          \
    MACRO(fgetc, __VA_ARGS__);                                                 \
    MACRO(ungetc, __VA_ARGS__);                                                \
    MACRO(getline, __VA_ARGS__);                                               \
    MACRO(getpeername, __VA_ARGS__);                                           \
    MACRO(getsockname, __VA_ARGS__);                                           \
    MACRO(ioctl, __VA_ARGS__);                                           \
    MACRO(libc_memalign, __VA_ARGS__);                                         \
    MACRO(lseek, __VA_ARGS__);                                                 \
    MACRO(link, __VA_ARGS__);                                                  \
    MACRO(listen, __VA_ARGS__);                                                \
    MACRO(lxstat, __VA_ARGS__);                                                \
    MACRO(lxstat64, __VA_ARGS__);                                              \
    MACRO(malloc, __VA_ARGS__);                                                \
    MACRO(mkdir, __VA_ARGS__);                                                 \
    MACRO(mkstemp, __VA_ARGS__);                                               \
    MACRO(mmap, __VA_ARGS__);                                                  \
    MACRO(mmap64, __VA_ARGS__);                                                \
    MACRO(mremap, __VA_ARGS__);                                                \
    MACRO(munmap, __VA_ARGS__);                                                \
    MACRO(open, __VA_ARGS__);                                                  \
    MACRO(open64, __VA_ARGS__);                                                \
    MACRO(opendir, __VA_ARGS__);					       \
    MACRO(pread, __VA_ARGS__);                                                 \
    MACRO(putc, __VA_ARGS__);                                                  \
    MACRO(pwrite, __VA_ARGS__);                                                \
    MACRO(pthread_detach, __VA_ARGS__);                                        \
    MACRO(pthread_create, __VA_ARGS__);                                        \
    MACRO(pthread_cond_broadcast, __VA_ARGS__);                                \
    MACRO(pthread_cond_signal, __VA_ARGS__);                                   \
    MACRO(pthread_mutex_lock, __VA_ARGS__);                                    \
    MACRO(pthread_mutex_trylock, __VA_ARGS__);                                 \
    MACRO(pthread_mutex_unlock, __VA_ARGS__);                                  \
    MACRO(pthread_cond_wait, __VA_ARGS__);                                     \
    MACRO(pthread_cond_timedwait, __VA_ARGS__);                                \
    MACRO(pthread_exit, __VA_ARGS__);                                          \
    MACRO(pthread_join, __VA_ARGS__);                                          \
    MACRO(pthread_kill, __VA_ARGS__);                                          \
    MACRO(pthread_rwlock_unlock, __VA_ARGS__);                                 \
    MACRO(pthread_rwlock_rdlock, __VA_ARGS__);                                 \
    MACRO(pthread_rwlock_wrlock, __VA_ARGS__);                                 \
    MACRO(rand, __VA_ARGS__);                                                  \
    MACRO(read, __VA_ARGS__);                                                  \
    MACRO(readdir, __VA_ARGS__);                                               \
    MACRO(readdir_r, __VA_ARGS__);                                             \
    MACRO(readlink, __VA_ARGS__);                                              \
    MACRO(realloc, __VA_ARGS__);                                               \
    MACRO(rename, __VA_ARGS__);                                                \
    MACRO(rewind, __VA_ARGS__);                                                \
    MACRO(rmdir, __VA_ARGS__);                                                 \
    MACRO(select, __VA_ARGS__);                                                \
    MACRO(signal_handler, __VA_ARGS__);                                        \
    MACRO(sigwait, __VA_ARGS__);                                               \
    MACRO(setsockopt, __VA_ARGS__);                                            \
    MACRO(srand, __VA_ARGS__);                                                 \
    MACRO(socket, __VA_ARGS__);                                                \
    MACRO(time, __VA_ARGS__);                                                  \
    MACRO(unlink, __VA_ARGS__);                                                \
    MACRO(write, __VA_ARGS__);                                                 \
    MACRO(xstat, __VA_ARGS__);                                                 \
    MACRO(xstat64, __VA_ARGS__);                                               \
    MACRO(user, __VA_ARGS__);                                                  \
  } while(0)

/* Event codes: */
typedef enum {
  unknown_event = -1,
  accept_event = 1,
  accept4_event,
  access_event,
  bind_event,
  calloc_event,
  close_event,
  closedir_event,
  connect_event,
  dup_event,
  exec_barrier_event,
  fclose_event,
  fcntl_event,
  fdatasync_event,
  fdopen_event,
  fgets_event,
  fflush_event,
  fopen_event,
  fopen64_event,
  fprintf_event,
  fscanf_event,
  fputs_event,
  free_event,
  fsync_event,
  ftell_event,
  fwrite_event,
  fxstat_event,
  fxstat64_event,
  getc_event,
  gettimeofday_event,
  ioctl_event,
  fgetc_event,
  ungetc_event,
  getline_event,
  getpeername_event,
  getsockname_event,
  getsockopt_event,
  libc_memalign_event,
  link_event,
  listen_event,
  lseek_event,
  lxstat_event,
  lxstat64_event,
  malloc_event,
  mkdir_event,
  mkstemp_event,
  mmap_event,
  mmap64_event,
  mremap_event,
  munmap_event,
  open_event,
  open64_event,
  opendir_event,
  pread_event,
  putc_event,
  pwrite_event,
  pthread_detach_event,
  pthread_create_event,
  pthread_cond_broadcast_event,
  pthread_mutex_lock_event,
  pthread_mutex_trylock_event,
  pthread_cond_signal_event,
  pthread_mutex_unlock_event,
  pthread_cond_wait_event,
  pthread_cond_timedwait_event,
  pthread_exit_event,
  pthread_join_event,
  pthread_kill_event, // no return event -- asynchronous
  pthread_rwlock_unlock_event,
  pthread_rwlock_rdlock_event,
  pthread_rwlock_wrlock_event,
  rand_event,
  read_event,
  readdir_event,
  readdir_r_event,
  readlink_event,
  realloc_event,
  rename_event,
  rewind_event,
  rmdir_event,
  select_event,
  signal_handler_event,
  sigwait_event,
  setsockopt_event,
  socket_event,
  srand_event,
  time_event,
  unlink_event,
  user_event,
  write_event,
  xstat_event,
  xstat64_event
} event_code_t;
/* end event codes */

typedef struct {
  // For pthread_mutex_{lock,trylock,unlock}():
  pthread_mutex_t *addr;
  pthread_mutex_t mutex;
} log_event_pthread_mutex_lock_t,
  log_event_pthread_mutex_trylock_t,
  log_event_pthread_mutex_unlock_t;

static const int
log_event_pthread_mutex_lock_size = sizeof(log_event_pthread_mutex_lock_t);

static const int
log_event_pthread_mutex_trylock_size = sizeof(log_event_pthread_mutex_trylock_t);

static const int
log_event_pthread_mutex_unlock_size = sizeof(log_event_pthread_mutex_unlock_t);

typedef struct {
  // For pthread_rwlock_{rdlock,wrlock,unlock}():
  pthread_rwlock_t *addr;
  pthread_rwlock_t rwlock;
} log_event_pthread_rwlock_rdlock_t,
  log_event_pthread_rwlock_wrlock_t,
  log_event_pthread_rwlock_unlock_t;

static const int
log_event_pthread_rwlock_unlock_size = sizeof(log_event_pthread_rwlock_unlock_t);

static const int
log_event_pthread_rwlock_rdlock_size = sizeof(log_event_pthread_rwlock_rdlock_t);

static const int
log_event_pthread_rwlock_wrlock_size = sizeof(log_event_pthread_rwlock_wrlock_t);

typedef struct {
  // For pthread_cond_signal():
  // For pthread_cond_broadcast():
  pthread_cond_t *addr;
  pthread_cond_t cond;
  int signal_target;
} log_event_pthread_cond_signal_t, log_event_pthread_cond_broadcast_t;

static const int
log_event_pthread_cond_signal_size = sizeof(log_event_pthread_cond_signal_t);

static const int
log_event_pthread_cond_broadcast_size = sizeof(log_event_pthread_cond_broadcast_t);

typedef struct {
  // For pthread_cond_wait():
  pthread_mutex_t *mutex_addr;
  pthread_cond_t *cond_addr;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} log_event_pthread_cond_wait_t;

static const int log_event_pthread_cond_wait_size = sizeof(log_event_pthread_cond_wait_t);

typedef struct {
  // For pthread_cond_timedwait():
  pthread_mutex_t *mutex_addr;
  pthread_cond_t *cond_addr;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  struct timespec *abstime;
} log_event_pthread_cond_timedwait_t;

static const int log_event_pthread_cond_timedwait_size = sizeof(log_event_pthread_cond_timedwait_t);

typedef struct {
  // For pthread_exit():
  void *value_ptr;
} log_event_pthread_exit_t;

static const int log_event_pthread_exit_size = sizeof(log_event_pthread_exit_t);

typedef struct {
  // For pthread_join():
  pthread_t thread;
  void *value_ptr;
} log_event_pthread_join_t;

static const int log_event_pthread_join_size = sizeof(log_event_pthread_join_t);

typedef struct {
  // For pthread_kill():
  pthread_t thread;
  int sig;
} log_event_pthread_kill_t;

static const int log_event_pthread_kill_size = sizeof(log_event_pthread_kill_t);

typedef struct {
  // For rand():
  int x; // unused, but prevents optimizing away this struct.
} log_event_rand_t;

static const int log_event_rand_size = sizeof(log_event_rand_t);

typedef struct {
  // For rename():
  char *oldpath;
  char *newpath;
} log_event_rename_t;

static const int log_event_rename_size = sizeof(log_event_rename_t);

typedef struct {
  // For rewind():
  FILE *stream;
} log_event_rewind_t;

static const int log_event_rewind_size = sizeof(log_event_rewind_t);

typedef struct {
  // For rmdir():
  char *pathname;
} log_event_rmdir_t;

static const int log_event_rmdir_size = sizeof(log_event_rmdir_t);

typedef struct {
  // For select():
  int nfds;
  fd_set readfds;
  fd_set writefds;
  fd_set *exceptfds; // just save address for now
  struct timeval *timeout;
} log_event_select_t;

static const int log_event_select_size = sizeof(log_event_select_t);

typedef struct {
  // For signal handlers:
  int sig;
} log_event_signal_handler_t;

static const int log_event_signal_handler_size = sizeof(log_event_signal_handler_t);

typedef struct {
  // For sigwait():
  sigset_t *set;
  int *sigwait_sig;
  int sig;
} log_event_sigwait_t;

static const int log_event_sigwait_size = sizeof(log_event_sigwait_t);

typedef struct {
  // For read():
  int readfd;
  void* buf_addr;
  size_t count;
  off_t data_offset; // offset into read saved data file
} log_event_read_t;

static const int log_event_read_size = sizeof(log_event_read_t);

typedef struct {
  // For readdir():
  DIR *dirp;
  struct dirent retval;
} log_event_readdir_t;

static const int log_event_readdir_size = sizeof(log_event_readdir_t);

typedef struct {
  // For readdir_r():
  DIR *dirp;
  struct dirent *entry;
  struct dirent **result;
  struct dirent ret_entry;
  struct dirent *ret_result;
} log_event_readdir_r_t;

static const int log_event_readdir_r_size = sizeof(log_event_readdir_r_t);

typedef struct {
  // For readlink():
  char *path;
  char *buf;
  size_t bufsiz;
  off_t data_offset;
} log_event_readlink_t;

static const int log_event_readlink_size = sizeof(log_event_readlink_t);

typedef struct {
  // For unlink():
  char *pathname;
} log_event_unlink_t;

static const int log_event_unlink_size = sizeof(log_event_unlink_t);

typedef struct {
  // For user event:
  int x; // unused, but prevents optimizing away this struct.
} log_event_user_t;

static const int log_event_user_size = sizeof(log_event_user_t);

typedef struct {
  // For write():
  int writefd;
  void* buf_addr;
  size_t count;
} log_event_write_t;

static const int log_event_write_size = sizeof(log_event_write_t);

typedef struct {
  // For accept():
  int sockfd;
  struct sockaddr *addr;
  socklen_t *addrlen;
  struct sockaddr ret_addr;
  socklen_t ret_addrlen;
} log_event_accept_t;

static const int log_event_accept_size = sizeof(log_event_accept_t);

typedef struct {
  // For accept4():
  int sockfd;
  struct sockaddr *addr;
  socklen_t *addrlen;
  int flags;
  struct sockaddr ret_addr;
  socklen_t ret_addrlen;
} log_event_accept4_t;

static const int log_event_accept4_size = sizeof(log_event_accept4_t);

typedef struct {
  // For access():
  char *pathname;
  int mode;
} log_event_access_t;

static const int log_event_access_size = sizeof(log_event_access_t);

typedef struct {
  // For bind():
  int sockfd;
  struct sockaddr *addr;
  socklen_t addrlen;
} log_event_bind_t;

static const int log_event_bind_size = sizeof(log_event_bind_t);

typedef struct {
  // For getpeername():
  int sockfd;
  struct sockaddr *addr;
  socklen_t *addrlen;
  struct sockaddr ret_addr;
  socklen_t ret_addrlen;
} log_event_getpeername_t;

static const int log_event_getpeername_size = sizeof(log_event_getpeername_t);

typedef struct {
  // For getsockname():
  int sockfd;
  struct sockaddr *addr;
  socklen_t *addrlen;
  struct sockaddr ret_addr;
  socklen_t ret_addrlen;
} log_event_getsockname_t;

static const int log_event_getsockname_size = sizeof(log_event_getsockname_t);

typedef struct {
  // For setsockopt():
  int sockfd;
  int level;
  int optname;
  void *optval;
  socklen_t optlen;
} log_event_setsockopt_t;

static const int log_event_setsockopt_size = sizeof(log_event_setsockopt_t);

typedef struct {
  // For getsockopt():
  int sockfd;
  int level;
  int optname;
  void *optval;
  socklen_t *optlen;
} log_event_getsockopt_t;

static const int log_event_getsockopt_size = sizeof(log_event_getsockopt_t);

typedef struct {
  // For ioctl():
  int d;
  int request;
  void *arg;
  struct winsize win_val;
  struct ifconf ifconf_val;
  off_t data_offset;
} log_event_ioctl_t;

static const int log_event_ioctl_size = sizeof(log_event_ioctl_t);

typedef struct {
  // For pthread_create():
  pthread_t *thread;
  pthread_attr_t *attr;
  void *(*start_routine)(void*);
  void *arg;
  void *stack_addr;
  size_t stack_size;
} log_event_pthread_create_t;

static const int log_event_pthread_create_size = sizeof(log_event_pthread_create_t);

typedef struct {
  // For pthread_detach():
  pthread_t thread;
} log_event_pthread_detach_t;

static const int log_event_pthread_detach_size = sizeof(log_event_pthread_detach_t);

typedef struct {
  // For __libc_memalign():
  size_t boundary;
  size_t size;
  void *return_ptr;
} log_event_libc_memalign_t;

static const int log_event_libc_memalign_size = sizeof(log_event_libc_memalign_t);

typedef struct {
  // For fclose():
  FILE *fp;
} log_event_fclose_t;

static const int log_event_fclose_size = sizeof(log_event_fclose_t);

typedef struct {
  // For fcntl():
  int fd;
  int cmd;
  long arg_3_l;
  struct flock *arg_3_f;
} log_event_fcntl_t;

static const int log_event_fcntl_size = sizeof(log_event_fcntl_t);

typedef struct {
  // For fdatasync():
  int fd;
} log_event_fdatasync_t;

static const int log_event_fdatasync_size = sizeof(log_event_fdatasync_t);

typedef struct {
  // For fdopen():
  int fd;
  char *mode;
  // Size is approximately 216 bytes:
  FILE fdopen_retval;
} log_event_fdopen_t;

static const int log_event_fdopen_size = sizeof(log_event_fdopen_t);

typedef struct {
  // For fgets():
  char *s;
  int size;
  FILE *stream;
  off_t data_offset;
} log_event_fgets_t;

static const int log_event_fgets_size = sizeof(log_event_fgets_t);

typedef struct {
  // For fflush():
  FILE *stream;
} log_event_fflush_t;

static const int log_event_fflush_size = sizeof(log_event_fflush_t);

typedef struct {
  // For fopen():
  char *name;
  char *mode;
  // Size is approximately 216 bytes:
  FILE fopen_retval;
} log_event_fopen_t;

static const int log_event_fopen_size = sizeof(log_event_fopen_t);

typedef struct {
  // For fopen64():
  char *name;
  char *mode;
  // Size is approximately 216 bytes:
  FILE fopen64_retval;
} log_event_fopen64_t;

static const int log_event_fopen64_size = sizeof(log_event_fopen64_t);

typedef struct {
  // For fprintf():
  FILE *stream;
  char *format;
  va_list ap;
} log_event_fprintf_t;

static const int log_event_fprintf_size = sizeof(log_event_fprintf_t);

typedef struct {
  // For fscanf():
  FILE *stream;
  char *format;
  int bytes;
  off_t data_offset;
} log_event_fscanf_t;

static const int log_event_fscanf_size = sizeof(log_event_fscanf_t);

typedef struct {
  // For fputs():
  char *s;
  FILE *stream;
} log_event_fputs_t;

static const int log_event_fputs_size = sizeof(log_event_fputs_t);

typedef struct {
  // For getc():
  FILE *stream;
} log_event_getc_t;

static const int log_event_getc_size = sizeof(log_event_getc_t);

typedef struct {
  // For gettimeofday():
  struct timeval *tv;
  struct timezone *tz;
  struct timeval tv_val;
  struct timezone tz_val;
  int gettimeofday_retval;
} log_event_gettimeofday_t;

static const int log_event_gettimeofday_size = sizeof(log_event_gettimeofday_t);

typedef struct {
  // For fgetc():
  FILE *stream;
} log_event_fgetc_t;

static const int log_event_fgetc_size = sizeof(log_event_fgetc_t);

typedef struct {
  // For ungetc():
  int c;
  FILE *stream;
} log_event_ungetc_t;

static const int log_event_ungetc_size = sizeof(log_event_ungetc_t);

typedef struct {
  // For getline():
  char *lineptr;
  char *new_lineptr;
  size_t n;
  size_t new_n;
  FILE *stream;
  off_t data_offset;
} log_event_getline_t;

static const int log_event_getline_size = sizeof(log_event_getline_t);

typedef struct {
  // For link():
  char *oldpath;
  char *newpath;
} log_event_link_t;

static const int log_event_link_size = sizeof(log_event_link_t);

typedef struct {
  // For listen():
  int sockfd;
  int backlog;
} log_event_listen_t;

static const int log_event_listen_size = sizeof(log_event_listen_t);

typedef struct {
  // For lseek():
  int fd;
  off_t offset;
  int whence;
} log_event_lseek_t;

static const int log_event_lseek_size = sizeof(log_event_lseek_t);

typedef struct {
  // For lxstat():
  int vers;
  char *path;
  struct stat buf;
} log_event_lxstat_t;

static const int log_event_lxstat_size = sizeof(log_event_lxstat_t);

typedef struct {
  // For lxstat64():
  int vers;
  char *path;
  struct stat64 buf;
} log_event_lxstat64_t;

static const int log_event_lxstat64_size = sizeof(log_event_lxstat64_t);

typedef struct {
  // For malloc():
  size_t size;
} log_event_malloc_t;

static const int log_event_malloc_size = sizeof(log_event_malloc_t);

typedef struct {
  // For mkdir():
  char *pathname;
  mode_t mode;
} log_event_mkdir_t;

static const int log_event_mkdir_size = sizeof(log_event_mkdir_t);

typedef struct {
  // For mkstemp():
  char *temp;
} log_event_mkstemp_t;

static const int log_event_mkstemp_size = sizeof(log_event_mkstemp_t);

typedef struct {
  // For mmap():
  void *addr;
  size_t length;
  int prot;
  int flags;
  int fd;
  off_t offset;
} log_event_mmap_t;

static const int log_event_mmap_size = sizeof(log_event_mmap_t);

typedef struct {
  // For mmap64():
  void *addr;
  size_t length;
  int prot;
  int flags;
  int fd;
  off64_t offset;
} log_event_mmap64_t;

static const int log_event_mmap64_size = sizeof(log_event_mmap64_t);

typedef struct {
  // For mremap():
  void *old_address;
  size_t old_size;
  size_t new_size;
  int flags;
} log_event_mremap_t;

static const int log_event_mremap_size = sizeof(log_event_mremap_t);

typedef struct {
  // For munmap():
  void *addr;
  size_t length;
} log_event_munmap_t;

static const int log_event_munmap_size = sizeof(log_event_munmap_t);

typedef struct {
  // For open():
  char *path;
  int flags;
  mode_t open_mode;
} log_event_open_t;

static const int log_event_open_size = sizeof(log_event_open_t);

typedef struct {
  // For open64():
  char *path;
  int flags;
  mode_t open_mode;
} log_event_open64_t;

static const int log_event_open64_size = sizeof(log_event_open64_t);

typedef struct {
  // For opendir():
  char *name;
} log_event_opendir_t;

static const int log_event_opendir_size = sizeof(log_event_opendir_t);

typedef struct {
  // For pread():
  int fd;
  void* buf;
  size_t count;
  off_t offset;
  off_t data_offset; // offset into read saved data file
} log_event_pread_t;

static const int log_event_pread_size = sizeof(log_event_pread_t);

typedef struct {
  // For putc():
  int c;
  FILE *stream;
} log_event_putc_t;

static const int log_event_putc_size = sizeof(log_event_putc_t);

typedef struct {
  // For pwrite():
  int fd;
  void* buf;
  size_t count;
  off_t offset;
} log_event_pwrite_t;

static const int log_event_pwrite_size = sizeof(log_event_pwrite_t);

typedef struct {
  // For calloc():
  size_t nmemb;
  size_t size;
} log_event_calloc_t;

static const int log_event_calloc_size = sizeof(log_event_calloc_t);

typedef struct {
  // For close():
  int fd;
} log_event_close_t;

static const int log_event_close_size = sizeof(log_event_close_t);

typedef struct {
  // For closedir():
  DIR *dirp;
} log_event_closedir_t;

static const int log_event_closedir_size = sizeof(log_event_closedir_t);

typedef struct {
  // For connect():
  int sockfd;
  struct sockaddr *serv_addr;
  socklen_t addrlen;
} log_event_connect_t;

static const int log_event_connect_size = sizeof(log_event_connect_t);

typedef struct {
  // For dup():
  int oldfd;
} log_event_dup_t;

static const int log_event_dup_size = sizeof(log_event_dup_t);

typedef struct {
  // For exec_barrier: special case.
} log_event_exec_barrier_t;

static const int log_event_exec_barrier_size = sizeof(log_event_exec_barrier_t);

typedef struct {
  // For realloc():
  size_t size;
  void *ptr;
} log_event_realloc_t;

static const int log_event_realloc_size = sizeof(log_event_realloc_t);

typedef struct {
  // For free():
  void *ptr;
} log_event_free_t;

static const int log_event_free_size = sizeof(log_event_free_t);

typedef struct {
  // For ftell():
  FILE *stream;
} log_event_ftell_t;

static const int log_event_ftell_size = sizeof(log_event_ftell_t);

typedef struct {
  // For fwrite():
  void *ptr;
  size_t size;
  size_t nmemb;
  FILE *stream;
} log_event_fwrite_t;

static const int log_event_fwrite_size = sizeof(log_event_fwrite_t);

typedef struct {
  // For fsync():
  int fd;
} log_event_fsync_t;

static const int log_event_fsync_size = sizeof(log_event_fsync_t);

typedef struct {
  // For fxstat():
  int vers;
  int fd;
  struct stat buf;
} log_event_fxstat_t;

static const int log_event_fxstat_size = sizeof(log_event_fxstat_t);

typedef struct {
  // For fxstat64():
  int vers;
  int fd;
  struct stat64 buf;
} log_event_fxstat64_t;

static const int log_event_fxstat64_size = sizeof(log_event_fxstat64_t);

typedef struct {
  // For time():
  time_t time_retval;
  time_t *tloc;
} log_event_time_t;

static const int log_event_time_size = sizeof(log_event_time_t);

typedef struct {
  // For srand():
  unsigned int seed;
} log_event_srand_t;

static const int log_event_srand_size = sizeof(log_event_srand_t);

typedef struct {
  // For socket():
  int domain;
  int type;
  int protocol;
} log_event_socket_t;

static const int log_event_socket_size = sizeof(log_event_socket_t);

typedef struct {
  // For xstat():
  int vers;
  char *path;
  struct stat buf;
} log_event_xstat_t;

static const int log_event_xstat_size = sizeof(log_event_xstat_t);

typedef struct {
  // For xstat64():
  int vers;
  char *path;
  struct stat64 buf;
} log_event_xstat64_t;

static const int log_event_xstat64_size = sizeof(log_event_xstat64_t);

typedef struct {
  // FIXME:
  //event_code_t event;
  unsigned char event;
  unsigned char isOptional;
  log_id_t log_id;
  clone_id_t clone_id;
  int my_errno;
  void* retval;
} log_entry_header_t;

typedef struct {
  // We aren't going to map more than 256 system calls/functions.
  // We can expand it to 4 bytes if needed. However a single byte makes
  // things easier.
  // Shared among all events ("common area"):
  /* IMPORTANT: Adding new fields to the common area requires that you also
   * update the log_event_common_size definition. */
  log_entry_header_t header;

  union {
    log_event_access_t                           log_event_access;
    log_event_bind_t                             log_event_bind;
    log_event_pthread_mutex_lock_t               log_event_pthread_mutex_lock;
    log_event_pthread_mutex_trylock_t            log_event_pthread_mutex_trylock;
    log_event_pthread_mutex_unlock_t             log_event_pthread_mutex_unlock;
    log_event_pthread_rwlock_unlock_t            log_event_pthread_rwlock_unlock;
    log_event_pthread_rwlock_rdlock_t            log_event_pthread_rwlock_rdlock;
    log_event_pthread_rwlock_wrlock_t            log_event_pthread_rwlock_wrlock;
    log_event_pthread_cond_signal_t              log_event_pthread_cond_signal;
    log_event_pthread_cond_broadcast_t           log_event_pthread_cond_broadcast;
    log_event_pthread_cond_wait_t                log_event_pthread_cond_wait;
    log_event_pthread_cond_timedwait_t           log_event_pthread_cond_timedwait;
    log_event_select_t                           log_event_select;
    log_event_read_t                             log_event_read;
    log_event_readdir_t                          log_event_readdir;
    log_event_readdir_r_t                        log_event_readdir_r;
    log_event_readlink_t                         log_event_readlink;
    log_event_write_t                            log_event_write;
    log_event_close_t                            log_event_close;
    log_event_closedir_t                         log_event_closedir;
    log_event_connect_t                          log_event_connect;
    log_event_dup_t                              log_event_dup;
    log_event_exec_barrier_t                     log_event_exec_barrier;
    log_event_accept_t                           log_event_accept;
    log_event_accept4_t                          log_event_accept4;
    log_event_getpeername_t                      log_event_getpeername;
    log_event_getsockname_t                      log_event_getsockname;
    log_event_setsockopt_t                       log_event_setsockopt;
    log_event_getsockopt_t                       log_event_getsockopt;
    log_event_ioctl_t                            log_event_ioctl;
    log_event_pthread_create_t                   log_event_pthread_create;
    log_event_libc_memalign_t                    log_event_libc_memalign;
    log_event_fclose_t                           log_event_fclose;
    log_event_fcntl_t                            log_event_fcntl;
    log_event_fdatasync_t                        log_event_fdatasync;
    log_event_fdopen_t                           log_event_fdopen;
    log_event_fgets_t                            log_event_fgets;
    log_event_fflush_t                           log_event_fflush;
    log_event_fopen_t                            log_event_fopen;
    log_event_fopen64_t                          log_event_fopen64;
    log_event_fprintf_t                          log_event_fprintf;
    log_event_fscanf_t                           log_event_fscanf;
    log_event_fputs_t                            log_event_fputs;
    log_event_getc_t                             log_event_getc;
    log_event_gettimeofday_t                     log_event_gettimeofday;
    log_event_fgetc_t                            log_event_fgetc;
    log_event_ungetc_t                           log_event_ungetc;
    log_event_getline_t                          log_event_getline;
    log_event_open_t                             log_event_open;
    log_event_open64_t                           log_event_open64;
    log_event_opendir_t                          log_event_opendir;
    log_event_pread_t                            log_event_pread;
    log_event_putc_t                             log_event_putc;
    log_event_pwrite_t                           log_event_pwrite;
    log_event_pthread_kill_t                     log_event_pthread_kill;
    log_event_pthread_join_t                     log_event_pthread_join;
    log_event_pthread_exit_t                     log_event_pthread_exit;
    log_event_pthread_detach_t                   log_event_pthread_detach;
    log_event_link_t                             log_event_link;
    log_event_listen_t                           log_event_listen;
    log_event_lseek_t                            log_event_lseek;
    log_event_lxstat_t                           log_event_lxstat;
    log_event_lxstat64_t                         log_event_lxstat64;
    log_event_malloc_t                           log_event_malloc;
    log_event_mkdir_t                            log_event_mkdir;
    log_event_mkstemp_t                          log_event_mkstemp;
    log_event_mmap_t                             log_event_mmap;
    log_event_mmap64_t                           log_event_mmap64;
    log_event_mremap_t                           log_event_mremap;
    log_event_munmap_t                           log_event_munmap;
    log_event_calloc_t                           log_event_calloc;
    log_event_realloc_t                          log_event_realloc;
    log_event_free_t                             log_event_free;
    log_event_ftell_t                            log_event_ftell;
    log_event_fwrite_t                           log_event_fwrite;
    log_event_fsync_t                            log_event_fsync;
    log_event_fxstat_t                           log_event_fxstat;
    log_event_fxstat64_t                         log_event_fxstat64;
    log_event_time_t                             log_event_time;
    log_event_unlink_t                           log_event_unlink;
    log_event_user_t                             log_event_user;
    log_event_srand_t                            log_event_srand;
    log_event_socket_t                           log_event_socket;
    log_event_rand_t                             log_event_rand;
    log_event_rename_t                           log_event_rename;
    log_event_rewind_t                           log_event_rewind;
    log_event_rmdir_t                            log_event_rmdir;
    log_event_signal_handler_t                   log_event_signal_handler;
    log_event_sigwait_t                          log_event_sigwait;
    log_event_xstat_t                            log_event_xstat;
    log_event_xstat64_t                          log_event_xstat64;
  } event_data;
} log_entry_t;

#define log_event_common_size \
  (sizeof(GET_COMMON(currentLogEntry,event))      +                    \
   sizeof(GET_COMMON(currentLogEntry,isOptional)) +                    \
   sizeof(GET_COMMON(currentLogEntry,log_id))     +                    \
   sizeof(GET_COMMON(currentLogEntry,clone_id))   +                    \
   sizeof(GET_COMMON(currentLogEntry,my_errno))   +                    \
   sizeof(GET_COMMON(currentLogEntry,retval)))



#define GET_FIELD(entry, event, field)     entry.event_data.log_event_##event.field
#define GET_FIELD_PTR(entry, event, field) entry->event_data.log_event_##event.field

#define SET_FIELD2(entry,event,field,field2) GET_FIELD(entry, event, field) = field2

#define SET_FIELD(entry, event, field) SET_FIELD2(entry, event, field, field)

#define SET_FIELD_FROM(entry, event, field, source) \
  GET_FIELD(entry, event, field) = GET_FIELD(source, event, field)

#define GET_COMMON(entry, field) entry.header.field
#define GET_COMMON_PTR(entry, field) entry->header.field

#define SET_COMMON_PTR(entry, field) GET_COMMON_PTR(entry, field) = field

#define SET_COMMON2(entry, field, field2) GET_COMMON(entry, field) = field2
#define SET_COMMON(entry, field) SET_COMMON2(entry, field, field)

#define IS_EQUAL_COMMON(e1, e2, field) \
  (GET_COMMON(e1, field) == GET_COMMON(e2, field))
#define IS_EQUAL_FIELD(e1, e2, event, field) \
  (GET_FIELD(e1, event, field) == GET_FIELD(e2, event, field))


#if 1
#define IFNAME_GET_EVENT_SIZE(name, event, event_size)                  \
  do {                                                                  \
    if (event == name##_event)          \
      event_size = log_event_##name##_size;                             \
  } while(0)

#define IFNAME_READ_ENTRY_FROM_LOG(name, source, entry)                    \
  do {                                                                  \
    if (GET_COMMON(entry,event) == name##_event) {           \
      memcpy(&entry.event_data.log_event_##name, source,      \
             log_event_##name##_size);                                     \
    }                                                                   \
  } while(0)

#define IFNAME_WRITE_ENTRY_TO_LOG(name, dest, entry)                \
  do {                                                                  \
    if (GET_COMMON(entry,event) == name##_event) {               \
      memcpy(dest, &entry.event_data.log_event_##name,              \
             log_event_##name##_size);                                     \
    }                                                                   \
  } while(0)
#else
#define IFNAME_GET_EVENT_SIZE(name, event, event_size)                  \
  do {                                                                  \
    if (event == name##_event || event == name##_event_return)          \
      event_size = log_event_##name##_size;                             \
  } while(0)

#define IFNAME_READ_ENTRY_FROM_LOG(name, source, entry)                    \
  do {                                                                  \
    if (GET_COMMON(entry,event) == name##_event ||                  \
        GET_COMMON(entry,event) == name##_event_return) {           \
      memcpy(&entry.event_data.log_event_##name, source,      \
             log_event_##name##_size);                                     \
    }                                                                   \
  } while(0)

#define IFNAME_WRITE_ENTRY_TO_LOG(name, dest, entry)                \
  do {                                                                  \
    if (GET_COMMON(entry,event) == name##_event ||                      \
        GET_COMMON(entry,event) == name##_event_return) {               \
      memcpy(dest, &entry.event_data.log_event_##name,              \
             log_event_##name##_size);                                     \
    }                                                                   \
  } while(0)
#endif

#define GET_EVENT_SIZE(event, event_size)                               \
  do {                                                                  \
    FOREACH_NAME(IFNAME_GET_EVENT_SIZE, event, event_size);             \
  } while(0)

#define READ_ENTRY_FROM_LOG(source, entry)                          \
  do {                                                                  \
    FOREACH_NAME(IFNAME_READ_ENTRY_FROM_LOG, source, entry);        \
  } while(0)

#define WRITE_ENTRY_TO_LOG(dest, entry)                      \
  do {                                                                  \
    FOREACH_NAME(IFNAME_WRITE_ENTRY_TO_LOG, dest, entry);    \
  } while(0)

/* Typedefs */
// Type for predicate to check for a turn in the log.
typedef int (*turn_pred_t) (log_entry_t*, log_entry_t*);
typedef struct {
  int retval;
  int my_errno;
  void *value_ptr;
} pthread_join_retval_t;

/* Static constants: */
// Clone id to indicate anyone may do this event (used for exec):
static const int         CLONE_ID_ANYONE = -2;
static const log_entry_t EMPTY_LOG_ENTRY = {{0, 0, 0, 0, 0, 0}};
// Number to start clone_ids at:
static const int         GLOBAL_CLONE_COUNTER_INIT = 1;
static const int         RECORD_LOG_PATH_MAX = 256;

LIB_PRIVATE extern char GLOBAL_LOG_LIST_PATH[RECORD_LOG_PATH_MAX];
LIB_PRIVATE extern char RECORD_PATCHED_LOG_PATH[RECORD_LOG_PATH_MAX];
LIB_PRIVATE extern int global_log_list_fd;
LIB_PRIVATE extern pthread_mutex_t global_log_list_fd_mutex;

/* Library private: */
LIB_PRIVATE extern dmtcp::map<clone_id_t, pthread_t> clone_id_to_tid_table;
LIB_PRIVATE extern dmtcp::map<pthread_t, clone_id_t> tid_to_clone_id_table;
LIB_PRIVATE extern dmtcp::map<clone_id_t, dmtcp::SynchronizationLog*> clone_id_to_log_table;
LIB_PRIVATE extern void* unified_log_addr;
LIB_PRIVATE extern dmtcp::map<pthread_t, pthread_join_retval_t> pthread_join_retvals;
LIB_PRIVATE extern log_entry_t     currentLogEntry;
LIB_PRIVATE extern char RECORD_LOG_PATH[RECORD_LOG_PATH_MAX];
LIB_PRIVATE extern char RECORD_READ_DATA_LOG_PATH[RECORD_LOG_PATH_MAX];
LIB_PRIVATE extern int             record_log_fd;
LIB_PRIVATE extern int             read_data_fd;
LIB_PRIVATE extern int             sync_logging_branch;
LIB_PRIVATE extern int             log_all_allocs;
LIB_PRIVATE extern size_t          default_stack_size;

LIB_PRIVATE extern dmtcp::SynchronizationLog unified_log;

// TODO: rename this, since a log entry is not a char. maybe log_event_TYPE_SIZE?
#define LOG_ENTRY_SIZE sizeof(char)
LIB_PRIVATE extern pthread_cond_t  reap_cv;
LIB_PRIVATE extern pthread_mutex_t global_clone_counter_mutex;
LIB_PRIVATE extern pthread_mutex_t log_index_mutex;
LIB_PRIVATE extern pthread_mutex_t reap_mutex;
LIB_PRIVATE extern pthread_mutex_t thread_transition_mutex;
LIB_PRIVATE extern pthread_mutex_t wake_target_mutex;
LIB_PRIVATE extern pthread_t       thread_to_reap;

/* Thread locals: */
LIB_PRIVATE extern __thread clone_id_t my_clone_id;
LIB_PRIVATE extern __thread int in_mmap_wrapper;
LIB_PRIVATE extern __thread dmtcp::SynchronizationLog *my_log;
LIB_PRIVATE extern __thread unsigned char isOptionalEvent;

/* Volatiles: */
LIB_PRIVATE extern volatile size_t        record_log_entry_index;
LIB_PRIVATE extern volatile size_t        record_log_index;
LIB_PRIVATE extern volatile int           record_log_loaded;
LIB_PRIVATE extern volatile int           threads_to_wake_index;
LIB_PRIVATE extern volatile clone_id_t    global_clone_counter;
LIB_PRIVATE extern volatile off_t         read_log_pos;

/* Functions */
LIB_PRIVATE void   register_in_global_log_list(clone_id_t clone_id);
LIB_PRIVATE int    isUnlock(log_entry_t e);
LIB_PRIVATE void   addNextLogEntry(log_entry_t&);
LIB_PRIVATE void   prepareNextLogEntry(log_entry_t& e);
LIB_PRIVATE void   atomic_increment(volatile int *ptr);
LIB_PRIVATE void   atomic_decrement(volatile int *ptr);
LIB_PRIVATE void   set_sync_mode(int mode);
LIB_PRIVATE void   truncate_all_logs();
LIB_PRIVATE bool   close_all_logs();
LIB_PRIVATE void   copyFdSet(fd_set *src, fd_set *dest);
LIB_PRIVATE int    fdAvailable(fd_set *set);
LIB_PRIVATE int    fdSetDiff(fd_set *one, fd_set *two);
LIB_PRIVATE void   getNextLogEntry();
LIB_PRIVATE void   initializeLogNames();
LIB_PRIVATE void   initLogsForRecordReplay();
LIB_PRIVATE dmtcp::vector<clone_id_t> get_log_list();
LIB_PRIVATE void   logReadData(void *buf, int count);
LIB_PRIVATE void   sync_and_close_record_log();
LIB_PRIVATE void   map_record_log_to_read();
LIB_PRIVATE void   map_record_log_to_write();
LIB_PRIVATE void   primeLog();
//LIB_PRIVATE ssize_t pwriteAll(int fd, const void *buf, size_t count, off_t off);
LIB_PRIVATE void   reapThisThread();
LIB_PRIVATE void   recordDataStackLocations();
LIB_PRIVATE void   removeThreadToWake(clone_id_t clone_id);
LIB_PRIVATE int    shouldSynchronize(void *return_addr);
LIB_PRIVATE int    signalThread(int target, pthread_cond_t *cv);
LIB_PRIVATE int    threadsToWakeContains(clone_id_t clone_id);
LIB_PRIVATE int    threadsToWakeEmpty();
LIB_PRIVATE void   userSynchronizedEvent();
LIB_PRIVATE void   userSynchronizedEventBegin();
LIB_PRIVATE void   userSynchronizedEventEnd();
LIB_PRIVATE int    validAddress(void *addr);
LIB_PRIVATE ssize_t writeAll(int fd, const void *buf, size_t count);
LIB_PRIVATE void   writeLogsToDisk();

// THESE DECLARATIONS SEEM TO BE USED ONLY IN synchronizationlogging.cpp.
// IF THAT IS TRUE, THEY SHOULD BE 'static' AND NOT LIB_PRIVATE.
// IMPLYING HERE THAT THEY ARE USED BY MULTIPLE LIBRARY FILES (LIB_PRIVATE)
//   JUST CONFUSES THE READER.  -- Gene

LIB_PRIVATE log_entry_t create_accept_entry(clone_id_t clone_id, int event, int sockfd,
    struct sockaddr *addr, socklen_t *addrlen);
LIB_PRIVATE log_entry_t create_accept4_entry(clone_id_t clone_id, int event, int sockfd,
    struct sockaddr *addr, socklen_t *addrlen, int flags);
LIB_PRIVATE log_entry_t create_access_entry(clone_id_t clone_id, int event,
    const char *pathname, int mode);
LIB_PRIVATE log_entry_t create_bind_entry(clone_id_t clone_id, int event,
    int sockfd, const struct sockaddr *my_addr, socklen_t addrlen);
LIB_PRIVATE log_entry_t create_calloc_entry(clone_id_t clone_id, int event, size_t nmemb,
    size_t size);
LIB_PRIVATE log_entry_t create_close_entry(clone_id_t clone_id, int event, int fd);
LIB_PRIVATE log_entry_t create_closedir_entry(clone_id_t clone_id, int event, DIR *dirp);
LIB_PRIVATE log_entry_t create_connect_entry(clone_id_t clone_id, int event, int sockfd,
    const struct sockaddr *serv_addr, socklen_t addrlen);
LIB_PRIVATE log_entry_t create_dup_entry(clone_id_t clone_id, int event, int oldfd);
LIB_PRIVATE log_entry_t create_exec_barrier_entry();
LIB_PRIVATE log_entry_t create_fcntl_entry(clone_id_t clone_id, int event, int fd,
    int cmd, long arg_3_l, struct flock *arg_3_f);
LIB_PRIVATE log_entry_t create_fclose_entry(clone_id_t clone_id, int event,
    FILE *fp);
LIB_PRIVATE log_entry_t create_fdatasync_entry(clone_id_t clone_id, int event, int fd);
LIB_PRIVATE log_entry_t create_fdopen_entry(clone_id_t clone_id, int event, int fd,
    const char *mode);
LIB_PRIVATE log_entry_t create_fgets_entry(clone_id_t clone_id, int event, char *s,
    int size, FILE *stream);
LIB_PRIVATE log_entry_t create_fflush_entry(clone_id_t clone_id, int event,
    FILE *stream);
LIB_PRIVATE log_entry_t create_fopen_entry(clone_id_t clone_id, int event,
    const char *name, const char *mode);
LIB_PRIVATE log_entry_t create_fopen64_entry(clone_id_t clone_id, int event,
    const char *name, const char *mode);
LIB_PRIVATE log_entry_t create_fprintf_entry(clone_id_t clone_id, int event,
    FILE *stream, const char *format, va_list ap);
LIB_PRIVATE log_entry_t create_fscanf_entry(clone_id_t clone_id, int event,
    FILE *stream, const char *format, va_list ap);
LIB_PRIVATE log_entry_t create_fputs_entry(clone_id_t clone_id, int event,
    const char *s, FILE *stream);
LIB_PRIVATE log_entry_t create_free_entry(clone_id_t clone_id, int event,
    void *ptr);
LIB_PRIVATE log_entry_t create_fsync_entry(clone_id_t clone_id, int event, int fd);
LIB_PRIVATE log_entry_t create_ftell_entry(clone_id_t clone_id, int event,
    FILE *stream);
LIB_PRIVATE log_entry_t create_fwrite_entry(clone_id_t clone_id, int event,
    const void *ptr, size_t size, size_t nmemb, FILE *stream);
LIB_PRIVATE log_entry_t create_fxstat_entry(clone_id_t clone_id, int event, int vers,
    int fd, struct stat *buf);
LIB_PRIVATE log_entry_t create_fxstat64_entry(clone_id_t clone_id, int event, int vers,
    int fd, struct stat64 *buf);
LIB_PRIVATE log_entry_t create_getc_entry(clone_id_t clone_id, int event, FILE *stream);
LIB_PRIVATE log_entry_t create_gettimeofday_entry(clone_id_t clone_id, int event,
    struct timeval *tv, struct timezone *tz);
LIB_PRIVATE log_entry_t create_fgetc_entry(clone_id_t clone_id, int event, FILE *stream);
LIB_PRIVATE log_entry_t create_ungetc_entry(clone_id_t clone_id, int event, int c,
    FILE *stream);
LIB_PRIVATE log_entry_t create_getline_entry(clone_id_t clone_id, int event,
    char **lineptr, size_t *n, FILE *stream);
LIB_PRIVATE log_entry_t create_getpeername_entry(clone_id_t clone_id, int event,
    int sockfd, struct sockaddr *addr, socklen_t *addrlen);
LIB_PRIVATE log_entry_t create_getsockname_entry(clone_id_t clone_id, int event,
    int sockfd, struct sockaddr *addr, socklen_t *addrlen);
LIB_PRIVATE log_entry_t create_libc_memalign_entry(clone_id_t clone_id, int event,
    size_t boundary, size_t size);
LIB_PRIVATE log_entry_t create_link_entry(clone_id_t clone_id, int event,
    const char *oldpath, const char *newpath);
LIB_PRIVATE log_entry_t create_listen_entry(clone_id_t clone_id, int event,
    int sockfd, int backlog);
LIB_PRIVATE log_entry_t create_lseek_entry(clone_id_t clone_id, int event, int fd,
    off_t offset, int whence);
LIB_PRIVATE log_entry_t create_lxstat_entry(clone_id_t clone_id, int event, int vers,
    const char *path, struct stat *buf);
LIB_PRIVATE log_entry_t create_lxstat64_entry(clone_id_t clone_id, int event, int vers,
    const char *path, struct stat64 *buf);
LIB_PRIVATE log_entry_t create_malloc_entry(clone_id_t clone_id, int event, size_t size);
LIB_PRIVATE log_entry_t create_mkdir_entry(clone_id_t clone_id, int event,
    const char *pathname, mode_t mode);
LIB_PRIVATE log_entry_t create_mkstemp_entry(clone_id_t clone_id, int event, char *temp);
LIB_PRIVATE log_entry_t create_mmap_entry(clone_id_t clone_id, int event, void *addr,
    size_t length, int prot, int flags, int fd, off_t offset);
LIB_PRIVATE log_entry_t create_mmap64_entry(clone_id_t clone_id, int event, void *addr,
    size_t length, int prot, int flags, int fd, off64_t offset);
LIB_PRIVATE log_entry_t create_munmap_entry(clone_id_t clone_id, int event, void *addr,
    size_t length);
LIB_PRIVATE log_entry_t create_mremap_entry(clone_id_t clone_id, int event,
    void *old_address, size_t old_size, size_t new_size, int flags, void *new_addr);
LIB_PRIVATE log_entry_t create_open_entry(clone_id_t clone_id, int event,
    const char *path, int flags, mode_t mode);
LIB_PRIVATE log_entry_t create_open64_entry(clone_id_t clone_id, int event,
    const char *path, int flags, mode_t mode);
LIB_PRIVATE log_entry_t create_opendir_entry(clone_id_t clone_id, int event,
    const char *name);
LIB_PRIVATE log_entry_t create_pread_entry(clone_id_t clone_id, int event, int fd,
    void* buf, size_t count, off_t offset);
LIB_PRIVATE log_entry_t create_putc_entry(clone_id_t clone_id, int event, int c,
    FILE *stream);
LIB_PRIVATE log_entry_t create_pwrite_entry(clone_id_t clone_id, int event, int fd,
    const void* buf, size_t count, off_t offset);
LIB_PRIVATE log_entry_t create_pthread_cond_broadcast_entry(clone_id_t clone_id, int event,
    pthread_cond_t *cond_var);
LIB_PRIVATE log_entry_t create_pthread_cond_signal_entry(clone_id_t clone_id, int event,
    pthread_cond_t *cond_var);
LIB_PRIVATE log_entry_t create_pthread_cond_wait_entry(clone_id_t clone_id, int event,
    pthread_cond_t *cond_var, pthread_mutex_t *mutex);
LIB_PRIVATE log_entry_t create_pthread_cond_timedwait_entry(clone_id_t clone_id, int event,
    pthread_cond_t *cond_var, pthread_mutex_t *mutex, const struct timespec *abstime);
LIB_PRIVATE log_entry_t create_pthread_rwlock_unlock_entry(clone_id_t clone_id,
    int event, pthread_rwlock_t *rwlock);
LIB_PRIVATE log_entry_t create_pthread_rwlock_rdlock_entry(clone_id_t clone_id,
    int event, pthread_rwlock_t *rwlock);
LIB_PRIVATE log_entry_t create_pthread_rwlock_wrlock_entry(clone_id_t clone_id,
    int event, pthread_rwlock_t *rwlock);
LIB_PRIVATE log_entry_t create_pthread_create_entry(clone_id_t clone_id,
    int event, pthread_t *thread, const pthread_attr_t *attr,
    void *(*start_routine)(void*), void *arg);
LIB_PRIVATE log_entry_t create_pthread_detach_entry(clone_id_t clone_id,
    int event, pthread_t thread);
LIB_PRIVATE log_entry_t create_pthread_exit_entry(clone_id_t clone_id,
    int event, void *value_ptr);
LIB_PRIVATE log_entry_t create_pthread_join_entry(clone_id_t clone_id,
    int event, pthread_t thread, void *value_ptr);
LIB_PRIVATE log_entry_t create_pthread_kill_entry(clone_id_t clone_id,
    int event, pthread_t thread, int sig);
LIB_PRIVATE log_entry_t create_pthread_mutex_lock_entry(clone_id_t clone_id, int event,
    pthread_mutex_t *mutex);
LIB_PRIVATE log_entry_t create_pthread_mutex_trylock_entry(clone_id_t clone_id, int event,
    pthread_mutex_t *mutex);
LIB_PRIVATE log_entry_t create_pthread_mutex_unlock_entry(clone_id_t clone_id, int event,
    pthread_mutex_t *mutex);
LIB_PRIVATE log_entry_t create_rand_entry(clone_id_t clone_id, int event);
LIB_PRIVATE log_entry_t create_read_entry(clone_id_t clone_id, int event, int readfd,
    void* buf_addr, size_t count);
LIB_PRIVATE log_entry_t create_readdir_entry(clone_id_t clone_id, int event,
    DIR *dirp);
LIB_PRIVATE log_entry_t create_readdir_r_entry(clone_id_t clone_id, int event,
    DIR *dirp, struct dirent *entry, struct dirent **result);
LIB_PRIVATE log_entry_t create_readlink_entry(clone_id_t clone_id, int event,
    const char *path, char *buf, size_t bufsiz);
LIB_PRIVATE log_entry_t create_realloc_entry(clone_id_t clone_id, int event,
    void *ptr, size_t size);
LIB_PRIVATE log_entry_t create_rename_entry(clone_id_t clone_id, int event,
    const char *oldpath, const char *newpath);
LIB_PRIVATE log_entry_t create_rewind_entry(clone_id_t clone_id, int event,
    FILE *stream);
LIB_PRIVATE log_entry_t create_rmdir_entry(clone_id_t clone_id, int event,
    const char *pathname);
LIB_PRIVATE log_entry_t create_select_entry(clone_id_t clone_id, int event, int nfds,
    fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
    struct timeval *timeout);
LIB_PRIVATE log_entry_t create_setsockopt_entry(clone_id_t clone_id, int event,
    int sockfd, int level, int optname, const void* optval, socklen_t optlen);
LIB_PRIVATE log_entry_t create_getsockopt_entry(clone_id_t clone_id, int event,
    int sockfd, int level, int optname, void* optval, socklen_t* optlen);
LIB_PRIVATE log_entry_t create_ioctl_entry(clone_id_t clone_id, int event,
    int d, int request, void* arg);
LIB_PRIVATE log_entry_t create_signal_handler_entry(clone_id_t clone_id, int event,
    int sig);
LIB_PRIVATE log_entry_t create_sigwait_entry(clone_id_t clone_id, int event,
    const sigset_t *set, int *sig);
LIB_PRIVATE log_entry_t create_srand_entry(clone_id_t clone_id, int event,
    unsigned int seed);
LIB_PRIVATE log_entry_t create_socket_entry(clone_id_t clone_id, int event,
    int domain, int type, int protocol);
LIB_PRIVATE log_entry_t create_xstat_entry(clone_id_t clone_id, int event, int vers,
    const char *path, struct stat *buf);
LIB_PRIVATE log_entry_t create_xstat64_entry(clone_id_t clone_id, int event, int vers,
    const char *path, struct stat64 *buf);
LIB_PRIVATE log_entry_t create_time_entry(clone_id_t clone_id, int event,
    time_t *tloc);
LIB_PRIVATE log_entry_t create_unlink_entry(clone_id_t clone_id, int event,
    const char *pathname);
LIB_PRIVATE log_entry_t create_user_entry(clone_id_t clone_id, int event);
LIB_PRIVATE log_entry_t create_write_entry(clone_id_t clone_id, int event,
    int writefd, const void* buf_addr, size_t count);

LIB_PRIVATE void waitForTurn(log_entry_t my_entry, turn_pred_t pred);
LIB_PRIVATE void waitForExecBarrier();

/* Turn check predicates: */
LIB_PRIVATE TURN_CHECK_P(accept_turn_check);
LIB_PRIVATE TURN_CHECK_P(accept4_turn_check);
LIB_PRIVATE TURN_CHECK_P(access_turn_check);
LIB_PRIVATE TURN_CHECK_P(bind_turn_check);
LIB_PRIVATE TURN_CHECK_P(calloc_turn_check);
LIB_PRIVATE TURN_CHECK_P(close_turn_check);
LIB_PRIVATE TURN_CHECK_P(closedir_turn_check);
LIB_PRIVATE TURN_CHECK_P(connect_turn_check);
LIB_PRIVATE TURN_CHECK_P(dup_turn_check);
LIB_PRIVATE TURN_CHECK_P(fclose_turn_check);
LIB_PRIVATE TURN_CHECK_P(fcntl_turn_check);
LIB_PRIVATE TURN_CHECK_P(fdatasync_turn_check);
LIB_PRIVATE TURN_CHECK_P(fdopen_turn_check);
LIB_PRIVATE TURN_CHECK_P(fgets_turn_check);
LIB_PRIVATE TURN_CHECK_P(fflush_turn_check);
LIB_PRIVATE TURN_CHECK_P(fopen_turn_check);
LIB_PRIVATE TURN_CHECK_P(fopen64_turn_check);
LIB_PRIVATE TURN_CHECK_P(fprintf_turn_check);
LIB_PRIVATE TURN_CHECK_P(fscanf_turn_check);
LIB_PRIVATE TURN_CHECK_P(fputs_turn_check);
LIB_PRIVATE TURN_CHECK_P(free_turn_check);
LIB_PRIVATE TURN_CHECK_P(fsync_turn_check);
LIB_PRIVATE TURN_CHECK_P(ftell_turn_check);
LIB_PRIVATE TURN_CHECK_P(fwrite_turn_check);
LIB_PRIVATE TURN_CHECK_P(fxstat_turn_check);
LIB_PRIVATE TURN_CHECK_P(fxstat64_turn_check);
LIB_PRIVATE TURN_CHECK_P(getc_turn_check);
LIB_PRIVATE TURN_CHECK_P(gettimeofday_turn_check);
LIB_PRIVATE TURN_CHECK_P(fgetc_turn_check);
LIB_PRIVATE TURN_CHECK_P(ungetc_turn_check);
LIB_PRIVATE TURN_CHECK_P(getline_turn_check);
LIB_PRIVATE TURN_CHECK_P(getpeername_turn_check);
LIB_PRIVATE TURN_CHECK_P(getsockname_turn_check);
LIB_PRIVATE TURN_CHECK_P(libc_memalign_turn_check);
LIB_PRIVATE TURN_CHECK_P(lseek_turn_check);
LIB_PRIVATE TURN_CHECK_P(link_turn_check);
LIB_PRIVATE TURN_CHECK_P(listen_turn_check);
LIB_PRIVATE TURN_CHECK_P(lxstat_turn_check);
LIB_PRIVATE TURN_CHECK_P(lxstat64_turn_check);
LIB_PRIVATE TURN_CHECK_P(malloc_turn_check);
LIB_PRIVATE TURN_CHECK_P(mkdir_turn_check);
LIB_PRIVATE TURN_CHECK_P(mkstemp_turn_check);
LIB_PRIVATE TURN_CHECK_P(mmap_turn_check);
LIB_PRIVATE TURN_CHECK_P(mmap64_turn_check);
LIB_PRIVATE TURN_CHECK_P(mremap_turn_check);
LIB_PRIVATE TURN_CHECK_P(munmap_turn_check);
LIB_PRIVATE TURN_CHECK_P(open_turn_check);
LIB_PRIVATE TURN_CHECK_P(open64_turn_check);
LIB_PRIVATE TURN_CHECK_P(opendir_turn_check);
LIB_PRIVATE TURN_CHECK_P(pread_turn_check);
LIB_PRIVATE TURN_CHECK_P(putc_turn_check);
LIB_PRIVATE TURN_CHECK_P(pwrite_turn_check);
LIB_PRIVATE TURN_CHECK_P(pthread_cond_signal_turn_check);
LIB_PRIVATE TURN_CHECK_P(pthread_cond_broadcast_turn_check);
LIB_PRIVATE TURN_CHECK_P(pthread_cond_wait_turn_check);
LIB_PRIVATE TURN_CHECK_P(pthread_cond_timedwait_turn_check);
LIB_PRIVATE TURN_CHECK_P(pthread_rwlock_unlock_turn_check);
LIB_PRIVATE TURN_CHECK_P(pthread_rwlock_rdlock_turn_check);
LIB_PRIVATE TURN_CHECK_P(pthread_rwlock_wrlock_turn_check);
LIB_PRIVATE TURN_CHECK_P(pthread_create_turn_check);
LIB_PRIVATE TURN_CHECK_P(pthread_detach_turn_check);
LIB_PRIVATE TURN_CHECK_P(pthread_exit_turn_check);
LIB_PRIVATE TURN_CHECK_P(pthread_join_turn_check);
LIB_PRIVATE TURN_CHECK_P(pthread_kill_turn_check);
LIB_PRIVATE TURN_CHECK_P(pthread_mutex_lock_turn_check);
LIB_PRIVATE TURN_CHECK_P(pthread_mutex_trylock_turn_check);
LIB_PRIVATE TURN_CHECK_P(pthread_mutex_unlock_turn_check);
LIB_PRIVATE TURN_CHECK_P(rand_turn_check);
LIB_PRIVATE TURN_CHECK_P(read_turn_check);
LIB_PRIVATE TURN_CHECK_P(readdir_turn_check);
LIB_PRIVATE TURN_CHECK_P(readdir_r_turn_check);
LIB_PRIVATE TURN_CHECK_P(readlink_turn_check);
LIB_PRIVATE TURN_CHECK_P(realloc_turn_check);
LIB_PRIVATE TURN_CHECK_P(rename_turn_check);
LIB_PRIVATE TURN_CHECK_P(rewind_turn_check);
LIB_PRIVATE TURN_CHECK_P(rmdir_turn_check);
LIB_PRIVATE TURN_CHECK_P(select_turn_check);
LIB_PRIVATE TURN_CHECK_P(setsockopt_turn_check);
LIB_PRIVATE TURN_CHECK_P(getsockopt_turn_check);
LIB_PRIVATE TURN_CHECK_P(ioctl_turn_check);
LIB_PRIVATE TURN_CHECK_P(signal_handler_turn_check);
LIB_PRIVATE TURN_CHECK_P(sigwait_turn_check);
LIB_PRIVATE TURN_CHECK_P(srand_turn_check);
LIB_PRIVATE TURN_CHECK_P(socket_turn_check);
LIB_PRIVATE TURN_CHECK_P(time_turn_check);
LIB_PRIVATE TURN_CHECK_P(unlink_turn_check);
LIB_PRIVATE TURN_CHECK_P(user_turn_check);
LIB_PRIVATE TURN_CHECK_P(write_turn_check);
LIB_PRIVATE TURN_CHECK_P(xstat_turn_check);
LIB_PRIVATE TURN_CHECK_P(xstat64_turn_check);

#endif // RECORD_REPLAY
#endif // pthread_WRAPPERS_H
