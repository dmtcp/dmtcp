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

#include <signal.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include "jalloc.h"
#include "jassert.h"
#include "jconvert.h"
#include "jfilesystem.h"
#include "pluginmanager.h"
#include "config.h"
#include "dmtcp.h"
#include "glibc_pthread.h"
#include "pidwrappers.h"
#include "protectedfds.h"
#include "shareddata.h"
#include "util.h"
#include "util_assert.h"
#include "virtualpidtable.h"

static char PROC_PREFIX[] = "/proc/";

static char PROC_TASK_TOKEN[] = "/task/";
// Was more complicated C++ using constexpr and strlen.  But clang++
//  error'ed out.  So, instead we use the simpler C syntax, which is correct.
static const size_t PROC_TASK_TOKEN_LEN = sizeof(PROC_TASK_TOKEN) - 1;

using namespace dmtcp;

extern "C" pid_t dmtcp_update_ppid();
static volatile bool restartInProgress = false;

static string pidMapFile;

static vector<pid_t> *exitedChildTids = NULL;
static DmtcpMutex exitedChildTidsLock = DMTCP_MUTEX_INITIALIZER_LLL;

#ifndef USE_VIRTUAL_TID_LIBC_STRUCT_PTHREAD
#define dmtcp_pthread_set_tid(pth, tid) do {} while (0)
#endif

extern "C"
int
dmtcp_pid_is_enabled()
{
  static const int enabled =
    internalPluginEnabled(INTERNAL_PLUGIN_PID) ? 1 : 0;
  return enabled;
}

extern "C"
pid_t
dmtcp_pid_virtual_to_real(pid_t virtualPid)
{
  return dmtcp_pid_is_enabled() ?
         VirtualPidTable::instance().virtualToReal(virtualPid) : virtualPid;
}

extern "C"
pid_t
dmtcp_pid_real_to_virtual(pid_t realPid)
{
  return dmtcp_pid_is_enabled() ?
         VirtualPidTable::instance().realToVirtual(realPid) : realPid;
}

extern "C"
void
dmtcp_pid_update_mapping(pid_t virtualPid, pid_t realPid)
{
  if (!dmtcp_pid_is_enabled()) {
    return;
  }

  VirtualPidTable::instance().updateMapping(virtualPid, realPid);
  SharedData::setPidMap(virtualPid, realPid);
}

extern "C"
pid_t
dmtcp_pid_gettid()
{
  return dmtcp_pid_is_enabled() ? VirtualPidTable::gettid() : _real_gettid();
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
pid_t
dmtcp_update_virtual_to_real_tid(pid_t tid)
{
  if (!dmtcp_pid_is_enabled()) {
    return tid;
  }

  pid_t virtualTid = dmtcp_pid_real_to_virtual(tid);
  if (virtualTid == tid && _real_gettid() == _real_getpid()) {
    virtualTid = VirtualPidTable::getpid();
  }

  if (!restartInProgress) {
    restartInProgress = true;
    VirtualPidTable::instance().postRestart();
  }

  VirtualPidTable::instance().updateMapping(virtualTid, _real_gettid());

  dmtcp_pthread_set_tid(pthread_self(), virtualTid);
  return virtualTid;
}

static
void removeExitedChildTids()
{
  // First remove stale thread ids.
  pid_t realPid = _real_getpid();
  ASSERT_MUTEX_SUCCESS(DmtcpMutexLock(&exitedChildTidsLock));
  for (auto it = exitedChildTids->begin(); it != exitedChildTids->end();) {
    pid_t tid = *it;
    pid_t realTid = dmtcp_pid_virtual_to_real(tid);
    if (_real_tgkill(realPid, realTid, 0) == 0) {
      it++;
    } else {
      it = exitedChildTids->erase(it);
      VirtualPidTable::instance().erase(tid);
    }
  }
  ASSERT_MUTEX_SUCCESS(DmtcpMutexUnlock(&exitedChildTidsLock));
}

extern "C"
void dmtcp_init_virtual_tid()
{
  removeExitedChildTids();
  pid_t virtualTid = VirtualPidTable::instance().getNewVirtualTid();
  VirtualPidTable::resetTid(virtualTid);

  dmtcp_pthread_set_tid(pthread_self(), virtualTid);
}

static void
pidVirt_PrepareForExec(DmtcpEventData_t *data)
{
  pid_t virtPid = getpid();
  pid_t realPid = dmtcp_pid_virtual_to_real(virtPid);
  pid_t virtPpid = getppid();
  pid_t realPpid = dmtcp_pid_virtual_to_real(virtPpid);
  Util::setVirtualPidEnvVar(virtPid, realPid, virtPpid, realPpid);

  ASSERT_NOT_NULL(data);
  jalib::JBinarySerializeWriterRaw wr("", data->preExec.serializationFd);
  VirtualPidTable::instance().serialize(wr);
}

static void
pidVirt_PostExec(DmtcpEventData_t *data)
{
  ASSERT_NOT_NULL(data);
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

  char *tidStr = ptr + PROC_TASK_TOKEN_LEN;

  pid_t virtualTid = 0;
  size_t parsedLength = 0;
  if (Util::parseIntegerPrefix(tidStr, &virtualTid, &parsedLength) &&
      virtualTid > 0) {
    char *rest = tidStr + parsedLength;
    char buf[PATH_MAX - 20];
    strncpy(buf, rest, PATH_MAX - 20);
    pid_t realTid = dmtcp_pid_virtual_to_real(virtualTid);
    // Reserve char[20] for realTid below.
    ASSERT(20 + strlen(buf) < PATH_MAX,
           "translated /proc task path is too long: rest_len={}",
           strlen(buf));
    snprintf(tidStr, PATH_MAX, "%d%s", realTid, buf);
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
  char *pidStr = &data->virtualToRealPath.path[index];
  pid_t virtualPid = 0;
  size_t parsedLength = 0;
  if (Util::parseIntegerPrefix(pidStr, &virtualPid, &parsedLength) &&
      virtualPid > 0) {
    char *rest = pidStr + parsedLength;
    char newPath[PATH_MAX];
    pid_t realPid = dmtcp_pid_virtual_to_real(virtualPid);
    snprintf(newPath, PATH_MAX, "/proc/%d%s", realPid, rest);
    strncpy(data->virtualToRealPath.path, newPath, PATH_MAX);
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
  char *pidStr = &data->realToVirtualPath.path[index];
  pid_t realPid = 0;
  size_t parsedLength = 0;
  if (!Util::parseIntegerPrefix(pidStr, &realPid, &parsedLength) ||
      realPid <= 0) {
    return;
  }

  char *rest = pidStr + parsedLength;
  char newPath[PATH_MAX];
  pid_t virtualPid = dmtcp_pid_real_to_virtual(realPid);
  sprintf(newPath, "/proc/%d%s", virtualPid, rest);
  strncpy(data->realToVirtualPath.path, newPath, PATH_MAX);
}


static int
openSharedFile(string const& name, int flags)
{
  int fd;
  int errno_bkp;

  // try to create, truncate & open file

  dmtcp::string dir = jalib::Filesystem::DirName(name);

  JTRACE("shared file dir:")(dir);
  jalib::Filesystem::mkdir_r(dir, 0755);

  if ((fd = open(name.c_str(), O_EXCL | O_CREAT | O_TRUNC | flags, 0600)) >=
      0) {
    return fd;
  }
  errno_bkp = errno;

  if ((fd < 0) && (errno_bkp == EEXIST)) {
    errno = 0;
    if ((fd = open(name.c_str(), flags, 0600)) >= 0) {
      return fd;
    }
  }

  // unable to create & open OR open
  ASSERT_ERRNO(false, "cannot open file: path={}", name);
  return -1;
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

  ostringstream o;
  o << dmtcp_get_tmpdir() << "/dmtcpPidMap."
    << dmtcp_get_computation_id_str() << "."
    << std::hex << dmtcp_get_coordinator_timestamp();

  // Open and create pidMapFile if it doesn't exist.
  JTRACE("Open dmtcpPidMapFile")(o.str());
  pidMapFile = o.str();
  int fd = openSharedFile(pidMapFile, O_RDWR);
  ASSERT_VALID_FD_MSG(fd, "failed to open PID map file: path={}",
                      pidMapFile);

  VirtualPidTable::instance().writeMapsToFile(fd);
  dmtcp_local_barrier("PID:RESTART");
  VirtualPidTable::instance().readMapsFromFile(fd);

  close(fd);
  unlink(pidMapFile.c_str());
}

static void
pidVirt_ThreadExit(DmtcpEventData_t *data)
{
  /* This thread has finished its execution, do some cleanup on our part.
   *  erasing the original_tid entry from virtualpidtable
   *  FIXME: What if the process gets checkpointed after erase() but before the
   *  thread actually exits?
   */
  pid_t tid = VirtualPidTable::gettid();
  ASSERT_MUTEX_SUCCESS(DmtcpMutexLock(&exitedChildTidsLock));
  exitedChildTids->push_back(tid);
  ASSERT_MUTEX_SUCCESS(DmtcpMutexUnlock(&exitedChildTidsLock));
}

static void
pidVirt_ThreadResume()
{
  pid_t virtualTid = VirtualPidTable::gettid();
  VirtualPidTable::instance().updateMapping(virtualTid, _real_gettid());
  dmtcp_pthread_set_tid(pthread_self(), virtualTid);
}

static void
pid_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_INIT:
    SharedData::setPidMap(getpid(), _real_getpid());
    exitedChildTids = new vector<pid_t>();
    dmtcp_pthread_set_tid(pthread_self(), getpid());
    break;

  case DMTCP_EVENT_ATFORK_CHILD:
  case DMTCP_EVENT_VFORK_CHILD:
    VirtualPidTable::instance().resetOnFork();
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
    removeExitedChildTids();
    break;

  case DMTCP_EVENT_RESUME:
    break;

  case DMTCP_EVENT_THREAD_RESUME:
    pidVirt_ThreadResume();
    break;

  case DMTCP_EVENT_RESTART:
    pidVirt_PostRestart();
    restartInProgress = false;
    break;

  default:
    break;
  }
}


LIB_PRIVATE DmtcpPluginDescriptor_t pidPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "PID",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "PID virtualization plugin",
  pid_event_hook
};
