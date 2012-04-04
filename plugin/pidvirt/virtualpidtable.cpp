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
#include "protectedfds.h"
#include "pidwrappers.h"
#include "../jalib/jconvert.h"
#include "../jalib/jfilesystem.h"
#include "virtualpidtable.h"
#include "dmtcpplugin.h"

#define INITIAL_VIRTUAL_TID 1
#define MAX_VIRTUAL_TID 999
static pthread_mutex_t tblLock = PTHREAD_MUTEX_INITIALIZER;
static pid_t _nextVirtualTid = INITIAL_VIRTUAL_TID;
static int _numTids = 1;

static void _do_lock_tbl()
{
  JASSERT(pthread_mutex_lock(&tblLock) == 0) (JASSERT_ERRNO);
}

static void _do_unlock_tbl()
{
  JASSERT(pthread_mutex_unlock(&tblLock) == 0) (JASSERT_ERRNO);
}

dmtcp::VirtualPidTable::VirtualPidTable()
{
  _do_lock_tbl();
  _pidMapTable.clear();
  //_pidMapTable[getpid()] = _real_getpid();
  //_pidMapTable[getppid()] = _real_getppid();
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

void dmtcp::VirtualPidTable::postRestart()
{
  /*
   * PROTECTED_PIDMAP_FD corresponds to the file containg computation wide
   *  virtualPid -> realPid map to avoid pid/tid collisions.
   */
  _do_lock_tbl();
  _pidMapTable.clear();
  _pidMapTable[getpid()] = _real_getpid();
  _do_unlock_tbl();
}

void dmtcp::VirtualPidTable::printPidMaps()
{
  ostringstream out;
  out << "Pid mappings\n";
  out << "      Virtual" << "  ->  " << "Real" << "\n";
  for ( pid_iterator i = _pidMapTable.begin(); i != _pidMapTable.end(); ++i ) {
    pid_t virtualPid = i->first;
    pid_t realPid  = i->second;
    out << "\t" << virtualPid << "\t->   " << realPid << "\n";
  }
  JTRACE("Virtual To Real Pid Mappings:") (_pidMapTable.size()) (out.str());
}

void dmtcp::VirtualPidTable::refresh()
{
  pid_t pid = getpid();
  pid_iterator i;
  pid_iterator next;
  pid_t _real_pid = _real_getpid();

  _do_lock_tbl();
  for (i = _pidMapTable.begin(), next = i; i != _pidMapTable.end(); i = next) {
    next++;
    if (i->second > pid && i->second <= pid + MAX_VIRTUAL_TID
        && _real_tgkill(_real_pid, i->second, 0) == -1) {
      _pidMapTable.erase(i);
    }
  }
  _do_unlock_tbl();
  printPidMaps();
}

pid_t dmtcp::VirtualPidTable::getNewVirtualTid()
{
  pid_t tid = -1;

  if (_numTids == MAX_VIRTUAL_TID) {
    refresh();
  }

  JASSERT(_numTids < MAX_VIRTUAL_TID)
    .Text("Exceeded maximum number of threads allowed");

  _do_lock_tbl();
  int count = 0;
  while (1) {
    tid = getpid() + _nextVirtualTid++;
    if (_nextVirtualTid >= MAX_VIRTUAL_TID) {
      _nextVirtualTid = INITIAL_VIRTUAL_TID;
    }
    pid_iterator i = _pidMapTable.find(tid);
    if (i == _pidMapTable.end()) {
      break;
    }
    if (++count == MAX_VIRTUAL_TID) {
      break;
    }
  }
  _do_unlock_tbl();
  JASSERT(tid != -1) .Text("Not Reachable");
  return tid;
}

void dmtcp::VirtualPidTable::resetOnFork()
{
  pthread_mutex_t newlock = PTHREAD_MUTEX_INITIALIZER;
  tblLock = newlock;
  _nextVirtualTid = INITIAL_VIRTUAL_TID;
  _numTids = 1;
  _pidMapTable[getpid()] = _real_getpid();
  refresh();
  printPidMaps();
}

pid_t dmtcp::VirtualPidTable::virtualToReal(pid_t virtualPid)
{
  pid_t retVal = 0;

  if (virtualPid == -1 || virtualPid == 0) {
    return virtualPid;
  }

  /* This code is called from MTCP while the checkpoint thread is holding
     the JASSERT log lock. Therefore, don't call JTRACE/JASSERT/JINFO/etc. in
     this function. */
  _do_lock_tbl();
  pid_iterator i = _pidMapTable.find(virtualPid < -1 ? abs(virtualPid)
                                                     : virtualPid);
  if (i == _pidMapTable.end()) {
    _do_unlock_tbl();
    return virtualPid;
  }

  retVal = virtualPid < -1 ? (-i->second) : i->second;
  _do_unlock_tbl();
  return retVal;
}

//to allow linking without ptrace plugin
extern "C" int dmtcp_is_ptracing() __attribute__ ((weak));
pid_t dmtcp::VirtualPidTable::realToVirtual(pid_t realPid)
{
  if (realPid == -1 || realPid == 0) {
    return realPid;
  }

  /* This code is called from MTCP while the checkpoint thread is holding
     the JASSERT log lock. Therefore, don't call JTRACE/JASSERT/JINFO/etc. in
     this function. */
  _do_lock_tbl();
  for (pid_iterator i = _pidMapTable.begin(); i != _pidMapTable.end(); ++i) {
    if ( realPid == i->second ) {
      _do_unlock_tbl();
      return i->first;
    }
  }

  if (dmtcp_is_ptracing != 0 && dmtcp_is_ptracing()) {
    pid_t virtualPid = readVirtualTidFromFileForPtrace(gettid());
    if (virtualPid != -1) {
      _do_unlock_tbl();
      updateMapping(virtualPid, realPid);
      return virtualPid;
    }
  }

  //JWARNING(false) (realPid)
    //.Text("No virtual pid/tid found for the given real pid");
  _do_unlock_tbl();
  return realPid;
}

void dmtcp::VirtualPidTable::erase( pid_t virtualPid )
{
  _do_lock_tbl();
  _pidMapTable.erase( virtualPid );
  _do_unlock_tbl();
}

void dmtcp::VirtualPidTable::updateMapping( pid_t virtualPid, pid_t realPid )
{
  _do_lock_tbl();
  _pidMapTable[virtualPid] = realPid;
  _do_unlock_tbl();
}

dmtcp::vector< pid_t > dmtcp::VirtualPidTable::getPidVector( )
{
  dmtcp::vector< pid_t > pidVec;
  _do_lock_tbl();
  for ( pid_iterator i = _pidMapTable.begin(); i != _pidMapTable.end(); ++i )
    pidVec.push_back ( i->first );
  _do_unlock_tbl();
  return pidVec;
}

bool dmtcp::VirtualPidTable::realPidExists( pid_t pid )
{
  bool retval = false;
  _do_lock_tbl();
  for (pid_iterator i = _pidMapTable.begin(); i != _pidMapTable.end(); ++i) {
    if (i->second == pid) {
      retval = true;
      break;
    }
  }
  _do_unlock_tbl();
  return retval;
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

void dmtcp::VirtualPidTable::writeVirtualTidToFileForPtrace(pid_t pid)
{
  pid_t tracerPid = dmtcp::Util::getTracerPid();
  if (tracerPid != 0) {
    dmtcp::ostringstream o;
    char buf[80];
    o << dmtcp_get_tmpdir() << "/virtualPidOfNewlyCreatedThread_"
      << dmtcp_get_computation_id_str() << "_" << tracerPid;

    sprintf(buf, "%d", pid);
    int fd = open(o.str().c_str(), O_CREAT|O_WRONLY|O_TRUNC, 0600);
    JASSERT(fd >= 0) (o.str()) (JASSERT_ERRNO);
    dmtcp::Util::writeAll(fd, buf, strlen(buf) + 1);
    JTRACE("Writing virtual Pid/Tid to file") (pid) (o.str());
    close(fd);
  }
}

pid_t dmtcp::VirtualPidTable::readVirtualTidFromFileForPtrace(pid_t tid)
{
  dmtcp::ostringstream o;
  char buf[80];
  pid_t pid;
  int fd;
  ssize_t bytesRead;

  if (tid == -1) {
    tid = dmtcp::Util::getTracerPid();
    if (tid == 0) {
      return -1;
    }
  }

  o << dmtcp::UniquePid::getTmpDir() << "/virtualPidOfNewlyCreatedThread_"
    << dmtcp::UniquePid::ComputationId() << "_" << tid;

  fd = _real_open(o.str().c_str(), O_RDONLY, 0);
  if (fd < 0) {
    return -1;
  }
  bytesRead = dmtcp::Util::readAll(fd, buf, sizeof(buf));
  close(fd);
  unlink(o.str().c_str());

  if (bytesRead <= 0) {
    return -1;
  }

  sscanf(buf, "%d", &pid);
  JTRACE("Read virtual Pid/Tid from file") (pid) (o.str());
  return pid;
}

void dmtcp::VirtualPidTable::serialize ( jalib::JBinarySerializer& o )
{
  JSERIALIZE_ASSERT_POINT ( "dmtcp::VirtualPidTable:" );
  o.serializeMap(_pidMapTable);
  JSERIALIZE_ASSERT_POINT( "EOF" );
  printPidMaps();
}


void dmtcp::VirtualPidTable::writePidMapsToFile()
{
  //size_t numMaps = 0;
  dmtcp::string mapFile;
  mapFile = jalib::Filesystem::ResolveSymlink(
              "/proc/self/fd/" + jalib::XToString(PROTECTED_PIDMAP_FD));
  JASSERT (mapFile.length() > 0) (mapFile);
  JTRACE ("Write PidMaps to file") (mapFile);

  // Lock fileset before any operations
  Util::lockFile(PROTECTED_PIDMAP_FD);
  _do_lock_tbl();

  jalib::JBinarySerializeWriterRaw mapwr(mapFile, PROTECTED_PIDMAP_FD);
  mapwr.serializeMap(_pidMapTable);

  _do_unlock_tbl();
  Util::unlockFile(PROTECTED_PIDMAP_FD);
}

void dmtcp::VirtualPidTable::readPidMapsFromFile()
{
  dmtcp::string mapFile;
  size_t numMaps;

  mapFile = jalib::Filesystem::ResolveSymlink(
              "/proc/self/fd/" + jalib::XToString(PROTECTED_PIDMAP_FD));

  JASSERT(mapFile.length() > 0) (mapFile);
  JTRACE("Read PidMaps from file") (mapFile);

  Util::lockFile(PROTECTED_PIDMAP_FD);
  _do_lock_tbl();

  jalib::JBinarySerializeReaderRaw maprd(mapFile, PROTECTED_PIDMAP_FD);
  maprd.rewind();

  while (!maprd.isEOF()) {
    maprd.serializeMap(_pidMapTable);
  }

  _do_unlock_tbl();
  Util::unlockFile(PROTECTED_PIDMAP_FD);

  printPidMaps();
  close(PROTECTED_PIDMAP_FD);
  unlink(mapFile.c_str());
}
