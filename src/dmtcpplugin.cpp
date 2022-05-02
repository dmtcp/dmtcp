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
#undef dmtcp_set_coord_ckpt_dir
#undef dmtcp_get_coord_ckpt_dir
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
  return ThreadSync::wrapperExecutionLockLock();
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
  return ThreadSync::wrapperExecutionLockLock();
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
  static char tmpdir[PATH_MAX];

  JASSERT(SharedData::getTmpDir(tmpdir, sizeof(tmpdir)) != NULL);
  return tmpdir;
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
  static string *tmpdir = new string(ProcessInfo::instance().getCkptDir());
  return tmpdir->c_str();
}

EXTERNC int
dmtcp_set_ckpt_dir(const char *dir)
{
  if (dir != NULL) {
    ProcessInfo::instance().setCkptDir(dir);
  }
  return DMTCP_IS_PRESENT;
}

EXTERNC const char *
dmtcp_get_coord_ckpt_dir(void)
{
  static string *dir = new string(CoordinatorAPI::getCoordCkptDir());
  return dir->c_str();
}

EXTERNC int
dmtcp_set_coord_ckpt_dir(const char *dir)
{
  if (dir != NULL) {
    CoordinatorAPI::updateCoordCkptDir(dir);
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
  static string *filename =
    new string(ProcessInfo::instance().getCkptFilename());
  return filename->c_str();
}

EXTERNC const char *
dmtcp_get_ckpt_files_subdir(void)
{
  static string *tmpdir =
    new string(ProcessInfo::instance().getCkptFilesSubDir());
  return tmpdir->c_str();
}

EXTERNC void
dmtcp_get_bin_dir(char dmtcp_bin_dir[]) {
  char *lib = getenv("DMTCP_HIJACK_LIBS");
  JASSERT(lib != NULL && *lib != '\0')(lib).Text("Bad DMTCP_HIJACK_LIB");

  char *dmtcp_lib = strstr(lib, "/lib/dmtcp/libdmtcp_");
  dmtcp_lib[0] = '\0'; // temporarily set to '\0' for sake of strrchr()
  if (strrchr(lib, ':') != NULL) { // If DMTCP lib is not the first one
    lib = strrchr(lib, ':') + 1;
  }
  dmtcp_lib[0] = '/'; // restore changed character

  // If 'strstr(...) == NULL, then there is only one library.
  char *lib_end = strchr(lib, ':') ? strchr(lib, ':') : lib;
  dmtcp_bin_dir[0] = '\0';
  strncpy(dmtcp_bin_dir, lib, lib_end - lib);
  dmtcp_bin_dir[lib_end - lib] = '\0';
  int i = 3;
  for (i = 3; i > 0; i--) {
    strrchr(dmtcp_bin_dir, '/')[0] = '\0';
  }
  strcat(dmtcp_bin_dir, "/bin");
}

/* See include/dmtcp.h for examples of using this function.
 */
#undef dmtcp_get_libc_addr
EXTERNC void *
dmtcp_get_libc_addr(const char*libc_fnc) {
  char dmtcp_bin_dir[10000];
  JASSERT(strlen(getenv("DMTCP_HIJACK_LIBS")) < sizeof(dmtcp_bin_dir));
  dmtcp_get_bin_dir(dmtcp_bin_dir);
  strcat(dmtcp_bin_dir, "/dmtcp_get_libc_offset");
  char *dmtcp_get_libc_offset = dmtcp_bin_dir;

  int pipefd[2];
  JASSERT(pipe(pipefd) == 0)(JASSERT_ERRNO);
  // Use dmtcp_fork() instead of fork() in case a plugin is redefining
  //   fork() to call:  (*dmtcp_get_libc_addr("fork"))().
  extern int dmtcp_fork();
  int childpid = dmtcp_fork();
  if (childpid) { // if parent
    long int offset;
    while (read(pipefd[0], &offset, sizeof(offset)) != sizeof(offset)) {};
    waitpid(childpid, NULL, 0);
    // This assumes DMTCP does not wrap "read".  dlsym() returns ptr in libc.
    return (char *)dlsym(RTLD_NEXT, "read") + offset;
  } else { // else child
    // get offset of 'execv' in libc, and write it to parent
    char *command[] = {NULL, NULL, NULL};
    command[0] = dmtcp_get_libc_offset;
    command[1] = const_cast<char*>(libc_fnc);
    close(0);
    dup2(pipefd[1], 1); // command will write offset to stdout (fd: 1)
    // char *ld_preload = getenv("LD_PRELOAD");
    // char ld_preload_first_char = ld_preload[0]; // Unused; see below;
    // ld_preload[0] = '\0'; // No LD_PRELOAD on next program.
    environ[0] = NULL;
    execvpe(command[0], command, environ);
    // ld_preload[0] = ld_preload_first_char; // Not needed; never reached.
    exit(0); // to satisfy the compiler
  }
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

EXTERNC const char *
dmtcp_get_executable_path(void)
{
  return ProcessInfo::instance().procSelfExe().c_str();
}

EXTERNC const char *
dmtcp_get_uniquepid_str(void)
{
  static string *uniquepid_str =
    new string(UniquePid::ThisProcess(true).toString());
  return uniquepid_str->c_str();
}

EXTERNC DmtcpUniqueProcessId
dmtcp_get_uniquepid(void)
{
  return UniquePid::ThisProcess().upid();
}

EXTERNC DmtcpUniqueProcessId
dmtcp_get_computation_id(void)
{
  return SharedData::getCompId();
}

EXTERNC const char *
dmtcp_get_computation_id_str(void)
{
  static string *compid_str = NULL;

  if (compid_str == NULL) {
    UniquePid compId = SharedData::getCompId();
    compid_str = new string(compId.toString());
  }
  return compid_str->c_str();
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
  char *env_buf = NULL;

  if (fstat(env_fd, &statbuf) == 0) {
    size = statbuf.st_size + 1;
  }

  env_buf = (char*) JALLOC_HELPER_MALLOC(size);

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

  close(env_fd);
  JWARNING(rc != RESTART_ENV_DMTCP_BUF_TOO_SMALL)
    (name) (sizeof(env_buf)).Text("Resize env_buf[]");
  JALLOC_HELPER_FREE(env_buf);
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

EXTERNC int
dmtcp_send_key_val_pair_to_coordinator(const char *id,
                                       const void *key,
                                       uint32_t key_len,
                                       const void *val,
                                       uint32_t val_len)
{
  return CoordinatorAPI::sendKeyValPairToCoordinator(id,
                                                     key,
                                                     key_len,
                                                     val,
                                                     val_len);
}

// On input, val points to a buffer in user memory and *val_len is the maximum
// size of that buffer (the memory allocated by user).
// On output, we copy data to val, and set *val_len to the actual buffer size
// (to the size of the data that we copied to the user buffer).
EXTERNC int
dmtcp_send_query_to_coordinator(const char *id,
                                const void *key,
                                uint32_t key_len,
                                void *val,
                                uint32_t *val_len)
{
  return CoordinatorAPI::sendQueryToCoordinator(id, key, key_len, val, val_len);
}

EXTERNC int
dmtcp_get_unique_id_from_coordinator(const char *id,    // DB name
                                     const void *key,   // hostid, pid, etc.
                                     uint32_t key_len,  // Length of the key
                                     void *val,         // Result
                                     uint32_t offset,   // unique id offset
                                     uint32_t val_len)  // Expected val length
{
  return CoordinatorAPI::getUniqueIdFromCoordinator(id, key, key_len,
                                                    val, &val_len, offset);
}

EXTERNC int
dmtcp_send_query_all_to_coordinator(const char *id, void **buf, int *len)
{
  return CoordinatorAPI::sendQueryAllToCoordinator(id, buf, len);
}

EXTERNC void
dmtcp_get_local_ip_addr(struct in_addr *in)
{
  SharedData::getLocalIPAddr(in);
}

EXTERNC int
dmtcp_no_coordinator(void)
{
  return CoordinatorAPI::noCoordinator();
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
