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

VirtualPidTable::VirtualPidTable()
  : VirtualIdTable<pid_t>("Pid", getpid())
{
  // _do_lock_tbl();
  // _idMapTable[getpid()] = _real_getpid();
  // _idMapTable[getppid()] = _real_getppid();
  // _do_unlock_tbl();
}

VirtualPidTable *virtPidTableInst = NULL;

VirtualPidTable&
VirtualPidTable::instance()
{
  if (virtPidTableInst == NULL) {
    virtPidTableInst = new VirtualPidTable();
  }
  return *virtPidTableInst;
}

void
VirtualPidTable::postRestart()
{
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
  printMaps();
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
