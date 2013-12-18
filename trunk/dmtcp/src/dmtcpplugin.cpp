/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include "dmtcpplugin.h"
#include "dmtcpworker.h"
#include "coordinatorapi.h"
#include "syscallwrappers.h"
#include "processinfo.h"
#include "shareddata.h"

using namespace dmtcp;

EXTERNC int dmtcp_get_ckpt_signal(void)
{
  const int ckpt_signal = dmtcp::DmtcpWorker::determineMtcpSignal();
  return ckpt_signal;
}

EXTERNC const char* dmtcp_get_tmpdir(void)
{
  static dmtcp::string *tmpdir = NULL;
  if (tmpdir == NULL)
    tmpdir = new dmtcp::string(dmtcp::UniquePid::getTmpDir());
  return tmpdir->c_str();
}

EXTERNC void dmtcp_set_tmpdir(const char* dir)
{
  if (dir != NULL) {
    dmtcp::UniquePid::setTmpDir(dir);
  }
}

EXTERNC const char* dmtcp_get_ckpt_dir()
{
  static dmtcp::string tmpdir;
  tmpdir = dmtcp::UniquePid::getCkptDir();
  return tmpdir.c_str();
}

EXTERNC void dmtcp_set_ckpt_dir(const char* dir)
{
  if (dir != NULL) {
    dmtcp::UniquePid::setCkptDir(dir);
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

EXTERNC const char* dmtcp_get_ckpt_files_subdir(void)
{
  static dmtcp::string tmpdir;
  tmpdir = dmtcp::UniquePid::getCkptFilesSubDir();
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

EXTERNC const char* dmtcp_get_computation_id_str(void)
{
  static dmtcp::string *compid_str = NULL;
  if (compid_str == NULL)
    compid_str =
      new dmtcp::string(dmtcp::UniquePid::ComputationId().toString());
  return compid_str->c_str();
}

EXTERNC DmtcpUniqueProcessId dmtcp_get_coord_id(void)
{
  return CoordinatorAPI::instance().coordinatorId();
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
  return CoordinatorAPI::instance().coordTimeStamp();
}

EXTERNC uint32_t dmtcp_get_generation(void)
{
  return dmtcp::UniquePid::ComputationId().generation();
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

// EXTERNC int dmtcp_get_restart_env(char *key, char *value, int maxvaluelen);
// USAGE:
//   char value[MAXSIZE];
//   dmtcp_get_restart_env(key, value, MAXSIZE);
//  Returns 0 on success, -1 if key not found; -2 if value > MAXSIZE
// NOTE: This implementation assumes that a "key=value" string will be
//   no more than 2000 bytes.

EXTERNC int dmtcp_get_restart_env(char *key, char *value, int maxvaluelen) {
  int env_fd = dup(dmtcp_protected_environ_fd());
  int keylen = strlen(key);
  *value = '\0'; // Default is null string
#define SUCCESS 0
#define NOTFOUND -1
#define TOOLONG -2
#define DMTCP_BUF_TOO_SMALL -3
#define INTERNAL_ERROR -4
#define NULL_PTR -5
  int rc = NOTFOUND; // Default is -1: key not found

  char env_buf[2000]; // All "key=val" strings must be shorter than this.
  char *env_ptr_v[sizeof(env_buf)/4];
  char *key_ptr = env_buf;
  char *env_end_ptr = env_buf;

  if (key == NULL || value == NULL)
    return NULL_PTR;

  while (rc == NOTFOUND && env_end_ptr != NULL) {
    // if key_ptr is in second half of env_buf, move everything back to start
    if (key_ptr > env_buf) {
      memmove(env_buf, key_ptr, env_end_ptr - key_ptr);
      env_end_ptr -= (key_ptr - env_buf);
      key_ptr = env_buf;
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
    // Set up env_ptr_v[]
    int env_ptr_v_idx = 0;
    env_ptr_v[env_ptr_v_idx++] = key_ptr;
    int end_of_buf = 0;
    while ( ! end_of_buf ) {
      char *last_key_ptr = key_ptr;
      while (key_ptr < env_end_ptr && *key_ptr != '\0') {
        key_ptr++;
      }
      if (key_ptr < env_end_ptr) {
        JASSERT(*key_ptr == '\0');
        key_ptr++;
        env_ptr_v[env_ptr_v_idx++] = key_ptr; // Add key-value pair
      } else {
        JASSERT(key_ptr == env_end_ptr); // Reached end of what was read in
        end_of_buf = 1;
        key_ptr = last_key_ptr;
        env_ptr_v[env_ptr_v_idx - 1] = NULL; // Last key-value was incomplete
        if (key_ptr == env_buf) {
          rc = DMTCP_BUF_TOO_SMALL;
        }
      }
    }
    // Now search for key among strings of env_ptr_v[] that were read.
    int i;
    for (i = 0; env_ptr_v[i] != NULL; i++) {
      if (strncmp(env_ptr_v[i], key, keylen) == 0 &&
          *(env_ptr_v[i] + keylen) == '=') {
        strncpy(value, env_ptr_v[i] + keylen + 1, maxvaluelen);
        rc = SUCCESS;
        if (keylen + 1 > maxvaluelen)
          rc = TOOLONG; // value does not fit in user string
      }
    }
  }

  close(env_fd);
  JWARNING (rc == DMTCP_BUF_TOO_SMALL)
    (key) (sizeof(env_buf)) .Text("Resize env_buf[]");
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

EXTERNC void *dmtcp_get_libc_dlsym_addr(void)
{
  return _dmtcp_get_libc_dlsym_addr();
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
