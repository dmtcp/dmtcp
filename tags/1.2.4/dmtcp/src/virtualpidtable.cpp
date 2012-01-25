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

#include "virtualpidtable.h"
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
#include  "../jalib/jconvert.h"
#include  "../jalib/jfilesystem.h"

#ifdef PID_VIRTUALIZATION

static pthread_mutex_t tblLock = PTHREAD_MUTEX_INITIALIZER;

static void _do_lock_tbl()
{
  JASSERT(_real_pthread_mutex_lock(&tblLock) == 0) (JASSERT_ERRNO);
}

static void _do_unlock_tbl()
{
  JASSERT(_real_pthread_mutex_unlock(&tblLock) == 0) (JASSERT_ERRNO);
}

dmtcp::VirtualPidTable::VirtualPidTable()
{
  _do_lock_tbl();
  _pid = _real_getpid();
  _ppid = _real_getppid();
  _sid = -1;
  _gid = _real_getpgid(0);
  _isRootOfProcessTree = false;
  _childTable.clear();
  _tidVector.clear();
  _pthreadJoinId.clear();
  _inferiorVector.clear();
  _pidMapTable.clear();
  _pidMapTable[_pid] = _pid;
  _do_unlock_tbl();
}

static dmtcp::VirtualPidTable *inst = NULL;
dmtcp::VirtualPidTable& dmtcp::VirtualPidTable::instance()
{
  if (inst == NULL) {
    inst = new VirtualPidTable();
  }
  return *inst;
//  static VirtualPidTable *inst = new VirtualPidTable(); return *inst;
}

bool dmtcp::VirtualPidTable::isConflictingPid( pid_t pid)
{
  /*  If pid != originalToCurrentPid(pid), then there is a conflict because
   *    there is an original_pid same as this pid.
   *
   *  If pid == originalToCurrentPid(pid), then there are two cases:
   *    1. there is no mapping from some original_pid to pid ==> no conflict
   *    2. there is a mapping from pid to pid in the table, in which case again
   *       there is not conflict because that mapping essentially is about the
   *       current pid.
   */
#if 0
  // Use a similar block to emulate tid-conflict. This can come handy in
  // finding bugs related to tid-conflict.
  if (pid >= 10000 && pid < 20000) {
    return true;
  }
#endif
  if (pid == instance().originalToCurrentPid( pid ))
    return false;

  return true;
}

void dmtcp::VirtualPidTable::preCheckpoint()
{
  // Update Group information before checkpoint
  _ppid = getppid(); // refresh parent PID
  _gid = getpgid(0);

  _fgid = -1;
  dmtcp::string controllingTerm = jalib::Filesystem::GetControllingTerm();
  if (!controllingTerm.empty()) {
    int tfd = _real_open(controllingTerm.c_str(), O_RDONLY, 0);
    if (tfd >= 0) {
      _fgid = tcgetpgrp(tfd);
      _real_close(tfd);
    }
  }

  /*
  char s[ L_ctermid ];
  // play around group ID
  _fgid = -1;
  if( ctermid(s) ){
    int tfd = open(s,O_RDONLY);
    if( tfd >= 0 ){
      _fgid = tcgetpgrp(tfd);
      close(tfd);
    }
  }
  */

  JTRACE("CHECK GROUP PID")(_gid)(_fgid)(_ppid)(pidExists(_gid));
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
  dmtcp::string serialFile = "/proc/self/fd/" + jalib::XToString ( PROTECTED_PIDTBL_FD );

  serialFile = jalib::Filesystem::ResolveSymlink ( serialFile );
  JASSERT ( serialFile.length() > 0 ) ( serialFile );
  _real_close ( PROTECTED_PIDTBL_FD );

  JTRACE("Read original pids from pid-table file") (serialFile);
  jalib::JBinarySerializeReader rd ( serialFile );
  serialize ( rd );
}

void dmtcp::VirtualPidTable::restoreProcessGroupInfo()
{
  // Restore group assignment
  if( pidExists(_gid) ){
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
    JTRACE("VirtualPidTable::postRestart SKIP Group information, GID unknown");
  }
}

void dmtcp::VirtualPidTable::printPidMaps()
{
  ostringstream out;
  out << "Pid mappings\n";
  out << "      original" << "  ->  " << "current" << "\n";
  for ( pid_iterator i = _pidMapTable.begin(); i != _pidMapTable.end(); ++i ) {
    pid_t originalPid = i->first;
    pid_t currentPid  = i->second;
    out << "\t" << originalPid << "\t->   " << currentPid << "\n";
  }
  JTRACE("Original To Current Pid Mappings:") (_pidMapTable.size()) (out.str());
}

void dmtcp::VirtualPidTable::resetOnFork()
{
  pthread_mutex_t newlock = PTHREAD_MUTEX_INITIALIZER;
  tblLock = newlock;
  _pid = _real_getpid();
  _ppid = currentToOriginalPid ( _real_getppid() );
  _isRootOfProcessTree = false;
  _childTable.clear();
  _tidVector.clear();
  _pthreadJoinId.clear();
  _inferiorVector.clear();
  //_pidMapTable[_pid] = _pid;
  printPidMaps();
}

pid_t dmtcp::VirtualPidTable::originalToCurrentPid( pid_t originalPid )
{ pid_t retVal = 0;

  /* This code is called from MTCP while the checkpoint thread is holding
     the JASSERT log lock. Therefore, don't call JTRACE/JASSERT/JINFO/etc. in
     this function. */
  _do_lock_tbl();
  pid_iterator i = _pidMapTable.find(originalPid);
  if ( i == _pidMapTable.end() ) {
    _do_unlock_tbl();
    return originalPid;
  }

  // retVal required: in TID conflict, first and second clone call can interfere
  retVal = i->second;
  _do_unlock_tbl();
  return retVal;
}

pid_t dmtcp::VirtualPidTable::currentToOriginalPid( pid_t currentPid )
{
  /* This code is called from MTCP while the checkpoint thread is holding
     the JASSERT log lock. Therefore, don't call JTRACE/JASSERT/JINFO/etc. in
     this function. */
  _do_lock_tbl();
  for (pid_iterator i = _pidMapTable.begin(); i != _pidMapTable.end(); ++i)
  {
    if ( currentPid == i->second ) {
      _do_unlock_tbl();
      return i->first;
    }
  }

  _do_unlock_tbl();
  return currentPid;
}

void dmtcp::VirtualPidTable::insert ( pid_t originalPid, dmtcp::UniquePid uniquePid )
{
  _do_lock_tbl();
  iterator i = _childTable.find( originalPid );
  if ( i != _childTable.end() ) {
    _do_unlock_tbl();
    JTRACE ( "originalPid -> currentPid mapping exists!") ( originalPid ) ( i->second );
  }

  _childTable[originalPid] = uniquePid;
  _pidMapTable[originalPid] = originalPid;

  _do_unlock_tbl();

  JTRACE ( "Creating new originalPid -> currentPid mapping." ) ( originalPid ) ( uniquePid );
}

void dmtcp::VirtualPidTable::erase( pid_t originalPid )
{
  _do_lock_tbl();
  iterator i = _childTable.find ( originalPid );
  if ( i != _childTable.end() )
    _childTable.erase( originalPid );

  pid_iterator j = _pidMapTable.find ( originalPid );
  if ( j != _pidMapTable.end() )
    _pidMapTable.erase( originalPid );
  _do_unlock_tbl();
}

void dmtcp::VirtualPidTable::updateRootOfProcessTree()
{
  if ( _real_getppid() == 1 )
    _isRootOfProcessTree = true;
}

void dmtcp::VirtualPidTable::updateMapping( pid_t originalPid, pid_t currentPid )
{
  _do_lock_tbl();
  _pidMapTable[originalPid] = currentPid;
  _do_unlock_tbl();
}

dmtcp::vector< pid_t > dmtcp::VirtualPidTable::getPidVector( )
{
  dmtcp::vector< pid_t > pidVec;
  for ( pid_iterator i = _pidMapTable.begin(); i != _pidMapTable.end(); ++i )
    pidVec.push_back ( i->first );
  return pidVec;
}

dmtcp::vector< pid_t > dmtcp::VirtualPidTable::getChildPidVector( )
{
  dmtcp::vector< pid_t > childPidVec;
  for ( iterator i = _childTable.begin(); i != _childTable.end(); ++i )
    childPidVec.push_back ( i->first );
  return childPidVec;
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
  _do_lock_tbl();
  _tidVector.push_back ( tid );
  _do_unlock_tbl();
  return;
}

void dmtcp::VirtualPidTable::insertInferior( pid_t tid )
{
  eraseInferior( tid );
  _do_lock_tbl();
  _inferiorVector.push_back ( tid );
  _do_unlock_tbl();
  return;
}

void dmtcp::VirtualPidTable::eraseTid( pid_t tid )
{
  _do_lock_tbl();
  dmtcp::vector< pid_t >::iterator iter = _tidVector.begin();
  while ( iter != _tidVector.end() ) {
    if ( *iter == tid ) {
      _tidVector.erase( iter );
      _pidMapTable.erase(tid);
      break;
    }
    else
      ++iter;
  }
  _do_unlock_tbl();
  return;
}

void dmtcp::VirtualPidTable::postExec( )
{
  JTRACE("Post-Exec. Emptying tidVector");
  _do_lock_tbl();
  for (size_t i = 0; i < _tidVector.size(); i++) {
    _pidMapTable.erase( _tidVector[i] );
  }
  _tidVector.clear();
  _do_unlock_tbl();
}

void dmtcp::VirtualPidTable::eraseInferior( pid_t tid )
{
  _do_lock_tbl();
  dmtcp::vector< pid_t >::iterator iter = _inferiorVector.begin();
  while ( iter != _inferiorVector.end() ) {
    if ( *iter == tid ) {
      _inferiorVector.erase( iter );
      break;
    }
    else
      ++iter;
  }
  _do_unlock_tbl();
  return;
}

bool dmtcp::VirtualPidTable::beginPthreadJoin(pthread_t thread)
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

void dmtcp::VirtualPidTable::endPthreadJoin(pthread_t thread)
{
  _do_lock_tbl();
  JASSERT(pthread_equal(_pthreadJoinId[thread], pthread_self()));
  _pthreadJoinId.erase(thread);
  _do_unlock_tbl();
}

bool dmtcp::VirtualPidTable::pidExists( pid_t pid )
{
  bool retVal = false;
  _do_lock_tbl();
  pid_iterator j = _pidMapTable.find ( pid );
  if ( j != _pidMapTable.end() )
    retVal = true;

  _do_unlock_tbl();
  return retVal;
}

void dmtcp::VirtualPidTable::refresh()
{
  updateRootOfProcessTree();//      _isRootOfProcessTree = true;
  refreshChildTable();
  refreshTidVector();
}

void dmtcp::VirtualPidTable::refreshTidVector()
{
  dmtcp::vector< pid_t >::iterator iter;
  for (iter = _tidVector.begin(); iter != _tidVector.end(); ) {
    int retVal = syscall(SYS_tgkill, _pid, *iter, 0);
    if (retVal == -1 && errno == ESRCH) {
      erase(*iter);
      iter = _tidVector.erase( iter );
    } else {
      iter++;
    }
  }
  return;
}

void dmtcp::VirtualPidTable::refreshChildTable()
{
  for ( iterator i = _childTable.begin(); i != _childTable.end(); ++i ) {
    pid_t originalPid = i->first;
    int retVal = kill(originalPid, 0);
    /* Check to see if the child process is alive*/
    if (retVal == -1 && errno == ESRCH) {
      erase(originalPid);
    }
  }
}

void dmtcp::VirtualPidTable::serialize ( jalib::JBinarySerializer& o )
{
  JSERIALIZE_ASSERT_POINT ( "dmtcp::VirtualPidTable:" );

  if (o.isWriter()){
    updateRootOfProcessTree();//      _isRootOfProcessTree = true;
    //refreshChildTable();
    //refreshTidVector();
  }

  JTRACE("Save pid information")(_sid)(_ppid)(_gid)(_fgid);
  o & _isRootOfProcessTree & _pid & _sid & _ppid & _gid & _fgid;

  if ( _isRootOfProcessTree ) {
    JTRACE ( "This process is Root of Process Tree" );// ( UniquePid::ThisProcess() );
  }

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

  pid_t originalPid;
  pid_t currentPid;

  if ( o.isWriter() )
  {
    for ( pid_iterator i = _pidMapTable.begin(); i != _pidMapTable.end(); ++i ) {
      originalPid = i->first;
      currentPid  = i->second;
      serializePidMapEntry ( o, originalPid, currentPid );
    }
  }
  else
  {
    for (size_t i = 0; i < numMaps; i++) {
      serializePidMapEntry ( o, originalPid, currentPid );
      _pidMapTable[originalPid] = currentPid;
    }
  }

  JTRACE ("Serializing PidMap Table") (numMaps) (o.filename());
  printPidMaps();
}

void dmtcp::VirtualPidTable::serializePidMapEntry (
    jalib::JBinarySerializer& o, pid_t& originalPid, pid_t& currentPid )
{
  JSERIALIZE_ASSERT_POINT ( "PidMap:[" );
  o & originalPid & currentPid;
  JSERIALIZE_ASSERT_POINT ( "]" );
}

void dmtcp::VirtualPidTable::serializeEntryCount ( jalib::JBinarySerializer& o,
                                                   size_t& count )
{
  JSERIALIZE_ASSERT_POINT ( "NumEntries:[" );
  o & count;
  JSERIALIZE_ASSERT_POINT ( "]" );
  JTRACE("Num PidMaps:")(count);
}

void dmtcp::VirtualPidTable::InsertIntoPidMapFile( pid_t originalPid, pid_t currentPid)
{

  dmtcp::string pidMapFile = "/proc/self/fd/"
                             + jalib::XToString ( PROTECTED_PIDMAP_FD );
  dmtcp::string pidMapCountFile = "/proc/self/fd/"
                                  + jalib::XToString ( PROTECTED_PIDMAPCNT_FD );

  pidMapFile =  jalib::Filesystem::ResolveSymlink ( pidMapFile );
  pidMapCountFile =  jalib::Filesystem::ResolveSymlink ( pidMapCountFile );
  JASSERT ( pidMapFile.length() > 0 && pidMapCountFile.length() > 0 )
    ( pidMapFile )( pidMapCountFile ).Text("Failed to resolve symlink.");

  // Create Serializers
  jalib::JBinarySerializeWriterRaw mapwr( pidMapFile, PROTECTED_PIDMAP_FD );
  jalib::JBinarySerializeWriterRaw countwr(pidMapCountFile, PROTECTED_PIDMAPCNT_FD );
  jalib::JBinarySerializeReaderRaw countrd(pidMapCountFile, PROTECTED_PIDMAPCNT_FD );

  // Lock fileset before any operations
  Util::lockFile(PROTECTED_PIDMAP_FD);
  _do_lock_tbl();

  // Read old number of saved pid maps
  countrd.rewind();
  size_t numMaps;
  serializeEntryCount (countrd,numMaps);
  // Serialize new pair
  serializePidMapEntry (mapwr, originalPid, currentPid );

  // Commit changes into map count file
  countwr.rewind();
  numMaps++;
  serializeEntryCount (countwr,numMaps);

  _do_unlock_tbl();
  Util::unlockFile(PROTECTED_PIDMAP_FD);
}

void dmtcp::VirtualPidTable::readPidMapsFromFile()
{
  dmtcp::string pidMapFile = "/proc/self/fd/"
                             + jalib::XToString ( PROTECTED_PIDMAP_FD );
  pidMapFile =  jalib::Filesystem::ResolveSymlink ( pidMapFile );
  dmtcp::string pidMapCountFile = "/proc/self/fd/"
                                  + jalib::XToString ( PROTECTED_PIDMAPCNT_FD );
  pidMapCountFile =  jalib::Filesystem::ResolveSymlink ( pidMapCountFile );
  JASSERT ( pidMapFile.length() > 0 && pidMapCountFile.length() > 0 )
    ( pidMapFile )( pidMapCountFile );

  JTRACE ( "Read PidMaps from file" ) ( pidMapCountFile ) ( pidMapFile );

  _real_close( PROTECTED_PIDMAP_FD );
  _real_close( PROTECTED_PIDMAPCNT_FD );

  jalib::JBinarySerializeReader maprd( pidMapFile);
  jalib::JBinarySerializeReader countrd(pidMapCountFile);

  // Read number of PID mappings
  size_t numMaps;
  serializeEntryCount (countrd,numMaps);

  // Read pidMapping content
  pid_t originalPid;
  pid_t currentPid;
  while ( numMaps-- > 0 ){
    serializePidMapEntry ( maprd, originalPid, currentPid );
    _pidMapTable[originalPid] = currentPid;
  }
  printPidMaps();
}

#endif
