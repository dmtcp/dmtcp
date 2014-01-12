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

#include <stdlib.h>
#include <string.h>
#include <string>
#include <sstream>
#include <fcntl.h>
#include <sys/syscall.h>
#include "util.h"
#include "pidwrappers.h"
#include "../jalib/jconvert.h"
#include "../jalib/jfilesystem.h"
#include "virtualpidtable.h"
#include "dmtcp.h"
#include "shareddata.h"

static int _numTids = 1;

dmtcp::VirtualPidTable::VirtualPidTable()
  : VirtualIdTable<pid_t> ("Pid", getpid())
{
  //_do_lock_tbl();
  //_idMapTable[getpid()] = _real_getpid();
  //_idMapTable[getppid()] = _real_getppid();
  //_do_unlock_tbl();
}

static dmtcp::VirtualPidTable *virtPidTableInst = NULL;
dmtcp::VirtualPidTable& dmtcp::VirtualPidTable::instance()
{
  if (virtPidTableInst == NULL) {
    virtPidTableInst = new VirtualPidTable();
  }
  return *virtPidTableInst;
}

void dmtcp::VirtualPidTable::postRestart()
{
  VirtualIdTable<pid_t>::postRestart();
  _do_lock_tbl();
  _idMapTable[getpid()] = _real_getpid();
  _do_unlock_tbl();
}

void dmtcp::VirtualPidTable::refresh()
{
  id_iterator i;
  id_iterator next;
  pid_t _real_pid = _real_getpid();

  JASSERT(getpid() != -1);

  _do_lock_tbl();
  for (i = _idMapTable.begin(), next = i; i != _idMapTable.end(); i = next) {
    next++;
    if (isIdCreatedByCurrentProcess(i->second)
        && _real_tgkill(_real_pid, i->second, 0) == -1) {
      _idMapTable.erase(i);
    }
  }
  _do_unlock_tbl();
  printMaps();
}

pid_t dmtcp::VirtualPidTable::getNewVirtualTid()
{
  pid_t tid;
  if (VirtualIdTable<pid_t>::getNewVirtualId(&tid) == false) {
    refresh();
  }

  JASSERT(VirtualIdTable<pid_t>::getNewVirtualId(&tid))
    (_idMapTable.size()) .Text("Exceeded maximum number of threads allowed");

  return tid;
}

void dmtcp::VirtualPidTable::resetOnFork()
{
  VirtualIdTable<pid_t>::resetOnFork(getpid());
  _numTids = 1;
  _idMapTable[getpid()] = _real_getpid();
  refresh();
  printMaps();
}

void dmtcp::VirtualPidTable::updateMapping(pid_t virtualId, pid_t realId)
{
  if (virtualId > 0 && realId > 0) {
    _do_lock_tbl();
    _idMapTable[virtualId] = realId;
    _do_unlock_tbl();
  }
}

//to allow linking without ptrace plugin
extern "C" int dmtcp_is_ptracing() __attribute__ ((weak));

pid_t dmtcp::VirtualPidTable::realToVirtual(pid_t realPid)
{
  if (realIdExists(realPid)) {
    return VirtualIdTable<pid_t>::realToVirtual(realPid);
  }

  _do_lock_tbl();
  if (dmtcp_is_ptracing != 0 && dmtcp_is_ptracing() && realPid > 0) {
    pid_t virtualPid = readVirtualTidFromFileForPtrace(gettid());
    if (virtualPid != -1) {
      _do_unlock_tbl();
      updateMapping(virtualPid, realPid);
      return virtualPid;
    }
  }

  //JWARNING(false) (realPid)
    //.Text("No virtual pid/tid found for the given real pid");
  _do_unlock_tbl();
  return realPid;
}

pid_t dmtcp::VirtualPidTable::virtualToReal(pid_t virtualId)
{
  if (virtualId == -1) {
    return virtualId;
  }
  pid_t id = (virtualId < -1 ? abs(virtualId) : virtualId);
  pid_t retVal = VirtualIdTable<pid_t>::virtualToReal(id);
  if (retVal == id) {
    retVal = dmtcp::SharedData::getRealPid(id);
    if (retVal == -1) {
      retVal = id;
    }
  }
  retVal = virtualId < -1 ? -retVal : retVal;
  return retVal;
}

void dmtcp::VirtualPidTable::writeVirtualTidToFileForPtrace(pid_t pid)
{
  if (!dmtcp_is_ptracing || !dmtcp_is_ptracing()) {
    return;
  }
  pid_t tracerPid = dmtcp::Util::getTracerPid();
  if (tracerPid != 0) {
    dmtcp::SharedData::setPtraceVirtualId(tracerPid, pid);
  }
}

pid_t dmtcp::VirtualPidTable::readVirtualTidFromFileForPtrace(pid_t tid)
{
  pid_t pid;

  if (!dmtcp_is_ptracing || !dmtcp_is_ptracing()) {
    return -1;
  }
  if (tid == -1) {
    tid = dmtcp::Util::getTracerPid();
    if (tid == 0) {
      return -1;
    }
  }

  pid = dmtcp::SharedData::getPtraceVirtualId(tid);

  JTRACE("Read virtual Pid/Tid from shared-area") (pid);
  return pid;
}
