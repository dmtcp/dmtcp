/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef VIRTUAL_PID_TABLE_H
#define VIRTUAL_PID_TABLE_H

#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <iostream>
#include <map>
#include "../jalib/jalloc.h"
#include "../jalib/jserialize.h"
#include "dmtcp.h"
#include "dmtcpalloc.h"
#include "virtualidtable.h"

#define REAL_TO_VIRTUAL_PID(pid) \
  dmtcp::VirtualPidTable::instance().realToVirtual(pid)
#define VIRTUAL_TO_REAL_PID(pid) \
  dmtcp::VirtualPidTable::instance().virtualToReal(pid)

namespace dmtcp
{
class VirtualPidTable : public VirtualIdTable<pid_t>
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR
    VirtualPidTable();
    static VirtualPidTable &instance();
    static pid_t getPidFromEnvVar();

    virtual void postRestart();
    virtual void resetOnFork();

    void updateMapping(pid_t virtualId, pid_t realId);
    pid_t realToVirtual(pid_t realPid);
    pid_t virtualToReal(pid_t virtualId);
    void refresh();
    void writeVirtualTidToFileForPtrace(pid_t pid);
    pid_t readVirtualTidFromFileForPtrace(pid_t realTid = -1);

    pid_t getNewVirtualTid();

  private:
};
}
#endif // ifndef VIRTUAL_PID_TABLE_H
