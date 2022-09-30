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

#include <sys/types.h>
#include "pathbuffer.h"
#include "jalloc.h"
#include "jassert.h"
#include "jconvert.h"
#include "jfilesystem.h"
#include "config.h"
#include "dmtcp.h"
#include "pidwrappers.h"
#include "protectedfds.h"
#include "shareddata.h"
#include "virtualpidtable.h"

static constexpr const char* PROC_PREFIX = "/proc/";
static constexpr size_t PROC_PREFIX_LEN = strlen(PROC_PREFIX);

static constexpr const char* PROC_TASK_TOKEN = "/task/";
static constexpr size_t PROC_TASK_TOKEN_LEN = strlen(PROC_TASK_TOKEN);

using namespace dmtcp;

extern "C" pid_t dmtcp_update_ppid();
static volatile bool restartInProgress = false;

static string pidMapFile;

#ifdef ENABLE_PTHREAD_MUTEX_WRAPPERS
dmtcp::map<pthread_mutex_t *, pid_t>&
mapMutexVirtTid()
{
  static dmtcp::map<pthread_mutex_t *, pid_t> *instance = NULL;

  if (instance == NULL) {
    void *buffer = JALLOC_MALLOC(1024 * 1024);
    instance = new (buffer)dmtcp::map<pthread_mutex_t *, pid_t>();
  }
  return *instance;
}
#endif

extern "C"
pid_t
dmtcp_real_to_virtual_pid(pid_t realPid)
{
  return REAL_TO_VIRTUAL_PID(realPid);
}

extern "C"
pid_t
dmtcp_virtual_to_real_pid(pid_t virtualPid)
{
  return VIRTUAL_TO_REAL_PID(virtualPid);
}

// Also copied into src/threadlist.cpp, so that libdmtcp.sp
//   won't depend on libdmtcp_pid.sp
extern "C"
pid_t
dmtcp_get_real_pid()
{
  return _real_getpid();
}

extern "C"
pid_t
dmtcp_get_real_tid()
{
  return _real_gettid();
}

extern "C"
int
dmtcp_real_tgkill(pid_t tgid, pid_t tid, int sig)
{
  return _real_tgkill(tgid, tid, sig);
}

extern "C"
void
dmtcp_update_virtual_to_real_tid(pid_t tid)
{
  if (!restartInProgress) {
    restartInProgress = true;
    VirtualPidTable::instance().postRestart();
  }

  VirtualPidTable::instance().updateMapping(tid, _real_gettid());
}

extern "C"
void dmtcp_init_virtual_tid()
{
  pid_t virtualTid = VirtualPidTable::instance().getNewVirtualTid();
  dmtcpResetTid(virtualTid);
  VirtualPidTable::instance().updateMapping(virtualTid, _real_gettid());
}

static void
pidVirt_PrepareForExec(DmtcpEventData_t *data)
{
  pid_t virtPpid = getppid();
  pid_t realPpid = VIRTUAL_TO_REAL_PID(virtPpid);
  Util::setVirtualPidEnvVar(getpid(), virtPpid, realPpid);

  JASSERT(data != NULL);
  jalib::JBinarySerializeWriterRaw wr("", data->preExec.serializationFd);
  VirtualPidTable::instance().serialize(wr);
}

static void
pidVirt_PostExec(DmtcpEventData_t *data)
{
  JASSERT(data != NULL);
  jalib::JBinarySerializeReaderRaw rd("", data->postExec.serializationFd);
  VirtualPidTable::instance().serialize(rd);
  VirtualPidTable::instance().refresh();
}

static void
pidVirt_ProcessProcSelfTask(DmtcpEventData_t *data)
{
  if (!Util::strStartsWith(data->virtualToRealPath.path, PROC_PREFIX)) {
    return;
  }

  char *ptr = strstr(data->virtualToRealPath.path, PROC_TASK_TOKEN);
  if (ptr == nullptr) {
    return;
  }

  const char *rest = nullptr;
  char *tidStr = ptr + PROC_TASK_TOKEN_LEN;

  pid_t virtualTid = Util::atoi(tidStr, &rest);
  if (virtualTid > 0) {
    string buffer(rest);
    pid_t realTid = VIRTUAL_TO_REAL_PID(virtualTid);
    snprintf(tidStr, PATH_MAX, "%d%s", realTid, buffer.c_str());
  }
}

// FIXME:  This function needs third argument newpathsize, or assume PATH_MAX
static void
pid_virtual_to_real_filepath(DmtcpEventData_t *data)
{
  if (!Util::strStartsWith(data->virtualToRealPath.path, PROC_PREFIX)) {
    return;
  }

  int index = strlen(PROC_PREFIX);
  const char *rest;
  pid_t virtualPid = Util::atoi(&data->virtualToRealPath.path[index], &rest);

  if (virtualPid > 0) {
    string restStr(rest);
    pid_t realPid = VIRTUAL_TO_REAL_PID(virtualPid);
    char *path = data->virtualToRealPath.path;
    snprintf(path, PATH_MAX, "/proc/%d%s", realPid, restStr.c_str());
  }

  pidVirt_ProcessProcSelfTask(data);
}

// FIXME:  This function needs third argument newpathsize, or assume PATH_MAX
static void
pid_real_to_virtual_filepath(DmtcpEventData_t *data)
{
  if (!Util::strStartsWith(data->realToVirtualPath.path, PROC_PREFIX)) {
    return;
  }

  int index = strlen(PROC_PREFIX);
  const char *rest;
  pid_t realPid = Util::atoi(&data->realToVirtualPath.path[index], &rest);

  if (realPid <= 0) {
    return;
  }

  string restStr(rest);
  pid_t virtualPid = REAL_TO_VIRTUAL_PID(realPid);
  char *path = data->virtualToRealPath.path;
  snprintf(path, PATH_MAX, "/proc/%d%s", virtualPid, restStr.c_str());
}


static int
openSharedFile(string name, int flags)
{
  int fd;
  int errno_bkp;

  // try to create, truncate & open file

  dmtcp::string dir = jalib::Filesystem::DirName(name);

  JTRACE("shared file dir:")(dir);
  jalib::Filesystem::mkdir_r(dir, 0755);

  if ((fd =
         _real_open(name.c_str(), O_EXCL | O_CREAT | O_TRUNC | flags,
                    0600)) >= 0) {
    return fd;
  }
  errno_bkp = errno;

  JTRACE("_real_open: ")(strerror(errno))(fd)(flags);

  if ((fd < 0) && (errno_bkp == EEXIST)) {
    errno = 0;
    if ((fd = _real_open(name.c_str(), flags, 0600)) >= 0) {
      return fd;
    }
  }

  // unable to create & open OR open
  JASSERT(false)(name)(strerror(errno)).Text("Cannot open file");
  return -1;
}

static void
openOriginalToCurrentMappingFiles()
{
  int fd;
  ostringstream o;

  o << dmtcp_get_tmpdir() << "/dmtcpPidMap."
    << dmtcp_get_computation_id_str() << "."
    << std::hex << dmtcp_get_coordinator_timestamp();
  pidMapFile = o.str();

  // Open and create pidMapFile if it doesn't exist.
  JTRACE("Open dmtcpPidMapFile")(pidMapFile);
  if (!Util::isValidFd(PROTECTED_PIDMAP_FD)) {
    fd = openSharedFile(pidMapFile, O_RDWR);
    JASSERT(fd != -1);
    JASSERT(_real_dup2(fd, PROTECTED_PIDMAP_FD) == PROTECTED_PIDMAP_FD)
      (pidMapFile);
    _real_close(fd);
  }
}

static void
pidVirt_PostRestart()
{
  if (jalib::Filesystem::GetProgramName() == "screen") {
    send_sigwinch = 1;
  }

  // With hardstatus (bottom status line), screen process has diff. size window
  // Must send SIGWINCH to adjust it.
  // src/terminal.cpp:restore_term_settings() will send SIGWINCH to process
  // on restart.  This will force 'screen' to execute ioctl wrapper.
  // The wrapper will report a changed winsize,
  // so that 'screen' must re-initialize the screen (scrolling regions, etc.).
  // The wrapper will also send a second SIGWINCH.  Then 'screen' will
  // call ioctl and get the correct window size and resize again.
  // We can't just send two SIGWINCH's now, since window size has not
  // changed yet, and 'screen' will assume that there's nothing to do.

  dmtcp_update_ppid();
  openOriginalToCurrentMappingFiles();
  VirtualPidTable::instance().writeMapsToFile(PROTECTED_PIDMAP_FD);
}

#ifdef ENABLE_PTHREAD_MUTEX_WRAPPERS
static void
pidVirt_RefillTid()
{
  map<pthread_mutex_t *, pid_t>::iterator it;

  for (it = mapMutexVirtTid().begin(); it != mapMutexVirtTid().end(); it++) {
    if (it->first->__data.__owner != 0) {
      it->first->__data.__owner = VIRTUAL_TO_REAL_PID(it->second);
    }
  }
}
#endif

static void
pidVirt_PostRestartRefill()
{
  VirtualPidTable::instance().readMapsFromFile(PROTECTED_PIDMAP_FD);

  dmtcp_close_protected_fd(PROTECTED_PIDMAP_FD);
  unlink(pidMapFile.c_str());
#ifdef ENABLE_PTHREAD_MUTEX_WRAPPERS
  pidVirt_RefillTid();
#endif
}

static void
pidVirt_ThreadExit(DmtcpEventData_t *data)
{
  /* This thread has finished its execution, do some cleanup on our part.
   *  erasing the original_tid entry from virtualpidtable
   *  FIXME: What if the process gets checkpointed after erase() but before the
   *  thread actually exits?
   */
  pid_t tid = dmtcp_gettid();
  VirtualPidTable::instance().erase(tid);
}

static void
pid_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_INIT:
    SharedData::setPidMap(getpid(), _real_getpid());
    break;

  case DMTCP_EVENT_ATFORK_PREPARE:
    pidVirt_atfork_prepare();
    break;

  case DMTCP_EVENT_ATFORK_CHILD:
  case DMTCP_EVENT_VFORK_CHILD:
    pidVirt_atfork_child();
    break;

  case DMTCP_EVENT_VFORK_PREPARE:
    pidVirt_vfork_prepare();
    break;

  case DMTCP_EVENT_PRE_EXEC:
    pidVirt_PrepareForExec(data);
    break;

  case DMTCP_EVENT_POST_EXEC:
    pidVirt_PostExec(data);
    break;

  case DMTCP_EVENT_PTHREAD_RETURN:
  case DMTCP_EVENT_PTHREAD_EXIT:
    pidVirt_ThreadExit(data);
    break;

  case DMTCP_EVENT_REAL_TO_VIRTUAL_PATH:
    pid_real_to_virtual_filepath(data);
    break;

  case DMTCP_EVENT_VIRTUAL_TO_REAL_PATH:
    pid_virtual_to_real_filepath(data);
    break;

  case DMTCP_EVENT_PRESUSPEND:
    break;

  case DMTCP_EVENT_PRECHECKPOINT:
    break;

  case DMTCP_EVENT_RESUME:
    break;

  case DMTCP_EVENT_RESTART:
    pidVirt_PostRestart();
    dmtcp_local_barrier("PID:RESTART");
    pidVirt_PostRestartRefill();
    restartInProgress = false;
    break;

  default:
    break;
  }
}


DmtcpPluginDescriptor_t pidPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "pid",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "PID virtualization plugin",
  pid_event_hook
};

DMTCP_DECL_PLUGIN(pidPlugin);
