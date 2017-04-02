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
#include "dmtcpplugin.h"
#include "dmtcpworker.h"
#include "coordinatorapi.h"
#include "syscallwrappers.h"
#include "processinfo.h"
#include "shareddata.h"
#include "threadsync.h"
#include "mtcpinterface.h"
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
#undef dmtcp_set_global_ckpt_dir

using namespace dmtcp;

//global counters
static int numCheckpoints = 0;
static int numRestarts    = 0;

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

static void runCoordinatorCmd(char c,
                              int *coordCmdStatus = NULL,
                              int *numPeers = NULL,
                              int *isRunning = NULL)
{
  _dmtcp_lock();
  {
    CoordinatorAPI coordinatorAPI;

    dmtcp_disable_ckpt();
    coordinatorAPI.connectAndSendUserCommand(c, coordCmdStatus, numPeers,
                                             isRunning);
    dmtcp_enable_ckpt();
  }
  _dmtcp_unlock();
}

static int dmtcpRunCommand(char command)
{
  int coordCmdStatus;
  int i = 0;
  while (i < 100) {
    runCoordinatorCmd(command, &coordCmdStatus);
  // if we got error result - check it
	// There is possibility that checkpoint thread
	// did not send state=RUNNING yet or Coordinator did not receive it
	// -- Artem
    if (coordCmdStatus == CoordCmdStatus::ERROR_NOT_RUNNING_STATE) {
      struct timespec t;
      t.tv_sec = 0;
      t.tv_nsec = 1000000;
      nanosleep(&t, NULL);
      //printf("\nWAIT FOR CHECKPOINT ABLE\n\n");
    } else {
//      printf("\nEverything is OK - return\n");
      break;
    }
    i++;
  }
  return coordCmdStatus == CoordCmdStatus::NOERROR;
}

EXTERNC int dmtcp_checkpoint()
{
  int rv = 0;
  int oldNumRestarts    = numRestarts;
  int oldNumCheckpoints = numCheckpoints;
  memfence(); //make sure the reads above don't get reordered

  if(dmtcpRunCommand('c')){ //request checkpoint
    //and wait for the checkpoint
    while(oldNumRestarts==numRestarts && oldNumCheckpoints==numCheckpoints){
      //nanosleep should get interrupted by checkpointing with an EINTR error
      //though there is a race to get to nanosleep() before the checkpoint
      struct timespec t = {1,0};
      nanosleep(&t, NULL);
      memfence();  //make sure the loop condition doesn't get optimized
    }
    rv = (oldNumRestarts==numRestarts ? DMTCP_AFTER_CHECKPOINT : DMTCP_AFTER_RESTART);
  }else{
  	/// TODO: Maybe we need to process it in some way????
    /// EXIT????
    /// -- Artem
    //	printf("\n\n\nError requesting checkpoint\n\n\n");
  }

  return rv;
}

EXTERNC int dmtcp_get_coordinator_status(int *numPeers, int *isRunning)
{
  int coordCmdStatus;
  runCoordinatorCmd('s', &coordCmdStatus, numPeers, isRunning);
  return DMTCP_IS_PRESENT;;
}

EXTERNC int dmtcp_get_local_status(int *nCheckpoints, int *nRestarts)
{
  *nCheckpoints = numCheckpoints;
  *nRestarts = numRestarts;
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

EXTERNC int dmtcp_set_global_ckpt_dir(const char *dir)
{
  dmtcp_disable_ckpt();
  if (dir != NULL) {
    if(!CoordinatorAPI::instance().updateGlobalCkptDir(dir)) {
      JNOTE("Failed to set global checkpoint dir. "
            "Most probably this is because DMTCP is in the middle "
            "of a checkpoint. Please try again later") (dir);
      dmtcp_enable_ckpt();
      return -1;
    }
  }
  dmtcp_enable_ckpt();
  return DMTCP_IS_PRESENT;
}

// Returns a zero-length string when there's no
// coordinator or a checkpoint is in progress.
EXTERNC const char* dmtcp_get_coord_ckpt_dir(void)
{
  static string dir;
  CoordinatorAPI coordinatorAPI;
  dmtcp_disable_ckpt();
  dir = coordinatorAPI.getCoordCkptDir();
  dmtcp_enable_ckpt();
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

// EXTERNC int dmtcp_get_restart_env(char *name, char *value, int maxvaluelen);
// USAGE:
//   char value[RESTART_ENV_MAXSIZE];
//   dmtcp_get_restart_env(name, value, RESTART_ENV_MAXSIZE);
//  Returns 0 on success, -1 if name not found; -2 if value > RESTART_ENV_MAXSIZE
// NOTE: This implementation assumes that a "name=value" string will be
//   no more than RESTART_ENV_MAXSIZE bytes.

EXTERNC int
dmtcp_get_restart_env(const char *name,   // IN
                      char *value,        // OUT
                      size_t maxvaluelen)
{
  int env_fd = dup(dmtcp_protected_environ_fd());
  JASSERT(env_fd != -1)(env_fd)(dmtcp_protected_environ_fd());
  lseek(env_fd, 0, SEEK_SET);

  int rc = RESTART_ENV_NOTFOUND; // Default is -1: name not found

  char env_buf[RESTART_ENV_MAXSIZE] = {0}; // All "name=val" strings must be shorter than this.

  if (name == NULL || value == NULL) {
    close(env_fd);
    return RESTART_ENV_NULL_PTR;
  }

  int namelen = strlen(name);
  *value = '\0'; // Default is null string
  char *pos = NULL;

  while (rc == RESTART_ENV_NOTFOUND) {
   memset(env_buf, 0, RESTART_ENV_MAXSIZE);
   // read a flattened name-value pairs list
   int count = Util::readLine(env_fd, env_buf, RESTART_ENV_MAXSIZE);
   if (count == 0) {
     break;
   } else if (count == -1) {
     rc = RESTART_ENV_INTERNAL_ERROR;
   } else if (count == -2) {
     rc = RESTART_ENV_DMTCP_BUF_TOO_SMALL;
   } else {
     char *start_ptr = env_buf;
     // iterate over the flattened list of name-value pairs
     while (start_ptr - env_buf < (int) sizeof(env_buf)) {
       pos = NULL;
       if (strncmp(start_ptr, name, namelen) == 0) {
         if ((pos = strchr(start_ptr, '='))) {
           strncpy(value, pos + 1, maxvaluelen);
           if (strlen(pos+1) >= maxvaluelen) {
             rc = RESTART_ENV_TOOLONG; // value does not fit in the user-provided value buffer
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
  JWARNING (rc != RESTART_ENV_DMTCP_BUF_TOO_SMALL)
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

EXTERNC int dmtcp_send_key_val_pair_to_coordinator_sync(const char *id,
                                                        const void *key,
                                                        uint32_t key_len,
                                                        const void *val,
                                                        uint32_t val_len)
{
  return CoordinatorAPI::instance().sendKeyValPairToCoordinator(id, key, key_len,
                                                                val, val_len,
								1);
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

EXTERNC int dmtcp_get_unique_id_from_coordinator(const char *id,    // DB name
                                                 const void *key,   // hostid, pid, etc.
                                                 uint32_t key_len,  // Length of the key
                                                 void *val,         // Result
                                                 uint32_t offset,   // unique id offset
                                                 uint32_t val_len)  // Expected val length
{
  return CoordinatorAPI::instance().getUniqueIdFromCoordinator(id, key, key_len,
                                                               val, &val_len,
                                                               offset);
}

EXTERNC void dmtcp_get_local_ip_addr(struct in_addr *in)
{
  SharedData::getLocalIPAddr(in);
}

EXTERNC int dmtcp_no_coordinator(void)
{
  return CoordinatorAPI::noCoordinator();
}

EXTERNC void dmtcp_update_max_required_fd(int fd)
{
  ProcessInfo::instance().updateMaxUserFd(fd);
}

void dmtcp::increment_counters(int isRestart)
{
  if (isRestart) {
    numRestarts++;
  } else {
    numCheckpoints++;
  }
}
