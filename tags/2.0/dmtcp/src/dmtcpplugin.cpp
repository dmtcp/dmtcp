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

EXTERNC void dmtcp_event_hook(DmtcpEvent_t id, DmtcpEventData_t *data)
{
  DMTCP_NEXT_EVENT_HOOK(id, data);
}

EXTERNC int  dmtcp_get_ckpt_signal()
{
  const int ckpt_signal = dmtcp::DmtcpWorker::determineMtcpSignal();
  return ckpt_signal;
}

EXTERNC const char* dmtcp_get_tmpdir()
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

EXTERNC const char* dmtcp_get_ckpt_files_subdir()
{
  static dmtcp::string tmpdir;
  tmpdir = dmtcp::UniquePid::getCkptFilesSubDir();
  return tmpdir.c_str();
}

EXTERNC int dmtcp_should_ckpt_open_files()
{
  return getenv(ENV_VAR_CKPT_OPEN_FILES) != NULL;
}

EXTERNC const char* dmtcp_get_executable_path()
{
  return dmtcp::ProcessInfo::instance().procSelfExe().c_str();
}

EXTERNC const char* dmtcp_get_uniquepid_str()
{
  static dmtcp::string *uniquepid_str = NULL;
  uniquepid_str =
    new dmtcp::string(dmtcp::UniquePid::ThisProcess(true).toString());
  return uniquepid_str->c_str();
}

EXTERNC DmtcpUniqueProcessId dmtcp_get_uniquepid()
{
  return dmtcp::UniquePid::ThisProcess().upid();
}

EXTERNC const char* dmtcp_get_computation_id_str()
{
  static dmtcp::string *compid_str = NULL;
  if (compid_str == NULL)
    compid_str =
      new dmtcp::string(dmtcp::UniquePid::ComputationId().toString());
  return compid_str->c_str();
}

EXTERNC DmtcpUniqueProcessId dmtcp_get_coord_id()
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

EXTERNC time_t dmtcp_get_coordinator_timestamp()
{
  return CoordinatorAPI::instance().coordTimeStamp();
}

EXTERNC int  dmtcp_get_generation()
{
  return dmtcp::UniquePid::ComputationId().generation();
}

EXTERNC int  dmtcp_is_running_state()
{
  return dmtcp::WorkerState::currentState() == dmtcp::WorkerState::RUNNING;
}

EXTERNC int  dmtcp_is_initializing_wrappers()
{
  return dmtcp_wrappers_initializing;
}

EXTERNC int  dmtcp_is_protected_fd(int fd)
{
  return DMTCP_IS_PROTECTED_FD(fd);
}

EXTERNC void dmtcp_close_protected_fd(int fd)
{
  JASSERT(DMTCP_IS_PROTECTED_FD(fd));
  _real_close(fd);
}

EXTERNC int dmtcp_get_readlog_fd()
{
  return PROTECTED_READLOG_FD;
}

EXTERNC int dmtcp_get_ptrace_fd()
{
  return PROTECTED_PTRACE_FD;
}

EXTERNC void *dmtcp_get_libc_dlsym_addr()
{
  return _dmtcp_get_libc_dlsym_addr();
}

EXTERNC void dmtcp_block_ckpt_signal()
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

EXTERNC void dmtcp_unblock_ckpt_signal()
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

EXTERNC int dmtcp_send_key_val_pair_to_coordinator(const void *key,
                                                   size_t key_len,
                                                   const void *val,
                                                   size_t val_len)
{
  return CoordinatorAPI::instance().sendKeyValPairToCoordinator(key, key_len,
                                                                val, val_len);
}

// On input, val points to a buffer in user memory and *val_len is the maximum
//   size of that buffer (the memory allocated by user).
// On output, we copy data to val, and set *val_len to the actual buffer size
//   (to the size of the data that we copied to the user buffer).
EXTERNC int dmtcp_send_query_to_coordinator(const void *key, size_t key_len,
                                            void *val, size_t *val_len)
{
  return CoordinatorAPI::instance().sendQueryToCoordinator(key, key_len,
                                                           val, val_len);
}

EXTERNC void dmtcp_get_local_ip_addr(struct in_addr *in)
{
  SharedData::getLocalIPAddr(in);
}

EXTERNC int dmtcp_no_coordinator()
{
  return CoordinatorAPI::noCoordinator();
}
