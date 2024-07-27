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
#include <dlfcn.h>

#include "coordinatorapi.h"
#include "dmtcp.h"
#include "dmtcpworker.h"
#include "processinfo.h"
#include "shareddata.h"
#include "syscallwrappers.h"
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
#undef dmtcp_set_ckpt_dir
#undef dmtcp_get_ckpt_dir

using namespace dmtcp;

// I wish we could use pthreads for the trickery in this file, but much of our
// code is executed before the thread we want to wake is restored.  Thus we do
// it the bad way.
#if defined(__i386__) || defined(__x86_64__)
static inline void
memfence() {  asm volatile ("mfence" ::: "memory"); }

#elif defined(__arm__)
static inline void
memfence() {  asm volatile ("dmb" ::: "memory"); }

#elif defined(__aarch64__)
# include "membarrier.h"
static inline void
memfence() {  RMB; WMB; }

#else // if defined(__i386__) || defined(__x86_64__)
# define memfence() __sync_synchronize()
#endif // if defined(__i386__) || defined(__x86_64__)

EXTERNC int
dmtcp_is_enabled() { return 1; }

EXTERNC int
dmtcp_checkpoint()
{
  size_t oldNumRestarts, oldNumCheckpoints;

  while (1) {
    {
      WrapperLockExcl wrapperLockExcl;

      int status;
      CoordinatorAPI::connectAndSendUserCommand('c', &status);

      if (status != CoordCmdStatus::ERROR_NOT_RUNNING_STATE) {
        oldNumRestarts = ProcessInfo::instance().numRestarts();
        oldNumCheckpoints = ProcessInfo::instance().numCheckpoints();
        break;
      }
    }

    struct timespec t = { 0, 100 * 1000 * 1000 }; // 100ms.
    nanosleep(&t, NULL);
  }

  while (oldNumRestarts == ProcessInfo::instance().numRestarts() &&
         oldNumCheckpoints == ProcessInfo::instance().numCheckpoints()) {
    // nanosleep should get interrupted by checkpointing with an EINTR error
    // though there is a race to get to nanosleep() before the checkpoint
    struct timespec t = { 1, 0 };
    nanosleep(&t, NULL);
    memfence();  // make sure the loop condition doesn't get optimized
  }

  return ProcessInfo::instance().numRestarts() == oldNumRestarts
         ? DMTCP_AFTER_CHECKPOINT : DMTCP_AFTER_RESTART;
}

EXTERNC int
dmtcp_get_coordinator_status(int *numPeers, int *isRunning)
{
  WrapperLockExcl wrapperLockExcl;

  int status;
  CoordinatorAPI::connectAndSendUserCommand('s', &status, numPeers, isRunning);

  return DMTCP_IS_PRESENT;
}

EXTERNC int
dmtcp_get_local_status(int *nCheckpoints, int *nRestarts)
{
  *nCheckpoints = ProcessInfo::instance().numCheckpoints();
  *nRestarts = ProcessInfo::instance().numRestarts();
  return DMTCP_IS_PRESENT;
}

EXTERNC int
dmtcp_disable_ckpt()
{
  ThreadSync::wrapperExecutionLockLock();
  return 1;
}

EXTERNC int
dmtcp_enable_ckpt()
{
  ThreadSync::wrapperExecutionLockUnlock();
  return 1;
}

EXTERNC
int
dmtcp_plugin_disable_ckpt()
{
  ThreadSync::wrapperExecutionLockLock();
  return 1;
}

EXTERNC
void
dmtcp_plugin_enable_ckpt()
{
  ThreadSync::wrapperExecutionLockUnlock();
}

EXTERNC int
dmtcp_get_ckpt_signal(void)
{
  const int ckpt_signal = DmtcpWorker::determineCkptSignal();

  return ckpt_signal;
}

EXTERNC const char *
dmtcp_get_tmpdir(void)
{
  return SharedData::getTmpDir();
}

// EXTERNC void dmtcp_set_tmpdir(const char* dir)
// {
// if (dir != NULL) {
// UniquePid::setTmpDir(dir);
// }
// }

EXTERNC const char *
dmtcp_get_ckpt_dir()
{
  return ProcessInfo::instance().getCkptDir().c_str();
}

EXTERNC int
dmtcp_set_ckpt_dir(const char *dir)
{
  if (dir != NULL) {
    ProcessInfo::instance().setCkptDir(dir);
  }
  return DMTCP_IS_PRESENT;
}

EXTERNC void
dmtcp_set_ckpt_file(const char *filename)
{
  ProcessInfo::instance().setCkptFilename(filename);
}

EXTERNC const char *
dmtcp_get_ckpt_filename(void)
{
  return ProcessInfo::instance().getCkptFilename().c_str();
}

EXTERNC const char *
dmtcp_get_ckpt_files_subdir(void)
{
  return ProcessInfo::instance().getCkptFilesSubDir().c_str();
}

EXTERNC int
dmtcp_should_ckpt_open_files(void)
{
  return getenv(ENV_VAR_CKPT_OPEN_FILES) != NULL;
}

EXTERNC int
dmtcp_allow_overwrite_with_ckpted_files(void)
{
  return getenv(ENV_VAR_ALLOW_OVERWRITE_WITH_CKPTED_FILES) != NULL;
}

EXTERNC int
dmtcp_skip_truncate_file_at_restart(const char* path)
{
  return getenv(ENV_VAR_SKIP_TRUNCATE_FILE_AT_RESTART) != NULL;
}

EXTERNC const char *
dmtcp_get_executable_path(void)
{
  return ProcessInfo::instance().procSelfExe().c_str();
}

EXTERNC const char *
dmtcp_get_uniquepid_str(void)
{
  return ProcessInfo::instance().upidStr().c_str();
}

EXTERNC DmtcpUniqueProcessId
dmtcp_get_uniquepid(void)
{
  return ProcessInfo::instance().upid().upid();
}

EXTERNC DmtcpUniqueProcessId
dmtcp_get_computation_id(void)
{
  return ProcessInfo::instance().compGroup().upid();
}

EXTERNC const char *
dmtcp_get_computation_id_str(void)
{
  return ProcessInfo::instance().compGroupStr().c_str();
}

EXTERNC DmtcpUniqueProcessId
dmtcp_get_coord_id(void)
{
  return SharedData::getCoordId();
}

EXTERNC int
dmtcp_unique_pids_equal(DmtcpUniqueProcessId a, DmtcpUniqueProcessId b)
{
  return a._hostid == b._hostid &&
         a._pid == b._pid &&
         a._time == b._time &&
         a._computation_generation == b._computation_generation;
}

EXTERNC uint64_t
dmtcp_get_coordinator_timestamp(void)
{
  return SharedData::getCoordTimeStamp();
}

EXTERNC uint32_t
dmtcp_get_generation(void)
{
  return ProcessInfo::instance().get_generation();
}

EXTERNC int
checkpoint_is_pending(void)
{
  return SharedData::getCompId()._computation_generation >
         ProcessInfo::instance().get_generation();
}

EXTERNC int
dmtcp_is_running_state(void)
{
  return WorkerState::currentState() == WorkerState::RUNNING ||
         WorkerState::currentState() == WorkerState::PRESUSPEND;
}

EXTERNC int
dmtcp_is_protected_fd(int fd)
{
  return DMTCP_IS_PROTECTED_FD(fd);
}

EXTERNC int
dmtcp_protected_environ_fd(void)
{
  return PROTECTED_ENVIRON_FD;
}

EXTERNC void
dmtcp_close_protected_fd(int fd)
{
  JASSERT(DMTCP_IS_PROTECTED_FD(fd));
  _real_close(fd);
}

// dmtcp_get_restart_env() will take an environment variable, name,
// from the current environment at the time dmtcp_restart,
// and return its value.  This is useful since by default,
// an application would see only the restored memory from checkpoint
// time, which includes only environment variable values that
// existed at the time of checkpoint.
// The plugin modify-env uses this function intensively.
// EXTERNC int dmtcp_get_restart_env(char *name, char *value, int maxvaluelen);
// USAGE:
// char value[MAXSIZE];
// dmtcp_get_restart_env(name, value, MAXSIZE);
// Returns 0 on success, -1 if name not found; -2 if value > MAXSIZE
// NOTE: This implementation assumes that a "name=value" string will be
// no more than MAXSIZE bytes.

EXTERNC DmtcpGetRestartEnvErr_t
dmtcp_get_restart_env(const char *name,   // IN
                      char *value,        // OUT
                      size_t maxvaluelen)
{
  int env_fd = dup(dmtcp_protected_environ_fd());

  JASSERT(env_fd != -1)(env_fd)(dmtcp_protected_environ_fd());
  lseek(env_fd, 0, SEEK_SET);

  DmtcpGetRestartEnvErr_t rc = RESTART_ENV_NOTFOUND;

  if (name == NULL || value == NULL) {
    close(env_fd);
    return RESTART_ENV_NULL_PTR;
  }

  struct stat statbuf;
  size_t size = RESTART_ENV_MAXSIZE;

  if (fstat(env_fd, &statbuf) == 0) {
    size = statbuf.st_size + 1;
  }

  char env_buf_small[10000];
  char *env_buf;
  if (size <= sizeof env_buf_small) {
    env_buf = env_buf_small;
  } else {
    env_buf = (char *)JALLOC_MALLOC(size);
  }

  int namelen = strlen(name);
  *value = '\0'; // Default is null string
  char *pos = NULL;

  while (rc == RESTART_ENV_NOTFOUND) {
    memset(env_buf, 0, size);

    // read a flattened name-value pairs list
    int count = Util::readLine(env_fd, env_buf, size);
    if (count == 0) {
      break;
    } else if (count == -1) {
      rc = RESTART_ENV_INTERNAL_ERROR;
    } else if (count == -2) {
      rc = RESTART_ENV_DMTCP_BUF_TOO_SMALL;
    } else {
      char *start_ptr = env_buf;

      // iterate over the flattened list of name-value pairs
      while (start_ptr - env_buf < (int)sizeof(env_buf)) {
        pos = NULL;
        if (strncmp(start_ptr, name, namelen) == 0) {
          if ((pos = strchr(start_ptr, '='))) {
            strncpy(value, pos + 1, maxvaluelen);
            if (strlen(pos + 1) >= maxvaluelen) {
              rc = RESTART_ENV_TOOLONG; // value does not fit in the
                                        // user-provided value buffer
              break;
            }
          }
          rc = RESTART_ENV_SUCCESS;
          break;
        }

        // skip over a name-value pair
        start_ptr += strlen(start_ptr) + 1;
      }
    }
  }

  if (size > sizeof env_buf_small) {
    JALLOC_FREE(env_buf);
  } // Else env_buf was allocated on the stack as env_buf_small.
  close(env_fd);
  JWARNING(rc != RESTART_ENV_DMTCP_BUF_TOO_SMALL)
    (name) (sizeof(env_buf)).Text("Resize env_buf[]");
  return rc;
}

EXTERNC int
dmtcp_get_readlog_fd(void)
{
  return PROTECTED_READLOG_FD;
}

EXTERNC int
dmtcp_get_ptrace_fd(void)
{
  return PROTECTED_PTRACE_FD;
}

EXTERNC void
dmtcp_block_ckpt_signal(void)
{
  static sigset_t signals_set;
  static bool initialized = false;

  if (!initialized) {
    sigemptyset(&signals_set);
    sigaddset(&signals_set, dmtcp_get_ckpt_signal());
    initialized = true;
  }

  JASSERT(_real_pthread_sigmask(SIG_BLOCK, &signals_set, NULL) == 0);
}

EXTERNC void
dmtcp_unblock_ckpt_signal(void)
{
  static sigset_t signals_set;
  static bool initialized = false;

  if (!initialized) {
    sigemptyset(&signals_set);
    sigaddset(&signals_set, dmtcp_get_ckpt_signal());
    initialized = true;
  }

  JASSERT(_real_pthread_sigmask(SIG_UNBLOCK, &signals_set, NULL) == 0);
}

EXTERNC void
dmtcp_get_local_ip_addr(struct in_addr *in)
{
  SharedData::getLocalIPAddr(in);
}

EXTERNC void
dmtcp_global_barrier(const char *barrier)
{
  JTRACE("Waiting for global barrier") (barrier);
  if (!CoordinatorAPI::waitForBarrier(barrier)) {
    JTRACE("Failed to read message from coordinator; process exiting?");
    JASSERT(DmtcpWorker::isExitInProgress());
    DmtcpWorker::ckptThreadPerformExit();
  }
  JTRACE("Barrier Released") (barrier);
}

EXTERNC void
dmtcp_local_barrier(const char *barrier)
{
  JTRACE("Waiting for local barrier") (barrier);
  SharedData::waitForBarrier(barrier);
}

EXTERNC void
dmtcp_add_to_ckpt_header(const char *key, const char *value)
{
  ProcessInfo::instance().addKeyValuePairToCkptHeader(key, value);
}

EXTERNC void
dmtcp_set_restore_buf_addr(void *new_addr, size_t len)
{
  ProcessInfo::instance().updateRestoreBufAddr(new_addr, len);
}
