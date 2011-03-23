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
#include "dmtcpalloc.h"

#ifdef __x86_64__
typedef long long int clone_id_t;
typedef unsigned long long int log_id_t;
#else
typedef long int clone_id_t;
typedef unsigned long int log_id_t;
#endif

namespace dmtcp { class SynchronizationLog; }

#define LIB_PRIVATE __attribute__ ((visibility ("hidden")))

#define MAX_LOG_LENGTH ((size_t)512 * 1024 * 1024) // = 4096*4096. For what reason?
#define MAX_PATCH_LIST_LENGTH MAX_LOG_LENGTH
#define READLINK_MAX_LENGTH 256
#define WAKE_ALL_THREADS -1
#define SYNC_IS_REPLAY    (sync_logging_branch == 2)
#define SYNC_IS_LOG       (sync_logging_branch == 1)
#define SYNC_IS_RECORD    SYNC_IS_LOG
#define SET_SYNC_REPLAY() (sync_logging_branch = 2)
#define SET_SYNC_LOG()    (sync_logging_branch = 1)
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

#define WRAPPER_HEADER(ret_type, name, real_func, ...)                \
  void *return_addr = GET_RETURN_ADDRESS();                           \
  if (!shouldSynchronize(return_addr)) {                              \
    return real_func(__VA_ARGS__);                                    \
  }                                                                   \
  if (jalib::Filesystem::GetProgramName() == "gdb") {                 \
    return real_func(__VA_ARGS__);                                    \
  }                                                                   \
  ret_type retval;                                                    \
  log_entry_t my_entry = create_##name##_entry(my_clone_id,           \
      name##_event, __VA_ARGS__);                                     \
  log_entry_t my_return_entry = create_##name##_entry(my_clone_id,    \
      name##_event_return, __VA_ARGS__);

#define WRAPPER_HEADER_VOID(name, real_func, ...)                     \
  void *return_addr = GET_RETURN_ADDRESS();                           \
  if (!shouldSynchronize(return_addr)) {                              \
    real_func(__VA_ARGS__);                                           \
    return;                                                           \
  }                                                                   \
  if (jalib::Filesystem::GetProgramName() == "gdb") {                 \
    real_func(__VA_ARGS__);                                           \
    return;                                                           \
  }                                                                   \
  log_entry_t my_entry = create_##name##_entry(my_clone_id,           \
      name##_event, __VA_ARGS__);                                     \
  log_entry_t my_return_entry = create_##name##_entry(my_clone_id,    \
      name##_event_return, __VA_ARGS__);

#define WRAPPER_REPLAY(name)                                        \
  do {                                                              \
    waitForTurn(my_entry, &name##_turn_check);                      \
    getNextLogEntry();                                              \
    waitForTurn(my_return_entry, &name##_turn_check);               \
    retval = GET_COMMON(currentLogEntry, retval);                   \
    if (GET_COMMON(currentLogEntry, my_errno) != 0) {               \
      errno = GET_COMMON(currentLogEntry, my_errno);                \
    }                                                               \
    getNextLogEntry();                                              \
  } while (0)

#define WRAPPER_REPLAY_VOID(name)                                   \
  do {                                                              \
    waitForTurn(my_entry, &name##_turn_check);                      \
    getNextLogEntry();                                              \
    waitForTurn(my_return_entry, &name##_turn_check);               \
    if (GET_COMMON(currentLogEntry, my_errno) != 0) {               \
      errno = GET_COMMON(currentLogEntry, my_errno);                \
    }                                                               \
    getNextLogEntry();                                              \
  } while (0)

#define WRAPPER_LOG(real_func, ...)                                 \
  do {                                                              \
    addNextLogEntry(my_entry);                                      \
    retval = real_func(__VA_ARGS__);                                \
    SET_COMMON(my_return_entry, retval);                            \
    if (errno != 0) {                                               \
      SET_COMMON2(my_return_entry, my_errno, errno);                \
    }                                                               \
    addNextLogEntry(my_return_entry);                               \
  } while (0)

#define WRAPPER_LOG_VOID(real_func, ...)                            \
  do {                                                              \
    addNextLogEntry(my_entry);                                      \
    real_func(__VA_ARGS__);                                         \
    if (errno != 0) {                                               \
      SET_COMMON2(my_return_entry, my_errno, errno);                \
    }                                                               \
    addNextLogEntry(my_return_entry);                               \
  } while (0)

/* Your basic record wrapper template. Does not call _real_func on
   replay, but restores the return value and errno from the log. Also, the
   create_func_entry() function must handle the variable arguments and casting
   to correct types. */
#define BASIC_SYNC_WRAPPER(ret_type, name, real_func, ...)          \
  WRAPPER_HEADER(ret_type, name, real_func, __VA_ARGS__);           \
  if (SYNC_IS_REPLAY) {                                             \
    WRAPPER_REPLAY(name);                                           \
  } else if (SYNC_IS_LOG) {                                         \
    WRAPPER_LOG(real_func, __VA_ARGS__);                            \
  }                                                                 \
  return retval;

#define BASIC_SYNC_WRAPPER_VOID(name, real_func, ...)               \
  WRAPPER_HEADER_VOID(name, real_func, __VA_ARGS__);                \
  if (SYNC_IS_REPLAY) {                                             \
    WRAPPER_REPLAY_VOID(name);                                      \
  } else if (SYNC_IS_LOG) {                                         \
    WRAPPER_LOG_VOID(real_func, __VA_ARGS__);                       \
  }

#define FOREACH_NAME(MACRO, ...)                                               \
  do {                                                                         \
    MACRO(accept, __VA_ARGS__);                                                \
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
    MACRO(gettimeofday, __VA_ARGS__);                                          \
    MACRO(fgetc, __VA_ARGS__);                                                 \
    MACRO(ungetc, __VA_ARGS__);                                                \
    MACRO(getline, __VA_ARGS__);                                               \
    MACRO(getpeername, __VA_ARGS__);                                           \
    MACRO(getsockname, __VA_ARGS__);                                           \
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
  accept_event_return,
  access_event,
  access_event_return,
  bind_event,
  bind_event_return,
  calloc_event,
  calloc_event_return,
  close_event,
  close_event_return,
  closedir_event,
  closedir_event_return,
  connect_event,
  connect_event_return,
  dup_event,
  dup_event_return,
  exec_barrier_event,
  exec_barrier_event_return, // unused;
  fclose_event,
  fclose_event_return,
  fcntl_event,
  fcntl_event_return,
  fdatasync_event,
  fdatasync_event_return,
  fdopen_event,
  fdopen_event_return,
  fgets_event,
  fgets_event_return,
  fflush_event,
  fflush_event_return,
  fopen_event,
  fopen_event_return,
  fopen64_event,
  fopen64_event_return,
  fprintf_event,
  fprintf_event_return,
  fscanf_event,
  fscanf_event_return,
  fputs_event,
  fputs_event_return,
  free_event,
  free_event_return,
  fsync_event,
  fsync_event_return,
  ftell_event,
  ftell_event_return,
  fwrite_event,
  fwrite_event_return,
  fxstat_event,
  fxstat_event_return,
  fxstat64_event,
  fxstat64_event_return,
  getc_event,
  getc_event_return,
  gettimeofday_event,
  gettimeofday_event_return,
  fgetc_event,
  fgetc_event_return,
  ungetc_event,
  ungetc_event_return,
  getline_event,
  getline_event_return,
  getpeername_event,
  getpeername_event_return,
  getsockname_event,
  getsockname_event_return,
  libc_memalign_event,
  libc_memalign_event_return,
  link_event,
  link_event_return,
  listen_event,
  listen_event_return,
  lseek_event,
  lseek_event_return,
  lxstat_event,
  lxstat_event_return,
  lxstat64_event,
  lxstat64_event_return,
  malloc_event,
  malloc_event_return,
  mkdir_event,
  mkdir_event_return,
  mkstemp_event,
  mkstemp_event_return,
  mmap_event,
  mmap_event_return,
  mmap64_event,
  mmap64_event_return,
  mremap_event,
  mremap_event_return,
  munmap_event,
  munmap_event_return,
  open_event,
  open_event_return,
  open64_event,
  open64_event_return,
  opendir_event,
  opendir_event_return,
  pread_event,
  pread_event_return,
  putc_event,
  putc_event_return,
  pwrite_event,
  pwrite_event_return,
  pthread_detach_event,
  pthread_detach_event_return,
  pthread_create_event,
  pthread_create_event_return,
  // Remove these... not needed anymore.
//  pthread_cond_broadcast_anomalous_event,
//  pthread_cond_broadcast_anomalous_event_return, // unused;
//  pthread_cond_signal_anomalous_event,
//  pthread_cond_signal_anomalous_event_return, // unused;
  pthread_cond_broadcast_event,
  pthread_cond_broadcast_event_return,
  pthread_mutex_lock_event,
  pthread_mutex_lock_event_return, // unused;
  pthread_mutex_trylock_event,
  pthread_mutex_trylock_event_return, // USED!
  pthread_cond_signal_event,
  pthread_cond_signal_event_return,
  pthread_mutex_unlock_event,
  pthread_mutex_unlock_event_return, // unused;
  pthread_cond_wait_event,
  pthread_cond_wait_event_return,
  pthread_cond_timedwait_event,
  pthread_cond_timedwait_event_return,
  pthread_exit_event,
  pthread_exit_event_return, // unused;
  pthread_join_event,
  pthread_join_event_return,
  pthread_kill_event, // no return event -- asynchronous
  pthread_kill_event_return, // unused;
  pthread_rwlock_unlock_event,
  pthread_rwlock_unlock_event_return, // unused;
  pthread_rwlock_rdlock_event,
  pthread_rwlock_rdlock_event_return,
  pthread_rwlock_wrlock_event,
  pthread_rwlock_wrlock_event_return,
  rand_event,
  rand_event_return,
  read_event,
  read_event_return,
  readdir_event,
  readdir_event_return,
  readdir_r_event,
  readdir_r_event_return,
  readlink_event,
  readlink_event_return,
  realloc_event,
  realloc_event_return,
  rename_event,
  rename_event_return,
  rewind_event,
  rewind_event_return,
  rmdir_event,
  rmdir_event_return,
  select_event,
  select_event_return,
  signal_handler_event,
  signal_handler_event_return,
  sigwait_event,
  sigwait_event_return,
  setsockopt_event,
  setsockopt_event_return,
  socket_event,
  socket_event_return,
  srand_event,
  srand_event_return,
  time_event,
  time_event_return,
  unlink_event,
  unlink_event_return,
  user_event,
  user_event_return,
  write_event,
  write_event_return,
  xstat_event,
  xstat_event_return,
  xstat64_event,
  xstat64_event_return
} event_code_t;
/* end event codes */

typedef struct {
  // For pthread_mutex_lock():
  unsigned long int mutex;
} log_event_pthread_mutex_lock_t;

static const int log_event_pthread_mutex_lock_size = sizeof(log_event_pthread_mutex_lock_t);

typedef struct {
  // For pthread_mutex_trylock():
  unsigned long int mutex;
} log_event_pthread_mutex_trylock_t;

static const int log_event_pthread_mutex_trylock_size = sizeof(log_event_pthread_mutex_trylock_t);

typedef struct {
  // For pthread_mutex_unlock():
  unsigned long int mutex;
} log_event_pthread_mutex_unlock_t;

static const int log_event_pthread_mutex_unlock_size = sizeof(log_event_pthread_mutex_unlock_t);

typedef struct {
  // For pthread_cond_signal():
  unsigned long int cond_var;
  int signal_target;
} log_event_pthread_cond_signal_t;

static const int log_event_pthread_cond_signal_size = sizeof(log_event_pthread_cond_signal_t);

typedef struct {
  // For pthread_cond_broadcast():
  unsigned long int cond_var;
  int signal_target;
} log_event_pthread_cond_broadcast_t;

static const int log_event_pthread_cond_broadcast_size = sizeof(log_event_pthread_cond_broadcast_t);

typedef struct {
  // For pthread_cond_wait():
  unsigned long int mutex;
  unsigned long int cond_var;
} log_event_pthread_cond_wait_t;

static const int log_event_pthread_cond_wait_size = sizeof(log_event_pthread_cond_wait_t);

typedef struct {
  // For pthread_cond_timedwait():
  unsigned long int mutex;
  unsigned long int cond_var;
  unsigned long int abstime;
} log_event_pthread_cond_timedwait_t;

static const int log_event_pthread_cond_timedwait_size = sizeof(log_event_pthread_cond_timedwait_t);

typedef struct {
  // For pthread_rwlock_unlock():
  unsigned long int rwlock;
} log_event_pthread_rwlock_unlock_t;

static const int log_event_pthread_rwlock_unlock_size = sizeof(log_event_pthread_rwlock_unlock_t);

typedef struct {
  // For pthread_rwlock_rdlock():
  unsigned long int rwlock;
} log_event_pthread_rwlock_rdlock_t;

static const int log_event_pthread_rwlock_rdlock_size = sizeof(log_event_pthread_rwlock_rdlock_t);

typedef struct {
  // For pthread_rwlock_wrlock():
  unsigned long int rwlock;
} log_event_pthread_rwlock_wrlock_t;

static const int log_event_pthread_rwlock_wrlock_size = sizeof(log_event_pthread_rwlock_wrlock_t);

typedef struct {
  // For pthread_exit():
  unsigned long int value_ptr;
} log_event_pthread_exit_t;

static const int log_event_pthread_exit_size = sizeof(log_event_pthread_exit_t);

typedef struct {
  // For pthread_join():
  unsigned long int thread;
  unsigned long int value_ptr;
} log_event_pthread_join_t;

static const int log_event_pthread_join_size = sizeof(log_event_pthread_join_t);

typedef struct {
  // For pthread_kill():
  unsigned long int thread;
  int sig;
} log_event_pthread_kill_t;

static const int log_event_pthread_kill_size = sizeof(log_event_pthread_kill_t);

typedef struct {
  // For rand():
} log_event_rand_t;

static const int log_event_rand_size = sizeof(log_event_rand_t);

typedef struct {
  // For rename():
  unsigned long int oldpath;
  unsigned long int newpath;
} log_event_rename_t;

static const int log_event_rename_size = sizeof(log_event_rename_t);

typedef struct {
  // For rewind():
  unsigned long int stream;
} log_event_rewind_t;

static const int log_event_rewind_size = sizeof(log_event_rewind_t);

typedef struct {
  // For rmdir():
  unsigned long int pathname;
} log_event_rmdir_t;

static const int log_event_rmdir_size = sizeof(log_event_rmdir_t);

typedef struct {
  // For select():
  int nfds;
  fd_set readfds;
  fd_set writefds;
  unsigned long int exceptfds; // just save address for now
  unsigned long int timeout;
} log_event_select_t;

static const int log_event_select_size = sizeof(log_event_select_t);

typedef struct {
  // For signal handlers:
  int sig;
} log_event_signal_handler_t;

static const int log_event_signal_handler_size = sizeof(log_event_signal_handler_t);

typedef struct {
  // For sigwait():
  unsigned long int set;
  unsigned long int sigwait_sig;
  int sig;
} log_event_sigwait_t;

static const int log_event_sigwait_size = sizeof(log_event_sigwait_t);

typedef struct {
  // For read():
  int readfd;
  unsigned long int buf_addr;
  size_t count;
  off_t data_offset; // offset into read saved data file
} log_event_read_t;

static const int log_event_read_size = sizeof(log_event_read_t);

typedef struct {
  // For readdir():
  unsigned long int dirp;
  struct dirent retval;
} log_event_readdir_t;

static const int log_event_readdir_size = sizeof(log_event_readdir_t);

typedef struct {
  // For readdir_r():
  unsigned long int dirp;
  struct dirent entry;
  unsigned int result : 1; // Should be either 1 or 0
} log_event_readdir_r_t;

static const int log_event_readdir_r_size = sizeof(log_event_readdir_r_t);

typedef struct {
  // For readlink():
  unsigned long int path;
  char buf[READLINK_MAX_LENGTH];
  size_t bufsiz;
} log_event_readlink_t;

static const int log_event_readlink_size = sizeof(log_event_readlink_t);

typedef struct {
  // For unlink():
  unsigned long int pathname;
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
  unsigned long int buf_addr;
  size_t count;
} log_event_write_t;

static const int log_event_write_size = sizeof(log_event_write_t);

typedef struct {
  // For accept():
  int sockfd;
  unsigned long int sockaddr;
  unsigned long int addrlen;
} log_event_accept_t;

static const int log_event_accept_size = sizeof(log_event_accept_t);

typedef struct {
  // For access():
  unsigned long int pathname;
  int mode;
} log_event_access_t;

static const int log_event_access_size = sizeof(log_event_access_t);

typedef struct {
  // For bind():
  int sockfd;
  unsigned long int my_addr;
  socklen_t addrlen;
} log_event_bind_t;

static const int log_event_bind_size = sizeof(log_event_bind_t);

typedef struct {
  // For getpeername():
  int sockfd;
  struct sockaddr sockaddr;
  unsigned long int addrlen;
} log_event_getpeername_t;

static const int log_event_getpeername_size = sizeof(log_event_getpeername_t);

typedef struct {
  // For getsockname():
  int sockfd;
  unsigned long int sockaddr;
  unsigned long int addrlen;
} log_event_getsockname_t;

static const int log_event_getsockname_size = sizeof(log_event_getsockname_t);

typedef struct {
  // For setsockopt():
  int sockfd;
  int level;
  int optname;
  unsigned long int optval;
  socklen_t optlen;
} log_event_setsockopt_t;

static const int log_event_setsockopt_size = sizeof(log_event_setsockopt_t);

typedef struct {
  // For pthread_create():
  unsigned long int thread;
  unsigned long int attr;
  unsigned long int start_routine;
  unsigned long int arg;
  unsigned long int stack_addr;
  size_t stack_size;
} log_event_pthread_create_t;

static const int log_event_pthread_create_size = sizeof(log_event_pthread_create_t);

typedef struct {
  // For pthread_detach():
  unsigned long int thread;
} log_event_pthread_detach_t;

static const int log_event_pthread_detach_size = sizeof(log_event_pthread_detach_t);

typedef struct {
  // For __libc_memalign():
  size_t boundary;
  size_t size;
  unsigned long int return_ptr;
} log_event_libc_memalign_t;

static const int log_event_libc_memalign_size = sizeof(log_event_libc_memalign_t);

typedef struct {
  // For fclose():
  unsigned long int fp;
} log_event_fclose_t;

static const int log_event_fclose_size = sizeof(log_event_fclose_t);

typedef struct {
  // For fcntl():
  int fd;
  int cmd;
  long arg_3_l;
  unsigned long int arg_3_f;
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
  unsigned long int mode;
  // Size is approximately 216 bytes:
  FILE fdopen_retval;
} log_event_fdopen_t;

static const int log_event_fdopen_size = sizeof(log_event_fdopen_t);

typedef struct {
  // For fgets():
  unsigned long int s;
  int size;
  unsigned long int stream;
  char *retval;
  off_t data_offset;
} log_event_fgets_t;

static const int log_event_fgets_size = sizeof(log_event_fgets_t);

typedef struct {
  // For fflush():
  unsigned long int stream;
} log_event_fflush_t;

static const int log_event_fflush_size = sizeof(log_event_fflush_t);

typedef struct {
  // For fopen():
  unsigned long int name;
  unsigned long int mode;
  // Size is approximately 216 bytes:
  FILE fopen_retval;
} log_event_fopen_t;

static const int log_event_fopen_size = sizeof(log_event_fopen_t);

typedef struct {
  // For fopen64():
  unsigned long int name;
  unsigned long int mode;
  // Size is approximately 216 bytes:
  FILE fopen64_retval;
} log_event_fopen64_t;

static const int log_event_fopen64_size = sizeof(log_event_fopen64_t);

typedef struct {
  // For fprintf():
  unsigned long int stream;
  unsigned long int format;
} log_event_fprintf_t;

static const int log_event_fprintf_size = sizeof(log_event_fprintf_t);

typedef struct {
  // For fscanf():
  unsigned long int stream;
  unsigned long int format;
  int bytes;
  ssize_t retval;
  off_t data_offset;
} log_event_fscanf_t;

static const int log_event_fscanf_size = sizeof(log_event_fscanf_t);

typedef struct {
  // For fputs():
  unsigned long int s;
  unsigned long int stream;
} log_event_fputs_t;

static const int log_event_fputs_size = sizeof(log_event_fputs_t);

typedef struct {
  // For getc():
  unsigned long int stream;
} log_event_getc_t;

static const int log_event_getc_size = sizeof(log_event_getc_t);

typedef struct {
  // For gettimeofday():
  unsigned long int tv;
  unsigned long int tz;
  struct timeval tv_val;
  struct timezone tz_val;
  int gettimeofday_retval;
} log_event_gettimeofday_t;

static const int log_event_gettimeofday_size = sizeof(log_event_gettimeofday_t);

typedef struct {
  // For fgetc():
  unsigned long int stream;
} log_event_fgetc_t;

static const int log_event_fgetc_size = sizeof(log_event_fgetc_t);

typedef struct {
  // For ungetc():
  int c;
  unsigned long int stream;
} log_event_ungetc_t;

static const int log_event_ungetc_size = sizeof(log_event_ungetc_t);

typedef struct {
  // For getline():
  char *lineptr;
  size_t n;
  unsigned long int stream;
  ssize_t retval;
  off_t data_offset;
  unsigned int is_realloc : 1;
} log_event_getline_t;

static const int log_event_getline_size = sizeof(log_event_getline_t);

typedef struct {
  // For link():
  unsigned long int oldpath;
  unsigned long int newpath;
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
  unsigned long int path;
  struct stat buf;
} log_event_lxstat_t;

static const int log_event_lxstat_size = sizeof(log_event_lxstat_t);

typedef struct {
  // For lxstat64():
  int vers;
  unsigned long int path;
  struct stat64 buf;
} log_event_lxstat64_t;

static const int log_event_lxstat64_size = sizeof(log_event_lxstat64_t);

typedef struct {
  // For malloc():
  size_t size;
  unsigned long int return_ptr;
} log_event_malloc_t;

static const int log_event_malloc_size = sizeof(log_event_malloc_t);

typedef struct {
  // For mkdir():
  unsigned long int pathname;
  mode_t mode;
} log_event_mkdir_t;

static const int log_event_mkdir_size = sizeof(log_event_mkdir_t);

typedef struct {
  // For mkstemp():
  unsigned long int temp;
} log_event_mkstemp_t;

static const int log_event_mkstemp_size = sizeof(log_event_mkstemp_t);

typedef struct {
  // For mmap():
  unsigned long int addr;
  size_t length;
  int prot;
  int flags;
  int fd;
  off_t offset;
  unsigned long int retval;
} log_event_mmap_t;

static const int log_event_mmap_size = sizeof(log_event_mmap_t);

typedef struct {
  // For mmap64():
  unsigned long int addr;
  size_t length;
  int prot;
  int flags;
  int fd;
  off64_t offset;
  unsigned long int retval;
} log_event_mmap64_t;

static const int log_event_mmap64_size = sizeof(log_event_mmap64_t);

typedef struct {
  // For mremap():
  unsigned long int old_address;
  size_t old_size;
  size_t new_size;
  int flags;
  unsigned long int retval;
} log_event_mremap_t;

static const int log_event_mremap_size = sizeof(log_event_mremap_t);

typedef struct {
  // For munmap():
  unsigned long int addr;
  size_t length;
} log_event_munmap_t;

static const int log_event_munmap_size = sizeof(log_event_munmap_t);

typedef struct {
  // For open():
  unsigned long int path;
  int flags;
  mode_t open_mode;
} log_event_open_t;

static const int log_event_open_size = sizeof(log_event_open_t);

typedef struct {
  // For open64():
  unsigned long int path;
  int flags;
  mode_t open_mode;
} log_event_open64_t;

static const int log_event_open64_size = sizeof(log_event_open64_t);

typedef struct {
  // For opendir():
  unsigned long int name;
  DIR * opendir_retval;
} log_event_opendir_t;

static const int log_event_opendir_size = sizeof(log_event_opendir_t);

typedef struct {
  // For pread():
  int fd;
  unsigned long int buf;
  size_t count;
  off_t offset;
  off_t data_offset; // offset into read saved data file
} log_event_pread_t;

static const int log_event_pread_size = sizeof(log_event_pread_t);

typedef struct {
  // For putc():
  int c;
  unsigned long int stream;
} log_event_putc_t;

static const int log_event_putc_size = sizeof(log_event_putc_t);

typedef struct {
  // For pwrite():
  int fd;
  unsigned long int buf;
  size_t count;
  off_t offset;
} log_event_pwrite_t;

static const int log_event_pwrite_size = sizeof(log_event_pwrite_t);

typedef struct {
  // For calloc():
  size_t nmemb;
  size_t size;
  unsigned long int return_ptr;
} log_event_calloc_t;

static const int log_event_calloc_size = sizeof(log_event_calloc_t);

typedef struct {
  // For close():
  int fd;
} log_event_close_t;

static const int log_event_close_size = sizeof(log_event_close_t);

typedef struct {
  // For closedir():
  unsigned long int dirp;
} log_event_closedir_t;

static const int log_event_closedir_size = sizeof(log_event_closedir_t);

typedef struct {
  // For connect():
  int sockfd;
  unsigned long int serv_addr;
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
  unsigned long int ptr;
  unsigned long int return_ptr;
} log_event_realloc_t;

static const int log_event_realloc_size = sizeof(log_event_realloc_t);

typedef struct {
  // For free():
  unsigned long int ptr;
} log_event_free_t;

static const int log_event_free_size = sizeof(log_event_free_t);

typedef struct {
  // For ftell():
  unsigned long int stream;
} log_event_ftell_t;

static const int log_event_ftell_size = sizeof(log_event_ftell_t);

typedef struct {
  // For fwrite():
  unsigned long int ptr;
  size_t size;
  size_t nmemb;
  unsigned long int stream;
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
  unsigned long int tloc;
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
  unsigned long int path;
  struct stat buf;
} log_event_xstat_t;

static const int log_event_xstat_size = sizeof(log_event_xstat_t);

typedef struct {
  // For xstat64():
  int vers;
  unsigned long int path;
  struct stat64 buf;
} log_event_xstat64_t;

static const int log_event_xstat64_size = sizeof(log_event_xstat64_t);

typedef struct {
  // FIXME:
  //event_code_t event;
  unsigned char event;
  log_id_t log_id;
  clone_id_t clone_id;
  int my_errno;
  int retval;
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
    log_event_getpeername_t                      log_event_getpeername;
    log_event_getsockname_t                      log_event_getsockname;
    log_event_setsockopt_t                       log_event_setsockopt;
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
  (sizeof(GET_COMMON(currentLogEntry,event))       +                    \
      sizeof(GET_COMMON(currentLogEntry,log_id))   +                    \
      sizeof(GET_COMMON(currentLogEntry,clone_id)) +                    \
      sizeof(GET_COMMON(currentLogEntry,my_errno)) +                    \
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


#define IFNAME_GET_EVENT_SIZE(name, event, event_size)                  \
  do {                                                                  \
    if (event == name##_event || event == name##_event_return)          \
      event_size = log_event_##name##_size;                             \
  } while(0)

#define GET_EVENT_SIZE(event, event_size)                               \
  do {                                                                  \
    FOREACH_NAME(IFNAME_GET_EVENT_SIZE, event, event_size);             \
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
static const log_entry_t EMPTY_LOG_ENTRY = {{0, 0, 0, 0, 0}};
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
LIB_PRIVATE extern unsigned long   default_stack_size;

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
LIB_PRIVATE void   addNextLogEntry(log_entry_t);
LIB_PRIVATE void   atomic_increment(volatile int *ptr);
LIB_PRIVATE void   atomic_decrement(volatile int *ptr);
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
LIB_PRIVATE int    validAddress(unsigned long int addr);
LIB_PRIVATE ssize_t writeAll(int fd, const void *buf, size_t count);
LIB_PRIVATE void   writeLogsToDisk();

LIB_PRIVATE log_entry_t create_accept_entry(clone_id_t clone_id, int event, int sockfd,
    unsigned long int sockaddr, unsigned long int addrlen);
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
    int cmd, long arg_3_l, unsigned long int arg_3_f);
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
    FILE *stream, const char *format);
LIB_PRIVATE log_entry_t create_fscanf_entry(clone_id_t clone_id, int event,
    FILE *stream, const char *format);
LIB_PRIVATE log_entry_t create_fputs_entry(clone_id_t clone_id, int event,
    const char *s, FILE *stream);
LIB_PRIVATE log_entry_t create_free_entry(clone_id_t clone_id, int event,
    unsigned long int ptr);
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
    int sockfd, struct sockaddr sockaddr, unsigned long int addrlen);
LIB_PRIVATE log_entry_t create_getsockname_entry(clone_id_t clone_id, int event,
    int sockfd, unsigned long int sockaddr, unsigned long int addrlen);
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
    void *old_address, size_t old_size, size_t new_size, int flags);
LIB_PRIVATE log_entry_t create_open_entry(clone_id_t clone_id, int event,
    const char *path, int flags, mode_t mode);
LIB_PRIVATE log_entry_t create_open64_entry(clone_id_t clone_id, int event,
    const char *path, int flags, mode_t mode);
LIB_PRIVATE log_entry_t create_opendir_entry(clone_id_t clone_id, int event,
    const char *name);
LIB_PRIVATE log_entry_t create_pread_entry(clone_id_t clone_id, int event, int fd,
    unsigned long int buf, size_t count, off_t offset);
LIB_PRIVATE log_entry_t create_putc_entry(clone_id_t clone_id, int event, int c,
    FILE *stream);
LIB_PRIVATE log_entry_t create_pwrite_entry(clone_id_t clone_id, int event, int fd,
    unsigned long int buf, size_t count, off_t offset);
LIB_PRIVATE log_entry_t create_pthread_cond_broadcast_entry(clone_id_t clone_id, int event,
    unsigned long int cond_var);
LIB_PRIVATE log_entry_t create_pthread_cond_signal_entry(clone_id_t clone_id, int event,
    unsigned long int cond_var);
LIB_PRIVATE log_entry_t create_pthread_cond_wait_entry(clone_id_t clone_id, int event,
    unsigned long int mutex, unsigned long int cond_var);
LIB_PRIVATE log_entry_t create_pthread_cond_timedwait_entry(clone_id_t clone_id, int event,
    unsigned long int mutex, unsigned long int cond_var, unsigned long int abstime);
LIB_PRIVATE log_entry_t create_pthread_rwlock_unlock_entry(clone_id_t clone_id,
    int event, unsigned long int rwlock);
LIB_PRIVATE log_entry_t create_pthread_rwlock_rdlock_entry(clone_id_t clone_id,
    int event, unsigned long int rwlock);
LIB_PRIVATE log_entry_t create_pthread_rwlock_wrlock_entry(clone_id_t clone_id,
    int event, unsigned long int rwlock);
LIB_PRIVATE log_entry_t create_pthread_create_entry(clone_id_t clone_id,
    int event, unsigned long int thread, unsigned long int attr,
    unsigned long int start_routine, unsigned long int arg);
LIB_PRIVATE log_entry_t create_pthread_detach_entry(clone_id_t clone_id,
    int event, unsigned long int thread);
LIB_PRIVATE log_entry_t create_pthread_exit_entry(clone_id_t clone_id,
    int event, unsigned long int value_ptr);
LIB_PRIVATE log_entry_t create_pthread_join_entry(clone_id_t clone_id,
    int event, unsigned long int thread, unsigned long int value_ptr);
LIB_PRIVATE log_entry_t create_pthread_kill_entry(clone_id_t clone_id,
    int event, unsigned long int thread, int sig);
LIB_PRIVATE log_entry_t create_pthread_mutex_lock_entry(clone_id_t clone_id, int event,
    unsigned long int mutex);
LIB_PRIVATE log_entry_t create_pthread_mutex_trylock_entry(clone_id_t clone_id, int event,
    unsigned long int mutex);
LIB_PRIVATE log_entry_t create_pthread_mutex_unlock_entry(clone_id_t clone_id, int event,
    unsigned long int mutex);
LIB_PRIVATE log_entry_t create_rand_entry(clone_id_t clone_id, int event);
LIB_PRIVATE log_entry_t create_read_entry(clone_id_t clone_id, int event, int readfd,
    unsigned long int buf_addr, size_t count);
LIB_PRIVATE log_entry_t create_readdir_entry(clone_id_t clone_id, int event,
    DIR *dirp);
LIB_PRIVATE log_entry_t create_readdir_r_entry(clone_id_t clone_id, int event,
    DIR *dirp, struct dirent *entry, struct dirent **result);
LIB_PRIVATE log_entry_t create_readlink_entry(clone_id_t clone_id, int event,
    const char *path, char *buf, size_t bufsiz);
LIB_PRIVATE log_entry_t create_realloc_entry(clone_id_t clone_id, int event,
    unsigned long int ptr, size_t size);
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
    int sockfd, int level, int optname, unsigned long int optval,
    socklen_t optlen);
LIB_PRIVATE log_entry_t create_signal_handler_entry(clone_id_t clone_id, int event,
    int sig);
LIB_PRIVATE log_entry_t create_sigwait_entry(clone_id_t clone_id, int event,
    unsigned long int set, unsigned long int sigwait_sig);
LIB_PRIVATE log_entry_t create_srand_entry(clone_id_t clone_id, int event,
    unsigned int seed);
LIB_PRIVATE log_entry_t create_socket_entry(clone_id_t clone_id, int event,
    int domain, int type, int protocol);
LIB_PRIVATE log_entry_t create_xstat_entry(clone_id_t clone_id, int event, int vers,
    const char *path, struct stat *buf);
LIB_PRIVATE log_entry_t create_xstat64_entry(clone_id_t clone_id, int event, int vers,
    const char *path, struct stat64 *buf);
LIB_PRIVATE log_entry_t create_time_entry(clone_id_t clone_id, int event,
    unsigned long int tloc);
LIB_PRIVATE log_entry_t create_unlink_entry(clone_id_t clone_id, int event,
    const char *pathname);
LIB_PRIVATE log_entry_t create_user_entry(clone_id_t clone_id, int event);
LIB_PRIVATE log_entry_t create_write_entry(clone_id_t clone_id, int event,
    int writefd, unsigned long int buf_addr, size_t count);

LIB_PRIVATE void waitForTurn(log_entry_t my_entry, turn_pred_t pred);
LIB_PRIVATE void waitForExecBarrier();

/* Turn check predicates: */
LIB_PRIVATE TURN_CHECK_P(accept_turn_check);
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
