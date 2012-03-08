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

#ifndef PROCESS_INFO_H
#define PROCESS_INFO_H

#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <iostream>
#include <map>
#include "dmtcpalloc.h"
#include "../jalib/jserialize.h"
#include "../jalib/jalloc.h"
#include "uniquepid.h"
#include "constants.h"

namespace dmtcp
{
  class ProcessInfo
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      ProcessInfo();
      static ProcessInfo& instance();

      void postRestart();
      void restoreProcessGroupInfo();
      void preCheckpoint();

      void  insertTid(pid_t tid);
      void  eraseTid(pid_t tid);
      size_t numThreads() { refreshTidVector(); return _tidVector.size(); }

      void insertChild (pid_t virtualPid, dmtcp::UniquePid uniquePid);
      void eraseChild (pid_t virtualPid);
      void  postExec();

      void resetOnFork();

      bool beginPthreadJoin(pthread_t thread);
      void endPthreadJoin(pthread_t thread);

      void refresh();
      void refreshChildTable();
      void refreshTidVector();

      void serialize ( jalib::JBinarySerializer& o );

      void setRootOfProcessTree() { _isRootOfProcessTree = true; }
      bool isRootOfProcessTree() const { return _isRootOfProcessTree; }

      dmtcp::vector< pid_t > getChildPidVector();
      dmtcp::vector< pid_t > getTidVector();
      dmtcp::vector< pid_t > getInferiorVector();

      typedef dmtcp::map< pid_t , dmtcp::UniquePid >::iterator iterator;
      iterator begin() { return _childTable.begin(); }
      iterator end() { return _childTable.end(); }

      pid_t pid() const { return _pid; }
      pid_t ppid() const { return _ppid; }
      pid_t sid() const { return _sid; }
      pid_t gid() const { return _gid; }
      pid_t fgid() const { return _fgid; }

      void setppid(pid_t ppid) { _ppid = ppid; }
      void setsid(pid_t sid) { _sid = sid; }
      void setgid(pid_t gid) { _gid = gid; }
      void setfgid(pid_t fgid) { _fgid = fgid; }

      int numPeers() { return _numPeers; }
      void numPeers(int np) { _numPeers = np; }
      UniquePid compGroup() { return _compGroup; }
      void compGroup(UniquePid cg) { _compGroup = cg; }

      size_t argvSize() { return _argvSize; }
      void argvSize(int size) { _argvSize = size; }
      size_t envSize() { return _envSize; }
      void envSize(int size) { _envSize = size; }

      const dmtcp::string& procname() const { return _procname; }
      const dmtcp::string& hostname() const { return _hostname; }
      const UniquePid& upid() const { return _upid; }
      const UniquePid& uppid() const { return _uppid; }

    private:
      dmtcp::map< pid_t , dmtcp::UniquePid > _childTable;
      dmtcp::vector< pid_t > _tidVector;
      dmtcp::map<pthread_t, pthread_t> _pthreadJoinId;

      bool  _isRootOfProcessTree;
      pid_t _pid;
      pid_t _ppid;
      pid_t _sid;
      pid_t _gid;
      pid_t _fgid;

      UniquePid _compGroup;
      int       _numPeers;
      size_t    _argvSize;
      size_t    _envSize;

      dmtcp::string _procname;
      dmtcp::string _hostname;
      UniquePid     _upid;
      UniquePid     _uppid;
  };

}

#endif /* PROCESS_INFO */
