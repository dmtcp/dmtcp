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

#include <stdlib.h>
#include <string.h>
#include <string>
#include <sstream>
#include <fcntl.h>
#include <sys/syscall.h>
#include "constants.h"
#include "util.h"
#include "syscallwrappers.h"
#include "protectedfds.h"
#include "uniquepid.h"
#include "processinfo.h"
#include "virtualpidtable.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jfilesystem.h"

static pthread_mutex_t tblLock = PTHREAD_MUTEX_INITIALIZER;

static void _do_lock_tbl()
{
  JASSERT(_real_pthread_mutex_lock(&tblLock) == 0) (JASSERT_ERRNO);
}

static void _do_unlock_tbl()
{
  JASSERT(_real_pthread_mutex_unlock(&tblLock) == 0) (JASSERT_ERRNO);
}

dmtcp::ProcessInfo::ProcessInfo()
{
  _do_lock_tbl();
  _pid = -1;
  _ppid = -1;
  _gid = -1;
  _sid = -1;
  _isRootOfProcessTree = false;
  _childTable.clear();
  _tidVector.clear();
  _pthreadJoinId.clear();
  _do_unlock_tbl();
}

dmtcp::ProcessInfo& dmtcp::ProcessInfo::instance()
{
  static ProcessInfo *inst = new ProcessInfo(); return *inst;
}

void dmtcp::ProcessInfo::preCheckpoint()
{
  //refresh();
}

void dmtcp::ProcessInfo::postRestart()
{
}

void dmtcp::ProcessInfo::restoreProcessGroupInfo()
{
  // FIXME: This needs to be fixed
#ifdef PID_VIRTUALIZATION
  // Restore group assignment
  if( VirtualPidTable::instance().pidExists(_gid) ){
    pid_t cgid = getpgid(0);
    // Group ID is known inside checkpointed processes
    if( _gid != cgid && _pid != _gid ){
      JTRACE("Restore Group Assignment")
        ( _gid ) ( _fgid ) ( cgid ) ( _pid ) ( _ppid ) ( getppid() );
      JWARNING( setpgid(0,_gid) == 0 ) (_gid) (JASSERT_ERRNO)
        .Text("Cannot change group information");
    }else{
      JTRACE("Group is already assigned")(_gid)(cgid);
    }
  }else{
    JTRACE("SKIP Group information, GID unknown");
  }
#endif
}

void dmtcp::ProcessInfo::resetOnFork()
{
  pthread_mutex_t newlock = PTHREAD_MUTEX_INITIALIZER;
  tblLock = newlock;
  _ppid = _pid;
  _pid = getpid();
  _isRootOfProcessTree = false;
  _childTable.clear();
  _tidVector.clear();
  _pthreadJoinId.clear();
}

void dmtcp::ProcessInfo::insertChild(pid_t pid, dmtcp::UniquePid uniquePid)
{
  _do_lock_tbl();
  iterator i = _childTable.find( pid );
  JWARNING(i == _childTable.end()) (pid) (uniquePid) (i->second)
    .Text("child pid already exists!");

  _childTable[pid] = uniquePid;
  _do_unlock_tbl();

  JTRACE("Creating new virtualPid -> realPid mapping.") (pid) (uniquePid);
}

void dmtcp::ProcessInfo::eraseChild( pid_t virtualPid )
{
  _do_lock_tbl();
  iterator i = _childTable.find ( virtualPid );
  if ( i != _childTable.end() )
    _childTable.erase( virtualPid );
  _do_unlock_tbl();
}

dmtcp::vector< pid_t > dmtcp::ProcessInfo::getChildPidVector( )
{
  dmtcp::vector< pid_t > childPidVec;
  for ( iterator i = _childTable.begin(); i != _childTable.end(); ++i )
    childPidVec.push_back ( i->first );
  return childPidVec;
}

dmtcp::vector< pid_t > dmtcp::ProcessInfo::getTidVector( )
{
  return _tidVector;
}

void dmtcp::ProcessInfo::insertTid( pid_t tid )
{
  eraseTid( tid );
  _do_lock_tbl();
  _tidVector.push_back ( tid );
  _do_unlock_tbl();
  return;
}

void dmtcp::ProcessInfo::eraseTid( pid_t tid )
{
  _do_lock_tbl();
  dmtcp::vector< pid_t >::iterator iter = _tidVector.begin();
  while ( iter != _tidVector.end() ) {
    if ( *iter == tid ) {
      _tidVector.erase( iter );
      break;
    }
    else
      ++iter;
  }
  _do_unlock_tbl();
  return;
}

void dmtcp::ProcessInfo::postExec( )
{
  JTRACE("Post-Exec. Emptying tidVector");
  _do_lock_tbl();
#ifdef PID_VIRTUALIZATION
  for (size_t i = 0; i < _tidVector.size(); i++) {
    VirtualPidTable::instance().erase(_tidVector[i]);
  }
#endif
  _tidVector.clear();

  _procname   = jalib::Filesystem::GetProgramName();
  _upid       = UniquePid::ThisProcess();
  _uppid      = UniquePid::ParentProcess();
  _do_unlock_tbl();
}

bool dmtcp::ProcessInfo::beginPthreadJoin(pthread_t thread)
{
  bool res = false;
  _do_lock_tbl();
  dmtcp::map<pthread_t, pthread_t>::iterator i = _pthreadJoinId.find(thread);
  if (i == _pthreadJoinId.end()) {
    _pthreadJoinId[thread] = pthread_self();
    res = true;
  }
  _do_unlock_tbl();
  return res;
}

void dmtcp::ProcessInfo::endPthreadJoin(pthread_t thread)
{
  _do_lock_tbl();
  JASSERT(pthread_equal(_pthreadJoinId[thread], pthread_self()));
  _pthreadJoinId.erase(thread);
  _do_unlock_tbl();
}

void dmtcp::ProcessInfo::refresh()
{
  _pid = getpid();
  _ppid = getppid();
  _gid = getpgid(0);
  _sid = getsid(0);

  _fgid = -1;
  dmtcp::string controllingTerm = jalib::Filesystem::GetControllingTerm();
  if (!controllingTerm.empty()) {
    int tfd = _real_open(controllingTerm.c_str(), O_RDONLY, 0);
    if (tfd >= 0) {
      _fgid = tcgetpgrp(tfd);
      _real_close(tfd);
    }
  }

  if (_ppid == 1) {
    _isRootOfProcessTree = true;
  }

  _procname = jalib::Filesystem::GetProgramName();
  _hostname = jalib::Filesystem::GetCurrentHostname();
  _upid = UniquePid::ThisProcess();
  _uppid = UniquePid::ParentProcess();

  refreshChildTable();
  refreshTidVector();

  JTRACE("CHECK GROUP PID")(_gid)(_fgid)(_ppid);
}

void dmtcp::ProcessInfo::refreshTidVector()
{
  dmtcp::vector< pid_t >::iterator iter;
  for (iter = _tidVector.begin(); iter != _tidVector.end(); ) {
    int retVal = syscall(SYS_tgkill, _pid, *iter, 0);
    if (retVal == -1 && errno == ESRCH) {
#ifdef PID_VIRTUALIZATION
      VirtualPidTable::instance().erase(*iter);
#endif
      iter = _tidVector.erase( iter );
    } else {
      iter++;
    }
  }
  return;
}

void dmtcp::ProcessInfo::refreshChildTable()
{
  dmtcp::vector< pid_t > childPidVec = getChildPidVector();
  for (size_t i = 0; i < childPidVec.size(); i++) {
    pid_t pid = childPidVec[i];
    int retVal = kill(pid, 0);
    /* Check to see if the child process is alive*/
    if (retVal == -1 && errno == ESRCH) {
#ifdef PID_VIRTUALIZATION
      VirtualPidTable::instance().erase(pid);
#endif
      _childTable.erase(pid);
    }
  }
}

void dmtcp::ProcessInfo::serialize ( jalib::JBinarySerializer& o )
{
  JSERIALIZE_ASSERT_POINT ( "dmtcp::ProcessInfo:" );

  if (o.isWriter()){
    refresh();
  }

  o & _isRootOfProcessTree & _pid & _sid & _ppid & _gid & _fgid;
  o & _procname & _hostname & _upid & _uppid;
  o & _compGroup & _numPeers & _argvSize & _envSize;

  JTRACE("Serialized process information")
    (_sid) (_ppid) (_gid) (_fgid)
    (_procname) (_hostname) (_upid) (_uppid)
    (_compGroup) (_numPeers) (_argvSize) (_envSize);

  if ( _isRootOfProcessTree ) {
    JTRACE ( "This process is Root of Process Tree" );
  }

  serializeChildTable ( o );

  JTRACE ("Serializing tidVector");
  JSERIALIZE_ASSERT_POINT ( "TID Vector:[" );
  o & _tidVector;
  JSERIALIZE_ASSERT_POINT ( "}" );

  JSERIALIZE_ASSERT_POINT( "EOF" );
}


void dmtcp::ProcessInfo::serializeChildTable ( jalib::JBinarySerializer& o )
{
  size_t numPids = _childTable.size();
  serializeEntryCount(o, numPids);

  JTRACE ("Serializing ChildPid Table") (numPids) (o.filename());
  pid_t pid;
  dmtcp::UniquePid uniquePid;

  if ( o.isWriter() )
  {
    for ( iterator i = _childTable.begin(); i != _childTable.end(); ++i )
    {
      pid = i->first;
      uniquePid   = i->second;
      serializeChildTableEntry ( o, pid, uniquePid );
    }
  }
  else
  {
    while ( numPids-- > 0 )
    {
      serializeChildTableEntry ( o, pid, uniquePid );
      _childTable[pid] = uniquePid;
    }
  }
}

void  dmtcp::ProcessInfo::serializeChildTableEntry ( jalib::JBinarySerializer& o,
                                                     pid_t& pid,
                                                     dmtcp::UniquePid& uniquePid )
{
  JSERIALIZE_ASSERT_POINT ( "ChildPid:[" );
  o & pid & uniquePid;
  JSERIALIZE_ASSERT_POINT ( "]" );
}

void dmtcp::ProcessInfo::serializeEntryCount ( jalib::JBinarySerializer& o,
                                                   size_t& count )
{
  JSERIALIZE_ASSERT_POINT ( "NumEntries:[" );
  o & count;
  JSERIALIZE_ASSERT_POINT ( "]" );
  JTRACE("Num PidMaps:")(count);
}
