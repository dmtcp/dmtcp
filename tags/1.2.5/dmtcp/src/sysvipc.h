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

#ifndef SYSVIPC_H
#define SYSVIPC_H

#include "dmtcpalloc.h"
#include "dmtcpworker.h"
#include <vector>
#include <sys/types.h>
#include <sys/socket.h>
#include <map>
#include  "../jalib/jbuffer.h"
#include  "../jalib/jserialize.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jconvert.h"
#include "../jalib/jalloc.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>

namespace dmtcp
{
  class ShmSegment;

  class SysVIPC
  {
    enum SysVIPCTyle {
      SHM = 0x10000,
      TYPEMASK = SHM
    };

    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif

      SysVIPC();

      static SysVIPC& instance();

      int  originalToCurrentShmid(int shmid);
      int  currentToOriginalShmid(int shmid);
      bool isConflictingShmid(int shmid);

      void leaderElection();
      void preCkptDrain();
      void preCheckpoint();
      void postCheckpoint();
      void postRestart();
      void preResume();

      dmtcp::vector<int> getShmids();

      int  shmaddrToShmid(const void* shmaddr);
      void removeStaleShmObjects();

      void serializeOriginalShmids();
      void InsertIntoShmidMapFile(int originalShmid, int currentShmid);
      void readPidMapsFromFile();

      void on_shmget(key_t key, size_t size, int shmflg, int shmid);
      void on_shmat(int shmid, const void *shmaddr, int shmflg, void* newaddr);
      void on_shmdt(const void *shmaddr);

      void writeShmidMapsToFile(int fd);
      void readShmidMapsFromFile(int fd);
      void serialize(jalib::JBinarySerializer& o);
    private:
      typedef dmtcp::map<int, ShmSegment>::iterator ShmIterator;
      dmtcp::map<int, ShmSegment> _shm;
      typedef dmtcp::map<int, int>::iterator ShmidMapIter;
      dmtcp::map<int, int> _originalToCurrentShmids;
  };

  class ShmSegment
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif

      ShmSegment() { _originalShmid = -1; }
      ShmSegment(int shmid);
      ShmSegment(key_t key, int size, int shmflg, int shmid);

      bool isStale();

      int originalShmid() { return _originalShmid; }
      int currentShmid()  { return _currentShmid; }

      void updateCurrentShmid(int shmid) { _currentShmid = shmid; }

      bool isValidShmaddr(const void* shmaddr);
      bool isCkptLeader() { return _isCkptLeader; };

      void leaderElection();
      void preCkptDrain();
      void preCheckpoint();

      void remapAll();
      void recreateShmSegment();
      void remapFirstAddrForOwnerOnRestart();

      void on_shmget (key_t key, size_t size, int shmflg, int shmid);
      void on_shmat  (void *shmaddr, int shmflg);
      void on_shmdt  (const void *shmaddr);

//      virtual void preCheckpoint ( const dmtcp::vector<int>& fds
//                                   , KernelBufferDrainer& drain );
//      virtual void postCheckpoint ( const dmtcp::vector<int>& fds );
//      virtual void restore ( const dmtcp::vector<int>&, ConnectionRewirer& );
//      virtual void restoreOptions ( const dmtcp::vector<int>& fds );
//
//      virtual void serializeSubClass ( jalib::JBinarySerializer& o );

    private:
      key_t   _key;
      int     _shmgetFlags;
      int     _originalShmid;
      int     _currentShmid;
      int     _size;
      int     _creatorPid;
      int     _dmtcpMappedAddr;
      shmatt_t _nattch;
      unsigned short _mode;
      struct shmid_ds _shminfo;
      bool    _isCkptLeader;
      typedef dmtcp::map<void*, int> ShmaddrToFlag;
      typedef dmtcp::map<void*, int>::iterator ShmaddrToFlagIter;
      ShmaddrToFlag _shmaddrToFlag;
  };
}
#endif
