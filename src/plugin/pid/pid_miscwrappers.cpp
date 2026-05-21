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

#include <semaphore.h>
#include <signal.h>  // needed for SIGEV_THREAD_ID
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <linux/version.h>

#include "jassert.h"
#include "jconvert.h"
#include "jfilesystem.h"
#include "config.h"  // for HAS_CMA
#include "dmtcp.h"
#include "pid.h"
#include "pidwrappers.h"
#include "procselfmaps.h"
#include "shareddata.h"
#include "util.h"
#include "virtualpidtable.h"
#include "glibc_pthread.h"

using namespace dmtcp;

static pid_t childVirtualPid;

static pid_t vfork_saved_virt_pid;
static pid_t vfork_saved_real_pid;
static pid_t vfork_saved_ppid;
static pid_t vfork_saved_tid;

LIB_PRIVATE void
pidVirt_atfork_prepare()
{
  Util::getVirtualPidFromEnvVar(&childVirtualPid, NULL, NULL, NULL);
}

LIB_PRIVATE void
pidVirt_atfork_child()
{
  VirtualPidTable::instance().resetOnFork();
}


static bool
pidVirt_disabledByEnv()
{
  const char *disableAll = getenv("DMTCP_DISABLE_ALL_PLUGINS");
  return disableAll != NULL && strcmp(disableAll, "1") == 0;
}

extern "C" pid_t
dmtcp_pid_virtualize_child_pid(pid_t realPid)
{
  if (pidVirt_disabledByEnv()) {
    return realPid;
  }

  if (realPid > 0) { /* Parent Process */
    VirtualPidTable::instance().updateMapping(childVirtualPid, realPid);
    SharedData::setPidMap(childVirtualPid, realPid);
    return childVirtualPid;
  }

  return realPid;
}

extern "C" pid_t
dmtcp_pid_get_virtual_tid(void)
{
  if (pidVirt_disabledByEnv()) {
    return pid_real_gettid();
  }

  return VirtualPidTable::gettid();
}

extern "C" void
dmtcp_pid_erase_virtual_pid(pid_t virtualPid)
{
  if (!pidVirt_disabledByEnv()) {
    VirtualPidTable::instance().erase(virtualPid);
  }
}

extern VirtualPidTable *virtPidTableInst;
VirtualPidTable *vfork_saved_virtPidTableInst = nullptr;

LIB_PRIVATE void
pidVirt_vfork_prepare()
{
  vfork_saved_virt_pid = getpid();
  vfork_saved_real_pid = VIRTUAL_TO_REAL_PID(getpid());
  vfork_saved_ppid = getppid();
  vfork_saved_tid = VirtualPidTable::gettid();
  if (vfork_saved_virtPidTableInst == nullptr) {
    vfork_saved_virtPidTableInst = new VirtualPidTable(*virtPidTableInst);
  } else {
    *vfork_saved_virtPidTableInst = *virtPidTableInst;
  }

  Util::getVirtualPidFromEnvVar(&childVirtualPid, NULL, NULL, NULL);
}

static pid_t vforkPid = 0;
static char* stackStart;
static size_t stackSize;
static void* newStackAddr;

LIB_PRIVATE pid_t
pidVirt_vfork()
{
  char dummy = 0;

  static __typeof__(&vfork) vforkPtr =
    (__typeof__(&vfork)) dmtcp_dlsym(RTLD_NEXT, "vfork");

  // Save the contents of the current call frame before calling libc:vfork. The
  // vfork syscall won't return in the parent until the child process has either
  // exited or exec'd. In both cases, the current call frame on stack will
  // most-likely get overwritten by the child process. (The call frames below
  // the current call-frame are expected to be safe as the child process is not
  // expected to return from the current call frame).
  // Failure to restore the current call frame in the parent might result in
  // lost $RBP data that include the return address.
  //
  // NOTE: The vfork wrapper in execwrappers.cpp performs similar save/restore
  // of the current call frame. This duplication is required in case we decide
  // to not use the PID plugin.
  //
  // TODO: Deduplicate stack save/restore with similar code in
  // execwrappers.cpp's vfork wrapper.
  stackStart = &dummy;
  // Return address is stored at $rbp + sizeof(void*)
  stackSize =
    (char*) __builtin_frame_address(0) + (2 * sizeof(void*)) - stackStart;
  newStackAddr = JALLOC_MALLOC(stackSize);
  JASSERT(newStackAddr);
  memcpy(newStackAddr, stackStart, stackSize);

  vforkPid = vforkPtr();

  // Restore parent stack.
  if (vforkPid > 0) {
    memcpy(stackStart, newStackAddr, stackSize);
    JALLOC_FREE(newStackAddr);
  }

  if (vforkPid == 0) {
    pidVirt_atfork_child();
  } else { /* Parent Process */
    *virtPidTableInst = *vfork_saved_virtPidTableInst;

    Util::setVirtualPidEnvVar(vfork_saved_virt_pid,
                              vfork_saved_real_pid,
                              vfork_saved_ppid,
                              pid_real_getppid());
    VirtualPidTable::resetPidPpid();
    VirtualPidTable::resetTid(getpid());

    if (vforkPid > 0) {
      VirtualPidTable::instance().updateMapping(childVirtualPid, vforkPid);
      SharedData::setPidMap(childVirtualPid, vforkPid);
      return childVirtualPid;
    }

    munmap(newStackAddr, stackSize);
  }

  return vforkPid;
}

// Wait/fcntl/syscall wrappers are composed in src/miscwrappers.cpp and
// src/wrappers.cpp.  Timer, SysV IPC, and POSIX mqueue PID translations are
// now composed in their built-in owners, leaving this file with only
// non-duplicated PID helper wrappers.

#ifdef HAS_CMA
EXTERNC
ssize_t
process_vm_readv(pid_t pid,
                 const struct iovec *local_iov,
                 unsigned long liovcnt,
                 const struct iovec *remote_iov,
                 unsigned long riovcnt,
                 unsigned long flags)
{
  pid_t realPid;
  ssize_t ret;

  DMTCP_PLUGIN_DISABLE_CKPT();
  realPid = VIRTUAL_TO_REAL_PID(pid);
  ret = pid_real_process_vm_readv(realPid, local_iov, liovcnt,
                               remote_iov, riovcnt, flags);
  DMTCP_PLUGIN_ENABLE_CKPT();

  return ret;
}

EXTERNC
ssize_t
process_vm_writev(pid_t pid,
                  const struct iovec *local_iov,
                  unsigned long liovcnt,
                  const struct iovec *remote_iov,
                  unsigned long riovcnt,
                  unsigned long flags)
{
  pid_t realPid;
  ssize_t ret;

  DMTCP_PLUGIN_DISABLE_CKPT();
  realPid = VIRTUAL_TO_REAL_PID(pid);
  ret = pid_real_process_vm_writev(realPid, local_iov, liovcnt,
                                remote_iov, riovcnt, flags);
  DMTCP_PLUGIN_ENABLE_CKPT();

  return ret;
}
#endif // ifdef HAS_CMA

#ifdef USE_VIRTUAL_TID_LIBC_STRUCT_PTHREAD
/* Unfortunately the kernel headers do not export the TASK_COMM_LEN
   macro.  So we have to define it here.  */
#define TASK_COMM_LEN 16
#define TASK_COMM_FMT "/proc/self/task/%u/comm"
#define TASK_COMM_MAX_LEN strlen("/proc/self/task/18446744073709551615/comm")
extern "C" int
pthread_getname_np (pthread_t th, char *buf, size_t len)
{
  if (th == pthread_self()) {
    return prctl (PR_GET_NAME, buf) ? errno : 0;
  }

  pid_t tid = dmtcp_pthread_get_tid(th);

  // Copied from glibc.
  if (len < TASK_COMM_LEN)
    return ERANGE;

  char fname[TASK_COMM_MAX_LEN + 1];
  snprintf(fname, sizeof(fname), TASK_COMM_FMT, (unsigned int)tid);

  int fd = open(fname, O_RDONLY);
  if (fd == -1) {
    return errno;
  }

  int res = 0;
  ssize_t n = Util::readAll(fd, buf, len);
  if (n < 0) {
    res = errno;
  } else {
    if (buf[n - 1] == '\n') {
      buf[n - 1] = '\0';
    } else if ((size_t) n == len) {
      res = ERANGE;
    } else {
      buf[n] = '\0';
    }
  }

  close(fd);
  return res;
}

extern "C" int
pthread_setname_np (pthread_t th, const char *name)
{
  /* Unfortunately the kernel headers do not export the TASK_COMM_LEN
     macro.  So we have to define it here.  */
  size_t name_len = strlen (name);
  if (name_len >= TASK_COMM_LEN)
    return ERANGE;

  if (th == pthread_self()) {
    return prctl (PR_SET_NAME, name) ? errno : 0;
  }

  pid_t tid = dmtcp_pthread_get_tid(th);

  char fname[TASK_COMM_MAX_LEN + 1];
  snprintf(fname, sizeof(fname), TASK_COMM_FMT, (unsigned int)tid);

  int fd = open(fname, O_RDWR);
  if (fd == -1) {
    return errno;
  }

  int res = 0;
  ssize_t n = Util::writeAll(fd, name, name_len);
  if (n < 0) {
    res = errno;
  } else if ((size_t) n != name_len) {
    res = EIO;
  }

  close(fd);

  return res;
}

#endif // #ifdef USE_VIRTUAL_TID_LIBC_STRUCT_PTHREAD
