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

#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/types.h>
#include <unistd.h>
#include <map>
#include <vector>

#include "jalloc.h"
#include "jassert.h"
#include "jbuffer.h"
#include "jconvert.h"
#include "jserialize.h"
#include "dmtcpalloc.h"
#include "shareddata.h"
#include "virtualidtable.h"

#include "sysvipcwrappers.h"

#define REAL_TO_VIRTUAL_SHM_ID(id) SysVShm::instance().realToVirtualId(id)
#define VIRTUAL_TO_REAL_SHM_ID(id) SysVShm::instance().virtualToRealId(id)

#define REAL_TO_VIRTUAL_SHM_KEY(key) SysVShm::instance().realToVirtualKey(key)
#define VIRTUAL_TO_REAL_SHM_KEY(key) SysVShm::instance().virtualToRealKey(key)

#define REAL_TO_VIRTUAL_SEM_ID(id) SysVSem::instance().realToVirtualId(id)
#define VIRTUAL_TO_REAL_SEM_ID(id) SysVSem::instance().virtualToRealId(id)

#define REAL_TO_VIRTUAL_MSQ_ID(id) SysVMsq::instance().realToVirtualId(id)
#define VIRTUAL_TO_REAL_MSQ_ID(id) SysVMsq::instance().virtualToRealId(id)

union semun {
  int val;                 /* Value for SETVAL */
  struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
  unsigned short *array;   /* Array for GETALL, SETALL */
  struct seminfo *__buf;   /* Buffer for IPC_INFO (Linux-specific) */
};

namespace dmtcp
{
class SysVObj;
class ShmSegment;
class Semaphore;
class MsgQueue;

class SysVIPC
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR

    SysVIPC(const char *str, int32_t id, int type);
    void removeStaleObjects();
    void resetOnFork();
    void leaderElection();
    void preCkptDrain();
    void preCheckpoint();
    void preResume();
    void refill();
    void postRestart();
    int virtualToRealId(int virtId);
    int realToVirtualId(int realId);
    void updateMapping(int virtId, int realId);
    int getNewVirtualId();
    void serialize(jalib::JBinarySerializer &o);

    virtual void on_shmget(int shmid, key_t key, size_t size, int shmflg) {}

    virtual void on_shmat(int shmid,
                          const void *shmaddr,
                          int shmflg,
                          void *newaddr) {}

    virtual void on_shmdt(const void *shmaddr) {}

    virtual void on_semget(int semid, key_t key, int nsems, int semflg) {}

    virtual void on_semctl(int semid, int semnum, int cmd, union semun arg) {}

    virtual void on_semop(int semid, struct sembuf *sops, unsigned nsops) {}

    virtual void on_msgget(int msqid, key_t key, int msgflg) {}

    virtual void on_msgctl(int msqid, int cmd, struct msqid_ds *buf) {}

    virtual void on_msgsnd(int msqid, const void *msgp, size_t msgsz,
                           int msgflg) {}

    virtual void on_msgrcv(int msqid,
                           const void *msgp,
                           size_t msgsz,
                           int msgtyp,
                           int msgflg) {}

  protected:
    map<int, SysVObj *>_map;
    typedef map<int, SysVObj *>::iterator Iterator;
    VirtualIdTable<int32_t>_virtIdTable;
    int _type;
};

class SysVShm : public SysVIPC
{
  public:
    SysVShm()
      : SysVIPC("SysVShm", getpid(), SYSV_SHM_ID) {}

    static SysVShm &instance();

    int shmaddrToShmid(const void *shmaddr);
    virtual void on_shmget(int shmid, key_t realKey, key_t key,
                           size_t size, int shmflg);
    virtual void on_shmat(int shmid,
                          const void *shmaddr,
                          int shmflg,
                          void *newaddr);
    virtual void on_shmdt(const void *shmaddr);
    key_t virtualToRealKey(key_t key);
    key_t realToVirtualKey(key_t key);
    void updateKeyMapping(key_t v, key_t r);
  protected:
    map<key_t, key_t>_keyMap;
    typedef map<key_t, key_t>::iterator KIterator;
};

class SysVSem : public SysVIPC
{
  public:
    SysVSem()
      : SysVIPC("SysVSem", getpid(), SYSV_SEM_ID) {}

    static SysVSem &instance();
    virtual void on_semget(int realSemId, key_t key, int nsems, int semflg);
    virtual void on_semctl(int semid, int semnum, int cmd, union semun arg);
    virtual void on_semop(int semid, struct sembuf *sops, unsigned nsops);
};

class SysVMsq : public SysVIPC
{
  public:
    SysVMsq()
      : SysVIPC("SysVMsq", getpid(), SYSV_MSQ_ID) {}

    static SysVMsq &instance();
    virtual void on_msgget(int msqid, key_t key, int msgflg);
    virtual void on_msgctl(int msqid, int cmd, struct msqid_ds *buf);
    virtual void on_msgsnd(int msqid, const void *msgp, size_t msgsz,
                           int msgflg);
    virtual void on_msgrcv(int msqid,
                           const void *msgp,
                           size_t msgsz,
                           int msgtyp,
                           int msgflg);
};

class SysVObj
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR

    SysVObj(int id, int realId, int key, int flags)
    {
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
    virtual void refill() = 0;
    virtual void preResume() = 0;

  protected:
    int _id;
    int _realId;
    key_t _key;
    int _flags;
    bool _isCkptLeader;
};

class ShmSegment : public SysVObj
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR

    ShmSegment(int shmid, int realShmid, key_t key, size_t size, int shmflg);

    virtual bool isStale();
    virtual void resetOnFork() {}

    virtual void leaderElection();
    virtual void preCkptDrain();
    virtual void preCheckpoint();
    virtual void postRestart();
    virtual void refill();
    virtual void preResume();

    bool isValidShmaddr(const void *shmaddr);
    void remapAll();
    void remapFirstAddrForOwnerOnRestart();

    void on_shmat(const void *shmaddr, int shmflg);
    void on_shmdt(const void *shmaddr);

  private:
    size_t _size;
    int _dmtcpMappedAddr;
    shmatt_t _nattch;
    unsigned short _mode;
    struct shmid_ds _shminfo;
    typedef map<const void *, int>ShmaddrToFlag;
    typedef map<const void *, int>::iterator ShmaddrToFlagIter;
    ShmaddrToFlag _shmaddrToFlag;
};

class Semaphore : public SysVObj
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR
    Semaphore(int semid, int realSemid, key_t key, int nsems, int semflg);
    ~Semaphore() { delete _semval; delete _semadj; }

    void on_semop(struct sembuf *sops, unsigned nsops);

    virtual bool isStale();
    virtual void resetOnFork();
    virtual void leaderElection();
    virtual void preCkptDrain();
    virtual void preCheckpoint();
    virtual void postRestart();
    virtual void refill();
    virtual void preResume() {}

  private:
    int _nsems;
    unsigned short *_semval;
    int *_semadj;
};

class MsgQueue : public SysVObj
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR
    MsgQueue(int msqid, int realMsqid, key_t key, int msgflg);

    virtual bool isStale();
    virtual void resetOnFork() {}

    virtual void leaderElection();
    virtual void preCkptDrain();
    virtual void preCheckpoint();
    virtual void postRestart();
    virtual void refill();
    virtual void preResume() {}

  private:
    vector<jalib::JBuffer>_msgInQueue;
    msgqnum_t _qnum;
};
}
#endif // ifndef SYSVIPC_H
