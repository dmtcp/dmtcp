/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <fcntl.h>
#include <stdlib.h>
#include <syscall.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/futex.h>

#include "../jalib/jassert.h"
#include "../jalib/jconvert.h"
#include "constants.h"
#include "coordinatorapi.h"
#include "dmtcpalloc.h"
#include "membarrier.h"
#include "protectedfds.h"
#include "shareddata.h"
#include "syscallwrappers.h"
#include "uniquepid.h"
#include "util.h"

#define SHM_MAX_SIZE (sizeof(SharedData::Header))

using namespace dmtcp;
static struct SharedData::Header *sharedDataHeader = NULL;
static uint32_t nextVirtualPtyId = (uint32_t)-1;

#if defined(__x86_64__) || defined(__aarch64__)
static const SharedData::DMTCP_ARCH_MODE archMode = SharedData::DMTCP_ARCH_32;
#else
static const SharedData::DMTCP_ARCH_MODE archMode = SharedData::DMTCP_ARCH_64;
#endif

void
SharedData::initializeHeader(const char *tmpDir,
                             const char *installDir,
                             DmtcpUniqueProcessId *compId,
                             CoordinatorInfo *coordInfo,
                             struct in_addr *localIPAddr)
{
  JASSERT(tmpDir && installDir && compId && coordInfo && localIPAddr);

  off_t size = CEIL(SHM_MAX_SIZE, Util::pageSize());
  JASSERT(lseek(PROTECTED_SHM_FD, size, SEEK_SET) == size)
    (JASSERT_ERRNO);
  Util::writeAll(PROTECTED_SHM_FD, "", 1);
  memset(sharedDataHeader, 0, size);

  strcpy(sharedDataHeader->versionStr, SHM_VERSION_STR);
#if 0
  sharedDataHeader->coordHost[0] = '\0';
  sharedDataHeader->coordPort = -1;
  sharedDataHeader->ckptInterval = -1;
#endif // if 0
  sharedDataHeader->dlsymOffset = 0;
  sharedDataHeader->dlsymOffset_m32 = 0;
  sharedDataHeader->numSysVShmIdMaps = 0;
  sharedDataHeader->numSysVSemIdMaps = 0;
  sharedDataHeader->numSysVMsqIdMaps = 0;
  sharedDataHeader->numSysVShmKeyMaps = 0;
  sharedDataHeader->numPtraceIdMaps = 0;
  sharedDataHeader->numPtyNameMaps = 0;
  sharedDataHeader->initialized = true;

  sharedDataHeader->numIncomingConMaps = 0;
  sharedDataHeader->barrierInfo.numCkptPeers = 0;
  sharedDataHeader->barrierInfo.numIn = 0;
  sharedDataHeader->barrierInfo.curRound = 0;

  sharedDataHeader->archMode = archMode;

  memcpy(&sharedDataHeader->compId, compId, sizeof(*compId));
  memcpy(&sharedDataHeader->coordInfo, coordInfo, sizeof(*coordInfo));
  memcpy(&sharedDataHeader->localIPAddr, localIPAddr, sizeof(*localIPAddr));

  // The current implementation simply increments the last count and returns it.
  // Although highly unlikely, this can cause a problem if the counter resets to
  // zero. In that case we should have some more sophisticated code which checks
  // to see if the value pointed by counter is in use or not.
  if (nextVirtualPtyId != (uint32_t)-1) {
    sharedDataHeader->nextVirtualPtyId = nextVirtualPtyId;
  } else {
    sharedDataHeader->nextVirtualPtyId = 0;
  }
  JASSERT(strlen(tmpDir) < sizeof(sharedDataHeader->tmpDir) - 1) (tmpDir);
  strcpy(sharedDataHeader->tmpDir, tmpDir);

  JASSERT(strlen(installDir) < sizeof(sharedDataHeader->installDir) - 1)
    (installDir);
  strcpy(sharedDataHeader->installDir, installDir);
}

bool
SharedData::initialized()
{
  return sharedDataHeader != NULL;
}

void
SharedData::initialize(const char *tmpDir = NULL,
                       const char *installDir = NULL,
                       DmtcpUniqueProcessId *compId = NULL,
                       CoordinatorInfo *coordInfo = NULL,
                       struct in_addr *localIPAddr = NULL)
{
  /* FIXME: If the coordinator timestamp resolution is 1 second, during
   * subsequent restart, the coordinator timestamp may have the same value
   * causing conflict with SharedData file. In future, a better fix would be to
   * delete the file associated with SharedData in preCkpt phase and recreate
   * it in postCkpt/postRestart phase.
   */
  bool needToInitialize = false;

  JASSERT((coordInfo != NULL && localIPAddr != NULL) ||
          Util::isValidFd(PROTECTED_SHM_FD));
  if (!Util::isValidFd(PROTECTED_SHM_FD)) {
    JASSERT(tmpDir != NULL);
    ostringstream o;
    o << tmpDir << "/dmtcpSharedArea."
      << *compId << "." << std::hex << coordInfo->timeStamp;
    // THIS IS A DUP OF initializeHeader AND OF size, below; Pass this in as an
    // argument to it.
    off_t size = CEIL(SHM_MAX_SIZE, Util::pageSize());

    int fd = _real_open(o.str().c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd == -1 && errno == EEXIST) {
      /* If the shared data area already exists, it's probably a conflict
       * and two independent computations might have been launched at the
       * same time.
       * FIXME: We're unlikely to hit this bug since we are using a higher
       *        resolution timer, clock_gettime() (1 nanosecond).
       */
      JWARNING(false)
        ("Internal error detected! Shared data area already exists.");
      fd = _real_open(o.str().c_str(), O_RDWR, 0600);
    } else {
      // Extend file to size before 'mmap'
      JASSERT( truncate(o.str().c_str(), size) == 0);
      needToInitialize = true;
    }
    JASSERT(fd != -1) (JASSERT_ERRNO);
    JASSERT(_real_dup2(fd, PROTECTED_SHM_FD) == PROTECTED_SHM_FD)
      (JASSERT_ERRNO);
    _real_close(fd);
  }

  size_t size = CEIL(SHM_MAX_SIZE, Util::pageSize());
  void *addr = mmap((void *)sharedDataHeader, size,
                    PROT_READ | PROT_WRITE, MAP_SHARED,
                    PROTECTED_SHM_FD, 0);
  JASSERT(addr != MAP_FAILED) (JASSERT_ERRNO)
  .Text("Unable to find shared area.");

#if __arm__
  WMB;  // Ensure store to memory by kernel mmap call has completed
#endif // if __arm__

  sharedDataHeader = (struct Header *)addr;

  if (needToInitialize) {
    Util::lockFile(PROTECTED_SHM_FD);
    initializeHeader(tmpDir, installDir, compId, coordInfo, localIPAddr);
    Util::unlockFile(PROTECTED_SHM_FD);
  } else {
    struct stat statbuf;
    while (1) {
      bool initialized = false;
      Util::lockFile(PROTECTED_SHM_FD);
      JASSERT(fstat(PROTECTED_SHM_FD, &statbuf) != -1) (JASSERT_ERRNO);
      initialized = sharedDataHeader->initialized;
      Util::unlockFile(PROTECTED_SHM_FD);
      // If we got here, it implies that needtoinitialize was false, and
      // so some other peer won the race and is initializing the shared data
      // area header. The peer will set initialized last.  If initialized
      // is true, then we're ready to go.  If initialized is false,
      // then we will sleep a little longer and then test again inside the
      // while loop to see if the peer has now set 'initialized'.  As a
      // performance optimization, we could use a SysV // semaphore/condition
      // variable, instead of sleeping and re-trying inside the loop.
      // NOTE:  This code is correct under total store order or seq. consist.
      //   But the relaxed consstency model, partial store order, ouuld create a
      //   theoretically possible bug if initialized does not reach memory last.
      if (statbuf.st_size > 0 && initialized) {
        break;
      }
      struct timespec sleepTime = { 0, 100 * 1000 * 1000 };
      nanosleep(&sleepTime, NULL);
    }

    Util::lockFile(PROTECTED_SHM_FD);
    if (!Util::strStartsWith(sharedDataHeader->versionStr,
                             SHM_VERSION_STR)) {
      JASSERT(false) (sharedDataHeader->versionStr) (SHM_VERSION_STR)
      .Text("Wrong signature");
    }

    // Check if the computation is running in mixed mode.
    if (sharedDataHeader->archMode != archMode) {
      sharedDataHeader->archMode = DMTCP_ARCH_MIXED;
    }

    Util::unlockFile(PROTECTED_SHM_FD);
  }
  JTRACE("Shared area mapped") (sharedDataHeader);
}

bool
SharedData::isSharedDataRegion(void *addr)
{
  return addr == (void *)sharedDataHeader;
}

void
SharedData::resetBarrierInfo()
{
  sharedDataHeader->barrierInfo.numCkptPeers = 0;
  sharedDataHeader->barrierInfo.numIn = 0;
  sharedDataHeader->barrierInfo.curRound = 0;
}

// Here we reset some counters that are used by IPC plugin for local
// name-service database, etc. during ckpt/resume/restart phases.
void
SharedData::prepareForCkpt()
{
  nextVirtualPtyId = sharedDataHeader->nextVirtualPtyId;
  sharedDataHeader->numInodeConnIdMaps = 0;
  sharedDataHeader->numIncomingConMaps = 0;

  initializeBarrier();
}

void
SharedData::initializeBarrier()
{
  Util::lockFile(PROTECTED_SHM_FD);
  sharedDataHeader->barrierInfo.numCkptPeers++;

  if (sharedDataHeader->archMode != DMTCP_ARCH_MIXED) {
    pthread_barrierattr_t barrierAttr;
    pthread_barrierattr_setpshared(&barrierAttr, PTHREAD_PROCESS_SHARED);
    pthread_barrier_init(&sharedDataHeader->barrierInfo.barrier,
                         &barrierAttr,
                         sharedDataHeader->barrierInfo.numCkptPeers);
  } else {
    sharedDataHeader->barrierInfo.numIn = 0;
    sharedDataHeader->barrierInfo.curRound = 0;
  }
  Util::unlockFile(PROTECTED_SHM_FD);

  WMB;
}

void
SharedData::postRestart()
{
  initialize();
  initializeBarrier();
}

void
SharedData::waitForBarrier(const string &barrierId)
{
  if (sharedDataHeader->archMode != DMTCP_ARCH_MIXED) {
    pthread_barrier_wait(&sharedDataHeader->barrierInfo.barrier);
    return;
  }

  // TODO: Replace file locking with atomic built-ins such as
  // __sync_fetch_and_add.
  Util::lockFile(PROTECTED_SHM_FD);
  size_t numIn = ++sharedDataHeader->barrierInfo.numIn;
  size_t curRound = sharedDataHeader->barrierInfo.curRound;
  WMB;
  Util::unlockFile(PROTECTED_SHM_FD);

  if (numIn < sharedDataHeader->barrierInfo.numCkptPeers) {
    if (_real_syscall(SYS_futex,
                      &sharedDataHeader->barrierInfo.curRound,
                      FUTEX_WAIT,
                      curRound,
                      NULL, NULL, 0) != 0) {
      JASSERT(errno == EAGAIN);
      Util::lockFile(PROTECTED_SHM_FD);
      Util::unlockFile(PROTECTED_SHM_FD);
    }
  } else {
    Util::lockFile(PROTECTED_SHM_FD);
    sharedDataHeader->barrierInfo.numIn = 0;
    sharedDataHeader->barrierInfo.curRound++;
    WMB;
    _real_syscall(SYS_futex,
                  &sharedDataHeader->barrierInfo.curRound,
                  FUTEX_WAKE,
                  sharedDataHeader->barrierInfo.numCkptPeers,
                  NULL, NULL, 0);
    Util::unlockFile(PROTECTED_SHM_FD);
  }
}

string
SharedData::coordHost()
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  const struct sockaddr_in *sin =
    (const struct sockaddr_in *)&sharedDataHeader->coordInfo.addr;
  string remoteIP = inet_ntoa(sin->sin_addr);
  return remoteIP;
}

uint32_t
SharedData::coordPort()
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  const struct sockaddr_in *sin =
    (const struct sockaddr_in *)&sharedDataHeader->coordInfo.addr;
  return ntohs(sin->sin_port);
}

string
SharedData::getTmpDir()
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  JASSERT(sharedDataHeader->tmpDir[0] != '\0');
  return string(sharedDataHeader->tmpDir);
}

char *
SharedData::getTmpDir(char *buf, uint32_t len)
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  JASSERT(sharedDataHeader->tmpDir[0] != '\0');
  if (len <= strlen(sharedDataHeader->tmpDir)) {
    return NULL;
  }
  strcpy(buf, sharedDataHeader->tmpDir);
  return buf;
}

string
SharedData::getInstallDir()
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  return sharedDataHeader->installDir;
}

uint32_t
SharedData::getCkptInterval()
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  return sharedDataHeader->coordInfo.interval;
}

void
SharedData::updateGeneration(uint32_t generation)
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  sharedDataHeader->compId._computation_generation = generation;
}

DmtcpUniqueProcessId
SharedData::getCompId()
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  return sharedDataHeader->compId;
}

DmtcpUniqueProcessId
SharedData::getCoordId()
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  return sharedDataHeader->coordInfo.id;
}

uint64_t
SharedData::getCoordTimeStamp()
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  return sharedDataHeader->coordInfo.timeStamp;
}

void
SharedData::getCoordAddr(struct sockaddr *addr, uint32_t *len)
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  JASSERT(addr != NULL);
  *len = sharedDataHeader->coordInfo.addrLen;
  memcpy(addr, &sharedDataHeader->coordInfo.addr, *len);
}

void
SharedData::setCoordHost(struct in_addr *in)
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  JASSERT(in != NULL);
  struct sockaddr_in *sin =
    (struct sockaddr_in *)&sharedDataHeader->coordInfo.addr;
  memcpy(&sin->sin_addr, in, sizeof sin->sin_addr);
}

void
SharedData::getLocalIPAddr(struct in_addr *in)
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  JASSERT(in != NULL);
  memcpy(in, &sharedDataHeader->localIPAddr, sizeof *in);
}

void
SharedData::updateDlsymOffset(int32_t dlsymOffset, int32_t dlsymOffset_m32)
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  JASSERT(sharedDataHeader->dlsymOffset == 0 ||
          sharedDataHeader->dlsymOffset == dlsymOffset)
    (dlsymOffset) (sharedDataHeader->dlsymOffset);

  JASSERT(sharedDataHeader->dlsymOffset_m32 == 0 ||
          sharedDataHeader->dlsymOffset_m32 == dlsymOffset_m32)
    (dlsymOffset_m32) (sharedDataHeader->dlsymOffset_m32);
  sharedDataHeader->dlsymOffset = dlsymOffset;
  sharedDataHeader->dlsymOffset_m32 = dlsymOffset_m32;
}

int32_t
SharedData::getDlsymOffset(void)
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  return sharedDataHeader->dlsymOffset;
}

int32_t
SharedData::getDlsymOffset_m32(void)
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  return sharedDataHeader->dlsymOffset_m32;
}

pid_t
SharedData::getRealPid(pid_t virt)
{
  pid_t res = -1;

  if (sharedDataHeader == NULL) {
    initialize();
  }
  Util::lockFile(PROTECTED_SHM_FD);
  for (size_t i = 0; i < sharedDataHeader->numPidMaps; i++) {
    if (sharedDataHeader->pidMap[i].virt == virt) {
      res = sharedDataHeader->pidMap[i].real;
    }
  }
  Util::unlockFile(PROTECTED_SHM_FD);
  return res;
}

void
SharedData::setPidMap(pid_t virt, pid_t real)
{
  size_t i;

  if (sharedDataHeader == NULL) {
    initialize();
  }
  Util::lockFile(PROTECTED_SHM_FD);
  for (i = 0; i < sharedDataHeader->numPidMaps; i++) {
    if (sharedDataHeader->pidMap[i].virt == virt) {
      sharedDataHeader->pidMap[i].real = real;
      break;
    }
  }
  if (i == sharedDataHeader->numPidMaps) {
    JASSERT(sharedDataHeader->numPidMaps < MAX_PID_MAPS);
    sharedDataHeader->pidMap[i].virt = virt;
    sharedDataHeader->pidMap[i].real = real;
    sharedDataHeader->numPidMaps++;
  }
  Util::unlockFile(PROTECTED_SHM_FD);
}

int32_t
SharedData::getRealIPCId(int type, int32_t virt)
{
  int32_t res = -1;
  uint32_t nmaps = 0;
  IPCIdMap *map = NULL;

  if (sharedDataHeader == NULL) {
    initialize();
  }
  Util::lockFile(PROTECTED_SHM_FD);
  switch (type) {
  case SYSV_SHM_ID:
    nmaps = sharedDataHeader->numSysVShmIdMaps;
    map = sharedDataHeader->sysvShmIdMap;
    break;

  case SYSV_SEM_ID:
    nmaps = sharedDataHeader->numSysVSemIdMaps;
    map = sharedDataHeader->sysvSemIdMap;
    break;

  case SYSV_MSQ_ID:
    nmaps = sharedDataHeader->numSysVMsqIdMaps;
    map = sharedDataHeader->sysvMsqIdMap;
    break;

  case SYSV_SHM_KEY:
    nmaps = sharedDataHeader->numSysVShmKeyMaps;
    map = sharedDataHeader->sysvShmKeyMap;
    break;

  default:
    JASSERT(false) (type).Text("Unknown IPC-Id type.");
    break;
  }
  for (size_t i = 0; i < nmaps; i++) {
    if (map[i].virt == virt) {
      res = map[i].real;
    }
  }
  Util::unlockFile(PROTECTED_SHM_FD);
  return res;
}

void
SharedData::setIPCIdMap(int type, int32_t virt, int32_t real)
{
  size_t i;
  uint64_t *nmaps = NULL;
  IPCIdMap *map = NULL;

  if (sharedDataHeader == NULL) {
    initialize();
  }
  Util::lockFile(PROTECTED_SHM_FD);
  switch (type) {
  case SYSV_SHM_ID:
    nmaps = &sharedDataHeader->numSysVShmIdMaps;
    map = sharedDataHeader->sysvShmIdMap;
    break;

  case SYSV_SEM_ID:
    nmaps = &sharedDataHeader->numSysVSemIdMaps;
    map = sharedDataHeader->sysvSemIdMap;
    break;

  case SYSV_MSQ_ID:
    nmaps = &sharedDataHeader->numSysVMsqIdMaps;
    map = sharedDataHeader->sysvMsqIdMap;
    break;

  case SYSV_SHM_KEY:
    nmaps = &sharedDataHeader->numSysVShmKeyMaps;
    map = sharedDataHeader->sysvShmKeyMap;
    break;

  default:
    JASSERT(false) (type).Text("Unknown IPC-Id type.");
    break;
  }
  for (i = 0; i < *nmaps; i++) {
    if (map[i].virt == virt) {
      map[i].real = real;
      break;
    }
  }
  if (i == *nmaps) {
    JASSERT(*nmaps < MAX_IPC_ID_MAPS);
    map[i].virt = virt;
    map[i].real = real;
    *nmaps += 1;
  }
  Util::unlockFile(PROTECTED_SHM_FD);
}

pid_t
SharedData::getPtraceVirtualId(pid_t tracerId)
{
  pid_t childId = -1;

  if (sharedDataHeader == NULL) {
    initialize();
  }
  Util::lockFile(PROTECTED_SHM_FD);
  for (size_t i = 0; i < sharedDataHeader->numPtraceIdMaps; i++) {
    if (sharedDataHeader->ptraceIdMap[i].tracerId == tracerId) {
      childId = sharedDataHeader->ptraceIdMap[i].childId;
      sharedDataHeader->ptraceIdMap[i] =
        sharedDataHeader->ptraceIdMap[sharedDataHeader->numPtraceIdMaps];
      sharedDataHeader->numPtraceIdMaps--;
    }
  }
  Util::unlockFile(PROTECTED_SHM_FD);
  return childId;
}

void
SharedData::setPtraceVirtualId(pid_t tracerId, pid_t childId)
{
  size_t i;

  if (sharedDataHeader == NULL) {
    initialize();
  }
  Util::lockFile(PROTECTED_SHM_FD);
  for (i = 0; i < sharedDataHeader->numPtraceIdMaps; i++) {
    if (sharedDataHeader->ptraceIdMap[i].tracerId == tracerId) {
      break;
    }
  }

  if (i == sharedDataHeader->numPtraceIdMaps) {
    JASSERT(sharedDataHeader->numPtraceIdMaps < MAX_PTRACE_ID_MAPS);
    sharedDataHeader->numPtraceIdMaps++;
  }
  sharedDataHeader->ptraceIdMap[i].tracerId = tracerId;
  sharedDataHeader->ptraceIdMap[i].childId = childId;
  Util::unlockFile(PROTECTED_SHM_FD);
}

void
SharedData::createVirtualPtyName(const char *real, char *out, uint32_t len)
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  JASSERT(sharedDataHeader->nextVirtualPtyId != (unsigned)-1);

  Util::lockFile(PROTECTED_SHM_FD);
  string virt = VIRT_PTS_PREFIX_STR +
    jalib::XToString(sharedDataHeader->nextVirtualPtyId++);

  // FIXME: We should be removing ptys once they are gone.
  JASSERT(sharedDataHeader->numPtyNameMaps < MAX_PTY_NAME_MAPS);
  size_t n = sharedDataHeader->numPtyNameMaps++;
  JASSERT(strlen(real) < PTS_PATH_MAX);
  JASSERT(virt.length() < PTS_PATH_MAX);
  strcpy(sharedDataHeader->ptyNameMap[n].real, real);
  strcpy(sharedDataHeader->ptyNameMap[n].virt, virt.c_str());
  JASSERT(len > virt.length());
  strcpy(out, virt.c_str());
  Util::unlockFile(PROTECTED_SHM_FD);
}

uint32_t
SharedData::getVirtualPtyId()
{
  return sharedDataHeader->nextVirtualPtyId;
}

void
SharedData::setVirtualPtyId(uint32_t id)
{
  Util::lockFile(PROTECTED_SHM_FD);
  if (id != (uint32_t)-1 && id > sharedDataHeader->nextVirtualPtyId) {
    sharedDataHeader->nextVirtualPtyId = id;
  }
  Util::unlockFile(PROTECTED_SHM_FD);
}

void
SharedData::getRealPtyName(const char *virt, char *out, uint32_t len)
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  *out = '\0';
  Util::lockFile(PROTECTED_SHM_FD);
  for (size_t i = 0; i < sharedDataHeader->numPtyNameMaps; i++) {
    if (strcmp(virt, sharedDataHeader->ptyNameMap[i].virt) == 0) {
      JASSERT(strlen(sharedDataHeader->ptyNameMap[i].real) < len);
      strcpy(out, sharedDataHeader->ptyNameMap[i].real);
      break;
    }
  }
  Util::unlockFile(PROTECTED_SHM_FD);
}

void
SharedData::getVirtPtyName(const char *real, char *out, uint32_t len)
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  *out = '\0';
  Util::lockFile(PROTECTED_SHM_FD);
  for (size_t i = 0; i < sharedDataHeader->numPtyNameMaps; i++) {
    if (strcmp(real, sharedDataHeader->ptyNameMap[i].real) == 0) {
      JASSERT(strlen(sharedDataHeader->ptyNameMap[i].virt) < len);
      strcpy(out, sharedDataHeader->ptyNameMap[i].virt);
      break;
    }
  }
  Util::unlockFile(PROTECTED_SHM_FD);
}

void
SharedData::insertPtyNameMap(const char *virt, const char *real)
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  Util::lockFile(PROTECTED_SHM_FD);
  size_t n = sharedDataHeader->numPtyNameMaps++;
  JASSERT(strlen(virt) < PTS_PATH_MAX);
  JASSERT(strlen(real) < PTS_PATH_MAX);
  strcpy(sharedDataHeader->ptyNameMap[n].real, real);
  strcpy(sharedDataHeader->ptyNameMap[n].virt, virt);
  Util::unlockFile(PROTECTED_SHM_FD);
}

void
SharedData::registerIncomingCons(vector<const char *> &ids,
                                 struct sockaddr_un receiverAddr,
                                 socklen_t len)
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  Util::lockFile(PROTECTED_SHM_FD);
  for (size_t i = 0; i < ids.size(); i++) {
    size_t n = sharedDataHeader->numIncomingConMaps++;
    memcpy(sharedDataHeader->incomingConMap[n].id, ids[i], CON_ID_LEN);
    memcpy(&sharedDataHeader->incomingConMap[n].addr, &receiverAddr, len);
    sharedDataHeader->incomingConMap[n].len = len;
  }
  Util::unlockFile(PROTECTED_SHM_FD);
}

void
SharedData::getMissingConMaps(IncomingConMap **map, uint32_t *nmaps)
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  *map = sharedDataHeader->incomingConMap;
  *nmaps = sharedDataHeader->numIncomingConMaps;
}

void
SharedData::insertInodeConnIdMaps(vector<InodeConnIdMap> &maps)
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  Util::lockFile(PROTECTED_SHM_FD);
  size_t startIdx = sharedDataHeader->numInodeConnIdMaps;
  sharedDataHeader->numInodeConnIdMaps += maps.size();
  Util::unlockFile(PROTECTED_SHM_FD);

  for (size_t i = 0; i < maps.size(); i++) {
    sharedDataHeader->inodeConnIdMap[startIdx + i] = maps[i];
  }
}

bool
SharedData::getCkptLeaderForFile(dev_t devnum, ino_t inode, void *id)
{
  if (sharedDataHeader == NULL) {
    initialize();
  }
  JASSERT(id != NULL);
  if (sharedDataHeader->numInodeConnIdMaps > 0) {
    for (int i = sharedDataHeader->numInodeConnIdMaps - 1; i >= 0; i--) {
      InodeConnIdMap &map = sharedDataHeader->inodeConnIdMap[i];
      if (map.devnum == devnum && map.inode == inode) {
        memcpy(id, map.id, sizeof(map.id));
        return true;
      }
    }
  }
  return false;
}
