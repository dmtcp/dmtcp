/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef RESTORETARGET_H
#define RESTORETARGET_H

#include <unistd.h>
#include <stdlib.h>
#include <string>
#include <errno.h>
#include <vector>

#include "constants.h"
#include "dmtcpalloc.h"
#include "uniquepid.h"
#include "processinfo.h"
#include "connectionmanager.h"
#include "dmtcpcoordinatorapi.h"
#include "virtualpidtable.h"

namespace dmtcp
{

  class OriginalPidTable {
    public:
      OriginalPidTable(){}

      void insertFromProcessInfo ( dmtcp::ProcessInfo& vt )
      {
        dmtcp::vector< pid_t > tmpVector;

        _insert(vt.pid());

        tmpVector = vt.getChildPidVector();
        for ( size_t i = 0; i < tmpVector.size(); ++i )
          _insert(tmpVector[i]);

        tmpVector = vt.getTidVector();
        for ( size_t i = 0; i < tmpVector.size(); ++i )
          _insert(tmpVector[i]);
      }

      void _insert( pid_t pid )
      {
        if (!isConflictingChildPid (pid) /* && newVector[i] != getpid()*/) {
          _vector.push_back ( pid );
          JTRACE("New Pid Pushed to PidVector") (pid);
        }
      }

      bool isConflictingChildPid ( pid_t pid )
      {
        //iterator i = _vector.find ( pid );
        //if ( i == _vector.end() )
        //  return false;
        for ( size_t i = 0; i < _vector.size(); ++i )
          if ( _vector[i] == pid )
            return true;

        return false;
      }

      size_t numPids () { return _vector.size(); }

    private:
      typedef dmtcp::vector< pid_t >::iterator iterator;
      dmtcp::vector< pid_t > _vector;
  };

  class RestoreTarget
  {
  public:
    RestoreTarget(const dmtcp::string& path);

    typedef map<pid_t,bool> sidMapping;
    typedef sidMapping::iterator s_iterator;
    typedef vector<RestoreTarget *>::iterator t_iterator;

    const UniquePid& upid() const { return _conToFd.upid(); }
    const dmtcp::string& procname() const { return _conToFd.procname(); }
    void addChild(RestoreTarget *t) { _children.push_back(t); }

    ProcessInfo& getProcessInfo() { return _processInfo; }
#ifdef PID_VIRTUALIZATION
    VirtualPidTable& getVirtualPidTable() { return _virtualPidTable; }
#endif

    // Traverse this process subtree and set up information about sessions
    //   and their leaders for all children.
    sidMapping &setupSessions();
    sidMapping &getSmap() { return _smap; }

    dmtcp::string path() {return _path;}
    UniquePid compGroup() {return _compGroup;}
    int numPeers() {return _numPeers;}
    void markUsed() {_used = true;}
    bool isMarkedUsed() {return _used;}

    pid_t forkChild();
    bool isSessionLeader();
    bool isGroupLeader();
    bool isForegroundProcess();
    bool isInitChild();
    void bringToForeground(SlidingFdTable& slidingFd);
    int find_stdin(SlidingFdTable& slidingFd);

    void printMapping();
    int addRoot(RestoreTarget *t, pid_t sid);
    pid_t checkDependence(RestoreTarget *t);
    void restoreGroup(SlidingFdTable& slidingFd);

    void CreateProcess(DmtcpCoordinatorAPI& coordinatorAPI,
                       SlidingFdTable& slidingFd);
    void dupAllSockets(SlidingFdTable& slidingFd);
    void mtcpRestart();

  private:
    dmtcp::string _path;
    int _offset;
    ConnectionToFds _conToFd;
    UniquePid _compGroup;
    int _numPeers;
    size_t _argvSize;
    size_t _envSize;
#ifdef PID_VIRTUALIZATION
    VirtualPidTable _virtualPidTable;
#endif
    ProcessInfo _processInfo;
    // Links to children of this process
    vector<RestoreTarget *> _children;
    // Links to roots that depend on this target
    // i.e. have SID of this target in its tree.
    vector<RestoreTarget *> _roots;
    sidMapping _smap;
    bool _used;
  };
} // end namespace
#endif
