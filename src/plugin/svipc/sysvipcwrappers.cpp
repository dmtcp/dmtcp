/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

// msgrcv has conflicting return types on some systems (e.g. SLES 10)
// So, we temporarily rename it so that type declarations are not for msgrcv.
#define msgrcv msgrcv_glibc

#include <errno.h>
#include <stdarg.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>

#undef msgrcv

#include "pluginmanager.h"
#include "dmtcp.h"
#include "plugin/pid/pidhelpers.h"
#include "sysvipc.h"
#include "sysvipcwrappers.h"
#include "dmtcp_assert.h"
#include "wrapperlock.h"

using namespace dmtcp;

static struct timespec ts_100ms = { 0, 100 * 1000 * 1000 };

static bool
semctlCmdRequiresArg(int cmd)
{
  switch (cmd) {
  case IPC_STAT:
  case IPC_SET:
  case IPC_INFO:
  case SEM_INFO:
  case GETALL:
  case SETALL:
  case SETVAL:
#ifdef SEM_STAT
  case SEM_STAT:
#endif
#ifdef SEM_STAT_ANY
  case SEM_STAT_ANY:
#endif
    return true;
  default:
    return false;
  }
}

static bool
semctlCmdUsesIndex(int cmd)
{
  switch (cmd) {
#ifdef SEM_STAT
  case SEM_STAT:
#endif
#ifdef SEM_STAT_ANY
  case SEM_STAT_ANY:
#endif
    return true;
  default:
    return false;
  }
}

static bool
msgctlCmdUsesIndex(int cmd)
{
  switch (cmd) {
#ifdef MSG_STAT
  case MSG_STAT:
#endif
#ifdef MSG_STAT_ANY
  case MSG_STAT_ANY:
#endif
    return true;
  default:
    return false;
  }
}

static int
unsupportedIndexStatResult()
{
  errno = EINVAL;
  return -1;
}

static int
virtualizeShmIndexStatResult(int realId, struct shmid_ds *buf)
{
  int virtId = REAL_TO_VIRTUAL_SHM_ID(realId);
  if (virtId != -1) {
    return virtId;
  }

  if (buf == NULL) {
    return unsupportedIndexStatResult();
  }

  key_t key = buf->shm_perm.__key;
  SysVShm::instance().on_shmget(realId, key, key, buf->shm_segsz,
                                buf->shm_perm.mode);
  virtId = REAL_TO_VIRTUAL_SHM_ID(realId);
  return virtId != -1 ? virtId : unsupportedIndexStatResult();
}

static int
virtualizeSemIndexStatResult(int realId, union semun arg)
{
  int virtId = REAL_TO_VIRTUAL_SEM_ID(realId);
  if (virtId != -1) {
    return virtId;
  }

  if (arg.buf == NULL) {
    return unsupportedIndexStatResult();
  }

  SysVSem::instance().on_semget(realId, arg.buf->sem_perm.__key,
                                arg.buf->sem_nsems,
                                arg.buf->sem_perm.mode);
  virtId = REAL_TO_VIRTUAL_SEM_ID(realId);
  return virtId != -1 ? virtId : unsupportedIndexStatResult();
}

static int
virtualizeMsgIndexStatResult(int realId, struct msqid_ds *buf)
{
  int virtId = REAL_TO_VIRTUAL_MSQ_ID(realId);
  if (virtId != -1) {
    return virtId;
  }

  if (buf == NULL) {
    return unsupportedIndexStatResult();
  }

  SysVMsq::instance().on_msgget(realId, buf->msg_perm.__key,
                                buf->msg_perm.mode);
  virtId = REAL_TO_VIRTUAL_MSQ_ID(realId);
  return virtId != -1 ? virtId : unsupportedIndexStatResult();
}

static int
virtualizeSemctlPidResult(int cmd, int ret)
{
  if (ret != -1 && cmd == GETPID) {
    return dmtcp_pid_real_to_virtual(ret);
  }
  return ret;
}

static void
virtualizeMsgPidFields(int ret, int cmd, struct msqid_ds *buf)
{
  if (ret == -1 || buf == NULL) {
    return;
  }

  switch (cmd) {
  case IPC_STAT:
#ifdef MSG_STAT
  case MSG_STAT:
#endif
#ifdef MSG_STAT_ANY
  case MSG_STAT_ANY:
#endif
    buf->msg_lspid = dmtcp_pid_real_to_virtual(buf->msg_lspid);
    buf->msg_lrpid = dmtcp_pid_real_to_virtual(buf->msg_lrpid);
    return;
  default:
    return;
  }
}

/*
 * In Open MPI 2.0, shmdt() is intercepted by modifying libraries' global
 * offset table, meaning that _real_shmdt() will be redirected into
 * OpenMPI's hook function, instead of libc's shmdt(). The hook function
 * finally calls syscall() with the corresponding syscall number. The
 * inside_shmdt variable indicates if the code is inside our shmdt()
 * wrapper. If so, our syscall wrapper simply calls _real_syscall(),
 * avoiding the recursive call to the shmdt() wrapper. See the wrapper
 * of syscall() in miscwrappers.cpp.

 * FIXME: for the long term, we need to think about the case where user
 * code modifies its own global offset table.
 */
static __thread bool inside_shmdt ATTR_TLS_INITIAL_EXEC = false;

/******************************************************************************
 *
 * SysV Shm Methods
 *
 *****************************************************************************/

extern "C"
int
shmget(key_t key, size_t size, int shmflg)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SVIPC)) {
    return _real_shmget(key, size, shmflg);
  }

  int realId = -1;
  int virtId = -1;

  WrapperLock wrapperLock;

  // If multiple clients try to simultaneously create shm regions with
  // (IPC_PRIVATE | IPC_EXCL), there is a race condition that can cause the
  // shmget call to fail.
  //
  // The code relies on the `VIRTUAL_TO_REAL_SHM_KEY` API, which, in turn,
  // uses the DMTCP shared data area to share and assign unique keys for SysV
  // shm regions. The API returns -1 if the key does not exist in the DMTCP
  // mapping. Now, if one client raced ahead and managed to create the region,
  // it'd end up populating the DMTCP shared area with a "IPC_PRIVATE ->
  // realKey" mapping. Later, when the other client would try to create its
  // own shared memory region, the `VIRTUAL_TO_REAL_SHM_KEY` API would return
  // the key corresponding the earlier created entry and the `shmget(IPC_EXCL)`
  // call would fail with an `EEXIST` error.
  //
  // Therefore, we detect this special case of `IPC_PRIVATE` and use
  // a process's real pid to create the shared memory region. This also
  // preserves the semantics of `IPC_PRIVATE`

  key_t realKey = VIRTUAL_TO_REAL_SHM_KEY(key);

  realId = _real_shmget(realKey, size, shmflg);
  if (realId != -1) {
    SysVShm::instance().on_shmget(realId, realKey, key, size, shmflg);
    virtId = REAL_TO_VIRTUAL_SHM_ID(realId);
    TRACE("Created SysV shared-memory segment: key={} size={} flags={} "
          "real_shmid={} virt_shmid={}",
          key, size, shmflg, realId, virtId);
  }
  return virtId;
}

extern "C"
void *shmat(int shmid, const void *shmaddr, int shmflg)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SVIPC)) {
    return _real_shmat(shmid, shmaddr, shmflg);
  }

  WrapperLock wrapperLock;
  int realShmid = VIRTUAL_TO_REAL_SHM_ID(shmid);
  ASSERT(realShmid != -1, "shmat virtual id has no real mapping: shmid={}",
         shmid);
  void *ret = _real_shmat(realShmid, shmaddr, shmflg);
#ifdef __arm__

  // This is arguably a bug in Linux kernel 2.6.28, 2.6.29, 3.0 - 3.2 and others
  // See:  https://bugs.kde.org/show_bug.cgi?id=222545
  // On ARM, SHMLBA == 4*PAGE_SIZE instead of PAGESIZE
  // So, this fails:
  // shmaddr = shmat(shmid, NULL, 0); smdt(shmaddr); shmat(shmid, shaddr, 0);
  // when shmaddr % 0x4000 != 0 (when shmaddr not multiple of SMLBA)
  // Workaround for bug in Linux kernel for ARM follows.
  // WHEN KERNEL FIX IS AVAILABLE, DO THIS ONLY FOR BUGGY KERNEL VERSIONS.
  if (((long)ret % 0x4000 != 0) && (ret != (void *)-1)) { // if ret%SHMLBA != 0
    void *ret_addr[20];
    unsigned int i;
    for (i = 0; i < sizeof(ret_addr) / sizeof(ret_addr[0]); i++) {
      ret_addr[i] = ret; // Save bad address for detaching later
      ret = _real_shmat(realShmid, shmaddr, shmflg); // Try again
      // if ret % SHMLBA == 0 { ... }
      if (((long)ret % 0x4000 == 0) || (ret == (void *)-1)) {
        break; // Good address (or error return)
      }
    }

    // Detach all the bad addresses that are not SHMLBA-aligned.
    if (i < sizeof(ret_addr) / sizeof(ret_addr[0])) {
      for (unsigned int j = 0; j < i + 1; j++) {
        _real_shmdt(ret_addr[j]);
      }
    }
    ASSERT((long)ret % 0x4000 == 0,
           "failed to get SHMLBA-aligned address after 20 tries: "
           "shmaddr={} shmflg={} pid={}",
           shmaddr, shmflg, getpid());
  }
#endif // ifdef __arm__

  if (ret != (void *)-1) {
    SysVShm::instance().on_shmat(shmid, shmaddr, shmflg, ret);
    TRACE("Attached SysV shared-memory segment: virt_shmid={} "
          "real_shmid={} flags={} addr={}",
          shmid, realShmid, shmflg, ret);
  }
  return ret;
}

EXTERNC bool
dmtcp_svipc_inside_shmdt()
{
  return inside_shmdt;
}

extern "C"
int
shmdt(const void *shmaddr)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SVIPC)) {
    inside_shmdt = true;
    int ret = _real_shmdt(shmaddr);
    inside_shmdt = false;
    return ret;
  }

  WrapperLock wrapperLock;
  inside_shmdt = true;
  int ret = _real_shmdt(shmaddr);
  if (ret != -1) {
    SysVShm::instance().on_shmdt(shmaddr);
    TRACE("Detached SysV shared-memory segment: addr={}", shmaddr);
  }
  inside_shmdt = false;
  return ret;
}

extern "C"
int
shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SVIPC)) {
    int ret = _real_shmctl(shmid, cmd, buf);
    if (ret != -1 && buf != NULL) {
      switch (cmd) {
      case IPC_STAT:
#ifdef SHM_STAT
      case SHM_STAT:
#endif
#ifdef SHM_STAT_ANY
      case SHM_STAT_ANY:
#endif
        buf->shm_cpid = dmtcp_pid_real_to_virtual(buf->shm_cpid);
        buf->shm_lpid = dmtcp_pid_real_to_virtual(buf->shm_lpid);
        break;
      default:
        break;
      }
    }
    return ret;
  }

  WrapperLock wrapperLock;
  switch (cmd) {
#ifdef SHM_STAT
  case SHM_STAT:
#endif
#ifdef SHM_STAT_ANY
  case SHM_STAT_ANY:
#endif
  {
    int realId = _real_shmctl(shmid, cmd, buf);
    if (realId == -1) {
      return -1;
    }
    if (buf != NULL) {
      buf->shm_cpid = dmtcp_pid_real_to_virtual(buf->shm_cpid);
      buf->shm_lpid = dmtcp_pid_real_to_virtual(buf->shm_lpid);
    }
    return virtualizeShmIndexStatResult(realId, buf);
  }
  default:
    break;
  }

  int realShmid = VIRTUAL_TO_REAL_SHM_ID(shmid);
  ASSERT(realShmid != -1, "shmctl virtual id has no real mapping: shmid={}",
         shmid);
  int ret = _real_shmctl(realShmid, cmd, buf);
  if (ret != -1 && buf != NULL && cmd == IPC_STAT) {
    buf->shm_cpid = dmtcp_pid_real_to_virtual(buf->shm_cpid);
    buf->shm_lpid = dmtcp_pid_real_to_virtual(buf->shm_lpid);
  }
  return ret;
}

/******************************************************************************
 *
 * SysV Semaphore Methods
 *
 *****************************************************************************/

extern "C"
int
semget(key_t key, int nsems, int semflg)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SVIPC)) {
    return _real_semget(key, nsems, semflg);
  }

  int realId = -1;
  int virtId = -1;

  WrapperLock wrapperLock;
  realId = _real_semget(key, nsems, semflg);
  if (realId != -1) {
    SysVSem::instance().on_semget(realId, key, nsems, semflg);
    virtId = REAL_TO_VIRTUAL_SEM_ID(realId);
    TRACE("Created SysV semaphore set: key={} sem_count={} flags={}",
          key, nsems, semflg);
  }
  return virtId;
}

extern "C"
int
semop(int semid, struct sembuf *sops, size_t nsops)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SVIPC)) {
    return _real_semop(semid, sops, nsops);
  }

  return semtimedop(semid, sops, nsops, NULL);
}

extern "C"
int
semtimedop(int semid,
           struct sembuf *sops,
           size_t nsops,
           const struct timespec *timeout)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SVIPC)) {
    return _real_semtimedop(semid, sops, nsops, timeout);
  }

  struct timespec totaltime = { 0, 0 };
  int ret;
  int realId;
  bool ipc_nowait_specified = false;

  for (size_t i = 0; i < nsops; i++) {
    if (sops[i].sem_flg & IPC_NOWAIT) {
      ipc_nowait_specified = true;
      break;
    }
  }

  if (ipc_nowait_specified ||
      (timeout != NULL && TIMESPEC_CMP(timeout, &ts_100ms, <))) {
    WrapperLock wrapperLock;
    realId = VIRTUAL_TO_REAL_SEM_ID(semid);
    ASSERT(realId != -1,
           "semtimedop virtual id has no real mapping: semid={}", semid);
    ret = _real_semtimedop(realId, sops, nsops, timeout);
    if (ret == 0) {
      SysVSem::instance().on_semop(semid, sops, nsops);
    }
    return ret;
  }

  /*
   * We continue to call pthread_tryjoin_np (and sleep) until we have gone past
   * the abstime provided by the caller
   */
  while (timeout == NULL || TIMESPEC_CMP(&totaltime, timeout, <)) {
    ret = EAGAIN;
    {
      WrapperLock wrapperLock;
      realId = VIRTUAL_TO_REAL_SEM_ID(semid);
      ASSERT(realId != -1,
             "semtimedop virtual id has no real mapping: semid={}", semid);
      ret = _real_semtimedop(realId, sops, nsops, &ts_100ms);
      if (ret == 0) {
        SysVSem::instance().on_semop(semid, sops, nsops);
      }
    }

    // TODO Handle EIDRM
    if (ret == 0 ||
        (ret == -1 && errno != EAGAIN)) {
      return ret;
    }

    TIMESPEC_ADD(&totaltime, &ts_100ms, &totaltime);
  }
  errno = EAGAIN;
  return -1;
}

extern "C"
int
semctl(int semid, int semnum, int cmd, ...)
{
  union semun uarg = { 0 };
  va_list arg;

  if (semctlCmdRequiresArg(cmd)) {
    va_start(arg, cmd);
    uarg = va_arg(arg, union semun);
    va_end(arg);
  }
  int ret = -1;

  if (!internalPluginEnabled(INTERNAL_PLUGIN_SVIPC)) {
    return virtualizeSemctlPidResult(cmd,
                                     _real_semctl(semid, semnum, cmd, uarg));
  }

  if (cmd == SEM_INFO || cmd == IPC_INFO) {
    return _real_semctl(semid, semnum, cmd, uarg);
  }

  WrapperLock wrapperLock;
  if (semctlCmdUsesIndex(cmd)) {
    ret = _real_semctl(semid, semnum, cmd, uarg);
    if (ret == -1) {
      return -1;
    }
    return virtualizeSemctlPidResult(cmd, virtualizeSemIndexStatResult(ret,
                                                                       uarg));
  }

  int realId = VIRTUAL_TO_REAL_SEM_ID(semid);
  ASSERT(realId != -1,
         "semctl virtual id has no real mapping: semid={} semnum={} cmd={}",
         semid, semnum, cmd);
  ret = _real_semctl(realId, semnum, cmd, uarg);
  if (ret != -1) {
    SysVSem::instance().on_semctl(semid, semnum, cmd, uarg);
  }

  return virtualizeSemctlPidResult(cmd, ret);
}

/******************************************************************************
 *
 * SysV Msg Queue Methods
 *
 *****************************************************************************/

extern "C"
int
msgget(key_t key, int msgflg)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SVIPC)) {
    return _real_msgget(key, msgflg);
  }

  int realId = -1;
  int virtId = -1;

  WrapperLock wrapperLock;
  realId = _real_msgget(key, msgflg);
  if (realId != -1) {
    SysVMsq::instance().on_msgget(realId, key, msgflg);
    virtId = REAL_TO_VIRTUAL_MSQ_ID(realId);
    TRACE("Created SysV message queue: key={} flags={}",
          key, msgflg);
  }
  return virtId;
}

extern "C"
int
msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SVIPC)) {
    return _real_msgsnd(msqid, msgp, msgsz, msgflg);
  }

  int ret;
  int realId;

  /*
   * We continue to call msgsnd with IPC_NOWAIT (and sleep) until we succeed
   * or fail with something other than EAGAIN
   * If IPC_NOWAIT was specified and msgsnd fails with EAGAIN, return.
   */
  while (true) {
    {
      WrapperLock wrapperLock;
      realId = VIRTUAL_TO_REAL_MSQ_ID(msqid);
      ASSERT(realId != -1,
             "msgsnd virtual id has no real mapping: msqid={}", msqid);
      ret = _real_msgsnd(realId, msgp, msgsz, msgflg | IPC_NOWAIT);
      if (ret == 0) {
        SysVMsq::instance().on_msgsnd(msqid, msgp, msgsz, msgflg);
      }
    }

    // TODO Handle EIDRM
    if ((ret == 0) ||
        (ret == -1 && errno != EAGAIN) ||
        (msgflg & IPC_NOWAIT)) {
      return ret;
    }

    nanosleep(&ts_100ms, NULL);
  }
  ASSERT(false, "unreachable after msgsnd retry loop");
  return -1;
}

extern "C"
ssize_t
msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SVIPC)) {
    return _real_msgrcv(msqid, msgp, msgsz, msgtyp, msgflg);
  }

  ssize_t ret;
  int realId;

  /*
   * We continue to call msgrcv with IPC_NOWAIT (and sleep) until we succeed
   * or fail with something other than EAGAIN
   * If IPC_NOWAIT was specified and msgsnd fails with EAGAIN, return.
   */
  while (true) {
    {
      WrapperLock wrapperLock;
      realId = VIRTUAL_TO_REAL_MSQ_ID(msqid);
      ASSERT(realId != -1,
             "msgrcv virtual id has no real mapping: msqid={}", msqid);
      ret = _real_msgrcv(realId, msgp, msgsz, msgtyp, msgflg | IPC_NOWAIT);
      if (ret >= 0) {
        SysVMsq::instance().on_msgrcv(msqid, msgp, msgsz, msgtyp, msgflg);
      }
    }

    // TODO Handle EIDRM
    if ((ret >= 0) ||
        (ret == -1 && errno != ENOMSG) ||
        (msgflg & IPC_NOWAIT)) {
      return ret;
    }

    nanosleep(&ts_100ms, NULL);
  }
  ASSERT(false, "unreachable after msgrcv retry loop");
  return -1;
}

extern "C"
int
msgctl(int msqid, int cmd, struct msqid_ds *buf)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_SVIPC)) {
    int ret = _real_msgctl(msqid, cmd, buf);
    virtualizeMsgPidFields(ret, cmd, buf);
    return ret;
  }

  WrapperLock wrapperLock;
  if (msgctlCmdUsesIndex(cmd)) {
    int realId = _real_msgctl(msqid, cmd, buf);
    if (realId == -1) {
      return -1;
    }
    virtualizeMsgPidFields(realId, cmd, buf);
    return virtualizeMsgIndexStatResult(realId, buf);
  }

  int realId = VIRTUAL_TO_REAL_MSQ_ID(msqid);
  ASSERT(realId != -1, "msgctl virtual id has no real mapping: msqid={}",
         msqid);
  int ret = _real_msgctl(realId, cmd, buf);
  if (ret != -1) {
    SysVMsq::instance().on_msgctl(msqid, cmd, buf);
  }
  virtualizeMsgPidFields(ret, cmd, buf);
  return ret;
}
