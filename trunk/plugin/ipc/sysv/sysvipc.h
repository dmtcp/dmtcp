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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>

#include "jbuffer.h"
#include "jserialize.h"
#include "jassert.h"
#include "jconvert.h"
#include "jalloc.h"
#include "virtualidtable.h"
#include "shareddata.h"

#include "sysvipcwrappers.h"

#define REAL_TO_VIRTUAL_IPC_ID(id) \
  dmtcp::SysVIPC::instance().realToVirtualId(id)
#define VIRTUAL_TO_REAL_IPC_ID(id) \
  dmtcp::SysVIPC::instance().virtualToRealId(id)
namespace dmtcp
{
  class ShmSegment;
  class Semaphore;
  class MsgQueue;

  class SysVIPC
  {
    enum SysVIPCType {
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

      void resetOnFork();
      void leaderElection();
      void preCkptDrain();
      void preCheckpoint();
      void refill(bool isRestart);
      void postRestart();
      void preResume();

      int  virtualToRealId(int virtId) {
        if (_ipcVirtIdTable.virtualIdExists(virtId)) {
          return _ipcVirtIdTable.virtualToReal(virtId);
        } else {
          int realId = SharedData::getRealIPCId(virtId);
          return realId;
        }
      }
      int  realToVirtualId(int realId) {
        if (_ipcVirtIdTable.realIdExists(realId)) {
          return _ipcVirtIdTable.realToVirtual(realId);
        } else {
          return -1;
        }
      }
      void updateMapping(int virtId, int realId) {
        _ipcVirtIdTable.updateMapping(virtId, realId);
        SharedData::setIPCIdMap(virtId, realId);
      }

      int  getNewVirtualId();
      int  shmaddrToShmid(const void* shmaddr);
      void removeStaleObjects();

      void on_shmget(int shmid, key_t key, size_t size, int shmflg);
      void on_shmat(int shmid, const void *shmaddr, int shmflg, void* newaddr);
      void on_shmdt(const void *shmaddr);

      void on_semget(int semid, key_t key, int nsems, int semflg);
      void on_semctl(int semid, int semnum, int cmd, union semun arg);
      void on_semop(int semid, struct sembuf *sops, unsigned nsops);

      void on_msgget(int msqid, key_t key, int msgflg);
      void on_msgctl(int msqid, int cmd, struct msqid_ds *buf);
      void on_msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg);
      void on_msgrcv(int msqid, const void *msgp, size_t msgsz,
                     int msgtyp, int msgflg);

      void serialize(jalib::JBinarySerializer& o);
    private:
      dmtcp::map<int, ShmSegment*> _shm;
      typedef dmtcp::map<int, ShmSegment*>::iterator ShmIterator;
      dmtcp::map<int, Semaphore*> _sem;
      typedef dmtcp::map<int, Semaphore*>::iterator SemIterator;
      dmtcp::map<int, MsgQueue*> _msq;
      typedef dmtcp::map<int, MsgQueue*>::iterator MsqIterator;
      VirtualIdTable<int> _ipcVirtIdTable;
  };

  class SysVObj
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif

      SysVObj(int id, int realId, int key, int flags) {
        _key = key;
        _flags = flags;
        _id = id;
        _realId = realId;
        _isCkptLeader = false;
      }
      virtual ~SysVObj() {}

      int virtualId() { return _id; }

      bool isCkptLeader() { return _isCkptLeader; }

      virtual bool isStale() = 0;
      virtual void resetOnFork() = 0;
      virtual void leaderElection() = 0;
      virtual void preCkptDrain() = 0;
      virtual void preCheckpoint() = 0;
      virtual void postRestart() = 0;
      virtual void refill(bool isRestart) = 0;
      virtual void preResume() = 0;

    protected:
      int     _id;
      int     _realId;
      key_t   _key;
      int     _flags;
      bool    _isCkptLeader;
  };

  class ShmSegment : public SysVObj
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif

      ShmSegment(int shmid, int realShmid, key_t key, size_t size, int shmflg);

      virtual bool isStale();
      virtual void resetOnFork() {}
      virtual void leaderElection();
      virtual void preCkptDrain();
      virtual void preCheckpoint();
      virtual void postRestart();
      virtual void refill(bool isRestart) {}
      virtual void preResume();

      bool isValidShmaddr(const void* shmaddr);
      void remapAll();
      void remapFirstAddrForOwnerOnRestart();

      void on_shmat(const void *shmaddr, int shmflg);
      void on_shmdt(const void *shmaddr);

    private:
      size_t  _size;
      int     _dmtcpMappedAddr;
      shmatt_t _nattch;
      unsigned short _mode;
      struct shmid_ds _shminfo;
      typedef dmtcp::map<const void*, int> ShmaddrToFlag;
      typedef dmtcp::map<const void*, int>::iterator ShmaddrToFlagIter;
      ShmaddrToFlag _shmaddrToFlag;
  };

  class Semaphore : public SysVObj
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      Semaphore(int semid, int realSemid, key_t key, int nsems, int semflg);
      ~Semaphore() { delete _semval; delete _semadj; }
      void on_semop(struct sembuf *sops, unsigned nsops);

      virtual bool isStale();
      virtual void resetOnFork();
      virtual void leaderElection();
      virtual void preCkptDrain();
      virtual void preCheckpoint();
      virtual void postRestart();
      virtual void refill(bool isRestart);
      virtual void preResume() {}

    private:
      int     _nsems;
      unsigned short *_semval;
      int *_semadj;
  };

  class MsgQueue : public SysVObj
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      MsgQueue(int msqid, int realMsqid, key_t key, int msgflg);

      virtual bool isStale();
      virtual void resetOnFork() {}
      virtual void leaderElection();
      virtual void preCkptDrain();
      virtual void preCheckpoint();
      virtual void postRestart();
      virtual void refill(bool isRestart);
      virtual void preResume() {}

    private:
      dmtcp::vector<jalib::JBuffer> _msgInQueue;
      msgqnum_t _qnum;
  };
}
#endif
