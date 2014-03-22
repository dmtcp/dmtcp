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
#include "mtcpinterface.h"

using namespace dmtcp;

#undef dmtcp_is_enabled
#undef dmtcp_checkpoint
#undef dmtcp_disable_ckpt
#undef dmtcp_enable_ckpt
#undef dmtcp_install_hooks
#undef dmtcp_get_coordinator_status
#undef dmtcp_get_local_status
#undef dmtcp_get_uniquepid_str
#undef dmtcp_get_ckpt_filename

//global counters
static int numCheckpoints = 0;
static int numRestarts    = 0;

//user hook functions
static dmtcp_fnptr_t userHookPreCheckpoint = NULL;
static dmtcp_fnptr_t userHookPostCheckpoint = NULL;
static dmtcp_fnptr_t userHookPostRestart = NULL;

//I wish we could use pthreads for the trickery in this file, but much of our
//code is executed before the thread we want to wake is restored.  Thus we do
//it the bad way.
#if defined(__i386__) || defined(__x86_64__)
static inline void memfence(){  asm volatile ("mfence" ::: "memory"); }
#elif defined(__arm__)
static inline void memfence(){  asm volatile ("dmb" ::: "memory"); }
#endif

EXTERNC int dmtcp_is_enabled() { return 1; }

static void runCoordinatorCmd(char c,
                              int *coordCmdStatus = NULL,
                              int *numPeers = NULL,
                              int *isRunning = NULL)
{
  _dmtcp_lock();
  {
    dmtcp::CoordinatorAPI coordinatorAPI;

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
    if (coordCmdStatus == dmtcp::CoordCmdStatus::ERROR_NOT_RUNNING_STATE) {
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
  return coordCmdStatus == dmtcp::CoordCmdStatus::NOERROR;
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
  return 1;
}

EXTERNC int dmtcp_get_local_status(int *nCheckpoints, int *nRestarts)
{
  *nCheckpoints = numCheckpoints;
  *nRestarts = numRestarts;
  return 1;
}

EXTERNC int dmtcp_install_hooks(dmtcp_fnptr_t preCheckpoint,
                                dmtcp_fnptr_t postCheckpoint,
                                dmtcp_fnptr_t postRestart)
{
  userHookPreCheckpoint  = preCheckpoint;
  userHookPostCheckpoint = postCheckpoint;
  userHookPostRestart    = postRestart;
  return 1;
}

EXTERNC int dmtcp_disable_ckpt()
{
  dmtcp::ThreadSync::delayCheckpointsLock();
  return 1;
}

EXTERNC int dmtcp_enable_ckpt()
{
  dmtcp::ThreadSync::delayCheckpointsUnlock();
  return 1;
}

void dmtcp::userHookTrampoline_preCkpt()
{
  if(userHookPreCheckpoint != NULL)
    (*userHookPreCheckpoint)();
}

void dmtcp::userHookTrampoline_postCkpt(bool isRestart)
{
  //this function runs before other threads are resumed
  if(isRestart){
    numRestarts++;
    if(userHookPostRestart != NULL)
      (*userHookPostRestart)();
  }else{
    numCheckpoints++;
    if(userHookPostCheckpoint != NULL)
      (*userHookPostCheckpoint)();
  }
}

EXTERNC int dmtcp_get_ckpt_signal(void)
{
  const int ckpt_signal = dmtcp::DmtcpWorker::determineCkptSignal();
  return ckpt_signal;
}

EXTERNC const char* dmtcp_get_tmpdir(void)
{
  static char tmpdir[PATH_MAX];
  JASSERT(dmtcp::SharedData::getTmpDir(tmpdir, sizeof(tmpdir)) != NULL);
  return tmpdir;
}

//EXTERNC void dmtcp_set_tmpdir(const char* dir)
//{
//  if (dir != NULL) {
//    dmtcp::UniquePid::setTmpDir(dir);
//  }
//}

EXTERNC const char* dmtcp_get_ckpt_dir()
{
  static dmtcp::string tmpdir;
  tmpdir = dmtcp::ProcessInfo::instance().getCkptDir();
  return tmpdir.c_str();
}

EXTERNC void dmtcp_set_ckpt_dir(const char* dir)
{
  if (dir != NULL) {
    dmtcp::ProcessInfo::instance().setCkptDir(dir);
  }
}

EXTERNC const char* dmtcp_get_coord_ckpt_dir(void)
{
  static dmtcp::string dir;
  dir = CoordinatorAPI::instance().getCoordCkptDir();
  return dir.c_str();
}

EXTERNC void dmtcp_set_coord_ckpt_dir(const char* dir)
{
  if (dir != NULL) {
    CoordinatorAPI::instance().updateCoordCkptDir(dir);
  }
}

EXTERNC void dmtcp_set_ckpt_file(const char *filename)
{
  dmtcp::ProcessInfo::instance().setCkptFilename(filename);
}

EXTERNC const char* dmtcp_get_ckpt_filename(void)
{
  static dmtcp::string filename;
  filename = dmtcp::ProcessInfo::instance().getCkptFilename();
  return filename.c_str();
}

EXTERNC const char* dmtcp_get_ckpt_files_subdir(void)
{
  static dmtcp::string tmpdir;
  tmpdir = dmtcp::ProcessInfo::instance().getCkptFilesSubDir();
  return tmpdir.c_str();
}

EXTERNC int dmtcp_should_ckpt_open_files(void)
{
  return getenv(ENV_VAR_CKPT_OPEN_FILES) != NULL;
}

EXTERNC const char* dmtcp_get_executable_path(void)
{
  return dmtcp::ProcessInfo::instance().procSelfExe().c_str();
}

EXTERNC const char* dmtcp_get_uniquepid_str(void)
{
  static dmtcp::string *uniquepid_str = NULL;
  uniquepid_str =
    new dmtcp::string(dmtcp::UniquePid::ThisProcess(true).toString());
  return uniquepid_str->c_str();
}

EXTERNC DmtcpUniqueProcessId dmtcp_get_uniquepid(void)
{
  return dmtcp::UniquePid::ThisProcess().upid();
}

EXTERNC DmtcpUniqueProcessId dmtcp_get_computation_id(void)
{
  return SharedData::getCompId();
}

EXTERNC const char* dmtcp_get_computation_id_str(void)
{
  static dmtcp::string *compid_str = NULL;
  if (compid_str == NULL) {
    UniquePid compId = SharedData::getCompId();
    compid_str = new dmtcp::string(compId.toString());
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
         a._generation == b._generation;
}

EXTERNC uint64_t dmtcp_get_coordinator_timestamp(void)
{
  return SharedData::getCoordTimeStamp();
}

EXTERNC uint32_t dmtcp_get_generation(void)
{
  return dmtcp::SharedData::getCompId()._generation;
}

EXTERNC int dmtcp_is_running_state(void)
{
  return dmtcp::WorkerState::currentState() == dmtcp::WorkerState::RUNNING;
}

EXTERNC int dmtcp_is_initializing_wrappers(void)
{
  return dmtcp_wrappers_initializing;
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
//   char value[MAXSIZE];
//   dmtcp_get_restart_env(name, value, MAXSIZE);
//  Returns 0 on success, -1 if name not found; -2 if value > MAXSIZE
// NOTE: This implementation assumes that a "name=value" string will be
//   no more than 2000 bytes.

EXTERNC int dmtcp_get_restart_env(char *name, char *value, int maxvaluelen) {
  int env_fd = dup(dmtcp_protected_environ_fd());
  JASSERT(env_fd != -1)(env_fd)(dmtcp_protected_environ_fd());
  lseek(env_fd, 0, SEEK_SET);
  int namelen = strlen(name);
  *value = '\0'; // Default is null string
#define SUCCESS 0
#define NOTFOUND -1
#define TOOLONG -2
#define DMTCP_BUF_TOO_SMALL -3
#define INTERNAL_ERROR -4
#define NULL_PTR -5
  int rc = NOTFOUND; // Default is -1: name not found

  char env_buf[2000]; // All "name=val" strings must be shorter than this.
  char *env_ptr_v[sizeof(env_buf)/4];
  char *name_ptr = env_buf;
  char *env_end_ptr = env_buf;

  if (name == NULL || value == NULL) {
    close(env_fd);
    return NULL_PTR;
  }

  while (rc == NOTFOUND && env_end_ptr != NULL) {
    // if name_ptr is in second half of env_buf, move everything back to start
    if (name_ptr > env_buf) {
      memmove(env_buf, name_ptr, env_end_ptr - name_ptr);
      env_end_ptr -= (name_ptr - env_buf);
      name_ptr = env_buf;
    }
    // if we haven't finished reading environment from env_fd,
    //    then read until it's full or there is no more
    while (env_end_ptr != NULL &&
           env_end_ptr - env_buf < (int) sizeof(env_buf)) {
      int count = read(env_fd,
                       env_end_ptr, sizeof(env_buf) - (env_end_ptr - env_buf));
      if (count == 0) {
        break;
      } else if (count == -1 && errno != EAGAIN && errno != EINTR) {
        rc = INTERNAL_ERROR;
        JASSERT (false) (count) (JASSERT_ERRNO) .Text("internal error");
      } else {
        env_end_ptr += count;
      }
    }
    JASSERT(env_end_ptr > env_buf || env_buf[0] == '\0') ((char *)env_buf);
    // Set up env_ptr_v[]
    int env_ptr_v_idx = 0;
    env_ptr_v[env_ptr_v_idx++] = name_ptr;
    int end_of_buf = 0;
    while ( ! end_of_buf ) {
      char *last_name_ptr = name_ptr;
      while (name_ptr < env_end_ptr && *name_ptr != '\0') {
        name_ptr++;
      }
      if (name_ptr < env_end_ptr) {
        JASSERT(*name_ptr == '\0');
        name_ptr++;
        env_ptr_v[env_ptr_v_idx++] = name_ptr; // Add name-value pair
      } else {
        JASSERT(name_ptr == env_end_ptr); // Reached end of what was read in
        end_of_buf = 1;
        name_ptr = last_name_ptr;
        env_ptr_v[env_ptr_v_idx - 1] = NULL; // Last name-value was incomplete
        if (name_ptr == env_buf) {
          rc = DMTCP_BUF_TOO_SMALL;
        }
      }
    }
    // Now search for name among strings of env_ptr_v[] that were read.
    int i;
    for (i = 0; env_ptr_v[i] != NULL; i++) {
      if (strncmp(env_ptr_v[i], name, namelen) == 0 &&
          *(env_ptr_v[i] + namelen) == '=') {
        strncpy(value, env_ptr_v[i] + namelen + 1, maxvaluelen);
        rc = SUCCESS;
        if (namelen + 1 > maxvaluelen)
          rc = TOOLONG; // value does not fit in user string
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

EXTERNC void dmtcp_get_local_ip_addr(struct in_addr *in)
{
  SharedData::getLocalIPAddr(in);
}

EXTERNC int dmtcp_no_coordinator(void)
{
  return CoordinatorAPI::noCoordinator();
}
