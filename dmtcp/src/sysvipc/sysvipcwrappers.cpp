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

extern "C"
int shmget(key_t key, size_t size, int shmflg)
{
  int ret;
  WRAPPER_EXECUTION_DISABLE_CKPT();
  while (true) {
    ret = _real_shmget(key, size, shmflg);
    if (ret != -1 &&
        dmtcp::SysVIPC::instance().isConflictingShmid(ret) == false) {
      dmtcp::SysVIPC::instance().on_shmget(key, size, shmflg, ret);
      break;
    }
    JASSERT(_real_shmctl(ret, IPC_RMID, NULL) != -1);
  };
  JTRACE ("Creating new Shared memory segment" ) (key) (size) (shmflg) (ret);
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return ret;
}

extern "C"
void *shmat(int shmid, const void *shmaddr, int shmflg)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  int currentShmid = dmtcp::SysVIPC::instance().originalToCurrentShmid(shmid);
  JASSERT(currentShmid != -1);
  void *ret = _real_shmat(currentShmid, shmaddr, shmflg);
  if (ret != (void *) -1) {
    dmtcp::SysVIPC::instance().on_shmat(shmid, shmaddr, shmflg, ret);
    JTRACE ("Mapping Shared memory segment" ) (shmid) (shmflg) (ret);
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
  int currentShmid = dmtcp::SysVIPC::instance().originalToCurrentShmid(shmid);
  JASSERT(currentShmid != -1);
  int ret = _real_shmctl(currentShmid, cmd, buf);
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return ret;
}
