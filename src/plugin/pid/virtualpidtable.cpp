/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include "virtualpidtable.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sstream>
#include <string>
#include "../jalib/jconvert.h"
#include "../jalib/jfilesystem.h"
#include "dmtcp.h"
#include "pidwrappers.h"
#include "shareddata.h"
#include "util.h"

using namespace dmtcp;

static int _numTids = 1;
static __thread pid_t _dmtcp_thread_tid ATTR_TLS_INITIAL_EXEC = -1;

static pid_t _dmtcp_pid = -1;
static pid_t _dmtcp_ppid = -1;
static pid_t _dmtcp_realPid = -1;
static pid_t _dmtcp_realPpid = -1;


VirtualPidTable::VirtualPidTable()
  : VirtualIdTable<pid_t>("Pid", _dmtcp_pid)
{
}

VirtualPidTable *virtPidTableInst = NULL;

VirtualPidTable&
VirtualPidTable::instance()
{
  if (virtPidTableInst == NULL) {
    Util::getVirtualPidFromEnvVar(&_dmtcp_pid, &_dmtcp_realPid,
                                  &_dmtcp_ppid, &_dmtcp_realPpid);

    virtPidTableInst = new VirtualPidTable();
  }
  return *virtPidTableInst;
}

void
VirtualPidTable::resetPidPpid()
{
  Util::getVirtualPidFromEnvVar(&_dmtcp_pid, &_dmtcp_realPid,
                                &_dmtcp_ppid, &_dmtcp_realPpid);

  if (_dmtcp_realPid == 0) {
    _dmtcp_realPid = _real_getpid();
  }

  VirtualPidTable::instance().updateMapping(_dmtcp_pid, _dmtcp_realPid);
  VirtualPidTable::instance().updateMapping(_dmtcp_ppid, _dmtcp_realPpid);
}

void
VirtualPidTable::resetTid(pid_t tid)
{
  _dmtcp_thread_tid = tid;
  instance().updateMapping(_dmtcp_thread_tid, _real_gettid());
}

pid_t
VirtualPidTable::getpid()
{
  if (_dmtcp_pid == -1) {
    resetPidPpid();
  }

  return _dmtcp_pid;
}

pid_t
VirtualPidTable::getppid()
{
  if (_dmtcp_ppid == -1) {
    resetPidPpid();
  }
  if (_real_getppid() != VIRTUAL_TO_REAL_PID(_dmtcp_ppid)) {
    // The original parent died; reset our ppid.
    //
    // On older systems, a process is inherited by init (pid = 1) after its
    // parent dies. However, with the new per-user init process, the parent
    // pid is no longer "1"; it's the pid of the user-specific init process.
    _dmtcp_ppid = _real_getppid();
  }
  return _dmtcp_ppid;
}

pid_t
VirtualPidTable::gettid()
{
  /* dmtcp::ThreadList::updateTid calls gettid() before calling
   * ThreadSync::decrementUninitializedThreadCount() and so the value is
   * cached before it is accessed by some other DMTCP code.
   */
  if (_dmtcp_thread_tid == -1) {
    _dmtcp_thread_tid = getpid();

    // Make sure this is the motherofall thread.
    JASSERT(_real_gettid() == _real_getpid()) (_real_gettid()) (_real_getpid());
  }
  return _dmtcp_thread_tid;
}

void
VirtualPidTable::postRestart()
{
  if (_dmtcp_ppid != 1) {
    updateMapping(_dmtcp_ppid, _real_getppid());
  }

  VirtualIdTable<pid_t>::postRestart();
  _do_lock_tbl();
  _idMapTable[getpid()] = _real_getpid();
  _do_unlock_tbl();
}

void
VirtualPidTable::refresh()
{
  id_iterator i;
  id_iterator next;
  pid_t _real_pid = _real_getpid();

  JASSERT(getpid() != -1);

  _do_lock_tbl();
  for (i = _idMapTable.begin(), next = i; i != _idMapTable.end(); i = next) {
    next++;
    if (isIdCreatedByCurrentProcess(i->first)
        && _real_tgkill(_real_pid, i->second, 0) == -1) {
      _idMapTable.erase(i);
    }
  }
  _do_unlock_tbl();
}

pid_t
VirtualPidTable::getNewVirtualTid()
{
  pid_t tid = -1;

  if (VirtualIdTable<pid_t>::getNewVirtualId(&tid) == false) {
    refresh();
  }

  JASSERT(VirtualIdTable<pid_t>::getNewVirtualId(&tid))
    (_idMapTable.size()).Text("Exceeded maximum number of threads allowed");

  return tid;
}

void
VirtualPidTable::resetOnFork()
{
  resetPidPpid();
  resetTid(getpid());
  VirtualIdTable<pid_t>::resetOnFork(getpid());
  _numTids = 1;
  _idMapTable[getpid()] = _real_getpid();
  refresh();
  printMaps();
}

void
VirtualPidTable::updateMapping(pid_t virtualId, pid_t realId)
{
  if (virtualId > 0 && realId > 0) {
    _do_lock_tbl();
    _idMapTable[virtualId] = realId;
    _do_unlock_tbl();
  }
}

pid_t
VirtualPidTable::realToVirtual(pid_t realPid)
{
  return VirtualIdTable<pid_t>::realToVirtual(realPid);
}

pid_t
VirtualPidTable::virtualToReal(pid_t virtualId)
{
  if (virtualId == -1) {
    return virtualId;
  }

  pid_t id = (virtualId < -1 ? abs(virtualId) : virtualId);

  pid_t realId;
  if (!VirtualIdTable::virtualToReal(id, &realId)) {
    // Try shared area to see if some other process contains this virtual id.
    realId = SharedData::getRealPid(id);
    if (realId == -1) {
      realId = id;
    }
  }

  realId = virtualId < -1 ? -realId : realId;
  return realId;
}
