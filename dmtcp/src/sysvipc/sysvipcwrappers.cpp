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

#include <sys/ipc.h>
#include <sys/shm.h>
#include "sysvipc.h"
#include "threadsync.h"
#include "../../jalib/jassert.h"

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
  WRAPPER_EXECUTION_DISABLE_CKPT();
  realId = _real_shmget(key, size, shmflg);
  if (realId != -1) {
    dmtcp::SysVIPC::instance().on_shmget(realId, key, size, shmflg);
    virtId = REAL_TO_VIRTUAL_IPC_ID(realId);
    JTRACE ("Creating new Shared memory segment")
      (key) (size) (shmflg) (realId) (virtId);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return virtId;
}

extern "C"
void *shmat(int shmid, const void *shmaddr, int shmflg)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  int realShmid = VIRTUAL_TO_REAL_IPC_ID(shmid);
  JASSERT(realShmid != -1) .Text("Not Implemented");
  void *ret = _real_shmat(realShmid, shmaddr, shmflg);
  if (ret != (void *) -1) {
    dmtcp::SysVIPC::instance().on_shmat(shmid, shmaddr, shmflg, ret);
    JTRACE ("Mapping Shared memory segment") (shmid) (realShmid) (shmflg) (ret);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return ret;
}

extern "C"
int shmdt(const void *shmaddr)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  int ret = _real_shmdt(shmaddr);
  if (ret != -1) {
    dmtcp::SysVIPC::instance().on_shmdt(shmaddr);
    JTRACE ("Unmapping Shared memory segment" ) (shmaddr);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return ret;
}

extern "C"
int shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  int realShmid = VIRTUAL_TO_REAL_IPC_ID(shmid);
  int ret = _real_shmctl(realShmid, cmd, buf);
  WRAPPER_EXECUTION_ENABLE_CKPT();
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
  WRAPPER_EXECUTION_DISABLE_CKPT();
  realId = _real_semget (key, nsems, semflg);
  if (realId != -1) {
    dmtcp::SysVIPC::instance().on_semget(realId, key, nsems, semflg);
    virtId = REAL_TO_VIRTUAL_IPC_ID(realId);
    JTRACE ("Creating new SysV Semaphore" ) (key) (nsems) (semflg);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
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
  static struct timespec ts_100ms = {0, 100 * 1000 * 1000};
  struct timespec totaltime = {0, 0};
  int ret;
  int realId;

  if (timeout != NULL && TIMESPEC_CMP(timeout, &ts_100ms, <)) {
    WRAPPER_EXECUTION_DISABLE_CKPT();
    realId = VIRTUAL_TO_REAL_IPC_ID(semid);
    JASSERT(realId != -1);
    ret = _real_semtimedop(realId, sops, nsops, timeout);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return ret;
  }

  /*
   * We continue to call pthread_tryjoin_np (and sleep) until we have gone past
   * the abstime provided by the caller
   */
  while (timeout == NULL || TIMESPEC_CMP(&totaltime, timeout, <)) {
    ret = EAGAIN;
    WRAPPER_EXECUTION_DISABLE_CKPT();
    realId = VIRTUAL_TO_REAL_IPC_ID(semid);
    ret = _real_semtimedop(realId, sops, nsops, &ts_100ms);
    WRAPPER_EXECUTION_ENABLE_CKPT();

    // TODO Handle EIDRM
    if (ret == 0) {
      dmtcp::SysVIPC::instance().on_semop(semid, sops, nsops);
      return ret;
    }
    if (ret == -1 && errno != EAGAIN) {
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

  WRAPPER_EXECUTION_DISABLE_CKPT();
  int realId = VIRTUAL_TO_REAL_IPC_ID(semid);
  JASSERT(realId != -1);
  int ret = _real_semctl(realId, semnum, cmd, uarg);
  if (ret != -1) {
    dmtcp::SysVIPC::instance().on_semctl(semid, semnum, cmd, uarg);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return ret;
}
