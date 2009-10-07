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
#include <fcntl.h>
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
  _sid = -1;
  _isRootOfProcessTree = false;
  _childTable.clear();
  _tidVector.clear();
  _inferiorVector.clear();
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
  /*
   * PROTECTED_PIDTBL_FD corresponds to the file containing the pids/uniquePids
   *  of all child processes, tids of all threads, and tids of all inferior
   *  process(threads) which are being traced by this process.
   *
   * PROTECTED_PIDMAP_FD corresponds to the file containg computation wide
   *  original_pid -> current_pid map to avoid pid/tid collisions.
   */
  JTRACE("VirtualPidTable::postRestart");
  dmtcp::string serialFile = "/proc/self/fd/" + jalib::XToString ( PROTECTED_PIDTBL_FD );

  serialFile = jalib::Filesystem::ResolveSymlink ( serialFile );
  JASSERT ( serialFile.length() > 0 ) ( serialFile );
  _real_close ( PROTECTED_PIDTBL_FD );
  
  jalib::JBinarySerializeReader rd ( serialFile );
  serialize ( rd );
}

void dmtcp::VirtualPidTable::postRestart2()
{
  JTRACE("VirtualPidTable::postRestart2");
  dmtcp::string pidMapFile = "/proc/self/fd/" + jalib::XToString ( PROTECTED_PIDMAP_FD );
  pidMapFile =  jalib::Filesystem::ResolveSymlink ( pidMapFile );
  JASSERT ( pidMapFile.length() > 0 ) ( pidMapFile );

  _real_close( PROTECTED_PIDMAP_FD );

  jalib::JBinarySerializeReader pidrd ( pidMapFile );
  serializePidMap( pidrd );
}

void dmtcp::VirtualPidTable::resetOnFork()
{
  _pid = _real_getpid();
  _ppid = currentToOriginalPid ( _real_getppid() );
  _isRootOfProcessTree = false;
  _childTable.clear();
  _tidVector.clear();
  _inferiorVector.clear();
  //_pidMapTable[_pid] = _pid;
}

pid_t dmtcp::VirtualPidTable::originalToCurrentPid( pid_t originalPid )
{
  pid_iterator i = _pidMapTable.find(originalPid); 
  if ( i == _pidMapTable.end() ) 
  {
    JTRACE ( "No currentPid found for the given originalPid (new or unknown pid/tid?), returning the originalPid") ( originalPid );
    return originalPid;
  }

  return i->second;
}

pid_t dmtcp::VirtualPidTable::currentToOriginalPid( pid_t currentPid )
{
  for (pid_iterator i = _pidMapTable.begin(); i != _pidMapTable.end(); ++i)
  {
    if ( currentPid == i->second )
      return i->first;
  }
    JTRACE ( "No originalPid found for the given currentPid (new or unknown pid/tid?), returning the currentPid") ( currentPid );

  return currentPid;
}

void dmtcp::VirtualPidTable::insert ( pid_t originalPid, dmtcp::UniquePid uniquePid )
{
  iterator i = _childTable.find( originalPid );
  if ( i != _childTable.end() )
    JTRACE ( "originalPid -> currentPid mapping exists!") ( originalPid ) ( i->second );

  JTRACE ( "Creating new originalPid -> currentPid mapping." ) ( originalPid ) ( uniquePid );

  _childTable[originalPid] = uniquePid;

  _pidMapTable[originalPid] = originalPid;
}

void dmtcp::VirtualPidTable::erase( pid_t originalPid )
{
  iterator i = _childTable.find ( originalPid );
  if ( i != _childTable.end() )
    _childTable.erase( originalPid );

  pid_iterator j = _pidMapTable.find ( originalPid );
  if ( j != _pidMapTable.end() )
    _pidMapTable.erase( originalPid );
}

void dmtcp::VirtualPidTable::updateRootOfProcessTree()
{
  if ( _real_getppid() == 1 )
    _isRootOfProcessTree = true;
}

void dmtcp::VirtualPidTable::updateMapping( pid_t originalPid, pid_t currentPid )
{
  _pidMapTable[originalPid] = currentPid;
}

dmtcp::vector< pid_t > dmtcp::VirtualPidTable::getPidVector( )
{
  dmtcp::vector< pid_t > pidVec;
  for ( pid_iterator i = _pidMapTable.begin(); i != _pidMapTable.end(); ++i )
    pidVec.push_back ( i->first );
  return pidVec;
}

dmtcp::vector< pid_t > dmtcp::VirtualPidTable::getTidVector( )
{
  return _tidVector;
}

dmtcp::vector< pid_t > dmtcp::VirtualPidTable::getInferiorVector( )
{
  return _inferiorVector;
}

void dmtcp::VirtualPidTable::insertTid( pid_t tid )
{
  eraseTid( tid );
  JTRACE ( "Inserting TID into tidVector" ) ( tid );
  _tidVector.push_back ( tid );
  return;
}

void dmtcp::VirtualPidTable::insertInferior( pid_t tid )
{
  eraseInferior( tid );
  _inferiorVector.push_back ( tid );
  return;
}

void dmtcp::VirtualPidTable::eraseTid( pid_t tid )
{
  dmtcp::vector< pid_t >::iterator iter = _tidVector.begin();
  while ( iter != _tidVector.end() ) {
    if ( *iter == tid ) {
      _tidVector.erase( iter );
      _pidMapTable.erase(tid);
    }
    else
      ++iter;
  }
  return;
}

void dmtcp::VirtualPidTable::prepareForExec( )
{
  int i;
  JTRACE("Preparing for exec. Emptying tidVector");
  for (i = 0; i < _tidVector.size(); i++) {
    _pidMapTable.erase( _tidVector[i] );
  }
  _tidVector.clear();
}

void dmtcp::VirtualPidTable::eraseInferior( pid_t tid )
{
  dmtcp::vector< pid_t >::iterator iter = _inferiorVector.begin();
  while ( iter != _inferiorVector.end() ) {
    if ( *iter == tid )
      _inferiorVector.erase( iter );
    else
      ++iter;
  }
  return;
}

bool dmtcp::VirtualPidTable::pidExists( pid_t pid )
{
  pid_iterator j = _pidMapTable.find ( pid );

  if ( j == _pidMapTable.end() )
    return false;

  return true;
}

void dmtcp::VirtualPidTable::serialize ( jalib::JBinarySerializer& o )
{
  JSERIALIZE_ASSERT_POINT ( "dmtcp::VirtualPidTable:" );

  if (o.isWriter() )
    updateRootOfProcessTree();//      _isRootOfProcessTree = true;

  o & _isRootOfProcessTree & _sid & _ppid;

  if ( _isRootOfProcessTree )
    JTRACE ( "This process is Root of Process Tree" );// ( UniquePid::ThisProcess() );

  serializeChildTable ( o );
  
  serializePidMap     ( o );

  JTRACE ("Serializing tidVector");
  JSERIALIZE_ASSERT_POINT ( "TID Vector:[" );
  o & _tidVector;
  JSERIALIZE_ASSERT_POINT ( "}" );

  JTRACE ("Serializing inferiorVector");
  JSERIALIZE_ASSERT_POINT ( "Inferior Vector:[" );
  o & _inferiorVector;
  JSERIALIZE_ASSERT_POINT ( "]" );

  JSERIALIZE_ASSERT_POINT( "EOF" );
}


void dmtcp::VirtualPidTable::serializeChildTable ( jalib::JBinarySerializer& o )
{
  size_t numPids = _childTable.size();
  serializeEntryCount(o, numPids);

  JTRACE ("Serializing ChildPid Table") (numPids) (o.filename());
  pid_t originalPid;
  dmtcp::UniquePid uniquePid;

  if ( o.isWriter() )
  {
    for ( iterator i = _childTable.begin(); i != _childTable.end(); ++i )
    {
      originalPid = i->first;
      uniquePid   = i->second;
      serializeChildTableEntry ( o, originalPid, uniquePid );
    }
  }
  else
  {
    while ( numPids-- > 0 )
    {
      serializeChildTableEntry ( o, originalPid, uniquePid );
      _childTable[originalPid] = uniquePid;
    }
  }
}

void  dmtcp::VirtualPidTable::serializeChildTableEntry ( 
    jalib::JBinarySerializer& o, pid_t& originalPid, dmtcp::UniquePid& uniquePid )
{
  JSERIALIZE_ASSERT_POINT ( "ChildPid:[" );
  o & originalPid & uniquePid;
  JSERIALIZE_ASSERT_POINT ( "]" );
}

void dmtcp::VirtualPidTable::serializePidMap ( jalib::JBinarySerializer& o )
{
  size_t numMaps = _pidMapTable.size();
  serializeEntryCount(o, numMaps);

  JTRACE ("Serializing PidMap Table") (numMaps) (o.filename());

  pid_t originalPid;
  pid_t currentPid;

  if ( o.isWriter() )
  {
    for ( pid_iterator i = _pidMapTable.begin(); i != _pidMapTable.end(); ++i )
    {
      originalPid = i->first;
      currentPid  = i->second;
      serializePidMapEntry ( o, originalPid, currentPid );
    }
  }
  else
  {
    while ( numMaps-- > 0 )
    {
      serializePidMapEntry ( o, originalPid, currentPid );
      _pidMapTable[originalPid] = currentPid;
    }
  }
}

void dmtcp::VirtualPidTable::serializePidMapEntry ( 
    jalib::JBinarySerializer& o, pid_t& originalPid, pid_t& currentPid )
{
  JSERIALIZE_ASSERT_POINT ( "PidMap:[" );
  o & originalPid & currentPid;
  JSERIALIZE_ASSERT_POINT ( "]" );
}

void dmtcp::VirtualPidTable::serializeEntryCount (
    jalib::JBinarySerializer& o, size_t& count )
{
  JSERIALIZE_ASSERT_POINT ( "NumEntries:[" );
  o & count;
  JSERIALIZE_ASSERT_POINT ( "]" );
}


void dmtcp::VirtualPidTable::InsertIntoPidMapFile(jalib::JBinarySerializer& o,
                                                  pid_t originalPid,
                                                  pid_t currentPid)
{
  struct flock fl;
  int fd;

  fl.l_type   = F_WRLCK;  /* F_RDLCK, F_WRLCK, F_UNLCK    */
  fl.l_whence = SEEK_SET; /* SEEK_SET, SEEK_CUR, SEEK_END */
  fl.l_start  = 0;        /* Offset from l_whence         */
  fl.l_len    = 0;        /* length, 0 = to EOF           */
  fl.l_pid    = _real_getpid(); /* our PID                      */

  int result = -1;
  errno = 0;
  while (result == -1 || errno == EINTR )
    result = fcntl(PROTECTED_PIDMAP_FD, F_SETLKW, &fl);  /* F_GETLK, F_SETLK, F_SETLKW */

  JASSERT ( result != -1 ) (strerror(errno)) (errno) . Text ( "Unable to lock the PID MAP file" );

  JTRACE ( "Serializing PID MAP Entry:" ) ( originalPid ) ( currentPid );
  /* Write the mapping to the file*/
  dmtcp::VirtualPidTable::serializePidMapEntry ( o, originalPid, currentPid );

  //fsync(PROTECTED_PIDMAP_FD);

  fl.l_type   = F_UNLCK;  /* tell it to unlock the region */
  result = fcntl(PROTECTED_PIDMAP_FD, F_SETLK, &fl); /* set the region to unlocked */

  JASSERT (result != -1 || errno == ENOLCK) .Text ( "Unlock Failed" ) ;
}

#endif
