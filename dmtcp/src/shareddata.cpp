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
#include <sys/shm.h>

#include "constants.h"
#include "protectedfds.h"
#include "dmtcpalloc.h"
#include "uniquepid.h"
#include "syscallwrappers.h"
#include "util.h"
#include "shareddata.h"
#include "coordinatorapi.h"
#include "../jalib/jassert.h"

#define SHM_MAX_SIZE (sizeof(dmtcp::SharedData::Header))

static struct dmtcp::SharedData::Header *sharedDataHeader = NULL;
static void *prevSharedDataHeaderAddr = NULL;

void dmtcp::SharedData::processEvent(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_INIT:
    case DMTCP_EVENT_POST_RESTART:
      initialize();
      break;

    case DMTCP_EVENT_POST_CKPT:
      if (!data->postCkptInfo.isRestart) {
        initialize();
      }
      break;

    case DMTCP_EVENT_POST_EXEC:
      if (data->serializerInfo.fd != -1) {
        initialize();
      }
      break;

    case DMTCP_EVENT_PREPARE_FOR_FORK:
      break;

    case DMTCP_EVENT_RESET_ON_FORK:
      break;

    case DMTCP_EVENT_PRE_CKPT:
      preCkpt();
      break;

    default:
      break;
  }
}

void dmtcp::SharedData::initializeHeader()
{
  off_t size = (SHM_MAX_SIZE + Util::pageSize() - 1) & Util::pageMask();
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
  sharedDataHeader->initialized = true;
}

void dmtcp::SharedData::initialize()
{
  bool preExisting = false;
  if (!Util::isValidFd(PROTECTED_SHM_FD)) {
    dmtcp::ostringstream o;
    o << UniquePid::getTmpDir() << "/dmtcpSharedArea."
      << UniquePid::ComputationId() << "."
      << std::hex << CoordinatorAPI::instance().coordTimeStamp();

    int fd = _real_open(o.str().c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd == -1 && errno == EEXIST) {
      fd = _real_open(o.str().c_str(), O_RDWR, 0600);
      preExisting = true;
    }
    JASSERT(fd != -1) (JASSERT_ERRNO);
    JASSERT(dup2(fd, PROTECTED_SHM_FD) == PROTECTED_SHM_FD) (JASSERT_ERRNO);
    _real_close(fd);
  }

  size_t size = (SHM_MAX_SIZE + Util::pageSize() - 1) & Util::pageMask();
  void *addr = _real_mmap(prevSharedDataHeaderAddr, size,
                          PROT_READ | PROT_WRITE, MAP_SHARED,
                          PROTECTED_SHM_FD, 0);
  JASSERT(addr != MAP_FAILED) (JASSERT_ERRNO)
    .Text("Unable to find shared area.");

  sharedDataHeader = (struct Header*) addr;
  prevSharedDataHeaderAddr = addr;

  if (!preExisting) {
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
  size_t size = (SHM_MAX_SIZE + Util::pageSize() - 1) & Util::pageMask();
  JASSERT(_real_munmap(sharedDataHeader, size) == 0) (JASSERT_ERRNO);
  sharedDataHeader = NULL;
}

dmtcp::string dmtcp::SharedData::getCoordHost()
{
  JASSERT(sharedDataHeader != NULL);
  return sharedDataHeader->coordHost;
}

void dmtcp::SharedData::setCoordHost(const char *host)
{
  JASSERT(sharedDataHeader != NULL);
  JASSERT(strlen(host) < sizeof(sharedDataHeader->coordHost));
  Util::lockFile(PROTECTED_SHM_FD);
  strcpy(sharedDataHeader->coordHost, host);
  Util::unlockFile(PROTECTED_SHM_FD);
}

int dmtcp::SharedData::getCoordPort()
{
  JASSERT(sharedDataHeader != NULL);
  return sharedDataHeader->coordPort;
}

void dmtcp::SharedData::setCoordPort(int port)
{
  JASSERT(sharedDataHeader != NULL);
  Util::lockFile(PROTECTED_SHM_FD);
  sharedDataHeader->coordPort = port;
  Util::unlockFile(PROTECTED_SHM_FD);
}

int dmtcp::SharedData::getCkptInterval()
{
  JASSERT(sharedDataHeader != NULL);
  return sharedDataHeader->ckptInterval;
}

void dmtcp::SharedData::setCkptInterval(int interval)
{
  JASSERT(sharedDataHeader != NULL);
  Util::lockFile(PROTECTED_SHM_FD);
  sharedDataHeader->ckptInterval = interval;
  Util::unlockFile(PROTECTED_SHM_FD);
}

int dmtcp::SharedData::getRealIPCId(int virtualId)
{
  int res = -1;
  JASSERT(sharedDataHeader != NULL);
  Util::lockFile(PROTECTED_SHM_FD);
  for (size_t i = 0; i < sharedDataHeader->numIPCIdMaps; i++) {
    if (sharedDataHeader->ipcIdMap[i].virtualId == virtualId) {
      res = sharedDataHeader->ipcIdMap[i].realId;
    }
  }
  Util::unlockFile(PROTECTED_SHM_FD);
  return res;
}

void dmtcp::SharedData::setIPCIdMap(int virtualId, int realId)
{
  size_t i;
  JASSERT(sharedDataHeader != NULL);
  Util::lockFile(PROTECTED_SHM_FD);
  for (i = 0; i < sharedDataHeader->numIPCIdMaps; i++) {
    if (sharedDataHeader->ipcIdMap[i].virtualId == virtualId) {
      sharedDataHeader->ipcIdMap[i].realId = realId;
      break;
    }
  }
  if (i == sharedDataHeader->numIPCIdMaps) {
    JASSERT(sharedDataHeader->numIPCIdMaps < MAX_IPC_ID_MAPS);
    sharedDataHeader->ipcIdMap[i].virtualId = virtualId;
    sharedDataHeader->ipcIdMap[i].realId = realId;
    sharedDataHeader->numIPCIdMaps++;
  }
  Util::unlockFile(PROTECTED_SHM_FD);
}

pid_t dmtcp::SharedData::getPtraceVirtualId(pid_t tracerId)
{
  pid_t childId = -1;
  if (sharedDataHeader == NULL) {
    return -1;
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

void dmtcp::SharedData::setPtraceVirtualId(pid_t tracerId, pid_t childId)
{
  size_t i;
  JASSERT(sharedDataHeader != NULL);
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
