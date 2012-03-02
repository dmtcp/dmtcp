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
#include  "../jalib/jassert.h"
#include  "../jalib/jfilesystem.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jserialize.h"
#include "syscallwrappers.h"
#include "dmtcpmessagetypes.h"
#include "dmtcpworker.h"
#include "protectedfds.h"
#include "virtualpidtable.h"
#include "util.h"
#include "sysvipc.h"

/* This code depends on PID-Virtualization */
#ifdef PID_VIRTUALIZATION

/*
 * Shmid virtualization closely follows PID-Virtualization model.
 * Algorithm for properly checkpointing shared memory segments.
 * Helper struct: struct shmMetaInfo { pid_t pid, int creatorSignature }
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

static void _do_lock_tbl()
{
  JASSERT(_real_pthread_mutex_lock(&tblLock) == 0) (JASSERT_ERRNO);
}

static void _do_unlock_tbl()
{
  JASSERT(_real_pthread_mutex_unlock(&tblLock) == 0) (JASSERT_ERRNO);
}

static bool isRestarting = false;

dmtcp::SysVIPC::SysVIPC()
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

int dmtcp::SysVIPC::originalToCurrentShmid(int shmid)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  int currentShmid = shmid;
  _do_lock_tbl();
  if (_originalToCurrentShmids.find(shmid) != _originalToCurrentShmids.end()) {
    currentShmid = _originalToCurrentShmids[shmid];
  }
  _do_unlock_tbl();
  WRAPPER_EXECUTION_ENABLE_CKPT();
  JTRACE("Original to current shmid") (shmid) (currentShmid);
  return currentShmid;
}

int dmtcp::SysVIPC::currentToOriginalShmid(int shmid)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  int originalShmid = -1;
  _do_lock_tbl();
  for (ShmidMapIter i = _originalToCurrentShmids.begin();
       i != _originalToCurrentShmids.end();
       ++i) {
    if ( shmid == i->second ) {
      originalShmid = i->first;
      break;
    }
  }
  _do_unlock_tbl();
  WRAPPER_EXECUTION_ENABLE_CKPT();
  JTRACE("current to original shmid") (shmid) (originalShmid);
  return originalShmid;
}

bool dmtcp::SysVIPC::isConflictingShmid(int shmid)
{
  return (originalToCurrentShmid(shmid) != shmid);
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

dmtcp::vector<int> dmtcp::SysVIPC::getShmids()
{
  dmtcp::vector<int> shmids;
  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    shmids.push_back(i->first);
  }
  return shmids;
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
  isRestarting = false;
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
  if (isRestarting) {
    _originalToCurrentShmids.clear();
    readShmidMapsFromFile(PROTECTED_SHMIDMAP_FD);
    _real_close(PROTECTED_SHMIDMAP_FD);
  }

  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    ShmSegment& shmObj = i->second;
    ShmidMapIter j = _originalToCurrentShmids.find(i->first);
    JASSERT(j != _originalToCurrentShmids.end())
      (i->first) (_originalToCurrentShmids.size());

    shmObj.updateCurrentShmid(_originalToCurrentShmids[i->first]);
//    if (isRestarting) {
//      shmObj.remapFirstAddrForOwnerOnRestart();
//    }
    shmObj.remapAll();
  }
}

void dmtcp::SysVIPC::postCheckpoint()
{
  if (!isRestarting) return;

  JTRACE("");

  _originalToCurrentShmids.clear();
  readShmidMapsFromFile(PROTECTED_SHMIDLIST_FD);
  _real_close(PROTECTED_SHMIDLIST_FD);

  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    ShmSegment& shmObj = i->second;
    shmObj.recreateShmSegment();
  }

  _originalToCurrentShmids.clear();
  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    ShmSegment& shmObj = i->second;
    if (shmObj.isCkptLeader()) {
      _originalToCurrentShmids[i->first] = shmObj.currentShmid();
    }
  }
  writeShmidMapsToFile(PROTECTED_SHMIDMAP_FD);
}

void dmtcp::SysVIPC::postRestart()
{
  isRestarting = true;
  _originalToCurrentShmids.clear();

  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    ShmSegment& shmObj = i->second;
    if (shmObj.isCkptLeader()) {
      JTRACE("Writing ShmidMap to file")(shmObj.originalShmid());
      _originalToCurrentShmids[shmObj.originalShmid()] = shmObj.currentShmid();
    }
  }
  if (_originalToCurrentShmids.size() > 0) {
    writeShmidMapsToFile(PROTECTED_SHMIDLIST_FD);
  }
}

void dmtcp::SysVIPC::on_shmget(key_t key, size_t size, int shmflg, int shmid)
{
  JASSERT(!isConflictingShmid(shmid)) (shmid) (key) (size)
    .Text("Duplicate shmid found");
  _do_lock_tbl();
  ShmSegment shmObj (key, size, shmflg, shmid);
  _shm[shmid] = shmObj;
  _originalToCurrentShmids[shmid] = shmid;
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
    _originalToCurrentShmids[shmid] = shmid;
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

void dmtcp::SysVIPC::writeShmidMapsToFile(int fd)
{
  dmtcp::string file = "/proc/self/fd/" + jalib::XToString ( fd );
  file = jalib::Filesystem::ResolveSymlink ( file );
  JASSERT ( file.length() > 0 ) ( file ) ( fd );

  jalib::JBinarySerializeWriterRaw wr (file, fd);

  Util::lockFile(fd);
  wr.serializeMap(_originalToCurrentShmids);
  Util::unlockFile(fd);
}

void dmtcp::SysVIPC::readShmidMapsFromFile(int fd)
{
  dmtcp::string file = "/proc/self/fd/" + jalib::XToString ( fd );
  file = jalib::Filesystem::ResolveSymlink ( file );
  JASSERT ( file.length() > 0 ) ( file );

  jalib::JBinarySerializeReader rd(file);

  while (!rd.isEOF()) {
    rd.serializeMap(_originalToCurrentShmids);
  }
}

void dmtcp::SysVIPC::serialize(jalib::JBinarySerializer& o)
{
  o.serializeMap(_originalToCurrentShmids);
}

/* ShmSegment Methods */

dmtcp::ShmSegment::ShmSegment(key_t key, int size, int shmflg, int shmid)
{
  _key = key;
  _size = size;
  _shmgetFlags = shmflg;
  _originalShmid = shmid;
  _currentShmid = shmid;
  _creatorPid = getpid();
  _isCkptLeader = false;
  JTRACE("New Shm Segment") (_key) (_size) (_shmgetFlags)
    (_currentShmid) (_originalShmid) (_creatorPid) (_isCkptLeader);
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
  _creatorPid = VirtualPidTable::instance().currentToOriginalPid(shminfo.shm_cpid);
  JTRACE("New Shm Segment") (_key) (_size) (_shmgetFlags)
    (_currentShmid) (_originalShmid) (_creatorPid) (_isCkptLeader);
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
      ShmaddrToFlagIter i = _shmaddrToFlag.begin();
      void *addr = _real_shmat(_originalShmid, NULL, 0);
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
  JASSERT(isRestarting);
  if (_isCkptLeader) {
    while (true) {
      int shmid = _real_shmget(_key, _size, _shmgetFlags);
      if (!SysVIPC::instance().isConflictingShmid(shmid)) {
        JTRACE("Recreating shared memory segment") (_originalShmid) (shmid);
        _currentShmid = shmid;
        break;
      }
      JASSERT(_real_shmctl(shmid, IPC_RMID, NULL) != -1);
    }
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
      (i->first) (i->second) (getpid()) (_creatorPid)
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

#endif // PID_VIRTUALIZATION
