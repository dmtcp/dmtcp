/****************************************************************************
 *   Copyright (C) 2012 by Kapil Arya <kapil@ccs.neu.edu>                   *
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
#include "shareddata.h"
#include "coordinatorapi.h"
#include "../jalib/jassert.h"
#include "../jalib/jconvert.h"

#define SHM_MAX_SIZE (sizeof(dmtcp::SharedData::Header))

static struct dmtcp::SharedData::Header *sharedDataHeader = NULL;
static void *prevSharedDataHeaderAddr = NULL;
static unsigned nextVirtualPtyId = -1;

void dmtcp::SharedData::initializeHeader()
{
  off_t size = CEIL(SHM_MAX_SIZE , Util::pageSize());
  JASSERT(lseek(PROTECTED_SHM_FD, size, SEEK_SET) == size)
    (JASSERT_ERRNO);
  Util::writeAll(PROTECTED_SHM_FD, "", 1);
  memset(sharedDataHeader, 0, size);

  strcpy(sharedDataHeader->versionStr, SHM_VERSION_STR);
  sharedDataHeader->coordHost[0] = '\0';
  sharedDataHeader->coordPort = -1;
  sharedDataHeader->ckptInterval = -1;
  sharedDataHeader->numIPCIdMaps = 0;
  sharedDataHeader->numPtraceIdMaps = 0;
  sharedDataHeader->numPtyNameMaps = 0;
  sharedDataHeader->initialized = true;
  sharedDataHeader->numProcessTreeRoots = 0;
  sharedDataHeader->numMissingConMaps = 0;
  // The current implementation simply increments the last count and returns it.
  // Although highly unlikely, this can cause a problem if the counter resets to
  // zero. In that case we should have some more sophisticated code which checks
  // to see if the value pointed by counter is in use or not.
  if (nextVirtualPtyId != -1) {
    sharedDataHeader->nextVirtualPtyId = nextVirtualPtyId;
  } else {
    sharedDataHeader->nextVirtualPtyId = 0;
  }
}

void dmtcp::SharedData::initialize()
{
  /* FIXME: If the coordinator timestamp resolution is 1 second, during
   * subsequent restart, the coordinator timestamp may have the same value
   * causing conflict with SharedData file. In future, a better fix would be to
   * delete the file associated with SharedData in preCkpt phase and recreate
   * it in postCkpt/postRestart phase.
   */
  bool needToInitialize = false;
  if (!Util::isValidFd(PROTECTED_SHM_FD)) {
    dmtcp::ostringstream o;
    o << UniquePid::getTmpDir() << "/dmtcpSharedArea."
      << UniquePid::ComputationId() << "."
      << std::hex << CoordinatorAPI::instance().coordTimeStamp();

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

  sharedDataHeader = (struct Header*) addr;
  prevSharedDataHeaderAddr = addr;

  if (needToInitialize) {
    Util::lockFile(PROTECTED_SHM_FD);
    initializeHeader();
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

void dmtcp::SharedData::preCkpt()
{
  if (sharedDataHeader != NULL) {
    nextVirtualPtyId = sharedDataHeader->nextVirtualPtyId;
    // Need to reset these counter before next post-restart/post-ckpt routines
    sharedDataHeader->numProcessTreeRoots = 0;
    sharedDataHeader->numMissingConMaps = 0;
    size_t size = CEIL(SHM_MAX_SIZE , Util::pageSize());
    JASSERT(_real_munmap(sharedDataHeader, size) == 0) (JASSERT_ERRNO);
    sharedDataHeader = NULL;
  }
}

void dmtcp::SharedData::refill()
{
  if (sharedDataHeader == NULL) initialize();
}

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

int dmtcp::SharedData::getCoordPort()
{
  if (sharedDataHeader == NULL) initialize();
  return sharedDataHeader->coordPort;
}

void dmtcp::SharedData::setCoordPort(int port)
{
  if (sharedDataHeader == NULL) initialize();
  Util::lockFile(PROTECTED_SHM_FD);
  sharedDataHeader->coordPort = port;
  Util::unlockFile(PROTECTED_SHM_FD);
}

int dmtcp::SharedData::getCkptInterval()
{
  if (sharedDataHeader == NULL) initialize();
  return sharedDataHeader->ckptInterval;
}

void dmtcp::SharedData::setCkptInterval(int interval)
{
  if (sharedDataHeader == NULL) initialize();
  Util::lockFile(PROTECTED_SHM_FD);
  sharedDataHeader->ckptInterval = interval;
  Util::unlockFile(PROTECTED_SHM_FD);
}

int dmtcp::SharedData::getRealIPCId(int virt)
{
  int res = -1;
  if (sharedDataHeader == NULL) initialize();
  Util::lockFile(PROTECTED_SHM_FD);
  for (size_t i = 0; i < sharedDataHeader->numIPCIdMaps; i++) {
    if (sharedDataHeader->ipcIdMap[i].virt == virt) {
      res = sharedDataHeader->ipcIdMap[i].real;
    }
  }
  Util::unlockFile(PROTECTED_SHM_FD);
  return res;
}

void dmtcp::SharedData::setIPCIdMap(int virt, int real)
{
  size_t i;
  if (sharedDataHeader == NULL) initialize();
  Util::lockFile(PROTECTED_SHM_FD);
  for (i = 0; i < sharedDataHeader->numIPCIdMaps; i++) {
    if (sharedDataHeader->ipcIdMap[i].virt == virt) {
      sharedDataHeader->ipcIdMap[i].real = real;
      break;
    }
  }
  if (i == sharedDataHeader->numIPCIdMaps) {
    JASSERT(sharedDataHeader->numIPCIdMaps < MAX_IPC_ID_MAPS);
    sharedDataHeader->ipcIdMap[i].virt = virt;
    sharedDataHeader->ipcIdMap[i].real = real;
    sharedDataHeader->numIPCIdMaps++;
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
    JASSERT(sharedDataHeader->ptraceIdMap[i].tracerId != tracerId)
      (tracerId)
      (sharedDataHeader->ptraceIdMap[i].tracerId)
      (sharedDataHeader->ptraceIdMap[i].childId)
      .Text ("Duplicate Entry");
  }

  JASSERT(sharedDataHeader->numPtraceIdMaps < MAX_PTRACE_ID_MAPS);
  i = sharedDataHeader->numPtraceIdMaps;
  sharedDataHeader->ptraceIdMap[i].tracerId = tracerId;
  sharedDataHeader->ptraceIdMap[i].childId = childId;
  sharedDataHeader->numPtraceIdMaps++;
  Util::unlockFile(PROTECTED_SHM_FD);
}

void dmtcp::SharedData::setProcessTreeRoot()
{
  if (sharedDataHeader == NULL) initialize();
  Util::lockFile(PROTECTED_SHM_FD);
  JASSERT(sharedDataHeader->numProcessTreeRoots < MAX_PROCESS_TREE_ROOTS);
  size_t i = sharedDataHeader->numProcessTreeRoots;
  sharedDataHeader->processTreeRoots[i] = UniquePid::ThisProcess();
  sharedDataHeader->numProcessTreeRoots++;
  Util::unlockFile(PROTECTED_SHM_FD);
}

void dmtcp::SharedData::getProcessTreeRoots(dmtcp::UniquePid **roots,
                                            size_t *numRoots)
{
  if (sharedDataHeader == NULL) initialize();
  Util::lockFile(PROTECTED_SHM_FD);
  *roots = sharedDataHeader->processTreeRoots;
  *numRoots = sharedDataHeader->numProcessTreeRoots;
  Util::unlockFile(PROTECTED_SHM_FD);
}

void dmtcp::SharedData::createVirtualPtyName(const char* real, char *out,
                                             size_t len)
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

void dmtcp::SharedData::getRealPtyName(const char* virt, char *out, size_t len)
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

void dmtcp::SharedData::getVirtPtyName(const char* real, char *out, size_t len)
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
                                          size_t *nmaps)
{
  if (sharedDataHeader == NULL) initialize();
  *map = sharedDataHeader->missingConMap;
  *nmaps = sharedDataHeader->numMissingConMaps;
}
