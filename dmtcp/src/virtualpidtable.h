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

#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <iostream>
#include <map>
#include "../jalib/jserialize.h"
#include "../jalib/jalloc.h"
#include "uniquepid.h"
#include "dmtcpalloc.h"
#include "constants.h"

#ifdef PID_VIRTUALIZATION
# define REAL_TO_VIRTUAL_PID(pid) \
  dmtcp::VirtualPidTable::instance().realToVirtual(pid)
# define VIRTUAL_TO_REAL_PID(pid) \
  dmtcp::VirtualPidTable::instance().virtualToReal(pid)
#else
# define REAL_TO_VIRTUAL_PID(pid) pid
# define VIRTUAL_TO_REAL_PID(pid) pid
#endif

#ifdef PID_VIRTUALIZATION
namespace dmtcp
{
  /* Shall we create separate classes for holding virtual to real pid map
   * and  for holding child process ids?
   */

  class VirtualPidTable
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      VirtualPidTable();
      static VirtualPidTable& instance();
      static bool isConflictingPid(pid_t pid);

      void postRestart();
      void preCheckpoint();
      void  postExec();

      pid_t virtualToReal(pid_t virtualPid);
      pid_t realToVirtual(pid_t realPid);
      bool pidExists(pid_t pid);

      void insert(pid_t virtualPid);
      void updateMapping(pid_t virtualPid, pid_t realPid);

      void printPidMaps();
      void erase(pid_t virtualPid);

      void refresh();

      static void InsertIntoPidMapFile( pid_t virtualPid, pid_t realPid);
      void serialize(jalib::JBinarySerializer& o);
      void serializePidMap(jalib::JBinarySerializer& o);
      static void serializePidMapEntry(jalib::JBinarySerializer& o,
                                       pid_t& virtualPid,
                                       pid_t& realPid );
      static void serializeEntryCount(jalib::JBinarySerializer& o, size_t& count);
      void readPidMapsFromFile();

      dmtcp::vector< pid_t > getPidVector();


      pid_t pid() const { return _pid; }
      pid_t ppid() const { return _ppid; }

      void setppid( pid_t ppid ) { _ppid = ppid; }

      void resetOnFork();

    private:
      typedef dmtcp::map< pid_t , pid_t >::iterator pid_iterator;
      dmtcp::map< pid_t , pid_t > _pidMapTable;

      pid_t _pid;
      pid_t _ppid;
  };
}

#endif
#endif
