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

#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>

#include "constants.h"
#include "protectedfds.h"
#include "dmtcpalloc.h"
#include "uniquepid.h"
#include "syscallwrappers.h"
#include "util.h"
#include  "membarrier.h"
#include "coordinatorapi.h"
#include "shareddata.h"
#include "../jalib/jassert.h"
#include "../jalib/jconvert.h"

#define SHM_MAX_SIZE (sizeof(dmtcp::SharedData::Header))

using namespace dmtcp;
static struct dmtcp::SharedData::Header *sharedDataHeader = NULL;
static void *prevSharedDataHeaderAddr = NULL;
static uint32_t nextVirtualPtyId = (uint32_t)-1;

void dmtcp_SharedData_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_INIT:
      break;

    case DMTCP_EVENT_THREADS_SUSPEND:
      SharedData::suspended();
      break;

    case DMTCP_EVENT_REGISTER_NAME_SERVICE_DATA:
    case DMTCP_EVENT_REFILL:
      dmtcp::SharedData::refill();
      break;

    case DMTCP_EVENT_RESTART:
      SharedData::updateHostAndPortEnv();
      break;

    default:
      break;
  }
}

void dmtcp::SharedData::initializeHeader(const char *tmpDir,
                                         DmtcpUniqueProcessId *compId,
                                         CoordinatorInfo *coordInfo,
                                         struct in_addr *localIPAddr)
{
  JASSERT(tmpDir != NULL && coordInfo != NULL && localIPAddr != NULL);
  off_t size = CEIL(SHM_MAX_SIZE , Util::pageSize());
  JASSERT(lseek(PROTECTED_SHM_FD, size, SEEK_SET) == size)
    (JASSERT_ERRNO);
  Util::writeAll(PROTECTED_SHM_FD, "", 1);
  memset(sharedDataHeader, 0, size);

  strcpy(sharedDataHeader->versionStr, SHM_VERSION_STR);
#if 0
  sharedDataHeader->coordHost[0] = '\0';
  sharedDataHeader->coordPort = -1;
  sharedDataHeader->ckptInterval = -1;
#endif
  JASSERT(getenv(ENV_VAR_DLSYM_OFFSET) != NULL);
  sharedDataHeader->dlsymOffset =
    (int32_t) strtol(getenv(ENV_VAR_DLSYM_OFFSET), NULL, 10);
  JASSERT(getenv(ENV_VAR_DLSYM_OFFSET_M32) != NULL);
  sharedDataHeader->dlsymOffset_m32 =
    (int32_t) strtol(getenv(ENV_VAR_DLSYM_OFFSET_M32), NULL, 10);
  sharedDataHeader->numSysVShmIdMaps = 0;
  sharedDataHeader->numSysVSemIdMaps = 0;
  sharedDataHeader->numSysVMsqIdMaps = 0;
  sharedDataHeader->numPtraceIdMaps = 0;
  sharedDataHeader->numPtyNameMaps = 0;
  sharedDataHeader->initialized = true;
  sharedDataHeader->numMissingConMaps = 0;
  memcpy(&sharedDataHeader->compId, compId, sizeof(*compId));
  memcpy(&sharedDataHeader->coordInfo, coordInfo, sizeof (*coordInfo));
  memcpy(&sharedDataHeader->localIPAddr, localIPAddr, sizeof (*localIPAddr));
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
}

void dmtcp::SharedData::initialize(const char *tmpDir = NULL,
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
    dmtcp::ostringstream o;
    o << tmpDir << "/dmtcpSharedArea."
      << *compId << "." << std::hex << coordInfo->timeStamp;

    int fd = _real_open(o.str().c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd == -1 && errno == EEXIST) {
      fd = _real_open(o.str().c_str(), O_RDWR, 0600);
    } else {
      needToInitialize = true;
    }
    JASSERT(fd != -1) (JASSERT_ERRNO);
    JASSERT(_real_dup2(fd, PROTECTED_SHM_FD) == PROTECTED_SHM_FD)
      (JASSERT_ERRNO);
    _real_close(fd);
  }

  size_t size = CEIL(SHM_MAX_SIZE , Util::pageSize());
  void *addr = _real_mmap(prevSharedDataHeaderAddr, size,
                          PROT_READ | PROT_WRITE, MAP_SHARED,
                          PROTECTED_SHM_FD, 0);
  JASSERT(addr != MAP_FAILED) (JASSERT_ERRNO)
    .Text("Unable to find shared area.");

#if __arm__
  WMB;  // Ensure store to memory by kernel mmap call has completed
#endif

  sharedDataHeader = (struct Header*) addr;
  prevSharedDataHeaderAddr = addr;

  if (needToInitialize) {
    Util::lockFile(PROTECTED_SHM_FD);
    initializeHeader(tmpDir, compId, coordInfo, localIPAddr);
    Util::unlockFile(PROTECTED_SHM_FD);
  } else {
    struct stat statbuf;
    while (1) {
      Util::lockFile(PROTECTED_SHM_FD);
      JASSERT(fstat(PROTECTED_SHM_FD, &statbuf) != -1) (JASSERT_ERRNO);
      Util::unlockFile(PROTECTED_SHM_FD);
      if (statbuf.st_size > 0) {
        break;
      }
      struct timespec sleepTime = {0, 100*1000*1000};
      nanosleep(&sleepTime, NULL);
    }

    Util::lockFile(PROTECTED_SHM_FD);
    if (!dmtcp::Util::strStartsWith(sharedDataHeader->versionStr,
                                    SHM_VERSION_STR)) {
      JASSERT(false) (sharedDataHeader->versionStr) (SHM_VERSION_STR)
        .Text("Wrong signature");
    }
    Util::unlockFile(PROTECTED_SHM_FD);
  }
  JTRACE("Shared area mapped") (sharedDataHeader);
}

void dmtcp::SharedData::suspended()
{
  if (sharedDataHeader == NULL) initialize();
  sharedDataHeader->numInodeConnIdMaps = 0;
}

void dmtcp::SharedData::preCkpt()
{
  if (sharedDataHeader != NULL) {
    nextVirtualPtyId = sharedDataHeader->nextVirtualPtyId;
    // Need to reset these counters before next post-restart/post-ckpt routines
    sharedDataHeader->numMissingConMaps = 0;
WMB;
    size_t size = CEIL(SHM_MAX_SIZE, Util::pageSize());
    JASSERT(_real_munmap(sharedDataHeader, size) == 0) (JASSERT_ERRNO);
    sharedDataHeader = NULL;
  }
}

void dmtcp::SharedData::refill()
{
  if (sharedDataHeader == NULL) initialize();
}

// At restart, the HOST/PORT used by dmtcp_coordinator could be different then
// those at checkpoint time. This could cause the child processes created after
// restart to fail to connect to the coordinator.
extern "C" int fred_record_replay_enabled() __attribute__ ((weak));
void dmtcp::SharedData::updateHostAndPortEnv()
{
  if (CoordinatorAPI::noCoordinator()) return;
  if (sharedDataHeader == NULL) initialize();

  /* This calls setenv() which calls malloc. Since this is only executed on
     restart, that means it there is an extra malloc on replay. Commenting this
     until we have time to fix it. */
  if (fred_record_replay_enabled != 0) return;

  struct sockaddr_storage *currAddr = &sharedDataHeader->coordInfo.addr;
  /* If the current coordinator is running on a HOST/PORT other than the
   * pre-checkpoint HOST/PORT, we need to update the environment variables
   * pointing to the coordinator HOST/PORT. This is needed if the new
   * coordinator has been moved around.
   */
  char ipstr[INET6_ADDRSTRLEN];
  int port;
  dmtcp::string portStr;

  // deal with both IPv4 and IPv6:
  if (currAddr->ss_family == AF_INET) {
    struct sockaddr_in *s = (struct sockaddr_in *)currAddr;
    port = ntohs(s->sin_port);
    inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);
  } else { // AF_INET6
    JASSERT (currAddr->ss_family == AF_INET6) (currAddr->ss_family);
    struct sockaddr_in6 *s = (struct sockaddr_in6 *)currAddr;
    port = ntohs(s->sin6_port);
    inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof ipstr);
  }

  portStr = jalib::XToString(port);
  if (getenv(ENV_VAR_NAME_HOST) && strcmp(getenv(ENV_VAR_NAME_HOST), ipstr)) {
    JASSERT(0 == setenv(ENV_VAR_NAME_HOST, ipstr, 1)) (JASSERT_ERRNO);
  }
  if (getenv(ENV_VAR_NAME_PORT) && strcmp(getenv(ENV_VAR_NAME_PORT),
                                          portStr.c_str())) {
    JASSERT(0 == setenv(ENV_VAR_NAME_PORT, portStr.c_str(), 1)) (JASSERT_ERRNO);
  }
}
#if 0
dmtcp::string dmtcp::SharedData::getCoordHost()
{
  if (sharedDataHeader == NULL) initialize();
  return sharedDataHeader->coordHost;
}

void dmtcp::SharedData::setCoordHost(const char *host)
{
  if (sharedDataHeader == NULL) initialize();
  JASSERT(strlen(host) < sizeof(sharedDataHeader->coordHost));
  Util::lockFile(PROTECTED_SHM_FD);
  strcpy(sharedDataHeader->coordHost, host);
  Util::unlockFile(PROTECTED_SHM_FD);
}

uint32_t dmtcp::SharedData::getCoordPort()
{
  if (sharedDataHeader == NULL) initialize();
  return sharedDataHeader->coordPort;
}

void dmtcp::SharedData::setCoordPort(uint32_t port)
{
  if (sharedDataHeader == NULL) initialize();
  Util::lockFile(PROTECTED_SHM_FD);
  sharedDataHeader->coordPort = port;
  Util::unlockFile(PROTECTED_SHM_FD);
}
#endif

char *dmtcp::SharedData::getTmpDir(char *buf, uint32_t len)
{
  if (sharedDataHeader == NULL) initialize();
  JASSERT(sharedDataHeader->tmpDir[0] != '\0');
  if (len <= strlen(sharedDataHeader->tmpDir)) {
    return NULL;
  }
  strcpy(buf, sharedDataHeader->tmpDir);
  return buf;
}

uint32_t dmtcp::SharedData::getCkptInterval()
{
  if (sharedDataHeader == NULL) initialize();
  return sharedDataHeader->coordInfo.interval;
}

void dmtcp::SharedData::setCkptInterval(uint32_t interval)
{
  if (sharedDataHeader == NULL) initialize();
  Util::lockFile(PROTECTED_SHM_FD);
  sharedDataHeader->coordInfo.interval = interval;
  Util::unlockFile(PROTECTED_SHM_FD);
}

void dmtcp::SharedData::updateGeneration(uint32_t generation)
{
  if (sharedDataHeader == NULL) initialize();
  sharedDataHeader->compId._generation = generation;
}
DmtcpUniqueProcessId dmtcp::SharedData::getCompId()
{
  if (sharedDataHeader == NULL) initialize();
  return sharedDataHeader->compId;
}

DmtcpUniqueProcessId dmtcp::SharedData::getCoordId()
{
  if (sharedDataHeader == NULL) initialize();
  return sharedDataHeader->coordInfo.id;
}

uint64_t dmtcp::SharedData::getCoordTimeStamp()
{
  if (sharedDataHeader == NULL) initialize();
  return sharedDataHeader->coordInfo.timeStamp;
}

void dmtcp::SharedData::getCoordAddr(struct sockaddr *addr, uint32_t *len)
{
  if (sharedDataHeader == NULL) initialize();
  JASSERT(addr != NULL);
  *len = sharedDataHeader->coordInfo.addrLen;
  memcpy(addr, &sharedDataHeader->coordInfo.addr, *len);
}

void dmtcp::SharedData::getLocalIPAddr(struct in_addr *in)
{
  if (sharedDataHeader == NULL) initialize();
  JASSERT(in != NULL);
  memcpy(in, &sharedDataHeader->localIPAddr, sizeof *in);
}

void dmtcp::SharedData::updateDlsymOffset(int32_t dlsymOffset,
                                          int32_t dlsymOffset_m32)
{
  if (sharedDataHeader == NULL) initialize();
  JASSERT(sharedDataHeader->dlsymOffset == 0 ||
          sharedDataHeader->dlsymOffset == dlsymOffset)
    (dlsymOffset) (sharedDataHeader->dlsymOffset);

  JASSERT(sharedDataHeader->dlsymOffset_m32 == 0 ||
          sharedDataHeader->dlsymOffset_m32 == dlsymOffset_m32)
    (dlsymOffset_m32) (sharedDataHeader->dlsymOffset_m32);
  sharedDataHeader->dlsymOffset = dlsymOffset;
  sharedDataHeader->dlsymOffset_m32 =  dlsymOffset_m32;
}

int32_t dmtcp::SharedData::getDlsymOffset(void)
{
  if (sharedDataHeader == NULL) initialize();
  JASSERT(sharedDataHeader->dlsymOffset != 0);
  return sharedDataHeader->dlsymOffset;
}

int32_t dmtcp::SharedData::getDlsymOffset_m32(void)
{
  if (sharedDataHeader == NULL) initialize();
  return sharedDataHeader->dlsymOffset_m32;
}

pid_t dmtcp::SharedData::getRealPid(pid_t virt)
{
  pid_t res = -1;
  if (sharedDataHeader == NULL) initialize();
  Util::lockFile(PROTECTED_SHM_FD);
  for (size_t i = 0; i < sharedDataHeader->numPidMaps; i++) {
    if (sharedDataHeader->pidMap[i].virt == virt) {
      res = sharedDataHeader->pidMap[i].real;
    }
  }
  Util::unlockFile(PROTECTED_SHM_FD);
  return res;
}

void dmtcp::SharedData::setPidMap(pid_t virt, pid_t real)
{
  size_t i;
  if (sharedDataHeader == NULL) initialize();
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

int32_t dmtcp::SharedData::getRealIPCId(int type, int32_t virt)
{
  int32_t res = -1;
  uint32_t nmaps = 0;
  IPCIdMap *map = NULL;
  if (sharedDataHeader == NULL) initialize();
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

    default:
      JASSERT(false) (type) .Text("Unknown IPC-Id type.");
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

void dmtcp::SharedData::setIPCIdMap(int type, int32_t virt, int32_t real)
{
  size_t i;
  uint32_t *nmaps = NULL;
  IPCIdMap *map = NULL;
  if (sharedDataHeader == NULL) initialize();
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

    default:
      JASSERT(false) (type) .Text("Unknown IPC-Id type.");
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

pid_t dmtcp::SharedData::getPtraceVirtualId(pid_t tracerId)
{
  pid_t childId = -1;
  if (sharedDataHeader == NULL) initialize();
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

void dmtcp::SharedData::setPtraceVirtualId(pid_t tracerId, pid_t childId)
{
  size_t i;
  if (sharedDataHeader == NULL) initialize();
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

void dmtcp::SharedData::createVirtualPtyName(const char* real, char *out,
                                             uint32_t len)
{
  if (sharedDataHeader == NULL) initialize();
  JASSERT(sharedDataHeader->nextVirtualPtyId != (unsigned) -1);

  Util::lockFile(PROTECTED_SHM_FD);
  dmtcp::string virt = VIRT_PTS_PREFIX_STR +
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

void dmtcp::SharedData::getRealPtyName(const char* virt,
                                       char *out, uint32_t len)
{
  if (sharedDataHeader == NULL) initialize();
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

void dmtcp::SharedData::getVirtPtyName(const char* real,
                                       char *out, uint32_t len)
{
  if (sharedDataHeader == NULL) initialize();
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

void dmtcp::SharedData::insertPtyNameMap(const char* virt, const char* real)
{
  if (sharedDataHeader == NULL) initialize();
  Util::lockFile(PROTECTED_SHM_FD);
  size_t n = sharedDataHeader->numPtyNameMaps++;
  JASSERT(strlen(virt) < PTS_PATH_MAX);
  JASSERT(strlen(real) < PTS_PATH_MAX);
  strcpy(sharedDataHeader->ptyNameMap[n].real, real);
  strcpy(sharedDataHeader->ptyNameMap[n].virt, virt);
  Util::unlockFile(PROTECTED_SHM_FD);
}

void dmtcp::SharedData::registerMissingCons(vector<const char*>& ids,
                                            struct sockaddr_un receiverAddr,
                                            socklen_t len)
{
  if (sharedDataHeader == NULL) initialize();
  Util::lockFile(PROTECTED_SHM_FD);
  for (size_t i = 0; i < ids.size(); i++) {
    size_t n = sharedDataHeader->numMissingConMaps++;
    memcpy(sharedDataHeader->missingConMap[n].id, ids[i], CON_ID_LEN);
    memcpy(&sharedDataHeader->missingConMap[n].addr, &receiverAddr, len);
    sharedDataHeader->missingConMap[n].len = len;
  }
  Util::unlockFile(PROTECTED_SHM_FD);
}

void dmtcp::SharedData::getMissingConMaps(struct SharedData::MissingConMap **map,
                                          uint32_t *nmaps)
{
  if (sharedDataHeader == NULL) initialize();
  *map = sharedDataHeader->missingConMap;
  *nmaps = sharedDataHeader->numMissingConMaps;
}

void SharedData::insertInodeConnIdMaps(vector<SharedData::InodeConnIdMap>& maps)
{
  if (sharedDataHeader == NULL) initialize();
  Util::lockFile(PROTECTED_SHM_FD);
  size_t startIdx = sharedDataHeader->numInodeConnIdMaps;
  sharedDataHeader->numInodeConnIdMaps += maps.size();
  Util::unlockFile(PROTECTED_SHM_FD);

  for (size_t i = 0; i < maps.size(); i++) {
    sharedDataHeader->inodeConnIdMap[startIdx + i] = maps[i];
  }
}

bool SharedData::getCkptLeaderForFile(dev_t devnum, ino_t inode, void *id)
{
  if (sharedDataHeader == NULL) initialize();
  JASSERT(id != NULL);
  if (sharedDataHeader->numInodeConnIdMaps > 0) {
    for (size_t i = sharedDataHeader->numInodeConnIdMaps - 1; i >= 0; i--) {
      InodeConnIdMap& map = sharedDataHeader->inodeConnIdMap[i];
      if (map.devnum == devnum && map.inode== inode) {
        memcpy(id, map.id, sizeof(map.id));
        return true;
      }
    }
  }
  return false;
}
