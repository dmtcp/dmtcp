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


#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <iostream>
#include <ios>
#include <fstream>
#include  "../../jalib/jassert.h"
#include  "../../jalib/jfilesystem.h"
#include  "../../jalib/jconvert.h"
#include  "../../jalib/jserialize.h"
#include "../syscallwrappers.h"
#include "../dmtcpmessagetypes.h"
#include "../dmtcpworker.h"
#include "../protectedfds.h"
#include "../util.h"
#include "../shareddata.h"
#include "sysvipc.h"

// FIXME: Check and verify the correctness of SEM_UNDO logic for Semaphores.

/*
 * Algorithm for properly checkpointing shared memory segments.
 *  1. BARRIER -- SUSPENDED
 *  2. Call shmat() and shmdt() for each shm-object. This way the last process
 *     to call shmdt() is elected as the ckptLeader.
 *  3. BARRIER -- LOCKED
 *  4. Each process marks itself as ckptLeader if it was elected ckptLeader.
 *     If the ckptLeader doesn't have the shm object mapped, map it now.
 *  6. BARRIER -- DRAINED
 *  7. For each shm-object, the ckptLeader unmaps all-but-first shmat() address.
 *  8. Non ckptLeader processes unmap all shmat() addresses corresponding to
 *     the shm-object.
 *  9. BARRIER -- CHECKPOINTED
 * 10. At this point, the contents of the memory-segment have been saved.
 * 11. BARRIER -- REFILLED
 * 12. Re-map the memory-segment into each process's memory as it existed prior
 *     to checkpoint.
 * 13. TODO: Unmap the memory that was mapped in step 4.
 * 14. BARRIER -- RESUME
 *
 * Steps involved in Restart
 *  0. BARRIER -- RESTARTING
 *  1. Restore process memory
 *  2. Insert original-shmids into a node-wide shared file so that other
 *     processes can know about all the existing shmids in order to avoid
 *     future conflicts.
 *  3. BARRIER -- CHECKPOINTED
 *  4. Read all original-shmids from the file
 *  5. Re-create shared-memory segments which were checkpointed by this process.
 *  6. Remap the shm-segment to a temp addr and copy the checkpointed contents
 *     to this address. Now unmap the area where the checkpointed contents were
 *     stored and map the shm-segment on that address. Unmap the temp addr now.
 *     Remap the shm-segment to the original location.
 *  7. Write original->current mappings for all shmids which we got from
 *     shmget() in previous step.
 *  8. BARRIER -- REFILLED
 *  9. Re-map the memory-segment into each process's memory as it existed prior
 *     to checkpoint.
 * 10. BARRIER -- RESUME
 */

/* TODO: Handle the case when the segment is marked for removal at ckpt time.
 */

static pthread_mutex_t tblLock = PTHREAD_MUTEX_INITIALIZER;

void dmtcp_SysVIPC_ProcessEvent(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_RESET_ON_FORK:
      dmtcp::SysVIPC::instance().resetOnFork();
      break;
    case DMTCP_EVENT_PRE_CKPT:
      dmtcp::SysVIPC::instance().preCheckpoint();
      break;

    case DMTCP_EVENT_LEADER_ELECTION:
      dmtcp::SysVIPC::instance().leaderElection();
      break;

    case DMTCP_EVENT_DRAIN:
      dmtcp::SysVIPC::instance().preCkptDrain();
      break;

    case DMTCP_EVENT_REFILL:
      dmtcp::SysVIPC::instance().refill(data->refillInfo.isRestart);
      break;

    case DMTCP_EVENT_RESUME:
      dmtcp::SysVIPC::instance().preResume();
      break;

    case DMTCP_EVENT_PRE_EXEC:
      {
        jalib::JBinarySerializeWriterRaw wr("", data->serializerInfo.fd);
        dmtcp::SysVIPC::instance().serialize(wr);
      }
      break;

    case DMTCP_EVENT_POST_EXEC:
      {
        jalib::JBinarySerializeReaderRaw rd("", data->serializerInfo.fd);
        dmtcp::SysVIPC::instance().serialize(rd);
      }
      break;

    case DMTCP_EVENT_POST_RESTART:
      dmtcp::SysVIPC::instance().postRestart();
      break;

    default:
      break;
  }
}

static void _do_lock_tbl()
{
  JASSERT(_real_pthread_mutex_lock(&tblLock) == 0) (JASSERT_ERRNO);
}

static void _do_unlock_tbl()
{
  JASSERT(_real_pthread_mutex_unlock(&tblLock) == 0) (JASSERT_ERRNO);
}

static void huge_memcpy(char *dest, char *src, size_t size)
{
  if (size < 100 * 1024 * 1024) {
    memcpy(dest, src, size);
    return;
  }
  const size_t hundredMB = (100 * 1024 * 1024);
  //const size_t oneGB = (1024 * 1024 * 1024);
  size_t chunkSize = hundredMB;
  static long page_size = sysconf(_SC_PAGESIZE);
  static long pagesPerChunk = chunkSize / page_size;
  size_t n = size / chunkSize;
  for (size_t i = 0; i < n; i++) {
    if (!dmtcp::Util::areZeroPages(src, pagesPerChunk)) {
      memcpy(dest, src, chunkSize);
    }
    madvise(src, chunkSize, MADV_DONTNEED);
    dest += chunkSize;
    src += chunkSize;
    size -= chunkSize;
  }
  memcpy(dest, src, size);
}

dmtcp::SysVIPC::SysVIPC()
  : _ipcVirtIdTable("SysVIPC")
{
  _do_lock_tbl();
  _shm.clear();
  _do_unlock_tbl();
}

static dmtcp::SysVIPC *sysvIPCInst = NULL;
dmtcp::SysVIPC& dmtcp::SysVIPC::instance()
{
  //static SysVIPC *inst = new SysVIPC(); return *inst;
  if (sysvIPCInst == NULL) {
    sysvIPCInst = new SysVIPC();
  }
  return *sysvIPCInst;
}

int dmtcp::SysVIPC::getNewVirtualId()
{
  int id = _ipcVirtIdTable.getNewVirtualId();

  JASSERT(id != -1) (_ipcVirtIdTable.size())
    .Text("Exceeded maximum number of Sys V objects allowed");

  return id;
}

void dmtcp::SysVIPC::resetOnFork()
{
  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    i->second->resetOnFork();
  }
  for (SemIterator i = _sem.begin(); i != _sem.end(); ++i) {
    i->second->resetOnFork();
  }
  for (MsqIterator i = _msq.begin(); i != _msq.end(); ++i) {
    i->second->resetOnFork();
  }
}

void dmtcp::SysVIPC::leaderElection()
{
  /* Remove all invalid/removed shm segments*/
  removeStaleObjects();

  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    i->second->leaderElection();
  }
  for (SemIterator i = _sem.begin(); i != _sem.end(); ++i) {
    i->second->leaderElection();
  }
  for (MsqIterator i = _msq.begin(); i != _msq.end(); ++i) {
    i->second->leaderElection();
  }
}

void dmtcp::SysVIPC::preCkptDrain()
{
  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    i->second->preCkptDrain();
  }
  for (SemIterator i = _sem.begin(); i != _sem.end(); ++i) {
    i->second->preCkptDrain();
  }
  for (MsqIterator i = _msq.begin(); i != _msq.end(); ++i) {
    i->second->preCkptDrain();
  }
}

void dmtcp::SysVIPC::preCheckpoint()
{
  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    i->second->preCheckpoint();
  }
  for (SemIterator i = _sem.begin(); i != _sem.end(); ++i) {
    i->second->preCheckpoint();
  }
  for (MsqIterator i = _msq.begin(); i != _msq.end(); ++i) {
    i->second->preCheckpoint();
  }
}

void dmtcp::SysVIPC::preResume()
{
  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    i->second->preResume();
  }
  for (SemIterator i = _sem.begin(); i != _sem.end(); ++i) {
    i->second->preResume();
  }
  for (MsqIterator i = _msq.begin(); i != _msq.end(); ++i) {
    i->second->preResume();
  }
}

void dmtcp::SysVIPC::refill(bool isRestart)
{
  if (!isRestart) return;

  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    i->second->refill(isRestart);
  }
  for (SemIterator i = _sem.begin(); i != _sem.end(); ++i) {
    i->second->refill(isRestart);
  }
  for (MsqIterator i = _msq.begin(); i != _msq.end(); ++i) {
    i->second->refill(isRestart);
  }
}

void dmtcp::SysVIPC::postRestart()
{
  _ipcVirtIdTable.clear();

  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    i->second->postRestart();
  }
  for (SemIterator i = _sem.begin(); i != _sem.end(); ++i) {
    i->second->postRestart();
  }
  for (MsqIterator i = _msq.begin(); i != _msq.end(); ++i) {
    i->second->postRestart();
  }
}

/*
 * Shared Memory
 */
void dmtcp::SysVIPC::on_shmget(int shmid, key_t key, size_t size, int shmflg)
{
  _do_lock_tbl();
  if (!_ipcVirtIdTable.realIdExists(shmid)) {
    JASSERT(_shm.find(shmid) == _shm.end());
    int virtId = getNewVirtualId();
    JTRACE ("Shmid not found in table. Creating new entry")
      (shmid) (virtId);
    updateMapping(virtId, shmid);
    _shm[virtId] = new ShmSegment(virtId, shmid, key, size, shmflg);
  } else {
    JASSERT(_shm.find(shmid) != _shm.end());
  }
  _do_unlock_tbl();
}

void dmtcp::SysVIPC::on_shmat(int shmid, const void *shmaddr, int shmflg,
                              void* newaddr)
{
  _do_lock_tbl();
  if (!_ipcVirtIdTable.virtualIdExists(shmid)) {
    int realId = dmtcp::SharedData::getRealIPCId(shmid);
    updateMapping(shmid, realId);
    _shm[shmid] = new ShmSegment(shmid, realId, -1, -1, -1);
  }
  JASSERT(_ipcVirtIdTable.virtualIdExists(shmid)) (shmid);
  JASSERT(_shm.find(shmid) != _shm.end()) (shmid);

  JASSERT(shmaddr == NULL || shmaddr == newaddr);
  _shm[shmid]->on_shmat(newaddr, shmflg);
  _do_unlock_tbl();
}

void dmtcp::SysVIPC::on_shmdt(const void *shmaddr)
{
  int shmid = shmaddrToShmid(shmaddr);
  JASSERT(shmid != -1) (shmaddr)
    .Text("No corresponding shmid found for given shmaddr");
  _do_lock_tbl();
  _shm[shmid]->on_shmdt(shmaddr);
  if (_shm[shmid]->isStale()) {
    _shm.erase(shmid);
  }
  _do_unlock_tbl();
}

int dmtcp::SysVIPC::shmaddrToShmid(const void* shmaddr)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  int shmid = -1;
  _do_lock_tbl();
  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    ShmSegment* shmObj = i->second;
    if (shmObj->isValidShmaddr(shmaddr)) {
      shmid = i->first;
      break;
    }
  }
  _do_unlock_tbl();
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return shmid;
}

void dmtcp::SysVIPC::removeStaleObjects()
{
  _do_lock_tbl();
  dmtcp::vector<int> staleShmids;
  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    ShmSegment* shmObj = i->second;
    if (shmObj->isStale()) {
      staleShmids.push_back(i->first);
    }
  }
  for (size_t j = 0; j < staleShmids.size(); ++j) {
    delete _shm[staleShmids[j]];
    _shm.erase(staleShmids[j]);
    _ipcVirtIdTable.erase(staleShmids[j]);
  }

  dmtcp::vector<int> staleSemids;
  for (SemIterator i = _sem.begin(); i != _sem.end(); ++i) {
    Semaphore* semObj = i->second;
    if (semObj->isStale()) {
      staleSemids.push_back(i->first);
    }
  }
  for (size_t j = 0; j < staleSemids.size(); ++j) {
    delete _sem[staleSemids[j]];
    _sem.erase(staleSemids[j]);
    _ipcVirtIdTable.erase(staleSemids[j]);
  }
  _do_unlock_tbl();
}

/*
 * Semaphore
 */
void dmtcp::SysVIPC::on_semget(int semid, key_t key, int nsems, int semflg)
{
  _do_lock_tbl();
  if (!_ipcVirtIdTable.realIdExists(semid)) {
    //JASSERT(key == IPC_PRIVATE || (semflg & IPC_CREAT) != 0) (key) (semid);
    JASSERT(_sem.find(semid) == _sem.end());
    JTRACE ("Semid not found in table. Creating new entry") (semid);
    int virtId = getNewVirtualId();
    updateMapping(virtId, semid);
    _sem[virtId] = new Semaphore (virtId, semid, key, nsems, semflg);
  } else {
    JASSERT(_sem.find(semid) != _sem.end());
  }
  _do_unlock_tbl();
}

void dmtcp::SysVIPC::on_semctl(int semid, int semnum, int cmd, union semun arg)
{
  _do_lock_tbl();
  if (cmd == IPC_RMID && _ipcVirtIdTable.virtualIdExists(semid)) {
    JASSERT(_sem[semid]->isStale()) (semid);
    _sem.erase(semid);
  }
  _do_unlock_tbl();
  return;
}

void dmtcp::SysVIPC::on_semop(int semid, struct sembuf *sops, unsigned nsops)
{
  _do_lock_tbl();
  if (!_ipcVirtIdTable.virtualIdExists(semid)) {
    int realId = dmtcp::SharedData::getRealIPCId(semid);
    updateMapping(semid, realId);
    _sem[semid] = new Semaphore(semid, realId, -1, -1, -1);
  }
  _sem[semid]->on_semop(sops, nsops);
  _do_unlock_tbl();
}

void dmtcp::SysVIPC::on_msgget(int msqid, key_t key, int msgflg)
{
  _do_lock_tbl();
  if (!_ipcVirtIdTable.realIdExists(msqid)) {
    JASSERT(_msq.find(msqid) == _msq.end());
    JTRACE ("Msqid not found in table. Creating new entry") (msqid);
    int virtId = getNewVirtualId();
    updateMapping(virtId, msqid);
    _msq[virtId] = new MsgQueue (virtId, msqid, key, msgflg);
  } else {
    JASSERT(_msq.find(msqid) != _msq.end());
  }
  _do_unlock_tbl();
}

void dmtcp::SysVIPC::on_msgctl(int msqid, int cmd, struct msqid_ds *buf)
{
  _do_lock_tbl();
  if (cmd == IPC_RMID && _ipcVirtIdTable.virtualIdExists(msqid)) {
    JASSERT(_msq[msqid]->isStale()) (msqid);
    _msq.erase(msqid);
  }
  _do_unlock_tbl();
  return;
}

void dmtcp::SysVIPC::on_msgsnd(int msqid, const void *msgp, size_t msgsz,
                               int msgflg)
{
  _do_lock_tbl();
  if (!_ipcVirtIdTable.virtualIdExists(msqid)) {
    int realId = dmtcp::SharedData::getRealIPCId(msqid);
    updateMapping(msqid, realId);
    _msq[msqid] = new MsgQueue(msqid, realId, -1, -1);
  }
  _do_unlock_tbl();
}

void dmtcp::SysVIPC::on_msgrcv(int msqid, const void *msgp, size_t msgsz,
                               int msgtyp, int msgflg)
{
  _do_lock_tbl();
  if (!_ipcVirtIdTable.virtualIdExists(msqid)) {
    int realId = dmtcp::SharedData::getRealIPCId(msqid);
    updateMapping(msqid, realId);
    _msq[msqid] = new MsgQueue(msqid, realId, -1, -1);
  }
  _do_unlock_tbl();
}

void dmtcp::SysVIPC::serialize(jalib::JBinarySerializer& o)
{
  _ipcVirtIdTable.serialize(o);
}

/******************************************************************************
 *
 * ShmSegment Methods
 *
 *****************************************************************************/

dmtcp::ShmSegment::ShmSegment(int shmid, int realShmid, key_t key, size_t size,
                              int shmflg)
  : SysVObj(shmid, realShmid, key, shmflg)
{
  _size = size;
  if (key == -1) {
    struct shmid_ds shminfo;
    JASSERT(_real_shmctl(_realId, IPC_STAT, &shminfo) != -1);
    _key = shminfo.shm_perm.__key;
    _size = shminfo.shm_segsz;
    _flags = shminfo.shm_perm.mode;
  }
  JTRACE("New Shm Segment") (_key) (_size) (_flags) (_id) (_isCkptLeader);
}

void dmtcp::ShmSegment::on_shmat(const void *shmaddr, int shmflg)
{
  _shmaddrToFlag[shmaddr] = shmflg;
}

void dmtcp::ShmSegment::on_shmdt(const void *shmaddr)
{
  JASSERT(isValidShmaddr(shmaddr));
  _shmaddrToFlag.erase((void*)shmaddr);

  // TODO: If num-attached == 0; and marked for deletion, remove this segment
}

bool dmtcp::ShmSegment::isValidShmaddr(const void* shmaddr)
{
  return _shmaddrToFlag.find((void*)shmaddr) != _shmaddrToFlag.end();
}

bool dmtcp::ShmSegment::isStale()
{
  struct shmid_ds shminfo;
  int ret = _real_shmctl(_realId, IPC_STAT, &shminfo);
  if (ret == -1) {
    JASSERT (errno == EIDRM || errno == EINVAL);
    JASSERT(_shmaddrToFlag.empty());
    return true;
  }
  _nattch = shminfo.shm_nattch;
  _mode = shminfo.shm_perm.mode;
  return false;
}

void dmtcp::ShmSegment::leaderElection()
{
  /* We attach and detach to the shmid object to set the shm_lpid to our pid.
   * The process who calls the last shmdt() is declared the leader.
   */
  void *addr = _real_shmat(_realId, NULL, 0);
  JASSERT(addr != (void*) -1) (_id) (JASSERT_ERRNO)
    .Text("_real_shmat() failed");

  JASSERT(_real_shmdt(addr) == 0) (_id) (addr) (JASSERT_ERRNO);
}

void dmtcp::ShmSegment::preCkptDrain()
{
  struct shmid_ds info;
  JASSERT(_real_shmctl(_realId, IPC_STAT, &info) != -1);

  /* If we are the ckptLeader for this object, map it now, if not mapped already.
   */
  _dmtcpMappedAddr = false;
  _isCkptLeader = false;

  if (info.shm_lpid == _real_getpid()) {
    _isCkptLeader = true;
    if (_shmaddrToFlag.size() == 0) {
      void *addr = _real_shmat(_realId, NULL, 0);
      JASSERT(addr != (void*) -1);
      _shmaddrToFlag[addr] = 0;
      _dmtcpMappedAddr = true;
    }
  }
}

void dmtcp::ShmSegment::preCheckpoint()
{
  ShmaddrToFlagIter i = _shmaddrToFlag.begin();
  /* If this process is the ckpt-leader, unmap all but first mapped addr,
   * otherwise, unmap all the the mappings of this memory-segment.
   */
  if (_isCkptLeader) {
    ++i;
  }
  for (; i != _shmaddrToFlag.end(); ++i) {
    JASSERT(_real_shmdt(i->first) == 0);
    JTRACE("Unmapping shared memory segment") (_id)(i->first);
  }
}

void dmtcp::ShmSegment::postRestart()
{
  if (!_isCkptLeader) return;

  _realId = _real_shmget(_key, _size, _flags);
  JASSERT(_realId != -1);
  SysVIPC::instance().updateMapping(_id, _realId);

  // Re-map first address for owner on restart
  JASSERT(_isCkptLeader);
  ShmaddrToFlagIter i = _shmaddrToFlag.begin();
  void *tmpaddr = _real_shmat(_realId, NULL, 0);
  JASSERT(tmpaddr != (void*) -1) (_realId)(JASSERT_ERRNO);
  huge_memcpy((char*) tmpaddr, (char*) i->first, _size);
  JASSERT(_real_shmdt(tmpaddr) == 0);
  munmap((void*)i->first, _size);

  if (!_dmtcpMappedAddr) {
    JASSERT (_real_shmat(_realId, i->first, i->second) != (void *) -1);
  }
  JTRACE("Remapping shared memory segment") (_id) (_realId);
}

void dmtcp::ShmSegment::preResume()
{
  // Re-map all remaining addresses
  ShmaddrToFlagIter i = _shmaddrToFlag.begin();

  if (_isCkptLeader && i != _shmaddrToFlag.end()) {
    i++;
  }

  for (; i != _shmaddrToFlag.end(); ++i) {
    JTRACE("Remapping shared memory segment")(_realId);
    JASSERT (_real_shmat(_realId, i->first, i->second) != (void *) -1)
      (JASSERT_ERRNO) (_realId) (_id) (_isCkptLeader)
      (i->first) (i->second) (getpid())
      .Text ("Error remapping shared memory segment");
  }
  // TODO: During Ckpt-resume, if the shm object was mapped by dmtcp
  //       (_dmtcpMappedAddr == true), then we should call shmdt() on it.
}

/******************************************************************************
 *
 * Semaphore Methods
 *
 *****************************************************************************/

dmtcp::Semaphore::Semaphore(int semid, int realSemid, key_t key, int nsems,
                            int semflg)
  : SysVObj(semid, realSemid, key, semflg)
{
  _nsems = nsems;
  if (key == -1) {
    struct semid_ds buf;
    union semun se;
    se.buf = &buf;
    JASSERT(_real_semctl(realSemid, 0, IPC_STAT, se) != -1) (JASSERT_ERRNO);
    _key = se.buf->sem_perm.__key;
    _nsems = se.buf->sem_nsems;
    _flags = se.buf->sem_perm.mode;
  }
  _semval = new unsigned short[_nsems];
  _semadj = new int[_nsems];
  for (int i = 0; i < _nsems; i++) {
    _semval[i] = 0;
    _semadj[i] = 0;
  }
  JTRACE("New Semaphore Segment")
    (_key) (_nsems) (_flags) (_id) (_isCkptLeader);
}

void dmtcp::Semaphore::on_semop(struct sembuf *sops, unsigned nsops)
{
  for (unsigned i = 0; i < nsops; i++) {
    int sem_num = sops[i].sem_num;
    _semadj[sem_num] -= sops[i].sem_op;
  }
}

bool dmtcp::Semaphore::isStale()
{
  int ret = _real_semctl(_realId, 0, GETPID);
  if (ret == -1) {
    JASSERT (errno == EIDRM || errno == EINVAL);
    return true;
  }
  return false;
}

void dmtcp::Semaphore::resetOnFork()
{
  for (int i = 0; i < _nsems; i++) {
    _semadj[i] = 0;
  }
}

void dmtcp::Semaphore::leaderElection()
{
  JASSERT(_realId != -1);
  /* Every process increments and decrements the semaphore value by 1 in order
   * to update the sempid value. The process who performs the last semop is
   * elected the leader.
   */
  struct sembuf sops;
  sops.sem_num = 0;
  sops.sem_op = 1;
  sops.sem_flg = 0;
  int ret = _real_semtimedop(_realId, &sops, 1, NULL);
  if (ret == 0) {
    sops.sem_num = 0;
    sops.sem_op = -1;
    sops.sem_flg = 0;
    JASSERT(_real_semtimedop(_realId, &sops, 1, NULL) == 0) (JASSERT_ERRNO) (_id);
  }
}

void dmtcp::Semaphore::preCkptDrain()
{
  _isCkptLeader = false;
  if (getpid() == _real_semctl(_realId, 0, GETPID)) {
    union semun info;
    info.array = _semval;
    JASSERT(_real_semctl(_realId, 0, GETALL, info) != -1);
    _isCkptLeader = true;
  }
}

void dmtcp::Semaphore::preCheckpoint()
{
}

void dmtcp::Semaphore::postRestart()
{
  if (_isCkptLeader) {
    _realId = _real_semget(_key, _nsems, _flags);
    JASSERT(_realId != -1) (JASSERT_ERRNO);
    SysVIPC::instance().updateMapping(_id, _realId);

    union semun info;
    info.array = _semval;
    JASSERT(_real_semctl(_realId, 0, SETALL, info) != -1);
  }
}

void dmtcp::Semaphore::refill(bool isRestart)
{
  if (!isRestart) return;
  /* Update the semadj value for this process.
   * The way we do it is by calling semop twice as follows:
   * semop(id, {semid, abs(semadj-value), flag1}*, nsems)
   * semop(id, {semid, -abs(semadj-value), flag2}*, nsems)
   *
   * where: flag1 = semadj-value > 0 ? 0 : SEM_UNDO
   *        flag2 = semadj-value < 0 ? SEM_UNDO : 0
   */
  struct sembuf sops;
  _realId = VIRTUAL_TO_REAL_IPC_ID(_id);
  JASSERT(_realId != -1);
  for (int i = 0; i < _nsems; i++) {
    if (_semadj[i] == 0) continue;
    sops.sem_num = i;
    sops.sem_op = abs(_semadj[i]);
    sops.sem_flg = _semadj[i] > 0 ? 0 : SEM_UNDO;
    JASSERT(_real_semop(_realId, &sops, 1) == 0);

    sops.sem_op = - abs(_semadj[i]);
    sops.sem_flg = _semadj[i] < 0 ? SEM_UNDO : 0;
    JASSERT(_real_semop(_realId, &sops, 1) == 0);
  }
}
/******************************************************************************
 *
 * MsgQueue Methods
 *
 *****************************************************************************/

dmtcp::MsgQueue::MsgQueue(int msqid, int realMsqid, key_t key, int msgflg)
  : SysVObj(msqid, realMsqid, key, msgflg)
{
  if (key == -1) {
    struct msqid_ds buf;
    JASSERT(_real_msgctl(_realId, IPC_STAT, &buf) == 0) (_id) (JASSERT_ERRNO);
    _key = buf.msg_perm.__key;
    _flags = buf.msg_perm.mode;
  }
  JTRACE("New MsgQueue Created") (_key) (_flags) (_id);
}

bool dmtcp::MsgQueue::isStale()
{
  struct msqid_ds buf;
  int ret = _real_msgctl(_realId, IPC_STAT, &buf);
  if (ret == -1) {
    JASSERT (errno == EIDRM || errno == EINVAL);
    return true;
  }
  return false;
}

void dmtcp::MsgQueue::leaderElection()
{
  // Leader election is done in preCkptDrain(), here we just fetch the number
  // of messages in the queue.
  struct msqid_ds buf;
  JASSERT(_real_msgctl(_realId, IPC_STAT, &buf) == 0) (_id) (JASSERT_ERRNO);

  _qnum = buf.msg_qnum;
}

void dmtcp::MsgQueue::preCkptDrain()
{
  // This is where we elect the leader
  /* Every process send a message to the queue. Later on, these excess messages
   * will be removed by the ckptLeader before the user threads are allowed to
   * resume.
   * The process whose pid matches the msg_lspid is the leader.
   */
  struct msgbuf msg;
  msg.mtype = getpid();
  JASSERT(_real_msgsnd(_realId, &msg, 0, IPC_NOWAIT) == 0) (_id) (JASSERT_ERRNO);
  _isCkptLeader = false;
}

void dmtcp::MsgQueue::preCheckpoint()
{
  struct msqid_ds buf;
  JASSERT(_real_msgctl(_realId, IPC_STAT, &buf) == 0) (_id) (JASSERT_ERRNO);

  if (buf.msg_lspid == getpid()) {
    size_t size = buf.__msg_cbytes;
    void *msgBuf = JALLOC_HELPER_MALLOC(size);
    _isCkptLeader = true;
    _msgInQueue.clear();
    for (size_t i = 0; i < _qnum; i++) {
      ssize_t numBytes = _real_msgrcv(_realId, msgBuf, size, 0, 0);
      JASSERT(numBytes != -1) (_id) (JASSERT_ERRNO);
      _msgInQueue.push_back(jalib::JBuffer((const char*)msgBuf,
                                           numBytes + sizeof (long)));
    }
    JASSERT(_msgInQueue.size() == _qnum) (_qnum);
    // Now remove all the messages that were sent during preCkptDrain phase.
    while (_real_msgrcv(_realId, msgBuf, size, 0, IPC_NOWAIT) != -1);
    JALLOC_HELPER_FREE(msgBuf);
  } else {
  }
}

void dmtcp::MsgQueue::postRestart()
{
  if (_isCkptLeader) {
    _realId = _real_msgget(_key, _flags);
    JASSERT(_realId != -1) (JASSERT_ERRNO);
    SysVIPC::instance().updateMapping(_id, _realId);
    JASSERT(_msgInQueue.size() == _qnum) (_msgInQueue.size()) (_qnum);
  }
}

void dmtcp::MsgQueue::refill(bool isRestart)
{
  if (_isCkptLeader) {
    struct msqid_ds buf;
    JASSERT(_real_msgctl(_realId, IPC_STAT, &buf) == 0) (_id) (JASSERT_ERRNO);
    if (isRestart) {
      // Now remove all the messages that were sent during preCkptDrain phase.
      size_t size = buf.__msg_cbytes;
      void *msgBuf = JALLOC_HELPER_MALLOC(size);
      while (_real_msgrcv(_realId, msgBuf, size, 0, IPC_NOWAIT) != -1);
      JALLOC_HELPER_FREE(msgBuf);

    } else {
      JASSERT(buf.msg_qnum == 0);
    }

    for (size_t i = 0; i < _qnum; i++) {
      JASSERT(_real_msgsnd(_realId, _msgInQueue[i].buffer(),
                           _msgInQueue[i].size(), IPC_NOWAIT) == 0);
    }
  }
  _msgInQueue.clear();
  _qnum = 0;
}
