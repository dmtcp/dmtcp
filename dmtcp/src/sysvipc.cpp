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
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <iostream>
#include <ios>
#include <fstream>

/* This code depends on PID-Virtualization */
#ifdef PID_VIRTUALIZATION

/*
 * Shmid virtualization closely follows PID-Virtualization model.
 * Algorithm for properly checkpointing shared memory segments.
 * Helper struct: struct shmMetaInfo { pid_t pid, int creatorSignature }
 *  1. BARRIER -- SUSPENDED
 *  2a. If the process is the creator process and the memory-segment wasn't
 *      mapped, map it now. We will unmap it later.
 *  2b. Copy first sizeof(shmMetaInfo) bytes of memory segment into a temp
 *      buffer, call it origInfo
 *  3. BARRIER -- LOCKED
 *  4. Populate a new object of type shmMetaInfo with the following values:
 *     pid = getpid()
 *     creatorSignature = | ~ origInfo.creatorSignature ; if this is the creator process
 *                        | origInfo.creatorSignature   ; otherwise
 *  5. Copy this new object to the start of the memory segment.
 *  6. BARRIER -- DRAINED
 *  7. If this is the creator process, 
 *  8.     do nothing.
 *  9. else 
 * 10.     unmap this shared-memory from process address space (there might be
 *         multiple copies, so unmap them all)
 * 11. endif
 * 12. BARRIER -- CHECKPOINTED
 * 13. At this point, the contents of the memory-segment have been saved.
 * 14. BARRIER -- REFILLED
 * 15. Re-map the memory-segment into each process's memory as it existed prior
 *     to checkpoint.
 * 16. Unmap the memory that was mapped in step 2a.
 * 17. BARRIER -- RESUME
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
 *  7. Write original->current mappings for all shmids which we got from
 *     shmget() in previous step.
 *  8. BARRIER -- REFILLED
 *  9. Re-map the memory-segment into each process's memory as it existed prior
 *     to checkpoint.
 * 10. Unmap the memory that was mapped in step 2a.
 * 11. BARRIER -- RESUME
 */

/*
 * TODO:
 * 1. Preserve Shmids across exec()     -- DONE
 * 2. Handle the case when the segment is marked for removal at ckpt time.
 */

static pthread_mutex_t tblLock = PTHREAD_MUTEX_INITIALIZER;

static void _do_lock_tbl()
{
  JASSERT(pthread_mutex_lock(&tblLock) == 0) (JASSERT_ERRNO);
}

static void _do_unlock_tbl()
{
  JASSERT(pthread_mutex_unlock(&tblLock) == 0) (JASSERT_ERRNO);
}

static bool isRestarting = false;

dmtcp::SysVIPC::SysVIPC()
{
  _do_lock_tbl();
  _shm.clear();
  _do_unlock_tbl();
}

dmtcp::SysVIPC& dmtcp::SysVIPC::instance()
{
  static SysVIPC *inst = new SysVIPC(); return *inst;
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
  for (int j = 0; j < staleShmids.size(); ++j) {
    _shm.erase(staleShmids[j]);
  }
}

void dmtcp::SysVIPC::prepareForLeaderElection()
{
  isRestarting = false;
  /* Remove all invalid/removed shm segments*/
  removeStaleShmObjects();

  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    ShmSegment& shmObj = i->second;
    shmObj.prepareForLeaderElection();
  }
}

void dmtcp::SysVIPC::leaderElection()
{
  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    ShmSegment& shmObj = i->second;
    shmObj.leaderElection();
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
    if (shmObj.isOwner()) {
      _originalToCurrentShmids[i->first] = shmObj.currentShmid();
    }
  }
  writeShmidMapsToFile(PROTECTED_SHMIDMAP_FD);
}

void dmtcp::SysVIPC::postRestart()
{
  isRestarting = true;
  _originalToCurrentShmids.clear();
    
  JTRACE("");
  for (ShmIterator i = _shm.begin(); i != _shm.end(); ++i) {
    ShmSegment& shmObj = i->second;
    if (shmObj.isOwner()) {
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

void dmtcp::SysVIPC::on_shmat(int shmid, const void *shmaddr, int shmflg, void* newaddr)
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
  _creatorPid = VirtualPidTable::instance().currentToOriginalPid(shminfo.shm_cpid);
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

void dmtcp::ShmSegment::prepareForLeaderElection()
{
  /* If the creator process hasn't mapped this object, map it now so that it
   * can be checkpointed. In the post restart routine, we will be unmapping
   * this address.
   *
   * TODO: If the segment has been marked for deletion, we might accidently
   * loose it if the unmapping happens before it is re-mapped by the other
   * processes.
   */
  if (_nattch == 0 || (_creatorPid == getpid() && _shmaddrToFlag.empty())) {

    void *mapaddr = _real_shmat(_originalShmid, NULL, 0);
    JASSERT(mapaddr != (void*) -1);
    _shmaddrToFlag[mapaddr] = 0;
    _dmtcpMappedAddr = true;
  } else {
    _dmtcpMappedAddr = false;
  }

  ShmaddrToFlagIter i = _shmaddrToFlag.begin();
  JASSERT (i != _shmaddrToFlag.end());

  pid_t *addr = (pid_t *) i->first;
  _originalInfo.pid = *addr;
  _originalInfo.creatorSignature = *(int*)(addr+1);
}

void dmtcp::ShmSegment::leaderElection()
{
  /*
   * We want only one process to save a copy of this segment in its checkpoint image.
   */
  _ownerInfo.pid = getpid();

  ShmaddrToFlagIter i = _shmaddrToFlag.begin();
  JASSERT (i != _shmaddrToFlag.end());

  pid_t *addr = (pid_t *) i->first;
  *addr = _ownerInfo.pid;
  if (getpid() == _creatorPid) {
    _ownerInfo.creatorSignature = ~(_originalInfo.creatorSignature);
    *(int*)(addr+1) = _ownerInfo.creatorSignature;
  } else {
    _ownerInfo.creatorSignature = _originalInfo.creatorSignature;
  }
}

void dmtcp::ShmSegment::preCheckpoint()
{
  ShmaddrToFlagIter i = _shmaddrToFlag.begin();
  JASSERT (i != _shmaddrToFlag.end());

  pid_t *addr = (pid_t *) i->first;
  _ownerInfo.pid = *addr;
  _ownerInfo.creatorSignature = *(int*)(addr+1);

  if (getpid() == _creatorPid) {
    /* This shared memory object was created by us. Checkpoint it */
    JASSERT (_ownerInfo.creatorSignature != _originalInfo.creatorSignature);
    _ownerInfo.pid = getpid();

    JTRACE("Owner/Creator of the shared memory segment. Will ckpt it.") (getpid());

    // Unmap all but first mapped addr
    ++i;
    for (; i != _shmaddrToFlag.end(); ++i) {
      JASSERT(_real_shmdt(i->first) == 0);
      JTRACE("Unmapping shared memory segment") (_originalShmid)(_currentShmid)(i->first);
    }

  } else if (_ownerInfo.creatorSignature == _originalInfo.creatorSignature &&
             getpid() == _ownerInfo.pid) {
    /* Creator process not alive and we have the leadership of this
     * shared-memory object, so checkpoint it. 
     */
    // Unmap all but first mapped addr
    ++i;
    for (; i != _shmaddrToFlag.end(); ++i) {
      JASSERT(_real_shmdt(i->first) == 0);
      JTRACE("Unmapping shared memory segment") (_originalShmid)(_currentShmid)(i->first);
    }
    
  } else {
    /* Either creator process is alive or this process was not elected to
     * checkpoint this area so it should unmap all the mappings of this
     * memory-segment.
     */
    _ownerInfo.pid = 0;
    for (; i != _shmaddrToFlag.end(); ++i) {
      JASSERT(_real_shmdt(i->first) == 0);
      JTRACE("Unmapping shared memory segment") (_originalShmid)(_currentShmid)(i->first);
    }
  }
}

void dmtcp::ShmSegment::recreateShmSegment()
{
  JASSERT(isRestarting);
  if (_ownerInfo.pid == getpid()) {
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
  JASSERT(_ownerInfo.pid == getpid());
  ShmaddrToFlagIter i = _shmaddrToFlag.begin();
  void *tmpaddr = _real_shmat(_currentShmid, NULL, 0);
  JASSERT(tmpaddr != (void*) -1) (_currentShmid)(JASSERT_ERRNO);
  memcpy(tmpaddr, i->first, _size);
  munmap(i->first, _size);
  JASSERT (_real_shmat(_currentShmid, i->first, i->second) != (void *) -1);
  JASSERT(_real_shmdt(tmpaddr) == 0);
  JTRACE("Remapping shared memory segment")(_currentShmid);
}

void dmtcp::ShmSegment::remapAll()
{
  ShmaddrToFlagIter i = _shmaddrToFlag.begin();
  if (_ownerInfo.pid == getpid()) {
    // The address is already mapped, so we won't segfault
    pid_t *addr = (pid_t *) i->first;
    *addr = _originalInfo.pid;
    *(int*)(addr+1) = _originalInfo.creatorSignature;
    JTRACE("Owner process, restoring first 8 bytes of shared area");
  }

  for (i = _shmaddrToFlag.begin() ; i != _shmaddrToFlag.end(); ++i) {
    if (_real_shmat(_currentShmid, i->first, i->second) == (void *) -1) {
      JASSERT(errno == EINVAL && _ownerInfo.pid == getpid()) (JASSERT_ERRNO) (_currentShmid) (_originalShmid)(i->first) (_ownerInfo.pid)(getpid()) (_creatorPid);
    }
    JTRACE("Remapping shared memory segment")(_currentShmid);
  }
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
