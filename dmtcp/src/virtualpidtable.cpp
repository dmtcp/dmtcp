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

#include "virtualpidtable.h"
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sstream>
#include "constants.h"
#include "syscallwrappers.h"
#include "protectedfds.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jfilesystem.h"

#ifdef PID_VIRTUALIZATION

dmtcp::VirtualPidTable::VirtualPidTable()
{
  _pid = _real_getpid();
  _ppid = _real_getppid();
  _isRootOfProcessTree = false;
  _childTable.clear();
  _pidMapTable.clear();
  _pidMapTable[_pid] = _pid;
}

dmtcp::VirtualPidTable& dmtcp::VirtualPidTable::Instance()
{
  static VirtualPidTable *inst = new VirtualPidTable(); return *inst;
}

void dmtcp::VirtualPidTable::postRestart()
{
  //getenv ( ENV_VAR_PIDTBLFILE_INITIAL );
  std::string serialFile = "/proc/self/fd/" + jalib::XToString ( PROTECTED_PIDTBL_FD );

  serialFile = jalib::Filesystem::ResolveSymlink ( serialFile );
  JASSERT ( serialFile.length() > 0 ) ( serialFile );
  _real_close ( PROTECTED_PIDTBL_FD );
  
  jalib::JBinarySerializeReader rd ( serialFile );
  serialize ( rd );

//   std::string pidMapFile = "/proc/self/fd/" + jalib::XToString ( PROTECTED_PIDMAP_FD );
//   pidMapFile =  jalib::Filesystem::ResolveSymlink ( pidMapFile );
//   JASSERT ( pidMapFile.length() > 0 ) ( pidMapFile );
// 
//   _real_close( PROTECTED_PIDMAP_FD );
// 
//   jalib::JBinarySerializeReader pidrd ( pidMapFile );
//   serializePidMap( pidrd );
}

void dmtcp::VirtualPidTable::resetOnFork()
{
  _pid = _real_getpid();
  _ppid = _real_getppid();
  _isRootOfProcessTree = false;
  _childTable.clear();
  _pidMapTable.clear();
  _pidMapTable[_pid] = _pid;
}

pid_t dmtcp::VirtualPidTable::oldToNewPid( pid_t oldPid )
{
  pid_iterator i = _pidMapTable.find(oldPid); 
  if ( i == _pidMapTable.end() ) 
  {
    JTRACE ( "No newPid not found for the given oldPid, returning the oldPid") ( oldPid );
    return oldPid;
  }

  return i->second;
}

pid_t dmtcp::VirtualPidTable::newToOldPid( pid_t newPid )
{
  for (pid_iterator i = _pidMapTable.begin(); i != _pidMapTable.end(); ++i)
  {
    if ( newPid == i->second )
      return i->first;
  }
    JTRACE ( "No oldPid not found for the given newPid, returning the newPid") ( newPid );

  return newPid;
}

void dmtcp::VirtualPidTable::insert ( pid_t oldPid, dmtcp::UniquePid uniquePid )
{
  iterator i = _childTable.find( oldPid );
  if ( i != _childTable.end() )
    JTRACE ( "oldPid -> newPid mapping exists!") ( oldPid ) ( i->second );

  JTRACE ( "Creating new oldPid -> newPid mapping." ) ( oldPid ) ( uniquePid );

  _childTable[oldPid] = uniquePid;

  _pidMapTable[oldPid] = oldPid;
}

void dmtcp::VirtualPidTable::erase( pid_t oldPid )
{
  _childTable.erase( oldPid );
}

void dmtcp::VirtualPidTable::updateRootOfProcessTree()
{
  if ( _real_getppid() == 1 )
    _isRootOfProcessTree = true;
}

void dmtcp::VirtualPidTable::updateMapping( pid_t old_pid, pid_t new_pid )
{
  _pidMapTable[old_pid] = new_pid;
}


void dmtcp::VirtualPidTable::serialize ( jalib::JBinarySerializer& o )
{
  JSERIALIZE_ASSERT_POINT ( "dmtcp::VirtualPidTable:" );

  if (o.isWriter() )
    updateRootOfProcessTree();//      _isRootOfProcessTree = true;

  o & _isRootOfProcessTree;

  if ( _isRootOfProcessTree )
    JTRACE ( "This process is Root of Process Tree" );// ( UniquePid::ThisProcess() );

  size_t numPids = _childTable.size();
  size_t numMaps = _pidMapTable.size();
  o & numPids & numMaps;

  JTRACE ("Serializing Virtual Pid Table") (numPids);

  if ( o.isWriter() )
  {
    for ( iterator i = _childTable.begin(); i != _childTable.end(); ++i )
    {
      JSERIALIZE_ASSERT_POINT ( "ChildPid:" );
      pid_t oldPid = i->first;
      dmtcp::UniquePid uniquePid = i->second;
      o & oldPid & uniquePid;//.pid() & uniquePid.ppid() & uniquePid.hostid() & uniquePid.time();
    }
    for ( pid_iterator i = _pidMapTable.begin(); i != _pidMapTable.end(); ++i )
    {
      JSERIALIZE_ASSERT_POINT ( "PidMap:" );
      pid_t oldPid = i->first;
      pid_t newPid = i->second;
      o & oldPid & newPid;
    }
  }
  else
  {
    while ( numPids-- > 0 )
    {
      JSERIALIZE_ASSERT_POINT ( "ChildPid:" );
      pid_t oldPid;
      pid_t pid, ppid;
      time_t time;
      long host;
      dmtcp::UniquePid uniquePid;//(host, pid, ppid, time);
      o & oldPid & uniquePid;//pid & ppid & host & time;
//      dmtcp::UniquePid uniquePid(host, pid, ppid, time);

      _childTable[oldPid] = uniquePid;
    }
    while ( numMaps-- > 0 )
    {
      JSERIALIZE_ASSERT_POINT ( "PidMap:" );
      pid_t oldPid;
      pid_t newPid;
      o & oldPid & newPid;

      _pidMapTable[oldPid] = newPid;
    }
  }

  JSERIALIZE_ASSERT_POINT( "EOF" );
}

void dmtcp::VirtualPidTable::serializePidMap ( jalib::JBinarySerializer& o )
{
  int numMaps;
  o & numMaps;

  while ( numMaps-- > 0 )
  {
    JSERIALIZE_ASSERT_POINT ( "PidMap:[" );
    pid_t oldPid;
    pid_t newPid;
    o & oldPid & newPid;
    JSERIALIZE_ASSERT_POINT ( "]" );

    _pidMapTable[oldPid] = newPid;
  }
}

#endif
