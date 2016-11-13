/*****************************************************************************
 *   Copyright (C) 2008-2013 Ana-Maria Visan, Kapil Arya, and Gene Cooperman *
 *   amvisan@cs.neu.edu, kapil@cs.neu.edu, and gene@ccs.neu.edu              *
 *                                                                           *
 *  This file is part of the PTRACE plugin of DMTCP (DMTCP:plugin/ptrace).   *
 *                                                                           *
 *  DMTCP:mtcp is free software: you can redistribute it and/or              *
 *  modify it under the terms of the GNU Lesser General Public License as    *
 *  published by the Free Software Foundation, either version 3 of the       *
 *  License, or (at your option) any later version.                          *
 *                                                                           *
 *  DMTCP:plugin/ptrace is distributed in the hope that it will be useful,   *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *  GNU Lesser General Public License for more details.                      *
 *                                                                           *
 *  You should have received a copy of the GNU Lesser General Public         *
 *  License along with DMTCP:dmtcp/src.  If not, see                         *
 *  <http://www.gnu.org/licenses/>.                                          *
 *****************************************************************************/

#include <fcntl.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include <linux/limits.h>
#include <linux/unistd.h>
#include <linux/version.h>

#include "jassert.h"
#include "jfilesystem.h"
#include "dmtcp.h"
#include "ptrace.h"
#include "ptraceinfo.h"
#include "util.h"

using namespace dmtcp;

static PtraceInfo *_ptraceInfo = NULL;
PtraceInfo&
PtraceInfo::instance()
{
  if (_ptraceInfo == NULL) {
    _ptraceInfo = new PtraceInfo();
  }
  return *_ptraceInfo;
}

void
PtraceInfo::createSharedFile()
{
  struct stat statbuf;
  int fd = dmtcp_get_ptrace_fd();

  if (fstat(fd, &statbuf) == -1 && errno == EBADF) {
    char path[PATH_MAX];
    int ptrace_fd = dmtcp_get_ptrace_fd();

    sprintf(path, "%s/%s-%s.%lx", dmtcp_get_tmpdir(), "ptraceSharedInfo",
            dmtcp_get_computation_id_str(),
            (unsigned long)dmtcp_get_coordinator_timestamp());

    int fd = _real_open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
    JASSERT(fd != -1) (path) (JASSERT_ERRNO);

    JASSERT(_real_lseek(fd, _sharedDataSize,
                        SEEK_SET) == (off_t)_sharedDataSize)
      (path) (_sharedDataSize) (JASSERT_ERRNO);
    Util::writeAll(fd, "", 1);
    JASSERT(_real_unlink(path) == 0) (path) (JASSERT_ERRNO);
    JASSERT(_real_dup2(fd, ptrace_fd) == ptrace_fd) (fd) (ptrace_fd);
    close(fd);
  }
}

void
PtraceInfo::mapSharedFile()
{
  int fd = dmtcp_get_ptrace_fd();

  _sharedData = (PtraceSharedData *)_real_mmap(0, _sharedDataSize,
                                               PROT_READ | PROT_WRITE,
                                               MAP_SHARED, fd, 0);
  JASSERT(_sharedData != MAP_FAILED) (fd) (_sharedDataSize);

  _sharedData->init();
}

bool
PtraceInfo::isPtracing()
{
  if (_sharedData == NULL) {
    mapSharedFile();
  }
  return _sharedData->isPtracing();
}

void
PtraceInfo::markAsCkptThread()
{
  if (_sharedData == NULL) {
    mapSharedFile();
  }
  pid_t superior = Util::getTracerPid();
  if (superior != 0) {
    _sharedData->insertInferior(superior, GETTID(), true);
  }
}

vector<pid_t>PtraceInfo::getInferiorVector(pid_t tid)
{
  if (_supToInfsMap.find(tid) == _supToInfsMap.end()) {
    vector<pid_t>vec;
    return vec;
  }
  return _supToInfsMap[tid];
}

void
PtraceInfo::setLastCmd(pid_t tid, int lastCmd)
{
  if (!isPtracing()) {
    return;
  }
  Inferior *inf = _sharedData->getInferior(tid);
  if (inf == NULL) {
    inf = _sharedData->insertInferior(getpid(), tid);
  }
  inf->setLastCmd(lastCmd);
}

void
PtraceInfo::insertInferior(pid_t tid)
{
  Inferior *inf = _sharedData->getInferior(tid);

  if (inf == NULL) {
    inf = _sharedData->insertInferior(GETTID(), tid);
  }
  _supToInfsMap[inf->superior()].push_back(tid);
  _infToSupMap[tid] = inf->superior();
}

void
PtraceInfo::eraseInferior(pid_t tid)
{
  if (_sharedData == NULL) {
    mapSharedFile();
  }
  Inferior *inf = _sharedData->getInferior(tid);
  JASSERT(inf != NULL) (tid);
  pid_t superior = inf->superior();
  _sharedData->eraseInferior(inf);

  vector<int> &vec = _supToInfsMap[superior];
  vector<int>::iterator it;
  for (it = vec.begin(); it != vec.end(); it++) {
    if (*it == tid) {
      vec.erase(it);
      break;
    }
  }

  _infToSupMap.erase(tid);
}

bool
PtraceInfo::isInferior(pid_t tid)
{
  Inferior *inf = _sharedData->getInferior(tid);

  if (inf != NULL) {
    return inf->superior() == GETTID();
  }
  return false;
}

void
PtraceInfo::setPtracing()
{
  static int markPtracing = 0;

  if (!markPtracing) {
    if (_sharedData == NULL) {
      mapSharedFile();
    }
    _sharedData->setPtracing();
    markPtracing = 1;
  }
}

void
PtraceInfo::processSuccessfulPtraceCmd(int request,
                                       pid_t pid,
                                       void *addr,
                                       void *data)
{
  Inferior *inf;

  if (pid <= 0) {
    return;
  }

  switch (request) {
  case PTRACE_TRACEME:
    _sharedData->insertInferior(getppid(), pid);
    return;

  case PTRACE_ATTACH:
    break;

  case PTRACE_SINGLESTEP:
    JTRACE("PTRACE_SINGLESTEP") (pid);
    break;

  case PTRACE_DETACH:
  case PTRACE_KILL:
    if (isInferior(pid)) {
      eraseInferior(pid);
    }
    return;

  case PTRACE_CONT:
    inf = _sharedData->getInferior(pid);
    if (inf == NULL) {
      inf = _sharedData->insertInferior(getpid(), pid);
    }
    inf->setLastCmd(request);
    JTRACE("PTRACE_CONT") (pid);
    break;

  case PTRACE_SYSCALL:
    inf = _sharedData->getInferior(pid);
    if (inf == NULL) {
      inf = _sharedData->insertInferior(getpid(), pid);
    }
    inf->setLastCmd(request);
    JTRACE("PTRACE_SYSCALL") (pid);
    break;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 6)
  case PTRACE_SETOPTIONS:
    inf = _sharedData->getInferior(pid);
    if (inf == NULL) {
      inf = _sharedData->insertInferior(getpid(), pid);
    }
    inf->setPtraceOptions(data);
    JTRACE("PTRACE_SETOPTIONS") (pid);
    break;
#endif // if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 6)

  default:
    JTRACE("PTRACE_XXX") (pid) (request);
    break;
  }

  if (_infToSupMap.find(pid) == _infToSupMap.end()) {
    insertInferior(pid);
  }
}

void
PtraceInfo::processSetOptions(pid_t tid, void *data)
{
  // TODO:
}

pid_t
PtraceInfo::getWait4Status(pid_t tid, int *status, struct rusage *rusage)
{
  if (!isPtracing()) {
    return -1;
  }
  JASSERT(status != NULL);
  Inferior *inf = _sharedData->getInferior(tid);
  if (inf != NULL && inf->getWait4Status(status, rusage) != -1) {
    return inf->tid();
  }
  return -1;
}

void
PtraceInfo::waitForSuperiorAttach()
{
  if (_sharedData == NULL) {
    mapSharedFile();
  }
  Inferior *inf = _sharedData->getInferior(GETTID());
  if (inf == NULL) {
    return;
  }
  inf->semWait();
  inf->semDestroy();
}

void
PtraceInfo::processPreResumeAttach(pid_t inferior)
{
  Inferior *inf = _sharedData->getInferior(inferior);

  JASSERT(inf != NULL) (inferior);
  inf->semPost();
}
