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
#include "util.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jtimer.h"
#include  "../jalib/jfilesystem.h"
#include <sys/select.h>
#include "synchronizationlogging.h"
#include "log.h"
#include <sys/resource.h>

#ifdef RECORD_REPLAY

/* #defined constants */
#define MAX_OPTIONAL_EVENTS 5

/* Prototypes */
char* map_file_to_memory(const char* path, size_t size, int flags, int mode);
/* End prototypes */

// TODO: Do we need LIB_PRIVATE again here if we had already specified it in
// the header file?
/* Library private: */
LIB_PRIVATE dmtcp::map<clone_id_t, pthread_t> clone_id_to_tid_table;
LIB_PRIVATE dmtcp::map<pthread_t, clone_id_t> tid_to_clone_id_table;
LIB_PRIVATE dmtcp::map<clone_id_t, dmtcp::SynchronizationLog*> clone_id_to_log_table;
LIB_PRIVATE void* unified_log_addr = NULL;
LIB_PRIVATE dmtcp::map<pthread_t, pthread_join_retval_t> pthread_join_retvals;
LIB_PRIVATE log_entry_t     currentLogEntry = EMPTY_LOG_ENTRY;
LIB_PRIVATE char GLOBAL_LOG_LIST_PATH[RECORD_LOG_PATH_MAX];
LIB_PRIVATE char RECORD_LOG_PATH[RECORD_LOG_PATH_MAX];
LIB_PRIVATE char RECORD_PATCHED_LOG_PATH[RECORD_LOG_PATH_MAX];
LIB_PRIVATE char RECORD_READ_DATA_LOG_PATH[RECORD_LOG_PATH_MAX];
LIB_PRIVATE int             read_data_fd = -1;
LIB_PRIVATE int             sync_logging_branch = 0;

/* Setting this will log/replay *ALL* malloc family
   functions (i.e. including ones from DMTCP, std C++ lib, etc.). */
LIB_PRIVATE int             log_all_allocs = 0;
LIB_PRIVATE size_t          default_stack_size = 0;
LIB_PRIVATE pthread_cond_t  reap_cv = PTHREAD_COND_INITIALIZER;
LIB_PRIVATE pthread_mutex_t fd_change_mutex = PTHREAD_MUTEX_INITIALIZER;
LIB_PRIVATE pthread_mutex_t global_clone_counter_mutex = PTHREAD_MUTEX_INITIALIZER;
LIB_PRIVATE pthread_mutex_t log_index_mutex = PTHREAD_MUTEX_INITIALIZER;
LIB_PRIVATE pthread_mutex_t reap_mutex = PTHREAD_MUTEX_INITIALIZER;
LIB_PRIVATE pthread_mutex_t thread_transition_mutex = PTHREAD_MUTEX_INITIALIZER;
LIB_PRIVATE pthread_t       thread_to_reap;


LIB_PRIVATE dmtcp::SynchronizationLog unified_log;

/* Thread locals: */
LIB_PRIVATE __thread clone_id_t my_clone_id = -1;
LIB_PRIVATE __thread int in_mmap_wrapper = 0;
LIB_PRIVATE __thread dmtcp::SynchronizationLog *my_log;
LIB_PRIVATE __thread unsigned char isOptionalEvent = 0;


/* Volatiles: */
LIB_PRIVATE volatile clone_id_t global_clone_counter = 0;
LIB_PRIVATE volatile off_t         read_log_pos = 0;

LIB_PRIVATE int global_log_list_fd = -1;
LIB_PRIVATE pthread_mutex_t global_log_list_fd_mutex = PTHREAD_MUTEX_INITIALIZER;

static char *code_lower  = 0;
static char *data_break  = 0;
static char *stack_lower = 0;
static char *stack_upper = 0;

static pthread_mutex_t   atomic_set_mutex     = PTHREAD_MUTEX_INITIALIZER;

/* File private volatiles: */
static volatile log_id_t next_log_id = 0;

static inline void memfence() {  asm volatile ("mfence" ::: "memory"); }

# ifndef __GCC_HAVE_SYNC_COMPARE_AND_SWAP_4
// gcc-4.1 and later has __sync_fetch_and_add, __sync_fetch_and_xor, etc.
// We need it for atomic_increment and atomic_decrement
// The version below is slow, but works.  It uses GNU extensions.
#define __sync_fetch_and_add(ptr,val) \
  ({ __typeof__(*(ptr)) tmp; \
    _real_pthread_mutex_lock(&log_index_mutex); \
    tmp = *(ptr); *(ptr) += (val); \
    _real_pthread_mutex_unlock(&log_index_mutex); \
    tmp; \
  })
#define __sync_fetch_and_xor(ptr,val) \
  ({ __typeof__(*(ptr)) tmp; \
    _real_pthread_mutex_lock(&log_index_mutex); \
    tmp = *(ptr); *(ptr) ^= (val); \
    _real_pthread_mutex_unlock(&log_index_mutex); \
    tmp; \
  })
#warning __sync_fetch_and_add not supported -- This will execute more slowly.
// Alternatively, we could copy and adjust some assembly language that we
// generate elsewhere.
# endif

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

// THIS FUNCTION EITHER HAS A BUG OR IS POORLY DOCUMENTED.
// CURRENTLY, IT'S NOT INVOKED.  FIX IT BEFORE INVOKING.
// WAS '__sync_fetch_and_xor(*ptr, *ptr)' INTENDED?  - Gene
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

/* Switch record/replay to specified mode. mode should be one of:
   SYNC_NOOP, SYNC_RECORD, SYNC_REPLAY. */
inline void set_sync_mode(int mode)
{
  char *x = getenv(ENV_VAR_LOG_REPLAY);
  // Don't call setenv() to avoid hidden malloc()
  if (mode == SYNC_NOOP) {
    x[0] = '0';
  } else if (mode == SYNC_RECORD) {
    x[0] = '1';
  } else if (mode == SYNC_REPLAY) {
    x[0] = '2';
  } else {
    JASSERT ( false ) ( mode ).Text("Invalid mode request.");
  }
  x[1] = '\0';
  sync_logging_branch = mode;
}

clone_id_t get_next_clone_id()
{
  return __sync_fetch_and_add (&global_clone_counter, 1);
}

log_id_t get_next_log_id()
{
  return __sync_fetch_and_add (&next_log_id, 1);
}

int shouldSynchronize(void *return_addr)
{
  // Returns 1 if we should synchronize this call, instead of calling _real
  // version. 0 if we should not.
  dmtcp::WorkerState state = dmtcp::WorkerState::currentState();
  if (state != dmtcp::WorkerState::RUNNING) {
    return 0;
  }
  if (!validAddress(return_addr)) {
    return 0;
  }
  return 1;
}

void register_in_global_log_list(clone_id_t clone_id)
{
  _real_pthread_mutex_lock(&global_log_list_fd_mutex);
  if (global_log_list_fd == -1) {
    global_log_list_fd = _real_open(GLOBAL_LOG_LIST_PATH,
                                    O_WRONLY | O_CREAT | O_APPEND,
                                    S_IRUSR | S_IWUSR);
    JASSERT(global_log_list_fd != -1) (JASSERT_ERRNO);
  }

  dmtcp::Util::writeAll(global_log_list_fd, &clone_id, sizeof(clone_id));
  _real_close(global_log_list_fd);
  global_log_list_fd = -1;
  _real_pthread_mutex_unlock(&global_log_list_fd_mutex);
}

dmtcp::vector<clone_id_t> get_log_list()
{
  dmtcp::vector<clone_id_t> clone_ids;
  int fd = _real_open(GLOBAL_LOG_LIST_PATH, O_RDONLY, 0);
  if (fd < 0) {
    return clone_ids;
  }
  clone_id_t id;
  while (dmtcp::Util::readAll(fd, &id, sizeof(id)) != 0) {
    clone_ids.push_back(id);
  }
  _real_close(fd);
  JTRACE("Total number of log files") (clone_ids.size());
  return clone_ids;
}

/* Initializes log pathnames. One log per process. */
void initializeLogNames()
{
  pid_t pid = getpid();
  dmtcp::string tmpdir = dmtcp::UniquePid::getTmpDir();
  snprintf(RECORD_LOG_PATH, RECORD_LOG_PATH_MAX, 
      "%s/synchronization-log-%d", tmpdir.c_str(), pid);
  snprintf(RECORD_PATCHED_LOG_PATH, RECORD_LOG_PATH_MAX, 
      "%s/synchronization-log-%d-patched", tmpdir.c_str(), pid);
  snprintf(RECORD_READ_DATA_LOG_PATH, RECORD_LOG_PATH_MAX, 
      "%s/synchronization-read-log-%d", tmpdir.c_str(), pid);
  snprintf(GLOBAL_LOG_LIST_PATH, RECORD_LOG_PATH_MAX,
      "%s/synchronization-global_log_list-%d", tmpdir.c_str(), pid);
}

/* Truncate all logs to their current positions. */
void truncate_all_logs()
{
  JASSERT ( SYNC_IS_REPLAY );
  dmtcp::map<clone_id_t, dmtcp::SynchronizationLog *>::iterator it;
  for (it = clone_id_to_log_table.begin();
       it != clone_id_to_log_table.end();
       it++) {
    if (it->second->isMappedIn()) {
      it->second->truncate();
    }
  }
  if (unified_log.isMappedIn()) {
    unified_log.truncate();
  }
}

/* Unmap all open logs, if any are in memory. Return whether any were
   unmapped. */
bool close_all_logs()
{
  bool result = false;
  dmtcp::map<clone_id_t, pthread_t>::iterator it;
  for (it = clone_id_to_tid_table.begin();
       it != clone_id_to_tid_table.end();
       it++) {
    if (clone_id_to_log_table[it->first]->isMappedIn()) {
      clone_id_to_log_table[it->first]->destroy();
      result = true;
    }
  }
  if (unified_log.isMappedIn()) {
    unified_log.destroy();
    result = true;
  }
  return result;
}

int isUnlock(log_entry_t e)
{
  return 0;
}
#if 0
int isUnlock(log_entry_t e)
{
  return GET_COMMON(e,event) == pthread_mutex_unlock_event ||
    GET_COMMON(e,event) == select_event_return ||
    GET_COMMON(e,event) == read_event_return ||
    GET_COMMON(e,event) == pthread_create_event_return ||
    GET_COMMON(e,event) == pthread_exit_event ||
    GET_COMMON(e,event) == malloc_event_return ||
    GET_COMMON(e,event) == calloc_event_return ||
    GET_COMMON(e,event) == realloc_event_return ||
    GET_COMMON(e,event) == free_event_return ||
    GET_COMMON(e,event) == accept_event_return ||
    GET_COMMON(e,event) == accept4_event_return ||
    GET_COMMON(e,event) == getsockname_event_return ||
    GET_COMMON(e,event) == fcntl_event_return ||
    GET_COMMON(e,event) == libc_memalign_event_return ||
    GET_COMMON(e,event) == setsockopt_event_return ||
    GET_COMMON(e,event) == write_event_return ||
    GET_COMMON(e,event) == rand_event_return ||
    GET_COMMON(e,event) == srand_event_return ||
    GET_COMMON(e,event) == time_event_return ||
    GET_COMMON(e,event) == pthread_detach_event_return ||
    GET_COMMON(e,event) == pthread_join_event_return ||
    GET_COMMON(e,event) == close_event_return ||
    GET_COMMON(e,event) == signal_handler_event_return ||
    GET_COMMON(e,event) == sigwait_event_return||
    GET_COMMON(e,event) == access_event_return ||
    GET_COMMON(e,event) == open_event_return ||
    GET_COMMON(e,event) == open64_event_return ||
    GET_COMMON(e,event) == pthread_rwlock_unlock_event ||
    GET_COMMON(e,event) == pthread_rwlock_rdlock_event_return ||
    GET_COMMON(e,event) == pthread_rwlock_wrlock_event_return ||
    GET_COMMON(e,event) == pthread_mutex_trylock_event_return ||
    GET_COMMON(e,event) == dup_event_return ||
    GET_COMMON(e,event) == xstat_event_return ||
    GET_COMMON(e,event) == xstat64_event_return ||
    GET_COMMON(e,event) == fxstat_event_return ||
    GET_COMMON(e,event) == fxstat64_event_return ||
    GET_COMMON(e,event) == lxstat_event_return ||
    GET_COMMON(e,event) == lxstat64_event_return ||
    GET_COMMON(e,event) == lseek_event_return ||
    GET_COMMON(e,event) == unlink_event_return ||
    GET_COMMON(e,event) == pread_event_return ||
    GET_COMMON(e,event) == pwrite_event_return ||
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
    GET_COMMON(e,event) == gettimeofday_event_return ||
    GET_COMMON(e,event) == fgetc_event_return ||
    GET_COMMON(e,event) == ungetc_event_return ||
    GET_COMMON(e,event) == getline_event_return ||
    GET_COMMON(e,event) == getpeername_event_return ||
    GET_COMMON(e,event) == fdopen_event_return ||
    GET_COMMON(e,event) == fdatasync_event_return ||
    GET_COMMON(e,event) == link_event_return ||
    GET_COMMON(e,event) == rename_event_return ||
    GET_COMMON(e,event) == bind_event_return ||
    GET_COMMON(e,event) == listen_event_return ||
    GET_COMMON(e,event) == socket_event_return ||
    GET_COMMON(e,event) == connect_event_return ||
    GET_COMMON(e,event) == readdir_event_return ||
    GET_COMMON(e,event) == readdir_r_event_return ||
    GET_COMMON(e,event) == fclose_event_return ||
    GET_COMMON(e,event) == fopen_event_return ||
    GET_COMMON(e,event) == fopen64_event_return ||
    GET_COMMON(e,event) == fgets_event_return ||
    GET_COMMON(e,event) == fflush_event_return ||
    GET_COMMON(e,event) == mkstemp_event_return ||
    GET_COMMON(e,event) == rewind_event_return ||
    GET_COMMON(e,event) == ftell_event_return ||
    GET_COMMON(e,event) == fsync_event_return ||
    GET_COMMON(e,event) == readlink_event_return ||
    GET_COMMON(e,event) == rmdir_event_return ||
    GET_COMMON(e,event) == mkdir_event_return ||
    GET_COMMON(e,event) == fprintf_event_return ||
    GET_COMMON(e,event) == fputs_event_return ||
    GET_COMMON(e,event) == fscanf_event_return ||
    GET_COMMON(e,event) == fwrite_event_return ||
    GET_COMMON(e,event) == putc_event_return ||
    GET_COMMON(e,event) == mmap_event_return ||
    GET_COMMON(e,event) == mmap64_event_return ||
    GET_COMMON(e,event) == munmap_event_return ||
    GET_COMMON(e,event) == mremap_event_return ||
    GET_COMMON(e,event) == opendir_event_return ||
    GET_COMMON(e,event) == closedir_event_return ||
    GET_COMMON(e,event) == user_event_return;
}
#endif

void initLogsForRecordReplay()
{
  // Initialize mmap()'d logs for the current threads.
  unified_log.initGlobalLog(RECORD_LOG_PATH, 10 * MAX_LOG_LENGTH);
  dmtcp::vector<clone_id_t> clone_ids = get_log_list();
  dmtcp::map<clone_id_t, pthread_t>::iterator it;
  for (it = clone_id_to_tid_table.begin(); it != clone_id_to_tid_table.end(); it++) {
    dmtcp::SynchronizationLog *log = clone_id_to_log_table[it->first];
    // Only append to global log if it doesn't already exist:
    log->initForCloneId(it->first, it->first > clone_ids.size());
  }

  if (SYNC_IS_REPLAY) {
    if (!unified_log.isUnified()) {
      JTRACE ( "Merging/Unifying Logs." );
      //SYNC_TIMER_START(merge_logs);
      unified_log.mergeLogs(clone_ids);
      //SYNC_TIMER_STOP(merge_logs);
    }
    getNextLogEntry();
  }
  // Move to end of each log we have so we don't overwrite old entries.
  dmtcp::map<clone_id_t, dmtcp::SynchronizationLog*>::iterator it2;
  for (it2 = clone_id_to_log_table.begin();
       it2 != clone_id_to_log_table.end();
       it2++) {
    it2->second->moveMarkersToEnd();
  }
}


// TODO: Since this is C++, couldn't we use some C++ string processing methods
// to simplify the logic? MAKE SURE TO AVOID MALLOC()
//
// FIXME: On some systems, stack might not be labelled as "[stack]"
//   Instead, use environ[NN] to find an address in the stack and then use
//   /proc/self/maps to find the mmap() location within which this address
//   falls.
LIB_PRIVATE void recordDataStackLocations()
{
  int maps_file = -1;
  char line[200], stack_line[200], code_line[200];
  dmtcp::string progname = jalib::Filesystem::GetProgramName();
  if ((maps_file = _real_open("/proc/self/maps", O_RDONLY, S_IRUSR)) == -1) {
    perror("open");
    exit(1);
  }
  while (dmtcp::Util::readLine(maps_file, line, 199) != 0) {
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
  stack_lower = (char*) strtoul(addr_lower, NULL, 16);
  stack_upper = (char*) strtoul(addr_upper, NULL, 16);
  addr_lower[0] = '0';
  addr_lower[1] = 'x';
  strncpy(addrs, code_line, 25);
  strncpy(addr_lower+2, addrs, 12);
  addr_lower[14] = '\0';
  code_lower = (char*) strtoul(addr_lower, NULL, 16);
#else
  strncpy(addrs, stack_line, 17);
  strncpy(addr_lower+2, addrs, 8);
  strncpy(addr_upper+2, addrs+9, 8);
  addr_lower[10] = '\0';
  addr_upper[10] = '\0';
  //printf("s_stack_lower=%s, s_stack_upper=%s\n", addr_lower, addr_upper);
  stack_lower = (char*) strtoul(addr_lower, NULL, 16);
  stack_upper = (char*) strtoul(addr_upper, NULL, 16);
  addr_lower[0] = '0';
  addr_lower[1] = 'x';
  strncpy(addrs, code_line, 17);
  strncpy(addr_lower+2, addrs, 8);
  addr_lower[10] = '\0';
  code_lower = (char*) strtoul(addr_lower, NULL, 16);
#endif
  // Returns the next address after the end of the heap.
  data_break = (char*) sbrk(0);
  // Also figure out the default stack size for NPTL threads using the
  // architecture-specific limits defined in nptl/sysdeps/ARCH/pthreaddef.h
  struct rlimit rl;
  JASSERT(0 == getrlimit(RLIMIT_STACK, &rl));
#ifdef __x86_64__
  size_t arch_default_stack_size = 32*1024*1024;
#else
  size_t arch_default_stack_size = 2*1024*1024;
#endif
  default_stack_size =
    (rl.rlim_cur == RLIM_INFINITY) ? arch_default_stack_size : rl.rlim_cur;
}

int validAddress(void *addr)
{
  // This code assumes the user's segments .text through .data are contiguous.
  if ((addr >= code_lower && addr < data_break) ||
      (addr >= stack_lower && addr < stack_upper)) {
    return 1;
  } else {
    return 0;
  }
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
  size_t length_fd_bits = FD_SETSIZE/NFDBITS;
  size_t i = 0, j = 0;
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

void prepareNextLogEntry(log_entry_t& e)
{
  if (SYNC_IS_REPLAY) {
    JASSERT (false).Text("Asked to log an event while in replay. "
        "This is probably not intended.");
  }
  JASSERT(GET_COMMON(e, log_id) == (log_id_t)-1) (GET_COMMON(e, log_id));
  log_id_t log_id = get_next_log_id();
  SET_COMMON2(e, log_id, log_id);
}

void addNextLogEntry(log_entry_t& e)
{
  if (GET_COMMON(e, log_id) == 0) {
    log_id_t log_id = get_next_log_id();
    SET_COMMON2(e, log_id, log_id);
  }
  my_log->appendEntry(e);
}

void getNextLogEntry()
{
  // If log is empty, don't do anything
  if (unified_log.numEntries() == 0) {
    return;
  }
  _real_pthread_mutex_lock(&log_index_mutex);
  if (unified_log.getNextEntry(currentLogEntry) == 0) {
    JTRACE ( "Switching back to record." );
    next_log_id = unified_log.numEntries();
    unified_log.setUnified(false);
    set_sync_mode(SYNC_RECORD);
  }
  _real_pthread_mutex_unlock(&log_index_mutex);
}

void logReadData(void *buf, int count)
{
  if (SYNC_IS_REPLAY) {
    JASSERT (false).Text("Asked to log read data while in replay. "
        "This is probably not intended.");
  }
  if (read_data_fd == -1) {
    read_data_fd = open(RECORD_READ_DATA_LOG_PATH,
        O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
  }
  int written = write(read_data_fd, buf, count);
  JASSERT ( written == count );
  read_log_pos += written;
}

ssize_t pwriteAll(int fd, const void *buf, size_t count, off_t offset)
{
  ssize_t retval = 0;
  ssize_t to_write = count;
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

static void setupCommonFields(log_entry_t *e, clone_id_t clone_id, int event)
{
  SET_COMMON_PTR(e, clone_id);
  SET_COMMON_PTR(e, event);
  // Zero out all other fields:
  // FIXME: Shouldn't we replace the memset with a simpler SET_COMMON_PTR()?
  memset(&(GET_COMMON_PTR(e, log_id)), 0, sizeof(GET_COMMON_PTR(e, log_id)));
  memset(&(GET_COMMON_PTR(e, my_errno)), 0, sizeof(GET_COMMON_PTR(e, my_errno)));
  memset(&(GET_COMMON_PTR(e, retval)), 0, sizeof(GET_COMMON_PTR(e, retval)));
}

log_entry_t create_accept_entry(clone_id_t clone_id, int event, int sockfd,
                                sockaddr *addr, socklen_t *addrlen)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, accept, sockfd);
  SET_FIELD(e, accept, addr);
  SET_FIELD(e, accept, addrlen);
  return e;
}

log_entry_t create_accept4_entry(clone_id_t clone_id, int event, int sockfd,
                                sockaddr *addr, socklen_t *addrlen, int flags)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, accept4, sockfd);
  SET_FIELD(e, accept4, addr);
  SET_FIELD(e, accept4, addrlen);
  SET_FIELD(e, accept4, flags);
  return e;
}

log_entry_t create_access_entry(clone_id_t clone_id, int event,
   const char *pathname, int mode)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, access, pathname, (char*)pathname);
  SET_FIELD(e, access, mode);
  return e;
}

log_entry_t create_bind_entry(clone_id_t clone_id, int event,
    int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, bind, sockfd);
  SET_FIELD2(e, bind, addr, (struct sockaddr*)addr);
  SET_FIELD(e, bind, addrlen);
  return e;
}

log_entry_t create_calloc_entry(clone_id_t clone_id, int event, size_t nmemb,
    size_t size)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, calloc, nmemb);
  SET_FIELD(e, calloc, size);
  return e;
}

log_entry_t create_close_entry(clone_id_t clone_id, int event, int fd)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, close, fd);
  return e;
}

log_entry_t create_closedir_entry(clone_id_t clone_id, int event, DIR *dirp)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, closedir, dirp, dirp);
  return e;
}

log_entry_t create_connect_entry(clone_id_t clone_id, int event, int sockfd,
                                 const struct sockaddr *serv_addr, socklen_t addrlen)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, connect, sockfd);
  SET_FIELD2(e, connect, serv_addr, (struct sockaddr*)serv_addr);
  SET_FIELD(e, connect, addrlen);
  return e;
}

log_entry_t create_dup_entry(clone_id_t clone_id, int event, int oldfd)
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

log_entry_t create_fclose_entry(clone_id_t clone_id, int event, FILE *fp)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, fclose, fp, fp);
  return e;
}

log_entry_t create_fcntl_entry(clone_id_t clone_id, int event, int fd, int cmd,
    long arg_3_l, struct flock *arg_3_f)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, fcntl, fd);
  SET_FIELD(e, fcntl, cmd);
  SET_FIELD(e, fcntl, arg_3_l);
  SET_FIELD(e, fcntl, arg_3_f);
  return e;
}

log_entry_t create_fdatasync_entry(clone_id_t clone_id, int event, int fd)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, fdatasync, fd);
  return e;
}

log_entry_t create_fdopen_entry(clone_id_t clone_id, int event, int fd,
    const char *mode)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, fdopen, fd);
  SET_FIELD2(e, fdopen, mode, (char*)mode);
  return e;
}

log_entry_t create_fgets_entry(clone_id_t clone_id, int event, char *s, int size,
    FILE *stream)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, fgets, s, s);
  SET_FIELD(e, fgets, size);
  SET_FIELD2(e, fgets, stream, stream);
  return e;
}

log_entry_t create_fflush_entry(clone_id_t clone_id, int event, FILE *stream)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, fflush, stream, stream);
  return e;
}

log_entry_t create_fopen_entry(clone_id_t clone_id, int event,
    const char *name, const char *mode)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, fopen, name, (char*)name);
  SET_FIELD2(e, fopen, mode, (char*)mode);
  return e;
}

log_entry_t create_fopen64_entry(clone_id_t clone_id, int event,
    const char *name, const char *mode)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, fopen64, name, (char*)name);
  SET_FIELD2(e, fopen64, mode, (char*)mode);
  return e;
}

log_entry_t create_fprintf_entry(clone_id_t clone_id, int event,
    FILE *stream, const char *format, va_list ap)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, fprintf, stream, stream);
  SET_FIELD2(e, fprintf, format, (char*)format);
  return e;
}

log_entry_t create_fscanf_entry(clone_id_t clone_id, int event,
    FILE *stream, const char *format, va_list ap)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, fscanf, stream, stream);
  SET_FIELD2(e, fscanf, format, (char*)format);
  return e;
}

log_entry_t create_fputs_entry(clone_id_t clone_id, int event,
    const char *s, FILE *stream)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, fputs, s, (char*)s);
  SET_FIELD2(e, fputs, stream, stream);
  return e;
}

log_entry_t create_free_entry(clone_id_t clone_id, int event, void *ptr)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, free, ptr);
  return e;
}

log_entry_t create_ftell_entry(clone_id_t clone_id, int event, FILE *stream)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, ftell, stream, stream);
  return e;
}

log_entry_t create_fwrite_entry(clone_id_t clone_id, int event, const void *ptr,
    size_t size, size_t nmemb, FILE *stream)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, fwrite, ptr, (void*)ptr);
  SET_FIELD(e, fwrite, size);
  SET_FIELD(e, fwrite, nmemb);
  SET_FIELD2(e, fwrite, stream, stream);
  return e;
}

log_entry_t create_fsync_entry(clone_id_t clone_id, int event, int fd)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, fsync, fd);
  return e;
}

log_entry_t create_fxstat_entry(clone_id_t clone_id, int event, int vers, int fd,
     struct stat *buf)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, fxstat, vers);
  SET_FIELD(e, fxstat, fd);
  memset(&GET_FIELD(e, fxstat, buf), '\0', sizeof(struct stat));
  return e;
}

log_entry_t create_fxstat64_entry(clone_id_t clone_id, int event, int vers, int fd,
     struct stat64 *buf)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, fxstat64, vers);
  SET_FIELD(e, fxstat64, fd);
  memset(&GET_FIELD(e, fxstat64, buf), '\0', sizeof(struct stat64));
  return e;
}

log_entry_t create_getc_entry(clone_id_t clone_id, int event, FILE *stream)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, getc, stream, stream);
  return e;
}

log_entry_t create_gettimeofday_entry(clone_id_t clone_id, int event,
    struct timeval *tv, struct timezone *tz)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, gettimeofday, tv, tv);
  SET_FIELD2(e, gettimeofday, tz, tz);
  return e;
}

log_entry_t create_fgetc_entry(clone_id_t clone_id, int event, FILE *stream)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, fgetc, stream, stream);
  return e;
}

log_entry_t create_ungetc_entry(clone_id_t clone_id, int event, int c, FILE *stream)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, ungetc, stream, stream);
  SET_FIELD2(e, ungetc, c, c);
  return e;
}

log_entry_t create_getline_entry(clone_id_t clone_id, int event, char **lineptr, size_t *n,
    FILE *stream)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, getline, lineptr, *lineptr);
  SET_FIELD2(e, getline, n, *n);
  SET_FIELD2(e, getline, stream, stream);
  return e;
}

log_entry_t create_getpeername_entry(clone_id_t clone_id, int event, int sockfd,
                                     struct sockaddr *addr, socklen_t *addrlen)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, getpeername, sockfd);
  SET_FIELD2(e, getpeername, addr, addr);
  SET_FIELD(e, getpeername, addrlen);
  return e;
}

log_entry_t create_getsockname_entry(clone_id_t clone_id, int event, int sockfd,
                                     sockaddr *addr, socklen_t *addrlen)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, getsockname, sockfd);
  SET_FIELD(e, getsockname, addr);
  SET_FIELD(e, getsockname, addrlen);
  return e;
}

log_entry_t create_libc_memalign_entry(clone_id_t clone_id, int event, size_t boundary,
    size_t size)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, libc_memalign, boundary);
  SET_FIELD(e, libc_memalign, size);
  return e;
}

log_entry_t create_lseek_entry(clone_id_t clone_id, int event, int fd, off_t offset,
     int whence)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, lseek, fd);
  SET_FIELD(e, lseek, offset);
  SET_FIELD(e, lseek, whence);
  return e;
}

log_entry_t create_link_entry(clone_id_t clone_id, int event, const char *oldpath,
    const char *newpath)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, link, oldpath, (char*)oldpath);
  SET_FIELD2(e, link, newpath, (char*)newpath);
  return e;
}

log_entry_t create_listen_entry(clone_id_t clone_id, int event, int sockfd, int backlog)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, listen, sockfd);
  SET_FIELD(e, listen, backlog);
  return e;
}

log_entry_t create_lxstat_entry(clone_id_t clone_id, int event, int vers,
    const char *path, struct stat *buf)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, lxstat, vers);
  SET_FIELD2(e, lxstat, path, (char*)path);
  memset(&GET_FIELD(e, lxstat, buf), '\0', sizeof(struct stat));
  return e;
}

log_entry_t create_lxstat64_entry(clone_id_t clone_id, int event, int vers,
    const char *path, struct stat64 *buf)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, lxstat64, vers);
  SET_FIELD2(e, lxstat64, path, (char*)path);
  memset(&GET_FIELD(e, lxstat64, buf), '\0', sizeof(struct stat64));
  return e;
}

log_entry_t create_malloc_entry(clone_id_t clone_id, int event, size_t size)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, malloc, size);
  return e;
}

log_entry_t create_mkdir_entry(clone_id_t clone_id, int event, const char *pathname,
    mode_t mode)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, mkdir, pathname, (char*)pathname);
  SET_FIELD2(e, mkdir, mode, mode);
  return e;
}

log_entry_t create_mkstemp_entry(clone_id_t clone_id, int event, char *temp)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, mkstemp, temp, temp);
  return e;
}

log_entry_t create_mmap_entry(clone_id_t clone_id, int event, void *addr,
    size_t length, int prot, int flags, int fd, off_t offset)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, mmap, addr, addr);
  SET_FIELD(e, mmap, length);
  SET_FIELD(e, mmap, prot);
  SET_FIELD(e, mmap, flags);
  SET_FIELD(e, mmap, fd);
  SET_FIELD(e, mmap, offset);
  return e;
}

log_entry_t create_mmap64_entry(clone_id_t clone_id, int event, void *addr,
    size_t length, int prot, int flags, int fd, off64_t offset)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, mmap64, addr, addr);
  SET_FIELD(e, mmap64, length);
  SET_FIELD(e, mmap64, prot);
  SET_FIELD(e, mmap64, flags);
  SET_FIELD(e, mmap64, fd);
  SET_FIELD(e, mmap64, offset);
  return e;
}

log_entry_t create_mremap_entry(clone_id_t clone_id, int event, void *old_address,
    size_t old_size, size_t new_size, int flags, void *new_addr)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, mremap, old_address, old_address);
  SET_FIELD(e, mremap, old_size);
  SET_FIELD(e, mremap, new_size);
  SET_FIELD(e, mremap, flags);
  return e;
}

log_entry_t create_munmap_entry(clone_id_t clone_id, int event, void *addr,
    size_t length)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, munmap, addr, addr);
  SET_FIELD(e, munmap, length);
  return e;
}

log_entry_t create_open_entry(clone_id_t clone_id, int event, const char *path,
   int flags, mode_t open_mode)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, open, path, (char*)path);
  SET_FIELD(e, open, flags);
  SET_FIELD(e, open, open_mode);
  return e;
}

log_entry_t create_open64_entry(clone_id_t clone_id, int event, const char *path,
   int flags, mode_t open_mode)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, open64, path, (char*)path);
  SET_FIELD(e, open64, flags);
  SET_FIELD(e, open64, open_mode);
  return e;
}

log_entry_t create_opendir_entry(clone_id_t clone_id, int event, const char *name)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, opendir, name, (char*)name);
  return e;
}

log_entry_t create_pread_entry(clone_id_t clone_id, int event, int fd, 
    void* buf, size_t count, off_t offset)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pread, fd);
  SET_FIELD(e, pread, buf);
  SET_FIELD(e, pread, count);
  SET_FIELD(e, pread, offset);
  return e;
}

log_entry_t create_putc_entry(clone_id_t clone_id, int event, int c,
    FILE *stream)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, putc, c);
  SET_FIELD2(e, putc, stream, stream);
  return e;
}

log_entry_t create_pwrite_entry(clone_id_t clone_id, int event, int fd, 
    const void* buf, size_t count, off_t offset)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pwrite, fd);
  SET_FIELD2(e, pwrite, buf, (void*)buf);
  SET_FIELD(e, pwrite, count);
  SET_FIELD(e, pwrite, offset);
  return e;
}

log_entry_t create_pthread_cond_broadcast_entry(clone_id_t clone_id, int event,
    pthread_cond_t *cond_var)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, pthread_cond_broadcast, addr, cond_var);
  return e;
}

log_entry_t create_pthread_cond_signal_entry(clone_id_t clone_id, int event,
    pthread_cond_t *cond_var)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, pthread_cond_signal, addr, cond_var);
  return e;
}

log_entry_t create_pthread_cond_wait_entry(clone_id_t clone_id, int event,
    pthread_cond_t *cond_var, pthread_mutex_t *mutex)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, pthread_cond_wait, mutex_addr, mutex);
  SET_FIELD2(e, pthread_cond_wait, cond_addr, cond_var);
  return e;
}

log_entry_t create_pthread_cond_timedwait_entry(clone_id_t clone_id, int event,
    pthread_cond_t *cond_var, pthread_mutex_t *mutex,
    const struct timespec *abstime)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, pthread_cond_timedwait, mutex_addr, mutex);
  SET_FIELD2(e, pthread_cond_timedwait, cond_addr, cond_var);
  SET_FIELD2(e, pthread_cond_timedwait, abstime, (struct timespec*) abstime);
  return e;
}

log_entry_t create_pthread_rwlock_unlock_entry(clone_id_t clone_id, int event,
    pthread_rwlock_t *rwlock)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, pthread_rwlock_unlock, addr, rwlock);
  return e;
}

log_entry_t create_pthread_rwlock_rdlock_entry(clone_id_t clone_id, int event,
    pthread_rwlock_t *rwlock)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, pthread_rwlock_rdlock, addr, rwlock);
  return e;
}

log_entry_t create_pthread_rwlock_wrlock_entry(clone_id_t clone_id, int event,
    pthread_rwlock_t *rwlock)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, pthread_rwlock_wrlock, addr, rwlock);
  return e;
}

log_entry_t create_pthread_create_entry(clone_id_t clone_id, int event,
    pthread_t *thread, const pthread_attr_t *attr, 
    void *(*start_routine)(void*), void *arg)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pthread_create, thread);
  SET_FIELD2(e, pthread_create, attr, (pthread_attr_t*) attr);
  SET_FIELD(e, pthread_create, start_routine);
  SET_FIELD(e, pthread_create, arg);
  return e;
}

log_entry_t create_pthread_detach_entry(clone_id_t clone_id, int event,
    pthread_t thread)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pthread_detach, thread);
  return e;
}

log_entry_t create_pthread_exit_entry(clone_id_t clone_id, int event,
    void *value_ptr)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pthread_exit, value_ptr);
  return e;
}

log_entry_t create_pthread_join_entry(clone_id_t clone_id, int event,
    pthread_t thread, void *value_ptr)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pthread_join, thread);
  SET_FIELD(e, pthread_join, value_ptr);
  return e;
}

log_entry_t create_pthread_kill_entry(clone_id_t clone_id, int event,
    pthread_t thread, int sig)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, pthread_kill, thread);
  SET_FIELD(e, pthread_kill, sig);
  return e;
}

log_entry_t create_pthread_mutex_lock_entry(clone_id_t clone_id, int event, pthread_mutex_t *mutex) {
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, pthread_mutex_lock, addr, mutex);
  return e;
}

log_entry_t create_pthread_mutex_trylock_entry(clone_id_t clone_id, int event, pthread_mutex_t *mutex) {
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, pthread_mutex_trylock, addr, mutex);
  return e;
}

log_entry_t create_pthread_mutex_unlock_entry(clone_id_t clone_id, int event, pthread_mutex_t *mutex) {
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, pthread_mutex_unlock, addr, mutex);
  return e;
}

log_entry_t create_rand_entry(clone_id_t clone_id, int event)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  return e;
}

log_entry_t create_read_entry(clone_id_t clone_id, int event, int readfd,
    void* buf_addr, size_t count)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, read, readfd);
  SET_FIELD(e, read, buf_addr);
  SET_FIELD(e, read, count);
  return e;
}

log_entry_t create_readdir_entry(clone_id_t clone_id, int event, DIR *dirp)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, readdir, dirp, dirp);
  return e;
}

log_entry_t create_readdir_r_entry(clone_id_t clone_id, int event, DIR *dirp,
    struct dirent *entry, struct dirent **result)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, readdir_r, dirp, dirp);
  SET_FIELD2(e, readdir_r, entry, entry);
  SET_FIELD2(e, readdir_r, result, result);
  return e;
}

log_entry_t create_readlink_entry(clone_id_t clone_id, int event,
    const char *path, char *buf, size_t bufsiz)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, readlink, path, (char*)path);
  SET_FIELD(e, readlink, buf);
  SET_FIELD(e, readlink, bufsiz);
  return e;
}

log_entry_t create_realloc_entry(clone_id_t clone_id, int event, 
    void *ptr, size_t size)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, realloc, ptr);
  SET_FIELD(e, realloc, size);
  return e;
}

log_entry_t create_rename_entry(clone_id_t clone_id, int event, const char *oldpath,
    const char *newpath)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, rename, oldpath, (char*)oldpath);
  SET_FIELD2(e, rename, newpath, (char*)newpath);
  return e;
}

log_entry_t create_rewind_entry(clone_id_t clone_id, int event, FILE *stream)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, rewind, stream, stream);
  return e;
}

log_entry_t create_rmdir_entry(clone_id_t clone_id, int event, const char *pathname)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, rmdir, pathname, (char*)pathname);
  return e;
}

log_entry_t create_select_entry(clone_id_t clone_id, int event, int nfds,
    fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
    struct timeval *timeout)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, select, nfds);
  // We have to do something special for 'readfds' and 'writefds' since we
  // do a deep copy.
  copyFdSet(readfds, &GET_FIELD(e, select, readfds));
  copyFdSet(writefds, &GET_FIELD(e, select, writefds));
  SET_FIELD2(e, select, exceptfds, exceptfds);
  SET_FIELD2(e, select, timeout, timeout);
  return e;
}

log_entry_t create_setsockopt_entry(clone_id_t clone_id, int event, int sockfd,
    int level, int optname, const void *optval, socklen_t optlen) {
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, setsockopt, sockfd);
  SET_FIELD(e, setsockopt, level);
  SET_FIELD(e, setsockopt, optname);
  SET_FIELD2(e, setsockopt, optval, (void*) optval);
  SET_FIELD(e, setsockopt, optlen);
  return e;
}

log_entry_t create_signal_handler_entry(clone_id_t clone_id, int event, int sig)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, signal_handler, sig);
  return e;
}

log_entry_t create_sigwait_entry(clone_id_t clone_id, int event, const sigset_t *set,
    int *sigwait_sig)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, sigwait, set, (sigset_t*)set);
  SET_FIELD(e, sigwait, sigwait_sig);
  return e;
}

log_entry_t create_srand_entry(clone_id_t clone_id, int event, unsigned int seed)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, srand, seed);
  return e;
}

log_entry_t create_socket_entry(clone_id_t clone_id, int event, int domain, int type,
    int protocol)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, socket, domain);
  SET_FIELD(e, socket, type);
  SET_FIELD(e, socket, protocol);
  return e;
}

log_entry_t create_xstat_entry(clone_id_t clone_id, int event, int vers,
    const char *path, struct stat *buf)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, xstat, vers);
  SET_FIELD2(e, xstat, path, (char*)path);
  memset(&GET_FIELD(e, xstat, buf), '\0', sizeof(struct stat));
  return e;
}

log_entry_t create_xstat64_entry(clone_id_t clone_id, int event, int vers,
    const char *path, struct stat64 *buf)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, xstat64, vers);
  SET_FIELD2(e, xstat64, path, (char*)path);
  memset(&GET_FIELD(e, xstat64, buf), '\0', sizeof(struct stat64));
  return e;
}

log_entry_t create_time_entry(clone_id_t clone_id, int event, time_t *tloc)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, time, tloc);
  return e;
}

log_entry_t create_unlink_entry(clone_id_t clone_id, int event,
     const char *pathname)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD2(e, unlink, pathname, (char*)pathname);
  return e;
}

log_entry_t create_user_entry(clone_id_t clone_id, int event)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  return e;
}

log_entry_t create_write_entry(clone_id_t clone_id, int event, int writefd,
    const void* buf_addr, size_t count)
{
  log_entry_t e = EMPTY_LOG_ENTRY;
  setupCommonFields(&e, clone_id, event);
  SET_FIELD(e, write, writefd);
  SET_FIELD2(e, write, buf_addr, (void*)buf_addr);
  SET_FIELD(e, write, count);
  return e;
}

static TURN_CHECK_P(base_turn_check)
{
  // Predicate function for a basic check -- event # and clone id.
  return GET_COMMON_PTR(e1,clone_id) == GET_COMMON_PTR(e2,clone_id) &&
         GET_COMMON_PTR(e1,event) == GET_COMMON_PTR(e2,event);
}

TURN_CHECK_P(pthread_mutex_lock_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, pthread_mutex_lock, addr) ==
      GET_FIELD_PTR(e2, pthread_mutex_lock, addr);
}

TURN_CHECK_P(pthread_mutex_trylock_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, pthread_mutex_trylock, addr) ==
      GET_FIELD_PTR(e2, pthread_mutex_trylock, addr);
}

TURN_CHECK_P(pthread_mutex_unlock_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, pthread_mutex_unlock, addr) ==
      GET_FIELD_PTR(e2, pthread_mutex_unlock, addr);
}

TURN_CHECK_P(pthread_cond_signal_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, pthread_cond_signal, addr) ==
      GET_FIELD_PTR(e2, pthread_cond_signal, addr);
}

TURN_CHECK_P(pthread_cond_broadcast_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, pthread_cond_broadcast, addr) ==
      GET_FIELD_PTR(e2, pthread_cond_broadcast, addr);
}

TURN_CHECK_P(pthread_cond_wait_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, pthread_cond_wait, mutex_addr) ==
      GET_FIELD_PTR(e2, pthread_cond_wait, mutex_addr) &&
    GET_FIELD_PTR(e1, pthread_cond_wait, cond_addr) ==
      GET_FIELD_PTR(e2, pthread_cond_wait, cond_addr);
}

TURN_CHECK_P(pthread_cond_timedwait_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, pthread_cond_timedwait, mutex_addr) ==
      GET_FIELD_PTR(e2, pthread_cond_timedwait, mutex_addr) &&
    GET_FIELD_PTR(e1, pthread_cond_timedwait, cond_addr) ==
      GET_FIELD_PTR(e2, pthread_cond_timedwait, cond_addr) &&
    GET_FIELD_PTR(e1, pthread_cond_timedwait, abstime) ==
      GET_FIELD_PTR(e2, pthread_cond_timedwait, abstime);
}

TURN_CHECK_P(pthread_rwlock_unlock_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, pthread_rwlock_unlock, addr) ==
      GET_FIELD_PTR(e2, pthread_rwlock_unlock, addr);
}

TURN_CHECK_P(pthread_rwlock_rdlock_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, pthread_rwlock_rdlock, addr) ==
      GET_FIELD_PTR(e2, pthread_rwlock_rdlock, addr);
}

TURN_CHECK_P(pthread_rwlock_wrlock_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, pthread_rwlock_wrlock, addr) ==
      GET_FIELD_PTR(e2, pthread_rwlock_wrlock, addr);
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

TURN_CHECK_P(user_turn_check)
{
  return base_turn_check(e1, e2);
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

TURN_CHECK_P(closedir_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, closedir, dirp) ==
      GET_FIELD_PTR(e2, closedir, dirp);
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
    GET_FIELD_PTR(e1, accept, addr) ==
      GET_FIELD_PTR(e2, accept, addr) &&
    GET_FIELD_PTR(e1, accept, addrlen) ==
      GET_FIELD_PTR(e2, accept, addrlen);
}

TURN_CHECK_P(accept4_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, accept4, addr) ==
      GET_FIELD_PTR(e2, accept4, addr) &&
    GET_FIELD_PTR(e1, accept4, addrlen) ==
      GET_FIELD_PTR(e2, accept4, addrlen) &&
    GET_FIELD_PTR(e1, accept4, flags) ==
      GET_FIELD_PTR(e2, accept4, flags);
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
    GET_FIELD_PTR(e1, bind, addr) ==
      GET_FIELD_PTR(e2, bind, addr) &&
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
  /*GET_FIELD_PTR(e1, getpeername, addr) ==
      GET_FIELD_PTR(e2, getpeername, addr)*/
}

TURN_CHECK_P(getsockname_turn_check)
{
  return base_turn_check(e1, e2) &&
    /*GET_FIELD_PTR(e1, getsockname, sockfd) ==
      GET_FIELD_PTR(e2, getsockname, sockfd) &&*/
    GET_FIELD_PTR(e1, getsockname, addrlen) ==
      GET_FIELD_PTR(e2, getsockname, addrlen) &&
    GET_FIELD_PTR(e1, getsockname, addr) ==
      GET_FIELD_PTR(e2, getsockname, addr);
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

TURN_CHECK_P(fflush_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, fflush, stream) ==
      GET_FIELD_PTR(e2, fflush, stream);
}

TURN_CHECK_P(getc_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, getc, stream) ==
      GET_FIELD_PTR(e2, getc, stream);
}

TURN_CHECK_P(gettimeofday_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, gettimeofday, tv) ==
      GET_FIELD_PTR(e2, gettimeofday, tv) &&
    GET_FIELD_PTR(e1, gettimeofday, tz) ==
      GET_FIELD_PTR(e2, gettimeofday, tz);
}

TURN_CHECK_P(fgetc_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, fgetc, stream) ==
      GET_FIELD_PTR(e2, fgetc, stream);
}

TURN_CHECK_P(ungetc_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, ungetc, stream) ==
      GET_FIELD_PTR(e2, ungetc, stream) &&
    GET_FIELD_PTR(e1, ungetc, c) ==
      GET_FIELD_PTR(e2, ungetc, c);
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

TURN_CHECK_P(fopen64_turn_check)
{
  return base_turn_check(e1,e2) &&
    GET_FIELD_PTR(e1, fopen64, name) ==
      GET_FIELD_PTR(e2, fopen64, name) &&
    GET_FIELD_PTR(e1, fopen64, mode) ==
      GET_FIELD_PTR(e2, fopen64, mode);
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

TURN_CHECK_P(mmap_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, mmap, addr) ==
      GET_FIELD_PTR(e2, mmap, addr) &&
    GET_FIELD_PTR(e1, mmap, length) ==
      GET_FIELD_PTR(e2, mmap, length) &&
    GET_FIELD_PTR(e1, mmap, prot) ==
      GET_FIELD_PTR(e2, mmap, prot) &&
    GET_FIELD_PTR(e1, mmap, flags) ==
      GET_FIELD_PTR(e2, mmap, flags) &&
    GET_FIELD_PTR(e1, mmap, fd) ==
      GET_FIELD_PTR(e2, mmap, fd) &&
    GET_FIELD_PTR(e1, mmap, offset) ==
      GET_FIELD_PTR(e2, mmap, offset);
}

TURN_CHECK_P(mmap64_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, mmap64, addr) ==
      GET_FIELD_PTR(e2, mmap64, addr) &&
    GET_FIELD_PTR(e1, mmap64, length) ==
      GET_FIELD_PTR(e2, mmap64, length) &&
    GET_FIELD_PTR(e1, mmap64, prot) ==
      GET_FIELD_PTR(e2, mmap64, prot) &&
    GET_FIELD_PTR(e1, mmap64, flags) ==
      GET_FIELD_PTR(e2, mmap64, flags) &&
    GET_FIELD_PTR(e1, mmap64, fd) ==
      GET_FIELD_PTR(e2, mmap64, fd) &&
    GET_FIELD_PTR(e1, mmap64, offset) ==
      GET_FIELD_PTR(e2, mmap64, offset);
}

TURN_CHECK_P(mremap_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, mremap, old_address) ==
      GET_FIELD_PTR(e2, mremap, old_address) &&
    GET_FIELD_PTR(e1, mremap, old_size) ==
      GET_FIELD_PTR(e2, mremap, old_size) &&
    GET_FIELD_PTR(e1, mremap, new_size) ==
      GET_FIELD_PTR(e2, mremap, new_size) &&
    GET_FIELD_PTR(e1, mremap, flags) ==
      GET_FIELD_PTR(e2, mremap, flags);
}

TURN_CHECK_P(munmap_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, munmap, addr) ==
      GET_FIELD_PTR(e2, munmap, addr) &&
    GET_FIELD_PTR(e1, munmap, length) ==
      GET_FIELD_PTR(e2, munmap, length);
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

TURN_CHECK_P(open64_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, open64, path) ==
      GET_FIELD_PTR(e2, open64, path) &&
    GET_FIELD_PTR(e1, open64, flags) ==
      GET_FIELD_PTR(e2, open64, flags) &&
    GET_FIELD_PTR(e1, open64, open_mode) ==
      GET_FIELD_PTR(e2, open64, open_mode);
}

TURN_CHECK_P(opendir_turn_check)
{
  return base_turn_check(e1, e2) &&
    GET_FIELD_PTR(e1, opendir, name) ==
      GET_FIELD_PTR(e2, opendir, name);
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

/* Populates the given array with any optional events associated with
   the given event. */
static void get_optional_events(log_entry_t *e, int *opt_events)
{
  event_code_t event_num = (event_code_t) GET_COMMON_PTR(e, event);
  if (event_num == fscanf_event ||
      event_num == fgets_event ||
      event_num == getc_event ||
      //event_num == fgetc_event ||
      event_num == fprintf_event ||
      event_num == accept_event ||
      event_num == accept4_event ||
      event_num == fdopen_event) {
    opt_events[0] = mmap_event;
  } else if (event_num == setsockopt_event) {
    opt_events[0] = malloc_event;
    opt_events[1] = free_event;
    opt_events[2] = mmap_event;
  } else if (event_num == fclose_event) {
    opt_events[0] = free_event;
  } else if (event_num == opendir_event) {
    opt_events[0] = malloc_event;
  } else if (event_num == closedir_event) {
    opt_events[0] = free_event;
  }
  // TODO: Some error checking that we do not accidently assign above
  // the index MAX_OPTIONAL_EVENTS
}

/* Returns 1 if the given event has at least one optional event, 0 otherwise. */
static int has_optional_event(log_entry_t *e)
{
  int opt_evts[MAX_OPTIONAL_EVENTS] = {0};
  get_optional_events(e, opt_evts);
  return opt_evts[0] != 0;
}

/* Given the event number of an optional event, executes the action to fulfill
   that event. */
static void execute_optional_event(int opt_event_num)
{
  _real_pthread_mutex_lock(&log_index_mutex);
  if (opt_event_num == mmap_event) {
    size_t length = GET_FIELD(currentLogEntry, mmap, length);
    int prot      = GET_FIELD(currentLogEntry, mmap, prot);
    int flags     = GET_FIELD(currentLogEntry, mmap, flags);
    int fd        = GET_FIELD(currentLogEntry, mmap, fd);
    off_t offset  = GET_FIELD(currentLogEntry, mmap, offset);
    _real_pthread_mutex_unlock(&log_index_mutex);
    JASSERT(mmap(NULL, length, prot, flags, fd, offset) != MAP_FAILED);
  } else if (opt_event_num == malloc_event) {
    size_t size = GET_FIELD(currentLogEntry, malloc, size);
    _real_pthread_mutex_unlock(&log_index_mutex);
    JASSERT(malloc(size) != NULL);
  } else if (opt_event_num == free_event) {
    /* The fact that this works depends on memory-accurate replay. */
    void *ptr = (void *)GET_FIELD(currentLogEntry, free, ptr);
    _real_pthread_mutex_unlock(&log_index_mutex);
    free(ptr);
  } else {
    JASSERT (false)(opt_event_num).Text("No action known for optional event.");
  }
}

/* Returns 1 if the given array of ints contains the given int, 0 otherwise. */
static int opt_events_contains(const int opt_events[MAX_OPTIONAL_EVENTS],
    int evt)
{
  int i = 0;
  for (i = 0; i < MAX_OPTIONAL_EVENTS; i++) {
    if (opt_events[i] == evt) return 1;
  }
  return 0;
}

/* Like waitForTurn(), but also handles events with "optional" events. For
   example, fscanf() can call mmap() sometimes. This method will execute that
   optional event if it occurs before the regular fscanf_event. If it never
   occurs, this function will also return when the regular fscanf_event is
   encountered.
   
   This function is useful for fscanf and others since they are NOT called on
   replay. If we don't call _real_fscanf, for example, libc is never able to
   call mmap. So we must do it manually. */
static void waitForTurnWithOptional(log_entry_t *my_entry, turn_pred_t pred)
{
  int opt_events[MAX_OPTIONAL_EVENTS] = {0};
  get_optional_events(my_entry, opt_events);
  while (1) {
    if ((*pred)(&currentLogEntry, my_entry))
      break;
    /* For the optional event, we can only check the clone_id and the event
       number, since we don't know any more information. */
    if (GET_COMMON(currentLogEntry, clone_id) == my_clone_id &&
        GET_COMMON(currentLogEntry, isOptional) == 1) {
      JASSERT(opt_events_contains(opt_events, GET_COMMON(currentLogEntry, event)));
      execute_optional_event(GET_COMMON(currentLogEntry, event));
    }
    memfence();
    usleep(15);
  }
}

void waitForTurn(log_entry_t my_entry, turn_pred_t pred)
{
  memfence();
  if (has_optional_event(&my_entry)) {
    waitForTurnWithOptional(&my_entry, pred);
  } else {
    while (1) {
      if ((*pred)(&currentLogEntry, &my_entry))
        break;
      
      memfence();
      usleep(15);
    }
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

/* A do-nothing event that can be called from user-space via
   dmtcp_userSynchronizedEvent() in the dmtcp aware api library. This
   provides extra coarse-grained synchronization for the user program
   where a mutex is not wanted. */
void userSynchronizedEvent()
{
  log_entry_t my_entry = create_user_entry(my_clone_id, user_event);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, user_turn_check);
    getNextLogEntry();
  } else if (SYNC_IS_RECORD) {
    addNextLogEntry(my_entry);
  }
}

void userSynchronizedEventBegin()
{
  log_entry_t my_entry = create_user_entry(my_clone_id, user_event);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, user_turn_check);
    getNextLogEntry();
  } else if (SYNC_IS_RECORD) {
    addNextLogEntry(my_entry);
  }
}

void userSynchronizedEventEnd()
{
  JASSERT(false);
  return;
}
#endif
