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

// msgrcv has confliciting return types on some systems (e.g. SLES 10)
#define msgrcv msgrcv_glibc

#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdarg.h>
#include "dmtcp.h"
#include "sysvipc.h"
#include "sysvipcwrappers.h"
#include "jassert.h"

#undef msgrcv

using namespace dmtcp;

static struct timespec ts_100ms = {0, 100 * 1000 * 1000};
/******************************************************************************
 *
 * SysV Shm Methods
 *
 *****************************************************************************/

extern "C"
int shmget(key_t key, size_t size, int shmflg)
{
  int realId = -1;
  int virtId = -1;
  DMTCP_PLUGIN_DISABLE_CKPT();
  realId = _real_shmget(key, size, shmflg);
  if (realId != -1) {
    dmtcp::SysVShm::instance().on_shmget(realId, key, size, shmflg);
    virtId = REAL_TO_VIRTUAL_SHM_ID(realId);
    JTRACE ("Creating new Shared memory segment")
      (key) (size) (shmflg) (realId) (virtId);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return virtId;
}

extern "C"
void *shmat(int shmid, const void *shmaddr, int shmflg)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int realShmid = VIRTUAL_TO_REAL_SHM_ID(shmid);
  JASSERT(realShmid != -1) .Text("Not Implemented");
  void *ret = _real_shmat(realShmid, shmaddr, shmflg);
#ifdef __arm__
  // This is arguably a bug in Linux kernel 2.6.28, 2.6.29, 3.0 - 3.2 and others
  // See:  https://bugs.kde.org/show_bug.cgi?id=222545
  //     On ARM, SHMLBA == 4*PAGE_SIZE instead of PAGESIZE
  //     So, this fails:
  //    shmaddr = shmat(shmid, NULL, 0); smdt(shmaddr); shmat(shmid, shaddr, 0);
  //       when shmaddr % 0x4000 != 0 (when shmaddr not multiple of SMLBA)
  // Workaround for bug in Linux kernel for ARM follows.
  // WHEN KERNEL FIX IS AVAILABLE, DO THIS ONLY FOR BUGGY KERNEL VERSIONS.
  if (((long)ret % 0x4000 != 0) && (ret != (void *)-1)) { // if ret%SHMLBA != 0
    void *ret_addr[20];
    unsigned int i;
    for (i = 0; i < sizeof(ret_addr) / sizeof(ret_addr[0]) ; i++) {
      ret_addr[i] = ret; // Save bad address for detaching later
      ret = _real_shmat(realShmid, shmaddr, shmflg); // Try again
      // if ret % SHMLBA == 0 { ... }
      if (((long)ret % 0x4000 == 0) || (ret == (void *)-1))
        break; // Good address (or error return)
    }
    // Detach all the bad addresses athat are not SHMLBA-aligned.
    if (i < sizeof(ret_addr) / sizeof(ret_addr[0]))
      for (unsigned int j = 0; j < i+1; j++)
        _real_shmdt( ret_addr[j] );
    JASSERT((long)ret % 0x4000 == 0)
      (shmaddr) (shmflg) (getpid())
      .Text ("Failed to get SHMLBA-aligned address after 20 tries");
  }
#endif

  if (ret != (void *) -1) {
    dmtcp::SysVShm::instance().on_shmat(shmid, shmaddr, shmflg, ret);
    JTRACE ("Mapping Shared memory segment") (shmid) (realShmid) (shmflg) (ret);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

extern "C"
int shmdt(const void *shmaddr)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int ret = _real_shmdt(shmaddr);
  if (ret != -1) {
    dmtcp::SysVShm::instance().on_shmdt(shmaddr);
    JTRACE ("Unmapping Shared memory segment" ) (shmaddr);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

extern "C"
int shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int realShmid = VIRTUAL_TO_REAL_SHM_ID(shmid);
  JASSERT(realShmid != -1);
  int ret = _real_shmctl(realShmid, cmd, buf);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}

/******************************************************************************
 *
 * SysV Semaphore Methods
 *
 *****************************************************************************/

extern "C"
int semget(key_t key, int nsems, int semflg)
{
  int realId = -1;
  int virtId = -1;
  DMTCP_PLUGIN_DISABLE_CKPT();
  realId = _real_semget (key, nsems, semflg);
  if (realId != -1) {
    dmtcp::SysVSem::instance().on_semget(realId, key, nsems, semflg);
    virtId = REAL_TO_VIRTUAL_SEM_ID(realId);
    JTRACE ("Creating new SysV Semaphore" ) (key) (nsems) (semflg);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return virtId;
}

extern "C"
int semop(int semid, struct sembuf *sops, size_t nsops)
{
  return semtimedop(semid, sops, nsops, NULL);
}

extern "C"
int semtimedop(int semid, struct sembuf *sops, size_t nsops,
               const struct timespec *timeout)
{
  struct timespec totaltime = {0, 0};
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
    DMTCP_PLUGIN_DISABLE_CKPT();
    realId = VIRTUAL_TO_REAL_SEM_ID(semid);
    JASSERT(realId != -1);
    ret = _real_semtimedop(realId, sops, nsops, timeout);
    if (ret == 0) {
      dmtcp::SysVSem::instance().on_semop(semid, sops, nsops);
    }
    DMTCP_PLUGIN_ENABLE_CKPT();
    return ret;
  }

  /*
   * We continue to call pthread_tryjoin_np (and sleep) until we have gone past
   * the abstime provided by the caller
   */
  while (timeout == NULL || TIMESPEC_CMP(&totaltime, timeout, <)) {
    ret = EAGAIN;
    DMTCP_PLUGIN_DISABLE_CKPT();
    realId = VIRTUAL_TO_REAL_SEM_ID(semid);
    JASSERT(realId != -1);
    ret = _real_semtimedop(realId, sops, nsops, &ts_100ms);
    if (ret == 0) {
      dmtcp::SysVSem::instance().on_semop(semid, sops, nsops);
    }
    DMTCP_PLUGIN_ENABLE_CKPT();

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
int semctl(int semid, int semnum, int cmd, ...)
{
  union semun uarg;
  va_list arg;
  va_start (arg, cmd);
  uarg = va_arg (arg, union semun);
  va_end (arg);

  DMTCP_PLUGIN_DISABLE_CKPT();
  int realId = VIRTUAL_TO_REAL_SEM_ID(semid);
  JASSERT(realId != -1);
  int ret = _real_semctl(realId, semnum, cmd, uarg);
  if (ret != -1) {
    dmtcp::SysVSem::instance().on_semctl(semid, semnum, cmd, uarg);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}
/******************************************************************************
 *
 * SysV Msg Queue Methods
 *
 *****************************************************************************/

extern "C"
int msgget(key_t key, int msgflg)
{
  int realId = -1;
  int virtId = -1;
  DMTCP_PLUGIN_DISABLE_CKPT();
  realId = _real_msgget (key, msgflg);
  if (realId != -1) {
    dmtcp::SysVMsq::instance().on_msgget(realId, key, msgflg);
    virtId = REAL_TO_VIRTUAL_MSQ_ID(realId);
    JTRACE ("Creating new SysV Msg Queue" ) (key) (msgflg);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return virtId;
}

extern "C"
int msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg)
{
  int ret;
  int realId;

  /*
   * We continue to call msgsnd with IPC_NOWAIT (and sleep) until we succeed
   * or fail with something other than EAGAIN
   * If IPC_NOWAIT was specified and msgsnd fails with EAGAIN, return.
   */
  while (true) {
    DMTCP_PLUGIN_DISABLE_CKPT();
    realId = VIRTUAL_TO_REAL_MSQ_ID(msqid);
    JASSERT(realId != -1);
    ret = _real_msgsnd(realId, msgp, msgsz, msgflg | IPC_NOWAIT);
    if (ret == 0) {
      dmtcp::SysVMsq::instance().on_msgsnd(msqid, msgp, msgsz, msgflg);
    }
    DMTCP_PLUGIN_ENABLE_CKPT();

    // TODO Handle EIDRM
    if ((ret == 0) ||
        (ret == -1 && errno != EAGAIN) ||
        (msgflg & IPC_NOWAIT)) {
      return ret;
    }

    nanosleep(&ts_100ms, NULL);
  }
  JASSERT(false) .Text("Not Reached");
  return -1;
}

extern "C"
ssize_t msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg)
{
  int ret;
  int realId;

  /*
   * We continue to call msgrcv with IPC_NOWAIT (and sleep) until we succeed
   * or fail with something other than EAGAIN
   * If IPC_NOWAIT was specified and msgsnd fails with EAGAIN, return.
   */
  while (true) {
    DMTCP_PLUGIN_DISABLE_CKPT();
    realId = VIRTUAL_TO_REAL_MSQ_ID(msqid);
    JASSERT(realId != -1);
    ret = _real_msgrcv(realId, msgp, msgsz, msgtyp, msgflg | IPC_NOWAIT);
    if (ret == 0) {
      dmtcp::SysVMsq::instance().on_msgrcv(msqid, msgp, msgsz, msgtyp, msgflg);
    }
    DMTCP_PLUGIN_ENABLE_CKPT();

    // TODO Handle EIDRM
    if ((ret >= 0) ||
        (ret == -1 && errno != ENOMSG) ||
        (msgflg & IPC_NOWAIT)) {
      return ret;
    }

    nanosleep(&ts_100ms, NULL);
  }
  JASSERT(false) .Text("Not Reached");
  return -1;
}


extern "C"
int msgctl(int msqid, int cmd, struct msqid_ds *buf)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int realId = VIRTUAL_TO_REAL_MSQ_ID(msqid);
  JASSERT(realId != -1);
  int ret = _real_msgctl(realId, cmd, buf);
  if (ret != -1) {
    dmtcp::SysVMsq::instance().on_msgctl(msqid, cmd, buf);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return ret;
}
