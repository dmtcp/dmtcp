/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <stdlib.h>
#include "dmtcp.h"
#include "dmtcpworker.h"
#include "coordinatorapi.h"
#include "syscallwrappers.h"
#include "processinfo.h"
#include "shareddata.h"
#include "threadsync.h"
#include "util.h"

#undef dmtcp_is_enabled
#undef dmtcp_checkpoint
#undef dmtcp_disable_ckpt
#undef dmtcp_enable_ckpt
#undef dmtcp_get_coordinator_status
#undef dmtcp_get_local_status
#undef dmtcp_get_uniquepid_str
#undef dmtcp_get_ckpt_filename
#undef dmtcp_set_coord_ckpt_dir
#undef dmtcp_get_coord_ckpt_dir
#undef dmtcp_set_ckpt_dir
#undef dmtcp_get_ckpt_dir

using namespace dmtcp;

//I wish we could use pthreads for the trickery in this file, but much of our
//code is executed before the thread we want to wake is restored.  Thus we do
//it the bad way.
#if defined(__i386__) || defined(__x86_64__)
static inline void memfence(){  asm volatile ("mfence" ::: "memory"); }
#elif defined(__arm__)
static inline void memfence(){  asm volatile ("dmb" ::: "memory"); }
#elif defined(__aarch64__)
# include "membarrier.h"
static inline void memfence(){  RMB; WMB; }
#else
# define memfence() __sync_synchronize()
#endif

EXTERNC int dmtcp_is_enabled() { return 1; }

EXTERNC int dmtcp_checkpoint()
{
  size_t oldNumRestarts, oldNumCheckpoints;

  while (1) {
    WRAPPER_EXECUTION_GET_EXCL_LOCK();

    CoordinatorAPI coordinatorAPI;
    int status;
    coordinatorAPI.connectAndSendUserCommand('c', &status);

    if (status != CoordCmdStatus::ERROR_NOT_RUNNING_STATE) {
      oldNumRestarts    = ProcessInfo::instance().numRestarts();
      oldNumCheckpoints = ProcessInfo::instance().numCheckpoints();
      WRAPPER_EXECUTION_RELEASE_EXCL_LOCK();
      break;
    }

    WRAPPER_EXECUTION_RELEASE_EXCL_LOCK();

    struct timespec t = {0, 100 * 1000 * 1000}; // 100ms.
    nanosleep(&t, NULL);
  }

  while (oldNumRestarts == ProcessInfo::instance().numRestarts() &&
         oldNumCheckpoints == ProcessInfo::instance().numCheckpoints()) {
    //nanosleep should get interrupted by checkpointing with an EINTR error
    //though there is a race to get to nanosleep() before the checkpoint
    struct timespec t = {1,0};
    nanosleep(&t, NULL);
    memfence();  //make sure the loop condition doesn't get optimized
  }

  return (ProcessInfo::instance().numRestarts() == oldNumRestarts
          ? DMTCP_AFTER_CHECKPOINT : DMTCP_AFTER_RESTART);
}

EXTERNC int dmtcp_get_coordinator_status(int *numPeers, int *isRunning)
{
  WRAPPER_EXECUTION_GET_EXCL_LOCK();

  int status;
  CoordinatorAPI coordinatorAPI;
  coordinatorAPI.connectAndSendUserCommand('s', &status, numPeers, isRunning);

  WRAPPER_EXECUTION_RELEASE_EXCL_LOCK();
  return DMTCP_IS_PRESENT;;
}

EXTERNC int dmtcp_get_local_status(int *nCheckpoints, int *nRestarts)
{
  *nCheckpoints = ProcessInfo::instance().numCheckpoints();
  *nRestarts = ProcessInfo::instance().numRestarts();
  return DMTCP_IS_PRESENT;;
}

EXTERNC int dmtcp_disable_ckpt()
{
  ThreadSync::delayCheckpointsLock();
  return 1;
}

EXTERNC int dmtcp_enable_ckpt()
{
  ThreadSync::delayCheckpointsUnlock();
  return 1;
}

EXTERNC int dmtcp_get_ckpt_signal(void)
{
  const int ckpt_signal = DmtcpWorker::determineCkptSignal();
  return ckpt_signal;
}

EXTERNC const char* dmtcp_get_tmpdir(void)
{
  static char tmpdir[PATH_MAX];
  JASSERT(SharedData::getTmpDir(tmpdir, sizeof(tmpdir)) != NULL);
  return tmpdir;
}

//EXTERNC void dmtcp_set_tmpdir(const char* dir)
//{
//  if (dir != NULL) {
//    UniquePid::setTmpDir(dir);
//  }
//}

EXTERNC const char* dmtcp_get_ckpt_dir()
{
  static string tmpdir;
  tmpdir = ProcessInfo::instance().getCkptDir();
  return tmpdir.c_str();
}

EXTERNC int dmtcp_set_ckpt_dir(const char* dir)
{
  if (dir != NULL) {
    ProcessInfo::instance().setCkptDir(dir);
  }
  return DMTCP_IS_PRESENT;
}

EXTERNC const char* dmtcp_get_coord_ckpt_dir(void)
{
  static string dir;
  dir = CoordinatorAPI::instance().getCoordCkptDir();
  return dir.c_str();
}

EXTERNC int dmtcp_set_coord_ckpt_dir(const char* dir)
{
  if (dir != NULL) {
    CoordinatorAPI::instance().updateCoordCkptDir(dir);
  }
  return DMTCP_IS_PRESENT;
}

EXTERNC void dmtcp_set_ckpt_file(const char *filename)
{
  ProcessInfo::instance().setCkptFilename(filename);
}

EXTERNC const char* dmtcp_get_ckpt_filename(void)
{
  static string filename;
  filename = ProcessInfo::instance().getCkptFilename();
  return filename.c_str();
}

EXTERNC const char* dmtcp_get_ckpt_files_subdir(void)
{
  static string tmpdir;
  tmpdir = ProcessInfo::instance().getCkptFilesSubDir();
  return tmpdir.c_str();
}

EXTERNC int dmtcp_should_ckpt_open_files(void)
{
  return getenv(ENV_VAR_CKPT_OPEN_FILES) != NULL;
}

EXTERNC int dmtcp_allow_overwrite_with_ckpted_files(void)
{
  return getenv(ENV_VAR_ALLOW_OVERWRITE_WITH_CKPTED_FILES) != NULL;
}

EXTERNC const char* dmtcp_get_executable_path(void)
{
  return ProcessInfo::instance().procSelfExe().c_str();
}

EXTERNC const char* dmtcp_get_uniquepid_str(void)
{
  static string *uniquepid_str = NULL;
  uniquepid_str =
    new string(UniquePid::ThisProcess(true).toString());
  return uniquepid_str->c_str();
}

EXTERNC DmtcpUniqueProcessId dmtcp_get_uniquepid(void)
{
  return UniquePid::ThisProcess().upid();
}

EXTERNC DmtcpUniqueProcessId dmtcp_get_computation_id(void)
{
  return SharedData::getCompId();
}

EXTERNC const char* dmtcp_get_computation_id_str(void)
{
  static string *compid_str = NULL;
  if (compid_str == NULL) {
    UniquePid compId = SharedData::getCompId();
    compid_str = new string(compId.toString());
  }
  return compid_str->c_str();
}

EXTERNC DmtcpUniqueProcessId dmtcp_get_coord_id(void)
{
  return SharedData::getCoordId();
}

EXTERNC int dmtcp_unique_pids_equal(DmtcpUniqueProcessId a,
                                    DmtcpUniqueProcessId b)
{
  return a._hostid == b._hostid &&
         a._pid == b._pid &&
         a._time == b._time &&
         a._computation_generation == b._computation_generation;
}

EXTERNC uint64_t dmtcp_get_coordinator_timestamp(void)
{
  return SharedData::getCoordTimeStamp();
}

EXTERNC uint32_t dmtcp_get_generation(void)
{
  return ProcessInfo::instance().get_generation();
}

EXTERNC int checkpoint_is_pending(void)
{
  return SharedData::getCompId()._computation_generation >
           ProcessInfo::instance().get_generation();
}

EXTERNC int dmtcp_is_running_state(void)
{
  return WorkerState::currentState() == WorkerState::RUNNING;
}

EXTERNC int dmtcp_is_protected_fd(int fd)
{
  return DMTCP_IS_PROTECTED_FD(fd);
}

EXTERNC int dmtcp_protected_environ_fd(void)
{
  return PROTECTED_ENVIRON_FD;
}

EXTERNC void dmtcp_close_protected_fd(int fd)
{
  JASSERT(DMTCP_IS_PROTECTED_FD(fd));
  _real_close(fd);
}

// dmtcp_get_restart_env() will take an environment variable, name,
//   from the current environment at the time dmtcp_restart,
//   and return its value.  This is useful since by default,
//   an application would see only the restored memory from checkpoint
//   time, which includes only environment variable values that
//   existed at the time of checkpoint.
// The plugin modify-env uses this function intensively.
// EXTERNC int dmtcp_get_restart_env(char *name, char *value, int maxvaluelen);
// USAGE:
//   char value[MAXSIZE];
//   dmtcp_get_restart_env(name, value, MAXSIZE);
//  Returns 0 on success, -1 if name not found; -2 if value > MAXSIZE
// NOTE: This implementation assumes that a "name=value" string will be
//   no more than MAXSIZE bytes.

EXTERNC int
dmtcp_get_restart_env(const char *name,   // IN
                      char *value,        // OUT
                      size_t maxvaluelen)
{
  int env_fd = dup(dmtcp_protected_environ_fd());
  JASSERT(env_fd != -1)(env_fd)(dmtcp_protected_environ_fd());
  lseek(env_fd, 0, SEEK_SET);

#define SUCCESS 0
#define NOTFOUND -1
#define TOOLONG -2
#define DMTCP_BUF_TOO_SMALL -3
#define INTERNAL_ERROR -4
#define NULL_PTR -5
#define MAXSIZE 12288*10

  int rc = NOTFOUND; // Default is -1: name not found

  char env_buf[MAXSIZE] = {0}; // All "name=val" strings must be shorter than this.

  if (name == NULL || value == NULL) {
    close(env_fd);
    return NULL_PTR;
  }

  int namelen = strlen(name);
  *value = '\0'; // Default is null string
  char *pos = NULL;

  while (rc == NOTFOUND) {
   memset(env_buf, 0, MAXSIZE);
   // read a flattened name-value pairs list
   int count = Util::readLine(env_fd, env_buf, MAXSIZE);
   if (count == 0) {
     break;
   } else if (count == -1) {
     rc = INTERNAL_ERROR;
   } else if (count == -2) {
     rc = DMTCP_BUF_TOO_SMALL;
   } else {
     char *start_ptr = env_buf;
     // iterate over the flattened list of name-value pairs
     while (start_ptr - env_buf < (int) sizeof(env_buf)) {
       pos = NULL;
       if (strncmp(start_ptr, name, namelen) == 0) {
         if ((pos = strchr(start_ptr, '='))) {
           strncpy(value, pos + 1, maxvaluelen);
           if (strlen(pos+1) >= maxvaluelen) {
             rc = TOOLONG; // value does not fit in the user-provided value buffer
             break;
           }
         }
         rc = SUCCESS;
         break;
       }
       // skip over a name-value pair
       start_ptr += strlen(start_ptr) + 1;
     }
   }
  }

  close(env_fd);
  JWARNING (rc != DMTCP_BUF_TOO_SMALL)
    (name) (sizeof(env_buf)) .Text("Resize env_buf[]");
  return rc;
}

EXTERNC int dmtcp_get_readlog_fd(void)
{
  return PROTECTED_READLOG_FD;
}

EXTERNC int dmtcp_get_ptrace_fd(void)
{
  return PROTECTED_PTRACE_FD;
}

LIB_PRIVATE int32_t dmtcp_dlsym_offset = -1;
typedef void* (*dlsym_fnptr_t) (void *handle, const char *symbol);
EXTERNC void *dmtcp_get_libc_dlsym_addr(void)
{
  static dlsym_fnptr_t _libc_dlsym_fnptr = NULL;
#ifndef CONFIG_M32
  const char *evar = ENV_VAR_DLSYM_OFFSET;
#else
  const char *evar = ENV_VAR_DLSYM_OFFSET_M32;
#endif

  if (_libc_dlsym_fnptr == NULL) {
    if (getenv(evar) == NULL) {
      fprintf(stderr,
              "%s:%d DMTCP Internal Error: Env var DMTCP_DLSYM_OFFSET not set.\n"
              "      Aborting.\n\n",
              __FILE__, __LINE__);
      abort();
    }

    dmtcp_dlsym_offset = (int32_t) strtol(getenv(evar), NULL, 10);

    _libc_dlsym_fnptr = (dlsym_fnptr_t)((char *)&LIBDL_BASE_FUNC +
                                        dmtcp_dlsym_offset);
  }

  return (void*) _libc_dlsym_fnptr;
}

EXTERNC void dmtcp_block_ckpt_signal(void)
{
  static sigset_t signals_set;
  static bool initialized = false;
  if (!initialized) {
    sigemptyset (&signals_set);
    sigaddset (&signals_set, dmtcp_get_ckpt_signal());
    initialized = true;
  }

  JASSERT(_real_pthread_sigmask (SIG_BLOCK, &signals_set, NULL) == 0);
}

EXTERNC void dmtcp_unblock_ckpt_signal(void)
{
  static sigset_t signals_set;
  static bool initialized = false;
  if (!initialized) {
    sigemptyset (&signals_set);
    sigaddset (&signals_set, dmtcp_get_ckpt_signal());
    initialized = true;
  }

  JASSERT(_real_pthread_sigmask (SIG_UNBLOCK, &signals_set, NULL) == 0);
}

EXTERNC int dmtcp_send_key_val_pair_to_coordinator(const char *id,
                                                   const void *key,
                                                   uint32_t key_len,
                                                   const void *val,
                                                   uint32_t val_len)
{
  return CoordinatorAPI::instance().sendKeyValPairToCoordinator(id, key, key_len,
                                                                val, val_len);
}

// On input, val points to a buffer in user memory and *val_len is the maximum
//   size of that buffer (the memory allocated by user).
// On output, we copy data to val, and set *val_len to the actual buffer size
//   (to the size of the data that we copied to the user buffer).
EXTERNC int dmtcp_send_query_to_coordinator(const char *id,
                                            const void *key, uint32_t key_len,
                                            void *val, uint32_t *val_len)
{
  return CoordinatorAPI::instance().sendQueryToCoordinator(id, key, key_len,
                                                           val, val_len);
}

EXTERNC void dmtcp_get_local_ip_addr(struct in_addr *in)
{
  SharedData::getLocalIPAddr(in);
}

EXTERNC int dmtcp_no_coordinator(void)
{
  return CoordinatorAPI::noCoordinator();
}
