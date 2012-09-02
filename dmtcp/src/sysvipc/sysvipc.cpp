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
#include "sysvipc.h"

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
    case DMTCP_EVENT_PRE_CKPT:
      dmtcp::SysVIPC::instance().preCheckpoint();
      break;

    case DMTCP_EVENT_POST_LEADER_ELECTION:
      dmtcp::SysVIPC::instance().leaderElection();
      break;

    case DMTCP_EVENT_POST_DRAIN:
      dmtcp::SysVIPC::instance().preCkptDrain();
      break;

    case DMTCP_EVENT_POST_CKPT:
      {
        DmtcpEventData_t *edata = (DmtcpEventData_t *) data;
        dmtcp::SysVIPC::instance().postCheckpoint(edata->postCkptInfo.isRestart);
      }
      break;

    case DMTCP_EVENT_POST_CKPT_RESUME:
    case DMTCP_EVENT_POST_RESTART_RESUME:
      dmtcp::SysVIPC::instance().preResume();
      break;

    case DMTCP_EVENT_PREPARE_FOR_EXEC:
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

int dmtcp::SysVIPC::shmaddrToShmid(const void* shmaddr)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  int shmid = -1;
  _do_lock_tbl();
  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    ShmSegment& shmObj = i->second;
    if (shmObj.isValidShmaddr(shmaddr)) {
      shmid = i->first;
      break;
    }
  }
  _do_unlock_tbl();
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return shmid;
}

void dmtcp::SysVIPC::removeStaleShmObjects()
{
  dmtcp::vector<int> staleShmids;
  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    ShmSegment& shmObj = i->second;
    if (shmObj.isStale()) {
      staleShmids.push_back(i->first);
    }
  }
  for (size_t j = 0; j < staleShmids.size(); ++j) {
    _shm.erase(staleShmids[j]);
  }
}

void dmtcp::SysVIPC::leaderElection()
{
  /* Remove all invalid/removed shm segments*/
  removeStaleShmObjects();

  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    ShmSegment& shmObj = i->second;
    shmObj.leaderElection();
  }
}

void dmtcp::SysVIPC::preCkptDrain()
{
  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    ShmSegment& shmObj = i->second;
    shmObj.preCkptDrain();
  }
}

void dmtcp::SysVIPC::preCheckpoint()
{
  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    ShmSegment& shmObj = i->second;
    shmObj.preCheckpoint();
  }
}

void dmtcp::SysVIPC::preResume()
{
  JTRACE("");
  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    ShmSegment& shmObj = i->second;
    JASSERT(_ipcVirtIdTable.virtualIdExists(i->first)) (i->first);
    int realShmId = VIRTUAL_TO_REAL_IPC_ID(i->first);

    shmObj.updateCurrentShmid(realShmId);
//    if (isRestarting) {
//      shmObj.remapFirstAddrForOwnerOnRestart();
//    }
    shmObj.remapAll();
  }
}

void dmtcp::SysVIPC::postCheckpoint(bool isRestart)
{
  if (!isRestart) return;
  _ipcVirtIdTable.clear();
  _ipcVirtIdTable.readMapsFromFile(PROTECTED_SHMIDMAP_FD);
}

void dmtcp::SysVIPC::postRestart()
{
  _ipcVirtIdTable.clear();

  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    ShmSegment& shmObj = i->second;
    shmObj.recreateShmSegment();
    if (shmObj.isCkptLeader()) {
      JTRACE("Writing ShmidMap to file")(shmObj.originalShmid());
      _ipcVirtIdTable.updateMapping(shmObj.originalShmid(),
                                    shmObj.currentShmid());
    }
  }
  _ipcVirtIdTable.writeMapsToFile(PROTECTED_SHMIDMAP_FD);
}

void dmtcp::SysVIPC::on_shmget(key_t key, size_t size, int shmflg, int shmid)
{
  _do_lock_tbl();
  int virtualShmid = getNewVirtualId();
  ShmSegment shmObj (key, size, shmflg, shmid);
  _shm[shmid] = shmObj;
  _ipcVirtIdTable.updateMapping(shmid, shmid);
  _do_unlock_tbl();
}

void dmtcp::SysVIPC::on_shmat(int shmid, const void *shmaddr, int shmflg,
                              void* newaddr)
{
  _do_lock_tbl();
  if (_shm.find( shmid ) == _shm.end()) {
    // This process doesn't know about the given shmid. Create a new entry
    JTRACE ("Shmid not found in table. Creating new entry") (shmid);
    ShmSegment shmObj (shmid);
    _shm[shmid] = shmObj;
    _ipcVirtIdTable.updateMapping(shmid, shmid);
  }

  JASSERT(shmaddr == NULL || shmaddr == newaddr);
  _shm[shmid].on_shmat(newaddr, shmflg);
  _do_unlock_tbl();
}

void dmtcp::SysVIPC::on_shmdt(const void *shmaddr)
{
  int shmid = shmaddrToShmid(shmaddr);
  JASSERT(shmid != -1) (shmaddr)
    .Text("No corresponding shmid found for given shmaddr");
  _do_lock_tbl();
  _shm[shmid].on_shmdt(shmaddr);
  _do_unlock_tbl();
}

void dmtcp::SysVIPC::serialize(jalib::JBinarySerializer& o)
{
  _ipcVirtIdTable.serialize(o);
}

/* ShmSegment Methods */

dmtcp::ShmSegment::ShmSegment(key_t key, size_t size, int shmflg, int shmid)
{
  _key = key;
  _size = size;
  _shmgetFlags = shmflg;
  _originalShmid = shmid;
  _currentShmid = shmid;
  _isCkptLeader = false;
  JTRACE("New Shm Segment") (_key) (_size) (_shmgetFlags)
    (_currentShmid) (_originalShmid) (_isCkptLeader);
}

dmtcp::ShmSegment::ShmSegment(int shmid)
{
  struct shmid_ds shminfo;
  JASSERT(_real_shmctl(shmid, IPC_STAT, &shminfo) != -1);
  _key = shminfo.shm_perm.__key;
  _size = shminfo.shm_segsz;
  _shmgetFlags = shminfo.shm_perm.mode;
  _originalShmid = shmid;
  _currentShmid = shmid;
  _isCkptLeader = false;
  JTRACE("New Shm Segment") (_key) (_size) (_shmgetFlags)
    (_currentShmid) (_originalShmid) (_isCkptLeader);
}

bool dmtcp::ShmSegment::isValidShmaddr(const void* shmaddr)
{
  return _shmaddrToFlag.find((void*)shmaddr) != _shmaddrToFlag.end();
}

bool dmtcp::ShmSegment::isStale()
{
  struct shmid_ds shminfo;
  int ret = _real_shmctl(_currentShmid, IPC_STAT, &shminfo);
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
  void *addr = _real_shmat(_currentShmid, NULL, 0);
  JASSERT(addr != (void*) -1) (_originalShmid) (JASSERT_ERRNO)
    .Text("_real_shmat() failed");

  JASSERT(_real_shmdt(addr) == 0) (_originalShmid) (addr) (JASSERT_ERRNO);
}

void dmtcp::ShmSegment::preCkptDrain()
{
  struct shmid_ds info;
  JASSERT(_real_shmctl(_currentShmid, IPC_STAT, &info) != -1);

  /* If we are the ckptLeader for this object, map it now, if not mapped already.
   */
  _dmtcpMappedAddr = false;
  _isCkptLeader = false;

  if (info.shm_lpid == _real_getpid()) {
    _isCkptLeader = true;
    if (_shmaddrToFlag.size() == 0) {
      void *addr = _real_shmat(_currentShmid, NULL, 0);
      JASSERT(addr != (void*) -1);
      _shmaddrToFlag[addr] = 0;
      _dmtcpMappedAddr = true;
      JNOTE("Explicit mapping");
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
    JTRACE("Unmapping shared memory segment") (_originalShmid)(_currentShmid)(i->first);
  }
}

void dmtcp::ShmSegment::recreateShmSegment()
{
  if (_isCkptLeader) {
    _currentShmid = _real_shmget(_key, _size, _shmgetFlags);
    remapFirstAddrForOwnerOnRestart();
  }
}

void dmtcp::ShmSegment::remapFirstAddrForOwnerOnRestart()
{
  JASSERT(_isCkptLeader);
  ShmaddrToFlagIter i = _shmaddrToFlag.begin();
  void *tmpaddr = _real_shmat(_currentShmid, NULL, 0);
  JASSERT(tmpaddr != (void*) -1) (_currentShmid)(JASSERT_ERRNO);
  memcpy(tmpaddr, i->first, _size);
  JASSERT(_real_shmdt(tmpaddr) == 0);
  munmap(i->first, _size);

  if (!_dmtcpMappedAddr) {
    JASSERT (_real_shmat(_currentShmid, i->first, i->second) != (void *) -1);
  }
  JTRACE("Remapping shared memory segment")(_currentShmid);
}

void dmtcp::ShmSegment::remapAll()
{
  ShmaddrToFlagIter i = _shmaddrToFlag.begin();

  if (_isCkptLeader && i != _shmaddrToFlag.end()) {
    i++;
  }

  for (; i != _shmaddrToFlag.end(); ++i) {
    JTRACE("Remapping shared memory segment")(_currentShmid);
    JASSERT (_real_shmat(_currentShmid, i->first, i->second) != (void *) -1)
      (JASSERT_ERRNO) (_currentShmid) (_originalShmid) (_isCkptLeader)
      (i->first) (i->second) (getpid())
      .Text ("Error remapping shared memory segment");
  }
  // TODO: During Ckpt-resume, if the shm object was mapped by dmtcp
  //       (_dmtcpMappedAddr == true), then we should call shmdt() on it.
}

void dmtcp::ShmSegment::on_shmat(void *shmaddr, int shmflg)
{
  _shmaddrToFlag[shmaddr] = shmflg;
}

void dmtcp::ShmSegment::on_shmdt(const void *shmaddr)
{
  JASSERT(isValidShmaddr(shmaddr));
  _shmaddrToFlag.erase((void*)shmaddr);

  // TODO: If num-attached == 0; and marked for deletion, remove this segment
}
