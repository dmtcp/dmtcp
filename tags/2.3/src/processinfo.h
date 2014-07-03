/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
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
#include "uniquepid.h"
#include "../jalib/jalloc.h"

#define MB 1024*1024
#define RESTORE_STACK_SIZE 5*MB
#define RESTORE_MEM_SIZE 5*MB
#define RESTORE_TOTAL_SIZE (RESTORE_STACK_SIZE+RESTORE_MEM_SIZE)

namespace dmtcp
{
  class ProcessInfo
  {
    public:
      enum ElfType {
        Elf_32,
        Elf_64
      };

#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      ProcessInfo();
      static ProcessInfo& instance();
      void init();
      void postExec();
      void resetOnFork();
      void restart();
      void postRestartRefill();
      void restoreProcessGroupInfo();
      void restoreHeap();
      void growStack();

      void insertChild (pid_t virtualPid, dmtcp::UniquePid uniquePid);
      void eraseChild (pid_t virtualPid);

      bool beginPthreadJoin(pthread_t thread);
      void endPthreadJoin(pthread_t thread);
      void clearPthreadJoinState(pthread_t thread);

      void refresh();
      void refreshChildTable();
      void setRootOfProcessTree() { _isRootOfProcessTree = true; }
      bool isRootOfProcessTree() const { return _isRootOfProcessTree; }

      void serialize ( jalib::JBinarySerializer& o );

      UniquePid compGroup() { return _compGroup; }
      void compGroup(UniquePid cg) { _compGroup = cg; }
      uint32_t numPeers() { return _numPeers; }
      void numPeers(uint32_t np) { _numPeers = np; }
      bool noCoordinator() { return _noCoordinator; }
      void noCoordinator(bool nc) { _noCoordinator = nc; }
      pid_t pid() const { return _pid; }
      pid_t sid() const { return _sid; }

      size_t argvSize() { return _argvSize; }
      void argvSize(int size) { _argvSize = size; }
      size_t envSize() { return _envSize; }
      void envSize(int size) { _envSize = size; }

      const dmtcp::string& procname() const { return _procname; }
      const dmtcp::string& procSelfExe() const { return _procSelfExe; }
      const dmtcp::string& hostname() const { return _hostname; }
      const UniquePid& upid() const { return _upid; }
      const UniquePid& uppid() const { return _uppid; }

      bool isOrphan() const { return _ppid == 1; }
      bool isSessionLeader() const { return _pid == _sid; }
      bool isGroupLeader() const { return _pid == _gid; }
      bool isForegroundProcess() const { return _gid == _fgid; }
      bool isChild(const UniquePid& upid);

      int elfType() const { return _elfType; }
      uint64_t savedBrk(void) const { return _savedBrk;}
      uint64_t restoreBufAddr(void) const { return _restoreBufAddr;}
      uint32_t restoreBufLen(void) const { return RESTORE_TOTAL_SIZE;}

      string getCkptFilename() const { return _ckptFileName; }
      string getCkptFilesSubDir() const { return _ckptFilesSubDir; }
      string getCkptDir() const { return _ckptDir; }
      void setCkptDir(const char*);
      void setCkptFilename(const char*);
      void updateCkptDirFileSubdir(string newCkptDir = "");

    private:
      dmtcp::map<pid_t, UniquePid> _childTable;
      dmtcp::map<pthread_t, pthread_t> _pthreadJoinId;
      dmtcp::map<pid_t, pid_t> _sessionIds;
      typedef dmtcp::map<pid_t, UniquePid>::iterator iterator;

      uint32_t  _isRootOfProcessTree;
      pid_t _pid;
      pid_t _ppid;
      pid_t _sid;
      pid_t _gid;
      pid_t _fgid;

      uint32_t  _numPeers;
      uint32_t  _noCoordinator;
      uint32_t  _argvSize;
      uint32_t  _envSize;
      uint32_t  _elfType;

      dmtcp::string _procname;
      dmtcp::string _procSelfExe;
      dmtcp::string _hostname;
      dmtcp::string _launchCWD;
      dmtcp::string _ckptCWD;

      dmtcp::string _ckptDir;
      dmtcp::string _ckptFileName;
      dmtcp::string _ckptFilesSubDir;

      UniquePid     _upid;
      UniquePid     _uppid;
      UniquePid     _compGroup;

      uint64_t      _restoreBufAddr;
      uint32_t      _restoreBufLen;
      uint64_t      _savedHeapStart;
      uint64_t      _savedBrk;
  };

}
#endif /* PROCESS_INFO */
