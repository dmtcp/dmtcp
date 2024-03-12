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
#include "../jalib/jalloc.h"
#include "uniquepid.h"

#define MB                 1024 * 1024
#define RESTORE_STACK_SIZE 16 * MB
#define RESTORE_MEM_SIZE   16 * MB
#define RESTORE_TOTAL_SIZE (RESTORE_STACK_SIZE + RESTORE_MEM_SIZE)

namespace dmtcp
{
class ProcessInfo : public DmtcpCkptHeader
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR
    ProcessInfo();
    static ProcessInfo &instance();
    void init();
    void postExec();
    void resetOnFork();
    void preCkpt();
    void restart();
    void restoreProcessGroupInfo();
    void restoreHeap();
    void growStack();

    bool beginPthreadJoin(pthread_t thread);
    void endPthreadJoin(pthread_t thread);
    void clearPthreadJoinState(pthread_t thread);

    void getState();
    void setRootOfProcessTree() { isRootOfProcessTree = 1; }

    bool getIsRootOfProcessTree() const { return isRootOfProcessTree == 1; }

    void serialize(jalib::JBinarySerializer &o);

    uint32_t getNumPeers() { return numPeers; }

    void setNumPeers(uint32_t np) { numPeers = np; }

    pid_t getPid() const { return pid; }

    pid_t getSid() const { return sid; }

    uint32_t get_generation() { return _generation; }

    void set_generation(uint32_t generation) { _generation = generation; }

    uint32_t numCheckpoints() { return _numCheckpoints; }

    uint32_t numRestarts() { return _numRestarts; }

    uint32_t incrementNumCheckpoints() { return _numCheckpoints++; }

    uint32_t incrementNumRestarts() { return _numRestarts++; }

    void processRlimit();

    string getProcname() const { return procname; }

    string getProcSelfExe() const { return procSelfExe; }

    const string &hostname() const { return _hostname; }

    UniquePid getUpid() {
      // Temporary fix until we remove the static members from UniquePid.cpp.
      if (upid == DmtcpUniqueProcessId()) {
        upid = UniquePid::ThisProcess(true);
      }
      return upid;
    }

    const string &upidStr() {
      if (_upidStr.empty()) {
        _upidStr = UniquePid(upid).toString();
      }
      return _upidStr;
    }

    UniquePid getUppid() {
      // Temporary fix until we remove the static members from UniquePid.cpp.
      if (uppid == UniquePid()) {
        uppid = UniquePid::ParentProcess();
      }
      return uppid;
    }

    UniquePid getCompGroup() const { return compGroup; }

    const string &compGroupStr() {
      if (_compGroupStr.empty()) {
        _compGroupStr = UniquePid(compGroup).toString();
      }
      return _compGroupStr;
    }

    void getCompGroup(UniquePid cg) { compGroup = cg; }

    void updateRestoreBufAddr(void* addr, uint64_t len);

    string const& getCkptFilename() const { return _ckptFileName; }
    string getTempCkptFilename() const { return _ckptFileName + ".temp"; }

    string const& getCkptFilesSubDir() const { return _ckptFilesSubDir; }

    string const& getCkptDir() const { return _ckptDir; }

    void setCkptDir(const char *);
    void setCkptFilename(const char *);
    void updateCkptDirFileSubdir(string newCkptDir = "");

    void addKeyValuePairToCkptHeader(const string &key, const string &value);
    const string& getValue(const string &key);

    const map<string, string>& getKeyValueMap() const { return kvmap; }

  private:
    map<string, string>kvmap;

    map<pthread_t, pthread_t>_pthreadJoinId;
    typedef map<pid_t, UniquePid>::iterator iterator;

    string _hostname;
    string _launchCWD;
    string _ckptCWD;

    string _ckptDir;
    string _ckptFileName;
    string _ckptFilesSubDir;

    string _upidStr;
    string _compGroupStr;

    uint64_t _savedHeapStart;
    uint64_t _initialSavedBrk;

    // Put <64-bit wide variabled here to ensure they are properly aligned.

    // _generation is per-process.  This constrasts with
    // _computation_generation, which is shared among all processes on a host.
    // _computation_generation is updated in shareddata.cpp by:
    // sharedDataHeader->compId._computation_generation = generation;
    // _generation is updated later when this process begins its checkpoint.
    uint32_t _generation;
    uint32_t _numCheckpoints;
    uint32_t _numRestarts;

    uint32_t _buf; // for alignment. Remove if adding a new 32-bit variable.
};
}
#endif /* PROCESS_INFO */
