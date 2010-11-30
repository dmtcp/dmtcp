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

#include <errno.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include "dmtcpworker.h"
#include "protectedfds.h"
#include "syscallwrappers.h"
#include "dmtcpmessagetypes.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jtimer.h"
#include  "../jalib/jfilesystem.h"
#include <sys/select.h>
#include "synchronizationlogging.h"
#include <sys/resource.h>

#ifdef SYNCHRONIZATION_LOG_AND_REPLAY

#ifdef SYNCHRONIZATION_LOG_AND_REPLAY_DEBUG
LIB_PRIVATE log_entry_t     log[MAX_LOG_LENGTH] = { EMPTY_LOG_ENTRY };
#else
LIB_PRIVATE char log[MAX_LOG_LENGTH] = { 0 };
#endif

#ifndef SYNCHRONIZATION_LOG_AND_REPLAY_DEBUG

/* IMPOTANT: if new fields are added to log_entry_t in the common area,
 * update the following. */
#define log_event_common_size \
  (sizeof(GET_COMMON(currentLogEntry,event))       +                    \
      sizeof(GET_COMMON(currentLogEntry,log_id))   +                    \
      sizeof(GET_COMMON(currentLogEntry,tid))      +                    \
      sizeof(GET_COMMON(currentLogEntry,clone_id)) +                    \
      sizeof(GET_COMMON(currentLogEntry,my_errno)) +                    \
      sizeof(GET_COMMON(currentLogEntry,retval)))

#define IFNAME_GET_EVENT_SIZE(name, event, event_size)                         \
  do {                                                                         \
    if (event == name##_event || event == name##_event_return)                 \
      event_size = log_event_##name##_size;                                    \
  } while(0)

#define IFNAME_COPY_TO_MEMORY_LOG(name, e)                              \
  do {                                                                  \
    if (GET_COMMON(e,event) == name##_event ||                          \
        GET_COMMON(e,event) == name##_event_return) {                   \
      memcpy(&log[log_index],                                           \
          &e.log_event_t.log_event_##name, log_event_##name##_size);    \
      memcpy(&currentLogEntry.log_event_t.log_event_##name,             \
          &e.log_event_t.log_event_##name, log_event_##name##_size);    \
    }                                                                   \
  } while(0)

#define IFNAME_COPY_FROM_MEMORY_SOURCE(name, source)                    \
  do {                                                                  \
    if (GET_COMMON(currentLogEntry,event) == name##_event ||             \
        GET_COMMON(currentLogEntry,event) == name##_event_return) {     \
      memcpy(&currentLogEntry.log_event_t.log_event_##name,             \
          &source, log_event_##name##_size);                            \
    }                                                                   \
  } while(0)

#define IFNAME_READ_ENTRY_FROM_DISK(name, fd, entry)                    \
  do {                                                                  \
    if (GET_COMMON_PTR(entry,event) == name##_event ||                  \
        GET_COMMON_PTR(entry,event) == name##_event_return) {           \
      return _real_read(fd, &entry->log_event_t.log_event_##name,       \
          log_event_##name##_size);                                     \
    }                                                                   \
  } while(0)

#define IFNAME_WRITE_ENTRY_TO_DISK(name, fd, entry, ret)                \
  do {                                                                  \
    if (GET_COMMON(entry,event) == name##_event ||                      \
        GET_COMMON(entry,event) == name##_event_return) {               \
      ret = write(fd, &entry.log_event_t.log_event_##name,              \
          log_event_##name##_size);                                     \
    }                                                                   \
  } while(0)

#define FOREACH_NAME(MACRO, ...)                                               \
  do {                                                                         \
    MACRO(accept, __VA_ARGS__);                                                \
    MACRO(access, __VA_ARGS__);                                                \
    MACRO(bind, __VA_ARGS__);                                                  \
    MACRO(calloc, __VA_ARGS__);                                                \
    MACRO(close, __VA_ARGS__);                                                 \
    MACRO(connect, __VA_ARGS__);                                               \
    MACRO(dup, __VA_ARGS__);                                                   \
    MACRO(exec_barrier, __VA_ARGS__);                                          \
    MACRO(fclose, __VA_ARGS__);                                                \
    MACRO(fcntl, __VA_ARGS__);                                                 \
    MACRO(fdatasync, __VA_ARGS__);                                             \
    MACRO(fdopen, __VA_ARGS__);                                                \
    MACRO(fgets, __VA_ARGS__);                                                 \
    MACRO(fopen, __VA_ARGS__);                                                 \
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
    MACRO(open, __VA_ARGS__);                                                  \
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
  } while(0)

#define GET_EVENT_SIZE(event, event_size)                                      \
  do {                                                                         \
    if (event == pthread_cond_signal_anomalous_event ||                        \
        event == pthread_cond_signal_anomalous_event_return)                   \
      event_size = log_event_pthread_cond_signal_size;                         \
    if (event == pthread_cond_broadcast_anomalous_event ||                     \
        event == pthread_cond_broadcast_anomalous_event_return)                \
      event_size = log_event_pthread_cond_broadcast_size;                      \
    FOREACH_NAME(IFNAME_GET_EVENT_SIZE, event, event_size);                    \
  } while(0)

#define COPY_TO_MEMORY_LOG(entry)                                              \
  do {                                                                         \
    FOREACH_NAME(IFNAME_COPY_TO_MEMORY_LOG, entry);                            \
  } while(0)

#define COPY_FROM_MEMORY_SOURCE(source)                                        \
  do {                                                                         \
    FOREACH_NAME(IFNAME_COPY_FROM_MEMORY_SOURCE, source);                      \
  } while(0)

#define READ_ENTRY_FROM_DISK(fd, entry)                                        \
  do {                                                                         \
    if (GET_COMMON_PTR(entry,event) == pthread_cond_signal_anomalous_event || \
        GET_COMMON_PTR(entry,event) == pthread_cond_signal_anomalous_event_return) { \
      return _real_read(fd,                                                    \
                        &entry->log_event_t.log_event_pthread_cond_signal,     \
                        log_event_pthread_cond_signal_size);                   \
    }                                                                          \
    if (GET_COMMON_PTR(entry,event) == pthread_cond_broadcast_anomalous_event ||              \
        GET_COMMON_PTR(entry,event) == pthread_cond_broadcast_anomalous_event_return) {       \
      return _real_read(fd,                                                    \
                        &entry->log_event_t.log_event_pthread_cond_broadcast,  \
                        log_event_pthread_cond_broadcast_size);                \
    }                                                                          \
    FOREACH_NAME(IFNAME_READ_ENTRY_FROM_DISK, fd, entry);                      \
  } while(0)

#define WRITE_ENTRY_TO_DISK(fd, entry, ret)                                    \
  do {                                                                         \
    if (GET_COMMON(entry,event) == pthread_cond_signal_anomalous_event || \
        GET_COMMON(entry,event) == pthread_cond_signal_anomalous_event_return) {           \
      ret = write(fd,                                                          \
                  &entry.log_event_t.log_event_pthread_cond_signal,            \
                  log_event_pthread_cond_signal_size);                         \
    }                                                                          \
    if (GET_COMMON(entry,event) == pthread_cond_broadcast_anomalous_event ||               \
        GET_COMMON(entry,event) == pthread_cond_broadcast_anomalous_event_return) {        \
      ret = write(fd,                                                          \
                  &entry.log_event_t.log_event_pthread_cond_broadcast,         \
                  log_event_pthread_cond_broadcast_size);                      \
    }                                                                          \
    FOREACH_NAME(IFNAME_WRITE_ENTRY_TO_DISK, fd, entry, ret);                  \
  } while(0)

static void log_entry_to_buffer(log_entry_t *entry, char *buffer)
{
  memcpy(buffer, &GET_COMMON_PTR(entry, event), sizeof(GET_COMMON_PTR(entry, event)));
  buffer += sizeof(GET_COMMON_PTR(entry, event));
  memcpy(buffer, &GET_COMMON_PTR(entry, log_id), sizeof(GET_COMMON_PTR(entry, log_id)));
  buffer += sizeof(GET_COMMON_PTR(entry, log_id));
  memcpy(buffer, &GET_COMMON_PTR(entry, tid), sizeof(GET_COMMON_PTR(entry, tid)));
  buffer += sizeof(GET_COMMON_PTR(entry, tid));
  memcpy(buffer, &GET_COMMON_PTR(entry, clone_id), sizeof(GET_COMMON_PTR(entry, clone_id)));
  buffer += sizeof(GET_COMMON_PTR(entry, clone_id));
  memcpy(buffer, &GET_COMMON_PTR(entry, my_errno), sizeof(GET_COMMON_PTR(entry, my_errno)));
  buffer += sizeof(GET_COMMON_PTR(entry, my_errno));
  memcpy(buffer, &GET_COMMON_PTR(entry, retval), sizeof(GET_COMMON_PTR(entry, retval)));
  buffer += sizeof(GET_COMMON_PTR(entry, retval));
}

static void buffer_to_log_entry(char *buffer, log_entry_t *entry)
{
  memcpy(&GET_COMMON_PTR(entry, event), buffer, sizeof(GET_COMMON_PTR(entry, event)));
  buffer += sizeof(GET_COMMON_PTR(entry, event));
  memcpy(&GET_COMMON_PTR(entry, log_id), buffer, sizeof(GET_COMMON_PTR(entry, log_id)));
  buffer += sizeof(GET_COMMON_PTR(entry, log_id));
  memcpy(&GET_COMMON_PTR(entry, tid), buffer, sizeof(GET_COMMON_PTR(entry, tid)));
  buffer += sizeof(GET_COMMON_PTR(entry, tid));
  memcpy(&GET_COMMON_PTR(entry, clone_id), buffer, sizeof(GET_COMMON_PTR(entry, clone_id)));
  buffer += sizeof(GET_COMMON_PTR(entry, clone_id));
  memcpy(&GET_COMMON_PTR(entry, my_errno), buffer, sizeof(GET_COMMON_PTR(entry, my_errno)));
  buffer += sizeof(GET_COMMON_PTR(entry, my_errno));
  memcpy(&GET_COMMON_PTR(entry, retval), buffer, sizeof(GET_COMMON_PTR(entry, retval)));
  buffer += sizeof(GET_COMMON_PTR(entry, retval));
}
#endif

/* Prototypes */
static off_t nextSelect (log_entry_t *select, int clone_id, int nfds, 
    unsigned long int exceptfds, unsigned long int timeout);
/* End prototypes */

// TODO: Do we need LIB_PRIVATE again here if we had already specified it in
// the header file?
/* Library private: */
LIB_PRIVATE dmtcp::map<long long int, pthread_t> clone_id_to_tid_table;
LIB_PRIVATE dmtcp::map<pthread_t, pthread_join_retval_t> pthread_join_retvals;
LIB_PRIVATE log_entry_t     currentLogEntry = EMPTY_LOG_ENTRY;
LIB_PRIVATE char SYNCHRONIZATION_LOG_PATH[SYNCHRONIZATION_LOG_PATH_MAX];
LIB_PRIVATE char SYNCHRONIZATION_READ_DATA_LOG_PATH[SYNCHRONIZATION_LOG_PATH_MAX];
LIB_PRIVATE int             synchronization_log_fd = -1;
LIB_PRIVATE int             read_data_fd = -1;
LIB_PRIVATE int             sync_logging_branch = 0;
// Debugging variable -- setting this will log/replay *ALL* malloc family
// functions (i.e. including ones from DMTCP, std C++ lib, etc.).
// We should eventually remove this.
LIB_PRIVATE int             tylerShouldLog = 0;
LIB_PRIVATE unsigned long   default_stack_size = 0;
LIB_PRIVATE pthread_cond_t  reap_cv = PTHREAD_COND_INITIALIZER;
LIB_PRIVATE pthread_mutex_t fd_change_mutex = PTHREAD_MUTEX_INITIALIZER;
LIB_PRIVATE pthread_mutex_t global_clone_counter_mutex = PTHREAD_MUTEX_INITIALIZER;
LIB_PRIVATE pthread_mutex_t log_index_mutex = PTHREAD_MUTEX_INITIALIZER;
LIB_PRIVATE pthread_mutex_t reap_mutex = PTHREAD_MUTEX_INITIALIZER;
LIB_PRIVATE pthread_mutex_t thread_transition_mutex = PTHREAD_MUTEX_INITIALIZER;
LIB_PRIVATE pthread_t       thread_to_reap;
/* Thread locals: */
LIB_PRIVATE __thread long long int my_clone_id = 0;
/* Volatiles: */
LIB_PRIVATE volatile int           log_entry_index = 0;
LIB_PRIVATE volatile int           log_index = 0;
LIB_PRIVATE volatile int           log_loaded = 0;
LIB_PRIVATE volatile long long int global_clone_counter = 0;
LIB_PRIVATE volatile off_t         read_log_pos = 0;

/* File private: */
static dmtcp::map<int, dmtcp::string> dummy_fds;
static dmtcp::vector<log_entry_t>     patch_list;
static dmtcp::string DUMMY_BASE_PATH = dmtcp::UniquePid::getTmpDir();
static char SYNCHRONIZATION_PATCHED_LOG_PATH[SYNCHRONIZATION_LOG_PATH_MAX];
static int               synchronization_patched_log_fd = -1;
static unsigned long int code_lower = 0, data_break = 0,
                         stack_lower = 0, stack_upper = 0;
static off_t             previous_wakeup_offset = 0;
static pthread_mutex_t   log_file_mutex       = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t   pthread_create_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t   atomic_set_mutex     = PTHREAD_MUTEX_INITIALIZER;
/* File private volatiles: */
static volatile long long int current_log_id = 0;
static volatile int           queued_threads = 0;
static volatile int           log_offset = 0;

static inline void memfence() {  asm volatile ("mfence" ::: "memory"); }

int readEntryFromDisk(int fd, log_entry_t *entry) {
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY_DEBUG
  return _real_read(fd, entry, sizeof(log_entry_t));
#else
  if (_real_read(fd, &GET_COMMON_PTR(entry,event), sizeof(GET_COMMON_PTR(entry,event))) == 0) return 0;
  if (_real_read(fd, &GET_COMMON_PTR(entry,log_id), sizeof(GET_COMMON_PTR(entry,log_id))) == 0) return 0;
  if (_real_read(fd, &GET_COMMON_PTR(entry,tid), sizeof(GET_COMMON_PTR(entry,tid))) == 0) return 0;
  if (_real_read(fd, &GET_COMMON_PTR(entry,clone_id), sizeof(GET_COMMON_PTR(entry,clone_id))) == 0) return 0;
  if (_real_read(fd, &GET_COMMON_PTR(entry,my_errno), sizeof(GET_COMMON_PTR(entry,my_errno))) == 0) return 0;
  if (_real_read(fd, &GET_COMMON_PTR(entry,retval), sizeof(GET_COMMON_PTR(entry,retval))) == 0) return 0;
  READ_ENTRY_FROM_DISK(fd, entry);
#endif
}

int writeEntryToDisk(int fd, log_entry_t entry) {
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY_DEBUG
  return write(fd, &entry, sizeof(log_entry_t));
#else
  int ret_event_size = write(fd, &GET_COMMON(entry,event), sizeof(GET_COMMON(entry,event)));
  int ret_log_id_size = write(fd, &GET_COMMON(entry,log_id), sizeof(GET_COMMON(entry,log_id)));
  int ret_tid_size = write(fd, &GET_COMMON(entry,tid), sizeof(GET_COMMON(entry,tid)));
  int ret_clone_id_size = write(fd, &GET_COMMON(entry,clone_id), sizeof(GET_COMMON(entry,clone_id)));
  int ret_my_errno_size = write(fd, &GET_COMMON(entry,my_errno), sizeof(GET_COMMON(entry,my_errno)));
  int ret_retval_size = write(fd, &GET_COMMON(entry,retval), sizeof(GET_COMMON(entry,retval)));
  int ret_log_event;
  WRITE_ENTRY_TO_DISK(fd, entry, ret_log_event);
  return ret_log_event + ret_event_size + ret_log_id_size + ret_tid_size +
    ret_clone_id_size + ret_my_errno_size + ret_retval_size;
#endif
}

void atomic_increment(volatile int *ptr)
{
  // This gcc builtin should eliminate the need for protecting each
  // increment of log_index with a mutex.
  __sync_fetch_and_add(ptr, 1);
}

void atomic_decrement(volatile int *ptr)
{
  // This gcc builtin should eliminate the need for protecting each
  // increment of log_index with a mutex.
  __sync_fetch_and_add(ptr, -1);
}

void atomic_set(volatile int *ptr, int val)
{
  // This isn't atomic just because we call two atomic functions...
  // We still need a lock here.
  _real_pthread_mutex_lock(&atomic_set_mutex);
  if (__builtin_expect((val != 0 && val != 1), 0)) {
    JASSERT(false).Text("atomic_set only implemented for 0 and 1.");
  }
  __sync_fetch_and_xor(ptr, *ptr); // Set to 0
  if (val == 1)
    atomic_increment(ptr);
  _real_pthread_mutex_unlock(&atomic_set_mutex);
}

int addDummyFd()
{
  dmtcp::ostringstream new_path;
  new_path << DUMMY_BASE_PATH << "/dummy-" << (int)dummy_fds.size();
  int fd = open(new_path.str().c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
  dummy_fds[fd] = new_path.str();
  return fd;
}

int isDummyFd(int fd)
{
  int retval = dummy_fds.find(fd) != dummy_fds.end();
  return retval;
}

void removeDummyFd(int fd)
{
  unlink(dummy_fds[fd].c_str());
  _real_close(fd);
  dummy_fds.erase(fd);
}

int shouldSynchronize(void *return_addr)
{
  // Returns 1 if we should synchronize this call, instead of calling _real
  // version. 0 if we should not.
  dmtcp::WorkerState state = dmtcp::WorkerState::currentState();
  if (state != dmtcp::WorkerState::RUNNING) {
    return 0;
  }
  if (!validAddress((unsigned long int)return_addr)) {
    return 0;
  }
  return 1;
}

/* Begin miscellaneous/helper functions. */
// Reads from fd until count bytes are read, or newline encountered.
// Returns NULL at EOF.
static int read_line(int fd, char *buf, int count)
{
  int i = 0;
  char c;
  while (1) {
    if (_real_read(fd, &c, 1) == 0) {
      buf[i] = '\0';
      return NULL;
    }
    buf[i++] = c;
    if (c == '\n') break;
  }
  buf[i++] = '\0';
  return i;
}

// TODO: Since this is C++, couldn't we use some C++ string processing methods
// to simplify the logic? MAKE SURE TO AVOID MALLOC()
LIB_PRIVATE void recordDataStackLocations()
{
  int maps_file = -1;
  char line[200], stack_line[200], code_line[200];
  dmtcp::string progname = jalib::Filesystem::GetProgramName();
  if ((maps_file = _real_open("/proc/self/maps", O_RDONLY, S_IRUSR)) == -1) {
    perror("open");
    exit(1);
  }
  while (read_line(maps_file, line, 199) != 0) {
    if (strstr(line, "r-xp") != NULL && strstr(line, progname.c_str()) != NULL) {
      // beginning of .text segment
      strncpy(code_line, line, 199);
    }
    if (strstr(line, "[stack]") != NULL) {
      strncpy(stack_line, line, 199);
    }
  }
  close(maps_file);
  char addrs[32];
  char addr_lower[16];
  char addr_upper[16];
  addr_lower[0] = '0';
  addr_lower[1] = 'x';
  addr_upper[0] = '0';
  addr_upper[1] = 'x';
  // TODO: there must be a better way to do this.
#ifdef __x86_64__
  strncpy(addrs, stack_line, 25);
  strncpy(addr_lower+2, addrs, 12);
  strncpy(addr_upper+2, addrs+13, 12);
  addr_lower[14] = '\0';
  addr_upper[14] = '\0';
  //printf("s_stack_lower=%s, s_stack_upper=%s\n", addr_lower, addr_upper);
  stack_lower = strtoul(addr_lower, NULL, 16);
  stack_upper = strtoul(addr_upper, NULL, 16);
  addr_lower[0] = '0';
  addr_lower[1] = 'x';
  strncpy(addrs, code_line, 25);
  strncpy(addr_lower+2, addrs, 12);
  addr_lower[14] = '\0';
  code_lower = strtoul(addr_lower, NULL, 16);
#else
  strncpy(addrs, stack_line, 17);
  strncpy(addr_lower+2, addrs, 8);
  strncpy(addr_upper+2, addrs+9, 8);
  addr_lower[10] = '\0';
  addr_upper[10] = '\0';
  //printf("s_stack_lower=%s, s_stack_upper=%s\n", addr_lower, addr_upper);
  stack_lower = strtoul(addr_lower, NULL, 16);
  stack_upper = strtoul(addr_upper, NULL, 16);
  addr_lower[0] = '0';
  addr_lower[1] = 'x';
  strncpy(addrs, code_line, 17);
  strncpy(addr_lower+2, addrs, 8);
  addr_lower[10] = '\0';
  code_lower = strtoul(addr_lower, NULL, 16);
#endif
  // Returns the next address after the end of the heap.
  data_break = (unsigned long int)sbrk(0);
  // Also save the current RLIMIT_STACK value -- this is the default stack
  // size for NPTL.
  struct rlimit rl;
  getrlimit(RLIMIT_STACK, &rl);
  default_stack_size = rl.rlim_cur;
}

int validAddress(unsigned long int addr)
{
  // This code assumes the user's segments .text through .data are contiguous.
  if ((addr >= code_lower && addr < data_break) ||
      (addr >= stack_lower && addr < stack_upper)) {
    return 1;
  } else {
    return 0;
  }
}

static void resetLog()
{
  JTRACE ( "resetting all log entries and log_index to 0." );
  int i = 0;
  for (i = 0; i < MAX_LOG_LENGTH; i++) {
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY_DEBUG
    log[i] = EMPTY_LOG_ENTRY;
#else
    log[i] = 0;
#endif
  }
  atomic_set(&log_index, 0);
}

static int isUnlock(log_entry_t e)
{
  return GET_COMMON(e,event) == pthread_mutex_unlock_event ||
    GET_COMMON(e,event) == select_event_return ||
    GET_COMMON(e,event) == read_event_return ||
    GET_COMMON(e,event) == pthread_create_event_return || GET_COMMON(e,event) == pthread_exit_event ||
    GET_COMMON(e,event) == malloc_event_return || GET_COMMON(e,event) == calloc_event_return ||
    GET_COMMON(e,event) == realloc_event_return || GET_COMMON(e,event) == free_event_return ||
    GET_COMMON(e,event) == accept_event_return || GET_COMMON(e,event) == getsockname_event_return ||
    GET_COMMON(e,event) == fcntl_event_return || GET_COMMON(e,event) == libc_memalign_event_return ||
    GET_COMMON(e,event) == setsockopt_event_return || GET_COMMON(e,event) == write_event_return ||
    GET_COMMON(e,event) == rand_event_return || GET_COMMON(e,event) == srand_event_return ||
    GET_COMMON(e,event) == time_event_return || GET_COMMON(e,event) == pthread_detach_event_return ||
    GET_COMMON(e,event) == pthread_join_event_return || GET_COMMON(e,event) == close_event_return ||
    GET_COMMON(e,event) == signal_handler_event_return || GET_COMMON(e,event) == sigwait_event_return||
    GET_COMMON(e,event) == access_event_return || GET_COMMON(e,event) == open_event_return ||
    GET_COMMON(e,event) == pthread_rwlock_unlock_event ||
    GET_COMMON(e,event) == pthread_rwlock_rdlock_event_return ||
    GET_COMMON(e,event) == pthread_rwlock_wrlock_event_return ||
    GET_COMMON(e,event) == pthread_mutex_trylock_event_return ||
    GET_COMMON(e,event) == dup_event_return ||
    GET_COMMON(e,event) == xstat_event_return || GET_COMMON(e,event) == xstat64_event_return ||
    GET_COMMON(e,event) == fxstat_event_return || GET_COMMON(e,event) == fxstat64_event_return ||
    GET_COMMON(e,event) == lxstat_event_return || GET_COMMON(e,event) == lxstat64_event_return ||
    GET_COMMON(e,event) == lseek_event_return || GET_COMMON(e,event) == unlink_event_return ||
    GET_COMMON(e,event) == pread_event_return || GET_COMMON(e,event) == pwrite_event_return ||
    GET_COMMON(e,event) == pthread_cond_signal_event_return ||
    GET_COMMON(e,event) == pthread_cond_broadcast_event_return ||
    /* We only use cond_*wait_return to store return value information. Thus,
       the "real" unlock events are the cond_*wait events themselves, the return
       events are only for storing return values. Nonetheless, they are "unlock"
       events themselves, since they are return events. */
    GET_COMMON(e,event) == pthread_cond_wait_event ||
    GET_COMMON(e,event) == pthread_cond_wait_event_return ||
    GET_COMMON(e,event) == pthread_cond_timedwait_event ||
    GET_COMMON(e,event) == pthread_cond_timedwait_event_return ||
    GET_COMMON(e,event) == getc_event_return ||
    GET_COMMON(e,event) == getline_event_return ||
    GET_COMMON(e,event) == getpeername_event_return || GET_COMMON(e,event) == fdopen_event_return ||
    GET_COMMON(e,event) == fdatasync_event_return || GET_COMMON(e,event) == link_event_return ||
    GET_COMMON(e,event) == rename_event_return || GET_COMMON(e,event) == bind_event_return ||
    GET_COMMON(e,event) == listen_event_return || GET_COMMON(e,event) == socket_event_return ||
    GET_COMMON(e,event) == connect_event_return || GET_COMMON(e,event) == readdir_event_return || GET_COMMON(e,event) == readdir_r_event_return ||
    GET_COMMON(e,event) == fclose_event_return || GET_COMMON(e,event) == fopen_event_return ||
    GET_COMMON(e,event) == fgets_event_return || GET_COMMON(e,event) == mkstemp_event_return ||
    GET_COMMON(e,event) == rewind_event_return || GET_COMMON(e,event) == ftell_event_return ||
    GET_COMMON(e,event) == fsync_event_return || GET_COMMON(e,event) == readlink_event_return ||
    GET_COMMON(e,event) == rmdir_event_return || GET_COMMON(e,event) == mkdir_event_return ||
    GET_COMMON(e,event) == fprintf_event_return || GET_COMMON(e,event) == fputs_event_return ||
    GET_COMMON(e,event) == fscanf_event_return ||
    GET_COMMON(e,event) == fwrite_event_return || GET_COMMON(e,event) == putc_event_return;
}

void copyFdSet(fd_set *src, fd_set *dest)
{
  // fd_set struct has one member: __fds_bits. which is an array of longs.
  // length of that array is FD_SETSIZE/NFDBITS
  // see /usr/include/sys/select.h
  if (src == NULL)
    dest = NULL;
  if (dest == NULL)
    return;
  int length_fd_bits = FD_SETSIZE/NFDBITS;
  int i = 0;
  for (i = 0; i < length_fd_bits; i++) {
    __FDS_BITS(dest)[i] = __FDS_BITS(src)[i];
  }
}

int fdAvailable(fd_set *set)
{
  // Returns 1 if 'set' has at least one fd available (for any action), else 0.
  int length_fd_bits = FD_SETSIZE/NFDBITS;
  int i = 0, j = 0;
  for (i = 0; i < length_fd_bits; i++) {
    for (j = 0; j < NFDBITS; j++) {
      if ((__FDS_BITS(set)[i]>>j) & 0x1)
        return 1;
    }
  }
  return 0;
}

int fdSetDiff(fd_set *one, fd_set *two)
{
  // Returns 1 if sets are different, 0 if they are the same.
  // fd_set struct has one member: __fds_bits. which is an array of longs.
  // length of that array is FD_SETSIZE/__NFDBITS
  // see /usr/include/sys/select.h
  if (one == NULL && two == NULL)
    return 0;
  if ((one == NULL && two != NULL) ||
      (one != NULL && two == NULL))
    return 1;
  int length_fd_bits = FD_SETSIZE/NFDBITS;
  int i = 0;
  for (i = 0; i < length_fd_bits; i++) {
    JTRACE ( "" ) ( __FDS_BITS(one)[i] ) ( __FDS_BITS(two)[i] );
  }
  for (i = 0; i < length_fd_bits; i++) {
    if (__FDS_BITS(one)[i] != __FDS_BITS(two)[i])
      return 1;
  }
  return 0;
}

// TODO: Think of a better name than 'pop' since it does more than simply pop.
// Specifically, given a clone_id, this returns the entry in patch_list with the 
// lowest index and same clone_id.
// If none found, returns EMPTY_LOG_ENTRY.
static log_entry_t pop(int clone_id)
{
  int i = 0;
  log_entry_t e = EMPTY_LOG_ENTRY;
  for (i = 0; i < patch_list.size(); i++) {
    if (GET_COMMON(patch_list[i],clone_id) == 0) {
      break;
    }
    if (GET_COMMON(patch_list[i],clone_id) == clone_id) {
      e = patch_list[i];
      patch_list.erase(patch_list.begin() + i);
      break;
    }
  }
  return e;
}

static int isLogPatched()
{
  char is_patched = 0;
  _real_read(synchronization_log_fd, &is_patched, sizeof(char));
  return is_patched == LOG_IS_PATCHED_VALUE;
}

static void markLogAsPatched()
{
  lseek(synchronization_log_fd, 0, SEEK_SET);
  LOG_IS_PATCHED_TYPE c = LOG_IS_PATCHED_VALUE;
  _real_write(synchronization_log_fd, (void *)&c, LOG_IS_PATCHED_SIZE);
  // Don't seek back to zero.
}

static void patchLog()
{
  JTRACE ( "begin log patching." );
  synchronization_patched_log_fd = open(SYNCHRONIZATION_PATCHED_LOG_PATH,
      O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
  log_entry_t entry = EMPTY_LOG_ENTRY, temp = EMPTY_LOG_ENTRY;
  log_entry_t entry_to_write = EMPTY_LOG_ENTRY;
  int i = 0;
  ssize_t write_ret = 0;
  // Read one log entry at a time from the file on disk, and write the patched
  // version out to synchronization_patched_log_fd.
  while (readEntryFromDisk(synchronization_log_fd, &entry) != 0) {
    if (GET_COMMON(entry,event) == exec_barrier_event) {
      // Nothing may move past an exec barrier. Dump everything in patch_list
      // into the new log before the exec barrier.
      for (i = 0; i < patch_list.size(); i++) {
        write_ret = writeEntryToDisk(synchronization_patched_log_fd, patch_list[i]);
      }
      patch_list.clear();
      write_ret = writeEntryToDisk(synchronization_patched_log_fd, entry);
      continue;
    }
    if (GET_COMMON(entry,event) == pthread_kill_event) {
      JTRACE ( "Found a pthread_kill in log. Not moving it." );
      write_ret = writeEntryToDisk(synchronization_patched_log_fd, entry);
      continue;
    }
    if (GET_COMMON(entry,clone_id) == 0) {
      JASSERT ( false ) .Text("Encountered a clone_id of 0 in log.");
    }
    if (isUnlock(entry)) {
      temp = pop(GET_COMMON(entry,clone_id));
      while (GET_COMMON(temp,clone_id) != 0) {
        write_ret = writeEntryToDisk(synchronization_patched_log_fd, temp);
        temp = pop(GET_COMMON(entry,clone_id));
      }
      write_ret = writeEntryToDisk(synchronization_patched_log_fd, entry);
    } else {
      patch_list.push_back(entry);
    }
  }
  // If the patch_list is not empty (patch_list_idx != 0), then there were
  // some leftover log entries that were not balanced. So we tack them on to
  // the very end of the log.
  if (patch_list.size() != 0) {
    JTRACE ( "Extra log entries. Tacking them onto end of log." ) ( patch_list.size() );
    for (i = 0; i < patch_list.size(); i++) {
      write_ret = writeEntryToDisk(synchronization_patched_log_fd, patch_list[i]);
    }
    patch_list.clear();
  }
  // NOW it should be empty.
  // TODO: Why does this sometimes trigger?
  //JASSERT ( patch_list_idx == 0 ) ( patch_list_idx );
  
  // Now copy the contents of the patched log over the original log.
  // TODO: Why can't we just use 'rename()' to move the patched log over the
  // original location?

  // Rewind so we can write out over the original log.
  lseek(synchronization_patched_log_fd, 0, SEEK_SET);
  // Close so we can re-open with O_TRUNC
  close(synchronization_log_fd);
  synchronization_log_fd = open(SYNCHRONIZATION_LOG_PATH, O_RDWR | O_TRUNC);
  markLogAsPatched();
  // Copy over to original log filename
  while (readEntryFromDisk(synchronization_patched_log_fd, &entry) != 0) {
    write_ret = writeEntryToDisk(synchronization_log_fd, entry);
  }
  close(synchronization_patched_log_fd);
  unlink(SYNCHRONIZATION_PATCHED_LOG_PATH);
  JTRACE ( "log patching finished." ) 
    ( SYNCHRONIZATION_LOG_PATH );
  lseek(synchronization_log_fd, 0+LOG_IS_PATCHED_SIZE, SEEK_SET);
} 

static off_t nextPthreadCreate(log_entry_t *create, unsigned long int thread,
  unsigned long int start_routine, unsigned long int attr,
  unsigned long int arg) {
  /* Finds the next pthread_create return event with the same thread,
     start_routine, attr, and arg as given.
     Returns the offset into the log file that the wakeup was found. */
  off_t old_pos = lseek(synchronization_log_fd, 0, SEEK_CUR);
  off_t pos = 0;
  log_entry_t e = EMPTY_LOG_ENTRY;
  while (1) {
    if (readEntryFromDisk(synchronization_log_fd, &e) == 0) {
      e = EMPTY_LOG_ENTRY;
      break;
    }
    if (GET_COMMON(e, event) == pthread_create_event_return &&
        GET_FIELD(e, pthread_create, thread) == thread &&
        GET_FIELD(e, pthread_create, start_routine) == start_routine &&
        GET_FIELD(e, pthread_create, attr) == attr &&
        GET_FIELD(e, pthread_create, arg) == arg) {
      break;
    }
  }
  if (create != NULL)
    *create = e;
  pos = lseek(synchronization_log_fd, 0, SEEK_CUR);
  lseek(synchronization_log_fd, old_pos, SEEK_SET);
  return pos;
}

static off_t nextGetline(log_entry_t *getline_entry, char *lineptr,
  unsigned long int stream) {
  /* Finds the next getline return event with the same lineptr,
     n and stream as given.
     Returns the offset into the log file that the wakeup was found. */
  off_t old_pos = lseek(synchronization_log_fd, 0, SEEK_CUR);
  off_t pos = 0;
  log_entry_t e = EMPTY_LOG_ENTRY;
  while (1) {
    if (readEntryFromDisk(synchronization_log_fd, &e) == 0) {
      e = EMPTY_LOG_ENTRY;
      break;
    }
    if (GET_COMMON(e, event) == getline_event_return &&
        GET_FIELD(e, getline, lineptr) == lineptr &&
        GET_FIELD(e, getline, stream) == stream) {
      break;
    }
  }
  if (getline_entry != NULL)
    *getline_entry = e;
  pos = lseek(synchronization_log_fd, 0, SEEK_CUR);
  lseek(synchronization_log_fd, old_pos, SEEK_SET);
  return pos;
}

static void annotateLog()
{
  /* This function performs several tasks for the log:
     1) Annotates pthread_create events with stack information from their
        respective return events.
     2) Annotates getline events with is_realloc information from their
        respective return events.
  
     This function should be called *before* log patching happens.*/
  synchronization_patched_log_fd = open(SYNCHRONIZATION_PATCHED_LOG_PATH,
      O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
  JTRACE ( "Annotating log." );
  log_entry_t entry = EMPTY_LOG_ENTRY;
  log_entry_t create_return = EMPTY_LOG_ENTRY;
  log_entry_t getline_return = EMPTY_LOG_ENTRY;
  ssize_t write_ret = 0;
  int entryNum = 0;
  while (readEntryFromDisk(synchronization_log_fd, &entry) != 0) {
    entryNum++;
    if (GET_COMMON(entry,event) == pthread_create_event) {
      nextPthreadCreate(&create_return,
          GET_FIELD(entry, pthread_create, thread),
          GET_FIELD(entry, pthread_create, start_routine),
          GET_FIELD(entry, pthread_create, attr),
          GET_FIELD(entry, pthread_create, arg));
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY_DEBUG
      entry.stack_size = create_return.stack_size;
      entry.stack_addr = create_return.stack_addr;
#else
      entry.log_event_t.log_event_pthread_create.stack_size = 
        create_return.log_event_t.log_event_pthread_create.stack_size;
      entry.log_event_t.log_event_pthread_create.stack_addr =
        create_return.log_event_t.log_event_pthread_create.stack_addr;
#endif
    } else if (GET_COMMON(entry,event) == getline_event) {
      nextGetline(&getline_return,
          GET_FIELD(entry, getline, lineptr),
          GET_FIELD(entry, getline, stream));
      SET_FIELD2(entry, getline, is_realloc,
        GET_FIELD(getline_return, getline, is_realloc));
    }
    write_ret = writeEntryToDisk(synchronization_patched_log_fd, entry);
  }

  // Rewind so we can write out over the original log.
  lseek(synchronization_patched_log_fd, 0, SEEK_SET);
  // Close so we can re-open in O_TRUNC mode
  close(synchronization_log_fd);
  synchronization_log_fd = open(SYNCHRONIZATION_LOG_PATH, O_RDWR | O_TRUNC);
  markLogAsPatched();
  // Copy over to original log filename
  while (readEntryFromDisk(synchronization_patched_log_fd, &entry) != 0) {
    write_ret = writeEntryToDisk(synchronization_log_fd, entry);
  }
  close(synchronization_patched_log_fd);
  unlink(SYNCHRONIZATION_PATCHED_LOG_PATH);
  JTRACE ( "log annotation finished. Opening patched/annotated log file." ) 
    ( SYNCHRONIZATION_LOG_PATH );
  lseek(synchronization_log_fd, 0+LOG_IS_PATCHED_SIZE, SEEK_SET);
}

/* Initializes log pathnames. One log per process. */
void initializeLog()
{
  pid_t pid = getpid();
  dmtcp::string tmpdir = dmtcp::UniquePid::getTmpDir();
  snprintf(SYNCHRONIZATION_LOG_PATH, SYNCHRONIZATION_LOG_PATH_MAX, 
      "%s/synchronization-log-%d", tmpdir.c_str(), pid);
  snprintf(SYNCHRONIZATION_PATCHED_LOG_PATH, SYNCHRONIZATION_LOG_PATH_MAX, 
      "%s/synchronization-log-%d-patched", tmpdir.c_str(), pid);
  snprintf(SYNCHRONIZATION_READ_DATA_LOG_PATH, SYNCHRONIZATION_LOG_PATH_MAX, 
      "%s/synchronization-read-log-%d", tmpdir.c_str(), pid);
  // Create the file:
  int fd = open(SYNCHRONIZATION_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 
      S_IRUSR | S_IWUSR);
  // Write the patched bit if the file is empty.
  struct stat st;
  fstat(fd, &st);
  if (st.st_size == 0) {
    char c = 0;
    _real_write(fd, (void *)&c, 1);
  }
  close(fd);
  JTRACE ( "Initialized synchronization log path to" ) ( SYNCHRONIZATION_LOG_PATH );
}

/* Patches the log, and reads in MAX_LOG_LENGTH entries (debug mode) /
 * bytes (non-debug). */
void primeLog()
{
  // If the trylock fails, another thread is already reading the log
  // into memory, so we skip this.
  if (_real_pthread_mutex_trylock(&log_index_mutex) != EBUSY) {
    JTRACE ( "Priming log." );
    int num_read = 0, total_read = 0;
    if (lseek(synchronization_log_fd, 0, SEEK_SET) == -1) {
      perror("lseek");
    }
    /******************* LOG PATCHING STUFF *******************/
    if (!isLogPatched()) {
      annotateLog();
      patchLog();
    }
    // TODO: comment out until fix the issue:
    //    signal, signal, wakeup, wakeup
    // where it thinks the second wakeup is spontaneous (resets signal_pos
    // at a bad time?)
    //fixSpontaneousWakeups();
    /******************* END LOG PATCHING STUFF *******************/
    num_read = _real_read(synchronization_log_fd, log, MAX_LOG_LENGTH*LOG_ENTRY_SIZE);
    total_read += num_read;
    // Read until we've gotten MAX_LOG_LENGTH or there is no more to read.
    while (num_read != (MAX_LOG_LENGTH*LOG_ENTRY_SIZE) && num_read != 0) {
      num_read = _real_read(synchronization_log_fd, log, MAX_LOG_LENGTH*LOG_ENTRY_SIZE);
      total_read += num_read;
    }
    JTRACE ( "read this many bytes." ) ( total_read );
    if (num_read == -1) {
      perror("read");
      JTRACE ( "error reading from synch log." ) ( errno );
    }
    atomic_set(&log_loaded, 1);
    _real_pthread_mutex_unlock(&log_index_mutex);
    getNextLogEntry();
  }
}

/* Reads the next MAX_LOG_LENGTH entries (debug mode) / bytes (non-debug mode)
 * (or most available) from the logfile. */
void readLogFromDisk()
{
  resetLog();
  int num_read = 0, total_read = 0;
  JTRACE ( "current position" ) ( lseek(synchronization_log_fd, 0, SEEK_CUR) );
  num_read = _real_read(synchronization_log_fd, log, MAX_LOG_LENGTH*LOG_ENTRY_SIZE);
  total_read += num_read;
  // Read until we've gotten MAX_LOG_LENGTH or there is no more to read.
  while (num_read != (MAX_LOG_LENGTH*LOG_ENTRY_SIZE) && num_read != 0) {
    num_read = _real_read(synchronization_log_fd, log, MAX_LOG_LENGTH*LOG_ENTRY_SIZE);
    total_read += num_read;
  }
  JTRACE ( "read entries from disk. " ) ( total_read );
  atomic_increment(&log_loaded);
  atomic_increment(&log_offset);
}

int readAll(int fd, char *buf, int count)
{
  int retval = 0, to_read = count;
  while (1) {
    retval = _real_read(fd, buf, to_read);
    if (retval == to_read) break;
    if (errno == EINTR || errno == EAGAIN) {
      buf += retval;
      to_read -= retval;
    } else {
      // other error
      break;
    }
  }
  return retval;
}

ssize_t writeAll(int fd, const void *buf, size_t count)
{
  ssize_t retval = 0;
  size_t to_write = count;
  while (1) {
    retval = _real_write(fd, buf, to_write);
    if (retval == to_write) break;
    if (errno == EINTR || errno == EAGAIN) {
      buf = (char *)buf + retval;
      to_write -= retval;
    } else {
      // other error
      break;
    }
  }
  return retval;
}

ssize_t pwriteAll(int fd, const void *buf, size_t count, off_t offset)
{
  ssize_t retval = 0;
  size_t to_write = count;
  while (1) {
    retval = _real_pwrite(fd, buf, to_write, offset);
    if (retval == to_write) break;
    if (errno == EINTR || errno == EAGAIN) {
      buf = (char *)buf + retval;
      to_write -= retval;
      offset += retval;
    } else {
      // other error
      break;
    }
  }
  return retval;
}

static void setupCommonFields(log_entry_t *e, int clone_id, int event)
{
  SET_COMMON_PTR(e, clone_id);
  SET_COMMON_PTR(e, event);
}

log_entry_t create_accept_entry(int clone_id, int event, int sockfd,
    unsigned long int sockaddr, unsigned long int addrlen)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, accept, sockfd);
  SET_FIELD(e, accept, sockaddr);
  SET_FIELD(e, accept, addrlen);
  return e;
}

log_entry_t create_access_entry(int clone_id, int event,
   unsigned long int pathname, int mode)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, access, pathname);
  SET_FIELD(e, access, mode);
  return e;
}

log_entry_t create_bind_entry(int clone_id, int event,
    int sockfd, const struct sockaddr *my_addr, socklen_t addrlen)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, bind, sockfd);
  SET_FIELD2(e, bind, my_addr, (unsigned long int)my_addr);
  SET_FIELD(e, bind, addrlen);
  return e;
}

log_entry_t create_calloc_entry(int clone_id, int event, size_t nmemb,
    size_t size)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, calloc, nmemb);
  SET_FIELD(e, calloc, size);
  return e;
}

log_entry_t create_close_entry(int clone_id, int event, int fd)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, close, fd);
  return e;
}

log_entry_t create_connect_entry(int clone_id, int event, int sockfd,
    const struct sockaddr *serv_addr, socklen_t addrlen)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, connect, sockfd);
  SET_FIELD2(e, connect, serv_addr, (unsigned long int)serv_addr);
  SET_FIELD(e, connect, addrlen);
  return e;
}

log_entry_t create_dup_entry(int clone_id, int event, int oldfd)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, dup, oldfd);
  return e;
}

log_entry_t create_exec_barrier_entry()
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, CLONE_ID_ANYONE, exec_barrier_event);
  return e;
}

log_entry_t create_fclose_entry(int clone_id, int event, FILE *fp)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, fclose, fp, (unsigned long int)fp);
  return e;
}

log_entry_t create_fcntl_entry(int clone_id, int event, int fd, int cmd,
    long arg_3_l, unsigned long int arg_3_f)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, fcntl, fd);
  SET_FIELD(e, fcntl, cmd);
  SET_FIELD(e, fcntl, arg_3_l);
  SET_FIELD(e, fcntl, arg_3_f);
  return e;
}

log_entry_t create_fdatasync_entry(int clone_id, int event, int fd)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, fdatasync, fd);
  return e;
}

log_entry_t create_fdopen_entry(int clone_id, int event, int fd,
    const char *mode)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, fdopen, fd);
  SET_FIELD2(e, fdopen, mode, (unsigned long int)mode);
  return e;
}

log_entry_t create_fgets_entry(int clone_id, int event, char *s, int size,
    FILE *stream)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, fgets, s, (unsigned long int)s);
  SET_FIELD(e, fgets, size);
  SET_FIELD2(e, fgets, stream, (unsigned long int)stream);
  return e;
}

log_entry_t create_fopen_entry(int clone_id, int event,
    const char *name, const char *mode)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, fopen, name, (unsigned long int)name);
  SET_FIELD2(e, fopen, mode, (unsigned long int)mode);
  return e;
}

log_entry_t create_fprintf_entry(int clone_id, int event,
    FILE *stream, const char *format)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, fprintf, stream, (unsigned long int)stream);
  SET_FIELD2(e, fprintf, format, (unsigned long int)format);
  return e;
}

log_entry_t create_fscanf_entry(int clone_id, int event,
    FILE *stream, const char *format)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, fscanf, stream, (unsigned long int)stream);
  SET_FIELD2(e, fscanf, format, (unsigned long int)format);
  return e;
}

log_entry_t create_fputs_entry(int clone_id, int event,
    const char *s, FILE *stream)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, fputs, s, (unsigned long int)s);
  SET_FIELD2(e, fputs, stream, (unsigned long int)stream);
  return e;
}

log_entry_t create_free_entry(int clone_id, int event, unsigned long int ptr)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, free, ptr);
  return e;
}

log_entry_t create_ftell_entry(int clone_id, int event, FILE *stream)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, ftell, stream, (unsigned long int)stream);
  return e;
}

log_entry_t create_fwrite_entry(int clone_id, int event, const void *ptr,
    size_t size, size_t nmemb, FILE *stream)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, fwrite, ptr, (unsigned long int)ptr);
  SET_FIELD(e, fwrite, size);
  SET_FIELD(e, fwrite, nmemb);
  SET_FIELD2(e, fwrite, stream, (unsigned long int)stream);
  return e;
}

log_entry_t create_fsync_entry(int clone_id, int event, int fd)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, fsync, fd);
  return e;
}

log_entry_t create_fxstat_entry(int clone_id, int event, int vers, int fd,
     struct stat buf)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, fxstat, vers);
  SET_FIELD(e, fxstat, fd);
  SET_FIELD(e, fxstat, buf);
  return e;
}

log_entry_t create_fxstat64_entry(int clone_id, int event, int vers, int fd,
     struct stat64 buf)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, fxstat64, vers);
  SET_FIELD(e, fxstat64, fd);
  SET_FIELD(e, fxstat64, buf);
  return e;
}

log_entry_t create_getc_entry(int clone_id, int event, FILE *stream)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, getc, stream, (unsigned long int)stream);
  return e;
}

log_entry_t create_getline_entry(int clone_id, int event, char **lineptr, size_t *n,
    FILE *stream)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, getline, lineptr, *lineptr);
  SET_FIELD2(e, getline, n, *n);
  SET_FIELD2(e, getline, stream, (unsigned long int)stream);
  return e;
}

log_entry_t create_getpeername_entry(int clone_id, int event, int sockfd,
    struct sockaddr sockaddr, unsigned long int addrlen)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, getpeername, sockfd);
  SET_FIELD(e, getpeername, sockaddr);
  SET_FIELD(e, getpeername, addrlen);
  return e;
}

log_entry_t create_getsockname_entry(int clone_id, int event, int sockfd,
    unsigned long int sockaddr, unsigned long int addrlen)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, getsockname, sockfd);
  SET_FIELD(e, getsockname, sockaddr);
  SET_FIELD(e, getsockname, addrlen);
  return e;
}

log_entry_t create_libc_memalign_entry(int clone_id, int event, size_t boundary,
    size_t size)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, libc_memalign, boundary);
  SET_FIELD(e, libc_memalign, size);
  return e;
}

log_entry_t create_lseek_entry(int clone_id, int event, int fd, off_t offset,
     int whence)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, lseek, fd);
  SET_FIELD(e, lseek, offset);
  SET_FIELD(e, lseek, whence);
  return e;
}

log_entry_t create_link_entry(int clone_id, int event, const char *oldpath,
    const char *newpath)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, link, oldpath, (unsigned long int)oldpath);
  SET_FIELD2(e, link, newpath, (unsigned long int)newpath);
  return e;
}

log_entry_t create_listen_entry(int clone_id, int event, int sockfd, int backlog)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, listen, sockfd);
  SET_FIELD(e, listen, backlog);
  return e;
}

log_entry_t create_lxstat_entry(int clone_id, int event, int vers,
    unsigned long int path, struct stat buf)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, lxstat, vers);
  SET_FIELD(e, lxstat, path);
  SET_FIELD(e, lxstat, buf);
  return e;
}

log_entry_t create_lxstat64_entry(int clone_id, int event, int vers,
    unsigned long int path, struct stat64 buf)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, lxstat64, vers);
  SET_FIELD(e, lxstat64, path);
  SET_FIELD(e, lxstat64, buf);
  return e;
}

log_entry_t create_malloc_entry(int clone_id, int event, size_t size)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, malloc, size);
  return e;
}

log_entry_t create_mkdir_entry(int clone_id, int event, const char *pathname,
    mode_t mode)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, mkdir, pathname, (unsigned long int)pathname);
  SET_FIELD(e, mkdir, mode);
  return e;
}

log_entry_t create_mkstemp_entry(int clone_id, int event, char *temp)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, mkstemp, temp, (unsigned long int)temp);
  return e;
}

log_entry_t create_open_entry(int clone_id, int event, unsigned long int path,
   int flags, mode_t open_mode)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, open, path);
  SET_FIELD(e, open, flags);
  SET_FIELD(e, open, open_mode);
  return e;
}

log_entry_t create_pread_entry(int clone_id, int event, int fd, 
    unsigned long int buf, size_t count, off_t offset)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pread, fd);
  SET_FIELD(e, pread, buf);
  SET_FIELD(e, pread, count);
  SET_FIELD(e, pread, offset);
  return e;
}

log_entry_t create_putc_entry(int clone_id, int event, int c,
    FILE *stream)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, putc, c);
  SET_FIELD2(e, putc, stream, (unsigned long int)stream);
  return e;
}

log_entry_t create_pwrite_entry(int clone_id, int event, int fd, 
    unsigned long int buf, size_t count, off_t offset)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pwrite, fd);
  SET_FIELD(e, pwrite, buf);
  SET_FIELD(e, pwrite, count);
  SET_FIELD(e, pwrite, offset);
  return e;
}

log_entry_t create_pthread_cond_broadcast_entry(int clone_id, int event,
    unsigned long int cond_var)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pthread_cond_broadcast, cond_var);
  return e;
}

log_entry_t create_pthread_cond_signal_entry(int clone_id, int event,
    unsigned long int cond_var)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pthread_cond_signal, cond_var);
  return e;
}

log_entry_t create_pthread_cond_wait_entry(int clone_id, int event,
    unsigned long int mutex, unsigned long int cond_var)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pthread_cond_wait, mutex);
  SET_FIELD(e, pthread_cond_wait, cond_var);
  return e;
}

log_entry_t create_pthread_cond_timedwait_entry(int clone_id, int event,
    unsigned long int mutex, unsigned long int cond_var,
    unsigned long int abstime)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pthread_cond_timedwait, mutex);
  SET_FIELD(e, pthread_cond_timedwait, cond_var);
  SET_FIELD(e, pthread_cond_timedwait, abstime);
  return e;
}

log_entry_t create_pthread_rwlock_unlock_entry(int clone_id, int event,
    unsigned long int rwlock)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pthread_rwlock_unlock, rwlock);
  return e;
}

log_entry_t create_pthread_rwlock_rdlock_entry(int clone_id, int event,
    unsigned long int rwlock)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pthread_rwlock_rdlock, rwlock);
  return e;
}

log_entry_t create_pthread_rwlock_wrlock_entry(int clone_id, int event,
    unsigned long int rwlock)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pthread_rwlock_wrlock, rwlock);
  return e;
}

log_entry_t create_pthread_create_entry(long long int clone_id, int event,
    unsigned long int thread, unsigned long int attr, 
    unsigned long int start_routine, unsigned long int arg)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pthread_create, thread);
  SET_FIELD(e, pthread_create, attr);
  SET_FIELD(e, pthread_create, start_routine);
  SET_FIELD(e, pthread_create, arg);
  return e;
}

log_entry_t create_pthread_detach_entry(long long int clone_id, int event,
    unsigned long int thread)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pthread_detach, thread);
  return e;
}

log_entry_t create_pthread_exit_entry(long long int clone_id, int event,
    unsigned long int value_ptr)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pthread_exit, value_ptr);
  return e;
}

log_entry_t create_pthread_join_entry(long long int clone_id, int event,
    unsigned long int thread, unsigned long int value_ptr)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pthread_join, thread);
  SET_FIELD(e, pthread_join, value_ptr);
  return e;
}

log_entry_t create_pthread_kill_entry(long long int clone_id, int event,
    unsigned long int thread, int sig)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pthread_kill, thread);
  SET_FIELD(e, pthread_kill, sig);
  return e;
}

log_entry_t create_pthread_mutex_lock_entry(int clone_id, int event, unsigned long int mutex) {
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pthread_mutex_lock, mutex);
  return e;
}

log_entry_t create_pthread_mutex_trylock_entry(int clone_id, int event, unsigned long int mutex) {
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pthread_mutex_trylock, mutex);
  return e;
}

log_entry_t create_pthread_mutex_unlock_entry(int clone_id, int event, unsigned long int mutex) {
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pthread_mutex_unlock, mutex);
  return e;
}

log_entry_t create_rand_entry(int clone_id, int event)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  return e;
}

log_entry_t create_read_entry(int clone_id, int event, int readfd,
    unsigned long int buf_addr, size_t count)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, read, readfd);
  SET_FIELD(e, read, buf_addr);
  SET_FIELD(e, read, count);
  return e;
}

log_entry_t create_readdir_entry(int clone_id, int event, DIR *dirp)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, readdir, dirp, (unsigned long int)dirp);
  return e;
}

log_entry_t create_readdir_r_entry(int clone_id, int event, DIR *dirp,
    struct dirent *entry, struct dirent **result)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, readdir_r, dirp, (unsigned long int)dirp);
  // Leave 'entry' uninitialized.
  SET_FIELD2(e, readdir_r, result, 0);
  return e;
}

log_entry_t create_readlink_entry(int clone_id, int event,
    const char *path, char *buf, size_t bufsiz)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, readlink, path, (unsigned long int)path);
  // Just initialize 'buf' to all 0s
  memset(GET_FIELD(e, readlink, buf), 0, READLINK_MAX_LENGTH);
  SET_FIELD(e, readlink, bufsiz);
  return e;
}

log_entry_t create_realloc_entry(int clone_id, int event, 
    unsigned long int ptr, size_t size)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, realloc, ptr);
  SET_FIELD(e, realloc, size);
  return e;
}

log_entry_t create_rename_entry(int clone_id, int event, const char *oldpath,
    const char *newpath)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, rename, oldpath, (unsigned long int)oldpath);
  SET_FIELD2(e, rename, newpath, (unsigned long int)newpath);
  return e;
}

log_entry_t create_rewind_entry(int clone_id, int event, FILE *stream)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, rewind, stream, (unsigned long int)stream);
  return e;
}

log_entry_t create_rmdir_entry(int clone_id, int event, const char *pathname)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, rmdir, pathname, (unsigned long int)pathname);
  return e;
}

log_entry_t create_select_entry(int clone_id, int event, int nfds,
    fd_set *readfds, fd_set *writefds,
    unsigned long int exceptfds, unsigned long int timeout)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, select, nfds);
  // We have to do something special for 'readfds' and 'writefds' since we
  // do a deep copy.
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY_DEBUG
  copyFdSet(readfds, &e.readfds);
  copyFdSet(writefds, &e.writefds);
#else
  copyFdSet(readfds, &e.log_event_t.log_event_select.readfds);
  copyFdSet(writefds, &e.log_event_t.log_event_select.writefds);
#endif
  SET_FIELD(e, select, exceptfds);
  SET_FIELD(e, select, timeout);
  return e;
}

log_entry_t create_setsockopt_entry(int clone_id, int event, int sockfd,
    int level, int optname, unsigned long int optval, socklen_t optlen) {
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, setsockopt, sockfd);
  SET_FIELD(e, setsockopt, level);
  SET_FIELD(e, setsockopt, optname);
  SET_FIELD(e, setsockopt, optval);
  SET_FIELD(e, setsockopt, optlen);
  return e;
}

log_entry_t create_signal_handler_entry(int clone_id, int event, int sig)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, signal_handler, sig);
  return e;
}

log_entry_t create_sigwait_entry(int clone_id, int event, unsigned long int set,
    unsigned long int sigwait_sig)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, sigwait, set);
  SET_FIELD(e, sigwait, sigwait_sig);
  return e;
}

log_entry_t create_srand_entry(int clone_id, int event, unsigned int seed)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, srand, seed);
  return e;
}

log_entry_t create_socket_entry(int clone_id, int event, int domain, int type,
    int protocol)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, socket, domain);
  SET_FIELD(e, socket, type);
  SET_FIELD(e, socket, protocol);
  return e;
}

log_entry_t create_xstat_entry(int clone_id, int event, int vers,
    unsigned long int path, struct stat buf)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, xstat, vers);
  SET_FIELD(e, xstat, path);
  //SET_FIELD(e, xstat, buf);
  return e;
}

log_entry_t create_xstat64_entry(int clone_id, int event, int vers,
    unsigned long int path, struct stat64 buf)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, xstat64, vers);
  SET_FIELD(e, xstat64, path);
  SET_FIELD(e, xstat64, buf);
  return e;
}

log_entry_t create_time_entry(int clone_id, int event, unsigned long int tloc)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, time, tloc);
  return e;
}

log_entry_t create_unlink_entry(int clone_id, int event,
     const char *pathname)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, unlink, pathname, (unsigned long int)pathname);
  return e;
}

log_entry_t create_write_entry(int clone_id, int event, int writefd,
    unsigned long int buf_addr, size_t count)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, write, writefd);
  SET_FIELD(e, write, buf_addr);
  SET_FIELD(e, write, count);
  return e;
}

void addNextLogEntry(log_entry_t e)
{
  if (SYNC_IS_REPLAY) {
    JASSERT (false).Text("Asked to log an event while in replay. "
        "This is probably not intended.");
  }
  _real_pthread_mutex_lock(&log_index_mutex);
  SET_COMMON2(e, log_id, current_log_id++);
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY_DEBUG
  log[log_index] = e;
  //JTRACE ( "logged event." ) ( e.clone_id ) ( e.event ) ( log_index );
  atomic_increment(&log_index);
  if (__builtin_expect(log_index >= MAX_LOG_LENGTH, 0)) {
    _real_pthread_mutex_lock(&log_file_mutex);
    JTRACE ( "Log overflowed bounds. Writing to disk." );
    writeLogsToDisk();
    _real_pthread_mutex_unlock(&log_file_mutex);
  }
#else
  int event_size;
  GET_EVENT_SIZE(GET_COMMON(e,event), event_size);
  if ((log_index + log_event_common_size + event_size) > MAX_LOG_LENGTH) {
    // The new entry doesn't fit in the current log. Write the log to disk.
    _real_pthread_mutex_lock(&log_file_mutex);
    JTRACE ( "Log overflowed bounds. Writing to disk." );
    writeLogsToDisk();
    _real_pthread_mutex_unlock(&log_file_mutex);
  }
  // Copy common data to log[] buffer:
  log_entry_to_buffer(&e, &log[log_index]);
  log_index += log_event_common_size;
  // Copy event-specific data to log[] buffer:
  COPY_TO_MEMORY_LOG(e);
  log_index += event_size;
#endif
  // Keep this up to date for debugging purposes:
  log_entry_index++;
  _real_pthread_mutex_unlock(&log_index_mutex);  
}

void getNextLogEntry() {
  _real_pthread_mutex_lock(&log_index_mutex);
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY_DEBUG
  currentLogEntry = log[log_index];
  log_index++;
  if (log_index >= MAX_LOG_LENGTH) {
    JTRACE ( "Ran out of log entries. Reading next from disk." ) 
           ( log_entry_index ) ( MAX_LOG_LENGTH );
    readLogFromDisk();
  }
#else
  int event_size;
  // Make sure to cast the event type byte to the correct type:
  GET_EVENT_SIZE((unsigned char)log[log_index], event_size);
  int newEntrySize = log_event_common_size + event_size;
  if (__builtin_expect((log_index + newEntrySize) <= MAX_LOG_LENGTH, 1)) {
    /* The entire currentLogEntry can be retrieved from the current log.
       This is the most common case. */
    // Copy common data to currentLogEntry:
    buffer_to_log_entry(&log[log_index], &currentLogEntry);
    log_index += log_event_common_size;
    // Copy event-specific data to currentLogEntry:
    COPY_FROM_MEMORY_SOURCE(log[log_index]);
    log_index += event_size;
    if (__builtin_expect((log_index) == MAX_LOG_LENGTH, 0)) {
      JTRACE ( "Ran out of log entries. Reading next from disk." ) 
             ( log_entry_index ) ( MAX_LOG_LENGTH );
      readLogFromDisk();
      log_index = 0;
    }
  } else {
    // The size that can be retrieved from the current log.
    int size = MAX_LOG_LENGTH - log_index;
    JASSERT ( size < 256 );
    char tmp[256] = {0};
    memcpy(tmp, &log[log_index], size);
    JTRACE ( "Ran out of log entries. Reading next from disk." ) 
           ( log_entry_index ) ( MAX_LOG_LENGTH );
    readLogFromDisk();
    memcpy(&tmp[size], &log[0], newEntrySize - size); 
    // Copy common data to currentLogEntry:
    buffer_to_log_entry(&tmp[0], &currentLogEntry);
    // Copy event-specific data to currentLogEntry:
    COPY_FROM_MEMORY_SOURCE(tmp[log_event_common_size]);
    // Update new log_index:
    log_index = newEntrySize - size;
  }
#endif
  log_entry_index++;
  _real_pthread_mutex_unlock(&log_index_mutex);
}

void logReadData(void *buf, int count)
{
  if (SYNC_IS_REPLAY) {
    JASSERT (false).Text("Asked to log read data while in replay. "
        "This is probably not intended.");
  }
  if (read_data_fd == -1) {
    read_data_fd = open(SYNCHRONIZATION_READ_DATA_LOG_PATH,
        O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
  }
  int written = write(read_data_fd, buf, count);
  JASSERT ( written == count );
  read_log_pos += written;
}

void writeLogsToDisk() {
  if (SYNC_IS_REPLAY) {
    JTRACE ( "calling writeLogsToDisk() while in replay. This is probably an error. Not writing." );
    return;
  }
  if (strlen(SYNCHRONIZATION_LOG_PATH) == 0) {
    JTRACE ( "SYNCHRONIZATION_LOG_PATH empty. Not writing." );
    return;
  }
  int numwritten = 0;
  int num_to_write = 0;
  /* 'num_to_write' needs some explanation, since off-by-one errors are
     very likely to creep in when using this kind of logic. There are
     two scenarios where writeLogsToDisk() is called:

     1) The in-memory log is full. This happens when a thread makes the last
        entry in the log (log[MAX_LOG_LENGTH-1]) and then increments
        log_index. Then, immediately after the increment of log_index, that
        thread checks to see if log_index >= MAX_LOG_LENGTH. When that is true,
        it calls writeLogsToDisk().  In that case, the check below (if
        log_index == MAX_LOG_LENGTH) is TRUE. Then we want to write
        MAX_LOG_LENGTH*sizeof(log_entry_t) bytes to the log.

     2) The user program has exited. This means that log_index may be anywhere
        in the (closed) interval [0, MAX_LOG_LENGTH]. If it is NOT EQUAL to
        MAX_LOG_LENGTH, then we need to write (log_index+1)*sizeof(log_entry_t)
        bytes. For example, the user program exits when log_index is 1862. That
        means that log entries 0-1862 (inclusive) are in memory. Thus, we add
        one to the index to get 1863, the total number of entries to write.
        However, if the user program exited and log_index is 0, that means that
        there was nothing recorded, so we should not write anything.

	NOT NECESSARILY: The last wrapper execution calls addNextLogEntry which
        logs at the current index, and then increments log_index. Thus, we're
        left with the index pointing at the next element, which is never
        recorded or needed since this was the last wrapper execution.
  */
  if (log_index == MAX_LOG_LENGTH) {
    num_to_write = LOG_ENTRY_SIZE*MAX_LOG_LENGTH;
  } else if (log_index == 0) {
    JTRACE ( "log size 0, so nothing written to disk." );
    return;
  } else {
    // SEE #2 above for comment on this branch. For now I'm going with the 'NOT
    // NECESSARILY' comment.
    num_to_write = LOG_ENTRY_SIZE*log_index;
  }
  //JTRACE ( "writing to log path" ) ( SYNCHRONIZATION_LOG_PATH );
  while ((synchronization_log_fd = open(SYNCHRONIZATION_LOG_PATH, 
              O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR)) == -1
      && errno == EINTR) ;
  JASSERT ( synchronization_log_fd != -1 ) ( strerror(errno) );
  numwritten = write(synchronization_log_fd, log, num_to_write);
  JASSERT ( numwritten != -1) ( strerror(errno) );
  JASSERT ( fsync(synchronization_log_fd) == 0 ) ( strerror(errno) );

  close(synchronization_log_fd);
  JTRACE ( "Synchronization log successfully written to disk." ) ( num_to_write ) ( numwritten );
  atomic_increment(&log_offset);
  resetLog();
}

static TURN_CHECK_P(base_turn_check)
{
  // Predicate function for a basic check -- event # and clone id.
  // TODO: factor out this anomalous signal business.
  return GET_COMMON_PTR(e1,clone_id) == GET_COMMON_PTR(e2,clone_id) &&
    ((GET_COMMON_PTR(e1,event) == GET_COMMON_PTR(e2,event)) ||
     (GET_COMMON_PTR(e1,event) == pthread_cond_signal_event && 
         GET_COMMON_PTR(e2,event) == pthread_cond_signal_anomalous_event) ||
     (GET_COMMON_PTR(e1,event) == pthread_cond_signal_anomalous_event && 
         GET_COMMON_PTR(e2,event) == pthread_cond_signal_event) ||
     (GET_COMMON_PTR(e1,event) == pthread_cond_broadcast_event && 
         GET_COMMON_PTR(e2,event) == pthread_cond_broadcast_anomalous_event) ||
     (GET_COMMON_PTR(e1,event) == pthread_cond_broadcast_anomalous_event && 
         GET_COMMON_PTR(e2,event) == pthread_cond_broadcast_event));
}

TURN_CHECK_P(pthread_mutex_lock_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, pthread_mutex_lock, mutex) ==
      GET_FIELD_PTR(e2, pthread_mutex_lock, mutex);
}

TURN_CHECK_P(pthread_mutex_trylock_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, pthread_mutex_trylock, mutex) ==
      GET_FIELD_PTR(e2, pthread_mutex_trylock, mutex);
}

TURN_CHECK_P(pthread_mutex_unlock_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, pthread_mutex_unlock, mutex) ==
      GET_FIELD_PTR(e2, pthread_mutex_unlock, mutex);
}

TURN_CHECK_P(pthread_cond_signal_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, pthread_cond_signal, cond_var) ==
      GET_FIELD_PTR(e2, pthread_cond_signal, cond_var);
}

TURN_CHECK_P(pthread_cond_broadcast_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, pthread_cond_broadcast, cond_var) ==
      GET_FIELD_PTR(e2, pthread_cond_broadcast, cond_var);
}

TURN_CHECK_P(pthread_cond_wait_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, pthread_cond_wait, mutex) ==
      GET_FIELD_PTR(e2, pthread_cond_wait, mutex) &&
    GET_FIELD_PTR(e1, pthread_cond_wait, cond_var) ==
      GET_FIELD_PTR(e2, pthread_cond_wait, cond_var);
}

TURN_CHECK_P(pthread_cond_timedwait_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, pthread_cond_timedwait, mutex) ==
      GET_FIELD_PTR(e2, pthread_cond_timedwait, mutex) &&
    GET_FIELD_PTR(e1, pthread_cond_timedwait, cond_var) ==
      GET_FIELD_PTR(e2, pthread_cond_timedwait, cond_var) &&
    GET_FIELD_PTR(e1, pthread_cond_timedwait, abstime) ==
      GET_FIELD_PTR(e2, pthread_cond_timedwait, abstime);
}

TURN_CHECK_P(pthread_rwlock_unlock_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, pthread_rwlock_unlock, rwlock) ==
      GET_FIELD_PTR(e2, pthread_rwlock_unlock, rwlock);
}

TURN_CHECK_P(pthread_rwlock_rdlock_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, pthread_rwlock_rdlock, rwlock) ==
      GET_FIELD_PTR(e2, pthread_rwlock_rdlock, rwlock);
}

TURN_CHECK_P(pthread_rwlock_wrlock_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, pthread_rwlock_wrlock, rwlock) ==
      GET_FIELD_PTR(e2, pthread_rwlock_wrlock, rwlock);
}

TURN_CHECK_P(pthread_create_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, pthread_create, thread) ==
      GET_FIELD_PTR(e2, pthread_create, thread) &&
    GET_FIELD_PTR(e1, pthread_create, attr) ==
      GET_FIELD_PTR(e2, pthread_create, attr) &&
    GET_FIELD_PTR(e1, pthread_create, start_routine) ==
      GET_FIELD_PTR(e2, pthread_create, start_routine) &&
    GET_FIELD_PTR(e1, pthread_create, arg) ==
      GET_FIELD_PTR(e2, pthread_create, arg);
}

TURN_CHECK_P(pthread_detach_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, pthread_detach, thread) ==
      GET_FIELD_PTR(e2, pthread_detach, thread);
}

TURN_CHECK_P(pthread_exit_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, pthread_exit, value_ptr) ==
      GET_FIELD_PTR(e2, pthread_exit, value_ptr);
}


TURN_CHECK_P(pthread_join_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, pthread_join, thread) ==
      GET_FIELD_PTR(e2, pthread_join, thread) &&
    GET_FIELD_PTR(e1, pthread_join, value_ptr) ==
      GET_FIELD_PTR(e2, pthread_join, value_ptr);
}

TURN_CHECK_P(pthread_kill_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, pthread_kill, thread) ==
      GET_FIELD_PTR(e2, pthread_kill, thread) &&
    GET_FIELD_PTR(e1, pthread_kill, sig) ==
      GET_FIELD_PTR(e2, pthread_kill, sig);
}

TURN_CHECK_P(read_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, read, buf_addr) ==
      GET_FIELD_PTR(e2, read, buf_addr) &&
    GET_FIELD_PTR(e1, read, count) ==
      GET_FIELD_PTR(e2, read, count);
}

TURN_CHECK_P(readdir_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, readdir, dirp) ==
      GET_FIELD_PTR(e2, readdir, dirp);
}

TURN_CHECK_P(readdir_r_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, readdir_r, dirp) ==
      GET_FIELD_PTR(e2, readdir_r, dirp);
}

TURN_CHECK_P(readlink_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, readlink, path) ==
      GET_FIELD_PTR(e2, readlink, path) &&
    GET_FIELD_PTR(e1, readlink, bufsiz) ==
      GET_FIELD_PTR(e2, readlink, bufsiz);
}

TURN_CHECK_P(unlink_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, unlink, pathname) ==
      GET_FIELD_PTR(e2, unlink, pathname);
}

TURN_CHECK_P(write_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, write, buf_addr) ==
      GET_FIELD_PTR(e2, write, buf_addr) &&
    GET_FIELD_PTR(e1, write, count) ==
      GET_FIELD_PTR(e2, write, count);
}

TURN_CHECK_P(close_turn_check)
{
  return base_turn_check(e1, e2);// && e1->fd == e2->fd;
}

TURN_CHECK_P(connect_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, connect, serv_addr) ==
      GET_FIELD_PTR(e2, connect, serv_addr) &&
    GET_FIELD_PTR(e1, connect, addrlen) ==
      GET_FIELD_PTR(e2, connect, addrlen);
}

TURN_CHECK_P(dup_turn_check)
{
  return base_turn_check(e1, e2);// && e1->fd == e2->fd;
}

TURN_CHECK_P(rand_turn_check)
{
  return base_turn_check(e1, e2);
}

TURN_CHECK_P(srand_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, srand, seed) ==
      GET_FIELD_PTR(e2, srand, seed);
}

TURN_CHECK_P(socket_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, socket, domain) ==
      GET_FIELD_PTR(e2, socket, domain) &&
    GET_FIELD_PTR(e1, socket, type) ==
      GET_FIELD_PTR(e2, socket, type) &&
    GET_FIELD_PTR(e1, socket, protocol) ==
      GET_FIELD_PTR(e2, socket, protocol);
}

TURN_CHECK_P(xstat_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, xstat, vers) ==
      GET_FIELD_PTR(e2, xstat, vers) &&
    GET_FIELD_PTR(e1, xstat, path) ==
      GET_FIELD_PTR(e2, xstat, path);
  /*GET_FIELD_PTR(e1, xstat, buf) ==
    GET_FIELD_PTR(e2, xstat, buf);*/
}

TURN_CHECK_P(xstat64_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, xstat64, vers) ==
      GET_FIELD_PTR(e2, xstat64, vers) &&
    GET_FIELD_PTR(e1, xstat64, path) ==
      GET_FIELD_PTR(e2, xstat64, path);
  /*GET_FIELD_PTR(e1, xstat64, buf) ==
    GET_FIELD_PTR(e2, xstat64, buf);*/
}

TURN_CHECK_P(time_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, time, tloc) ==
      GET_FIELD_PTR(e2, time, tloc);
}

TURN_CHECK_P(accept_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, accept, sockaddr) ==
      GET_FIELD_PTR(e2, accept, sockaddr) &&
    GET_FIELD_PTR(e1, accept, addrlen) ==
      GET_FIELD_PTR(e2, accept, addrlen);
}

TURN_CHECK_P(access_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, access, pathname) ==
      GET_FIELD_PTR(e2, access, pathname) &&
    GET_FIELD_PTR(e1, access, mode) ==
      GET_FIELD_PTR(e2, access, mode);
}

TURN_CHECK_P(bind_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, bind, my_addr) ==
      GET_FIELD_PTR(e2, bind, my_addr) &&
    GET_FIELD_PTR(e1, bind, addrlen) ==
      GET_FIELD_PTR(e2, bind, addrlen);
}

TURN_CHECK_P(getpeername_turn_check)
{
  return base_turn_check(e1, e2) &&
    /*GET_FIELD_PTR(e1, getpeername, sockfd) ==
      GET_FIELD_PTR(e2, getpeername, sockfd) &&*/
    GET_FIELD_PTR(e1, getpeername, addrlen) ==
      GET_FIELD_PTR(e2, getpeername, addrlen);
    // TODO: How to compare these:
  /*GET_FIELD_PTR(e1, getpeername, sockaddr) ==
      GET_FIELD_PTR(e2, getpeername, sockaddr)*/
}

TURN_CHECK_P(getsockname_turn_check)
{
  return base_turn_check(e1, e2) &&
    /*GET_FIELD_PTR(e1, getsockname, sockfd) ==
      GET_FIELD_PTR(e2, getsockname, sockfd) &&*/
    GET_FIELD_PTR(e1, getsockname, addrlen) ==
      GET_FIELD_PTR(e2, getsockname, addrlen) &&
    GET_FIELD_PTR(e1, getsockname, sockaddr) ==
      GET_FIELD_PTR(e2, getsockname, sockaddr);
}

TURN_CHECK_P(setsockopt_turn_check)
{
  return base_turn_check(e1,e2) && 
    GET_FIELD_PTR(e1, setsockopt, level) ==
      GET_FIELD_PTR(e2, setsockopt, level) &&
    GET_FIELD_PTR(e1, setsockopt, optname) ==
      GET_FIELD_PTR(e2, setsockopt, optname) &&
    GET_FIELD_PTR(e1, setsockopt, optval) ==
      GET_FIELD_PTR(e2, setsockopt, optval) &&
    GET_FIELD_PTR(e1, setsockopt, optlen) ==
      GET_FIELD_PTR(e2, setsockopt, optlen);
}

TURN_CHECK_P(signal_handler_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, signal_handler, sig) ==
    GET_FIELD_PTR(e2, signal_handler, sig);
}

TURN_CHECK_P(sigwait_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, sigwait, set) ==
      GET_FIELD_PTR(e2, sigwait, set) &&
    GET_FIELD_PTR(e1, sigwait, sigwait_sig) ==
      GET_FIELD_PTR(e2, sigwait, sigwait_sig);
}

TURN_CHECK_P(fclose_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, fclose, fp) ==
      GET_FIELD_PTR(e2, fclose, fp);
}

TURN_CHECK_P(fcntl_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, fcntl, cmd) ==
      GET_FIELD_PTR(e2, fcntl, cmd) &&
    GET_FIELD_PTR(e1, fcntl, arg_3_l) ==
      GET_FIELD_PTR(e2, fcntl, arg_3_l) &&
    GET_FIELD_PTR(e1, fcntl, arg_3_f) ==
      GET_FIELD_PTR(e2, fcntl, arg_3_f);
}

TURN_CHECK_P(fdatasync_turn_check)
{
  return base_turn_check(e1,e2);
}

TURN_CHECK_P(fdopen_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, fdopen, fd) ==
      GET_FIELD_PTR(e2, fdopen, fd) &&
    GET_FIELD_PTR(e1, fdopen, mode) ==
      GET_FIELD_PTR(e2, fdopen, mode);
}

TURN_CHECK_P(fgets_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, fgets, s) ==
      GET_FIELD_PTR(e2, fgets, s) &&
    GET_FIELD_PTR(e1, fgets, stream) ==
      GET_FIELD_PTR(e2, fgets, stream) &&
    GET_FIELD_PTR(e1, fgets, size) ==
      GET_FIELD_PTR(e2, fgets, size);
}

TURN_CHECK_P(getc_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, getc, stream) ==
      GET_FIELD_PTR(e2, getc, stream);
}

TURN_CHECK_P(getline_turn_check)
{
  /* We don't check for n because it might change, in case lineptr gets
     reallocated. */
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, getline, lineptr) ==
      GET_FIELD_PTR(e2, getline, lineptr) &&
    GET_FIELD_PTR(e1, getline, stream) ==
      GET_FIELD_PTR(e2, getline, stream);
}

TURN_CHECK_P(fopen_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, fopen, name) ==
      GET_FIELD_PTR(e2, fopen, name) &&
    GET_FIELD_PTR(e1, fopen, mode) ==
      GET_FIELD_PTR(e2, fopen, mode);
}

TURN_CHECK_P(fprintf_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, fprintf, stream) ==
      GET_FIELD_PTR(e2, fprintf, stream) &&
    GET_FIELD_PTR(e1, fprintf, format) ==
      GET_FIELD_PTR(e2, fprintf, format);
}

TURN_CHECK_P(fscanf_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, fscanf, stream) ==
      GET_FIELD_PTR(e2, fscanf, stream) &&
    GET_FIELD_PTR(e1, fscanf, format) ==
      GET_FIELD_PTR(e2, fscanf, format);
}

TURN_CHECK_P(fputs_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, fputs, s) ==
      GET_FIELD_PTR(e2, fputs, s) &&
    GET_FIELD_PTR(e1, fputs, stream) ==
      GET_FIELD_PTR(e2, fputs, stream);
}

TURN_CHECK_P(calloc_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, calloc, nmemb) ==
      GET_FIELD_PTR(e2, calloc, nmemb) &&
    GET_FIELD_PTR(e1, calloc, size) ==
      GET_FIELD_PTR(e2, calloc, size);
}

TURN_CHECK_P(lseek_turn_check)
{
  return base_turn_check(e1,e2) &&
    /*GET_FIELD_PTR(e1, lseek, fd) ==
      GET_FIELD_PTR(e2, lseek, fd) &&*/
    GET_FIELD_PTR(e1, lseek, offset) ==
      GET_FIELD_PTR(e2, lseek, offset) &&
    GET_FIELD_PTR(e1, lseek, whence) ==
      GET_FIELD_PTR(e2, lseek, whence);
}

TURN_CHECK_P(link_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, link, oldpath) ==
      GET_FIELD_PTR(e2, link, oldpath) &&
    GET_FIELD_PTR(e1, link, newpath) ==
      GET_FIELD_PTR(e2, link, newpath);
}

TURN_CHECK_P(listen_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, listen, sockfd) ==
      GET_FIELD_PTR(e2, listen, sockfd) &&
    GET_FIELD_PTR(e1, listen, backlog) ==
      GET_FIELD_PTR(e2, listen, backlog);
}

TURN_CHECK_P(lxstat_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, lxstat, vers) ==
      GET_FIELD_PTR(e2, lxstat, vers) &&
    GET_FIELD_PTR(e1, lxstat, path) ==
      GET_FIELD_PTR(e2, lxstat, path);
  /*GET_FIELD_PTR(e1, lxstat, buf) ==
    GET_FIELD_PTR(e2, lxstat, buf);*/
}

TURN_CHECK_P(lxstat64_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, lxstat64, vers) ==
      GET_FIELD_PTR(e2, lxstat64, vers) &&
    GET_FIELD_PTR(e1, lxstat64, path) ==
      GET_FIELD_PTR(e2, lxstat64, path);
  /*GET_FIELD_PTR(e1, lxstat64, buf) ==
    GET_FIELD_PTR(e2, lxstat64, buf);*/
}

TURN_CHECK_P(malloc_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, malloc, size) ==
      GET_FIELD_PTR(e2, malloc, size);
}

TURN_CHECK_P(mkdir_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, mkdir, pathname) ==
      GET_FIELD_PTR(e2, mkdir, pathname) &&
    GET_FIELD_PTR(e1, mkdir, mode) ==
      GET_FIELD_PTR(e2, mkdir, mode);
}

TURN_CHECK_P(mkstemp_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, mkstemp, temp) ==
      GET_FIELD_PTR(e2, mkstemp, temp);
}

TURN_CHECK_P(open_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, open, path) ==
      GET_FIELD_PTR(e2, open, path) &&
    GET_FIELD_PTR(e1, open, flags) ==
      GET_FIELD_PTR(e2, open, flags) &&
    GET_FIELD_PTR(e1, open, open_mode) ==
      GET_FIELD_PTR(e2, open, open_mode);
}

TURN_CHECK_P(pread_turn_check)
{
  return base_turn_check(e1, e2) &&
    /*GET_FIELD_PTR(e1, pread, fd) ==
      GET_FIELD_PTR(e2, pread, fd) &&*/
    GET_FIELD_PTR(e1, pread, buf) ==
      GET_FIELD_PTR(e2, pread, buf) &&
    GET_FIELD_PTR(e1, pread, count) ==
      GET_FIELD_PTR(e2, pread, count) &&
    GET_FIELD_PTR(e1, pread, offset) ==
      GET_FIELD_PTR(e2, pread, offset);
}

TURN_CHECK_P(putc_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, putc, c) ==
      GET_FIELD_PTR(e2, putc, c) &&
    GET_FIELD_PTR(e1, putc, stream) ==
      GET_FIELD_PTR(e2, putc, stream);
}

TURN_CHECK_P(pwrite_turn_check)
{
  return base_turn_check(e1, e2) &&
    /*GET_FIELD_PTR(e1, pwrite, fd) ==
      GET_FIELD_PTR(e2, pwrite, fd) &&*/
    GET_FIELD_PTR(e1, pwrite, buf) ==
      GET_FIELD_PTR(e2, pwrite, buf) &&
    GET_FIELD_PTR(e1, pwrite, count) ==
      GET_FIELD_PTR(e2, pwrite, count) &&
    GET_FIELD_PTR(e1, pwrite, offset) ==
      GET_FIELD_PTR(e2, pwrite, offset);
}

TURN_CHECK_P(libc_memalign_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, libc_memalign, boundary) ==
      GET_FIELD_PTR(e2, libc_memalign, boundary) &&
    GET_FIELD_PTR(e1, libc_memalign, size) ==
      GET_FIELD_PTR(e2, libc_memalign, size);
}

TURN_CHECK_P(free_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, free, ptr) ==
      GET_FIELD_PTR(e2, free, ptr);
}

TURN_CHECK_P(ftell_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, ftell, stream) ==
      GET_FIELD_PTR(e2, ftell, stream);
}

TURN_CHECK_P(fwrite_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, fwrite, ptr) ==
      GET_FIELD_PTR(e2, fwrite, ptr) &&
    GET_FIELD_PTR(e1, fwrite, size) ==
      GET_FIELD_PTR(e2, fwrite, size) &&
    GET_FIELD_PTR(e1, fwrite, nmemb) ==
      GET_FIELD_PTR(e2, fwrite, nmemb) &&
    GET_FIELD_PTR(e1, fwrite, stream) ==
      GET_FIELD_PTR(e2, fwrite, stream);
}

TURN_CHECK_P(fsync_turn_check)
{
  return base_turn_check(e1, e2);/* &&
    GET_FIELD_PTR(e1, fsync, fd) ==
    GET_FIELD_PTR(e2, fsync, fd);*/
}

TURN_CHECK_P(fxstat_turn_check)
{
  return base_turn_check(e1, e2) &&
    /*GET_FIELD_PTR(e1, fxstat, fd) ==
      GET_FIELD_PTR(e2, fxstat, fd) &&*/
    GET_FIELD_PTR(e1, fxstat, vers) ==
      GET_FIELD_PTR(e2, fxstat, vers);
    /*GET_FIELD_PTR(e1, fxstat, buf) ==
      GET_FIELD_PTR(e2, fxstat, buf);*/
}

TURN_CHECK_P(fxstat64_turn_check)
{
  return base_turn_check(e1, e2) &&
    /*GET_FIELD_PTR(e1, fxstat64, fd) ==
      GET_FIELD_PTR(e2, fxstat64, fd) &&*/
    GET_FIELD_PTR(e1, fxstat64, vers) ==
      GET_FIELD_PTR(e2, fxstat64, vers);
    /*GET_FIELD_PTR(e1, fxstat64, buf) ==
      GET_FIELD_PTR(e2, fxstat64, buf);*/
}

TURN_CHECK_P(realloc_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, realloc, ptr) ==
      GET_FIELD_PTR(e2, realloc, ptr) &&
    GET_FIELD_PTR(e1, realloc, size) ==
      GET_FIELD_PTR(e2, realloc, size);
}

TURN_CHECK_P(rename_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, rename, oldpath) ==
      GET_FIELD_PTR(e2, rename, oldpath) &&
    GET_FIELD_PTR(e1, rename, newpath) ==
      GET_FIELD_PTR(e2, rename, newpath);
}

TURN_CHECK_P(rewind_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, rewind, stream) ==
      GET_FIELD_PTR(e2, rewind, stream);
}

TURN_CHECK_P(rmdir_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, rmdir, pathname) ==
      GET_FIELD_PTR(e2, rmdir, pathname);
}

TURN_CHECK_P(select_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, select, nfds) ==
      GET_FIELD_PTR(e2, select, nfds) &&
    GET_FIELD_PTR(e1, select, exceptfds) ==
      GET_FIELD_PTR(e2, select, exceptfds) &&
    GET_FIELD_PTR(e1, select, timeout) ==
      GET_FIELD_PTR(e2, select, timeout);
}

const char *eventToString(int e) {
  switch (e) {
  case pthread_mutex_lock_event:
    return "lock";
  case pthread_mutex_unlock_event:
    return "unlock";
  case pthread_cond_signal_event:
    return "cond_signal";
  case pthread_cond_broadcast_event:
    return "cond_broadcast";
  case pthread_cond_signal_anomalous_event:
    return "anomalous_signal";
  case pthread_cond_broadcast_anomalous_event:
    return "anomalous_broadcast";
  case pthread_cond_wait_event:
    return "cond_wait";
  case pthread_cond_wait_event_return:
    return "cond_wakeup";
  case select_event:
    return "select";
  case read_event:
    return "read";
  case read_event_return:
    return "read_data";
  case pthread_create_event:
    return "pthread_create";
  case pthread_create_event_return:
    return "pthread_create_return";
  case exec_barrier_event:
    return "exec_barrier";
  case malloc_event:
    return "malloc";
  case malloc_event_return:
    return "malloc_return";
  case calloc_event:
    return "calloc";
  case calloc_event_return:
    return "calloc_return";
  case realloc_event:
    return "realloc";
  case realloc_event_return:
    return "realloc_return";
  case free_event:
    return "free";
  case free_event_return:
    return "free_return";
  default:
    return "unknown";
  };
}

void waitForTurn(log_entry_t my_entry, turn_pred_t pred)
{
  memfence();
  if (__builtin_expect(log_loaded == 0, 0)) {
    // If log_loaded == 0, then this is the first time.
    // Perform any initialization things here.
    primeLog();
  }
  while (1) {
    /*
    printf ("%d %d %d %d %d %d\n", currentLogEntry.event, currentLogEntry.clone_id,
                                   currentLogEntry.log_id,
                                   my_entry.event, my_entry.clone_id, my_entry.log_id);
    */
    // TODO: can we use __builtin_expect here?
    if ((*pred)(&currentLogEntry, &my_entry))
      break;

    memfence();
    usleep(15);
  }
}

void waitForExecBarrier()
{
  while (1) {
    if (GET_COMMON(currentLogEntry, event) == exec_barrier_event) {
      // We don't check clone ids because anyone can do an exec.
      break;
    }
    memfence();
    usleep(20);
  }
}
#endif
