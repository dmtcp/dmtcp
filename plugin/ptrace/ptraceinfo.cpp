/*****************************************************************************
 *   Copyright (C) 2008-2012 by Ana-Maria Visan, Kapil Arya, and             *
 *                                                            Gene Cooperman *
 *   amvisan@cs.neu.edu, kapil@cs.neu.edu, and gene@ccs.neu.edu              *
 *                                                                           *
 *   This file is part of the PTRACE plugin of DMTCP (DMTCP:mtcp).           *
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

#include <stdarg.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <linux/version.h>
// This was needed for:  SUSE LINUX 10.0 (i586) OSS
#ifndef PTRACE_SETOPTIONS
# include <linux/ptrace.h>
#endif
#include <linux/unistd.h>
#include <linux/limits.h>

#include "ptrace.h"
#include "ptraceinfo.h"
#include "dmtcpplugin.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"

using namespace dmtcp;

static dmtcp::PtraceInfo *_ptraceInfo = NULL;
dmtcp::PtraceInfo& dmtcp::PtraceInfo::instance()
{
  if (_ptraceInfo == NULL) _ptraceInfo = new PtraceInfo();
  return *_ptraceInfo;
}

void dmtcp::PtraceInfo::createSharedFile()
{
  struct stat statbuf;
  int fd = dmtcp_get_ptrace_fd();
  if (fstat(fd, &statbuf) == -1 && errno == EBADF) {
    char path[PATH_MAX];
    size_t length = sizeof(PtraceSharedData);
    int ptrace_fd = dmtcp_get_ptrace_fd();

    sprintf(path, "%s/%s-%s", dmtcp_get_tmpdir(), "ptraceSharedInfo",
            dmtcp_get_computation_id_str());

    int fd = _real_open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
    JASSERT(fd != -1) (path) (JASSERT_ERRNO);
    JASSERT(_real_lseek(fd, length, SEEK_SET) == length)
      (path) (length) (JASSERT_ERRNO);
    Util::writeAll(fd, "", 1);
    JASSERT(_real_unlink(path) == 0) (path) (JASSERT_ERRNO);
    JASSERT(_real_dup2(fd, ptrace_fd) == ptrace_fd) (fd) (ptrace_fd);
    close(fd);
  }
}

void dmtcp::PtraceInfo::mapSharedFile()
{
  size_t length = sizeof(PtraceSharedData);
  int fd = dmtcp_get_ptrace_fd();

  _sharedData = (PtraceSharedData*) _real_mmap(0, length, PROT_READ|PROT_WRITE,
                                         MAP_SHARED, fd, 0);
  JASSERT(_sharedData != MAP_FAILED) (fd) (length);

  _sharedData->init();
}

bool dmtcp::PtraceInfo::isPtracing()
{
  if (_sharedData == NULL) {
    mapSharedFile();
  }
  return _sharedData->isPtracing();
}

void dmtcp::PtraceInfo::markAsCkptThread()
{
  if (_sharedData == NULL) {
    mapSharedFile();
  }
  pid_t superior = dmtcp::Util::getTracerPid();
  if (superior != 0) {
    dmtcp::Inferior *inf = _sharedData->insertInferior(superior, GETTID(), true);
  }
}

dmtcp::vector<dmtcp::Inferior*> dmtcp::PtraceInfo::getInferiors(pid_t tid)
{
  if (_supToInfsMap.find(tid) == _supToInfsMap.end()) {
    dmtcp::vector<Inferior*> vec;
    return vec;
  }
  return _supToInfsMap[tid];
}

void dmtcp::PtraceInfo::setLastCmd(pid_t tid, int lastCmd)
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

void dmtcp::PtraceInfo::insertInferior(pid_t tid)
{
  dmtcp::Inferior *inf = _sharedData->getInferior(tid);
  if (inf == NULL) {
    inf = _sharedData->insertInferior(GETTID(), tid);
  }
  _supToInfsMap[inf->superior()].push_back(inf);
  _infToSupMap[inf->tid()] = inf->superior();
}

void dmtcp::PtraceInfo::eraseInferior(pid_t tid)
{
  if (_sharedData == NULL) {
    mapSharedFile();
  }
  dmtcp::Inferior *inf = _sharedData->getInferior(tid);
  pid_t superior = inf->superior();
  JASSERT(inf != NULL) (tid);
  _sharedData->eraseInferior(inf);

  dmtcp::vector<Inferior*>& vec = _supToInfsMap[superior];
  dmtcp::vector<Inferior*>::iterator it;
  for (it = vec.begin(); it != vec.end(); it++) {
    if (*it == inf) {
      vec.erase(it);
      break;
    }
  }

  _infToSupMap.erase(tid);
}

void dmtcp::PtraceInfo::setPtracing()
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

void dmtcp::PtraceInfo::processSuccessfulPtraceCmd(int request, pid_t pid,
                                                   void *addr, void *data)
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
      eraseInferior(pid);
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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,6)
    case PTRACE_SETOPTIONS:
      inf = _sharedData->getInferior(pid);
      if (inf == NULL) {
        inf = _sharedData->insertInferior(getpid(), pid);
      }
      inf->setPtraceOptions(data);
      JTRACE("PTRACE_SETOPTIONS") (pid);
      break;
#endif

    default:
      JTRACE("PTRACE_XXX") (pid) (request);
      break;
  }

  if (_infToSupMap.find(pid) == _infToSupMap.end()) {
    insertInferior(pid);
  }
  return;
}

void dmtcp::PtraceInfo::processSetOptions(pid_t tid, void *data)
{
  // TODO:
}

pid_t dmtcp::PtraceInfo::getWait4Status(pid_t tid, int *status,
                                        struct rusage *rusage)
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

void dmtcp::PtraceInfo::waitForSuperiorAttach()
{
  if (_sharedData == NULL) {
    mapSharedFile();
  }
  Inferior *inf = _sharedData->getInferior(GETTID());
  if (inf == NULL) {
    return;
  }
  inf->semWait();
}


void dmtcp::PtraceInfo::processPreResumeAttach(pid_t inferior)
{
  Inferior *inf = _sharedData->getInferior(inferior);
  JASSERT(inf != NULL) (inferior);
  inf->semPost();
}
