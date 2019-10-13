/****************************************************************************
 *   Copyright (C) 2012-2014 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <arpa/inet.h>
#include <linux/limits.h>
#include <netdb.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/un.h>

#include "dmtcp.h"
#include "dmtcpalloc.h"

#define PTS_PATH_MAX             32
#define MAX_PID_MAPS             32768
#define MAX_IPC_ID_MAPS          256
#define MAX_PTY_NAME_MAPS        256
#define MAX_PTRACE_ID_MAPS       256
#define MAX_INCOMING_CONNECTIONS 10240
#define MAX_INODE_PID_MAPS       10240
#define CON_ID_LEN \
  (sizeof(DmtcpUniqueProcessId) + sizeof(int64_t))

#define SHM_VERSION_STR          "DMTCP_GLOBAL_AREA_V0.99"
#define VIRT_PTS_PREFIX_STR      "/dev/pts/v"

#define SYSV_SHM_ID              1
#define SYSV_SEM_ID              2
#define SYSV_MSQ_ID              3
#define SYSV_SHM_KEY             4

namespace dmtcp
{
typedef struct CoordinatorInfo {
  DmtcpUniqueProcessId id;
  uint64_t timeStamp;
  uint32_t interval;
  uint32_t addrLen;
  struct sockaddr_storage addr;
} CoordinatorInfo;

namespace SharedData
{
// All structs should be 64-bit aligned.
struct PidMap {
  pid_t virt;
  pid_t real;
};

struct IPCIdMap {
  int32_t virt;
  int32_t real;
};

struct PtyNameMap {
  char virt[PTS_PATH_MAX];
  char real[PTS_PATH_MAX];
};

struct IncomingConMap {
  char id[CON_ID_LEN];
  struct sockaddr_un addr;
  union {
    socklen_t len;
    uint64_t _pad;
  };
};

struct PtraceIdMaps {
  pid_t tracerId;
  pid_t childId;
};

typedef struct InodeConnIdMap {
  uint64_t devnum;
  uint64_t inode;
  char id[CON_ID_LEN];
} InodeConnIdMap;

struct BarrierInfo {
  uint64_t numCkptPeers;

  // Futex
  uint32_t numIn;
  uint32_t curRound;

  // Posix Barrier
  pthread_barrier_t barrier;
};

typedef enum {
  DMTCP_ARCH_32,
  DMTCP_ARCH_64,
  DMTCP_ARCH_MIXED
} DMTCP_ARCH_MODE;

struct Header {
  uint64_t initialized;

  char tmpDir[PATH_MAX];
  char installDir[PATH_MAX];

  struct sockaddr_storage localIPAddr;

  int64_t dlsymOffset;
  int64_t dlsymOffset_m32;

  uint64_t numPidMaps;
  uint64_t numPtraceIdMaps;

  uint64_t numSysVShmIdMaps;
  uint64_t numSysVSemIdMaps;
  uint64_t numSysVMsqIdMaps;
  uint64_t numSysVShmKeyMaps;

  uint64_t numPtyNameMaps;
  uint64_t nextPtyName;
  uint64_t nextVirtualPtyId;

  uint64_t numIncomingConMaps;
  uint64_t numInodeConnIdMaps;

  union {
    struct BarrierInfo barrierInfo;
    char pad[128];
  };

  struct PidMap pidMap[MAX_PID_MAPS];
  struct IPCIdMap sysvShmIdMap[MAX_IPC_ID_MAPS];
  struct IPCIdMap sysvSemIdMap[MAX_IPC_ID_MAPS];
  struct IPCIdMap sysvMsqIdMap[MAX_IPC_ID_MAPS];
  struct IPCIdMap sysvShmKeyMap[MAX_IPC_ID_MAPS];
  struct PtraceIdMaps ptraceIdMap[MAX_PTRACE_ID_MAPS];
  struct PtyNameMap ptyNameMap[MAX_PTY_NAME_MAPS];
  struct IncomingConMap incomingConMap[MAX_INCOMING_CONNECTIONS];
  InodeConnIdMap inodeConnIdMap[MAX_INODE_PID_MAPS];

  char versionStr[32];
  DmtcpUniqueProcessId compId;
  CoordinatorInfo coordInfo;

  union {
    DMTCP_ARCH_MODE archMode;
    uint64_t _pad;
  };
  // char                 coordHost[NI_MAXHOST];
};

bool initialized();

void initialize(const char *tmpDir,
                const char *installDir,
                DmtcpUniqueProcessId *compId,
                CoordinatorInfo *coordInfo,
                struct in_addr *localIP);
void initializeHeader(const char *tmpDir,
                      const char *installDir,
                      DmtcpUniqueProcessId *compId,
                      CoordinatorInfo *coordInfo,
                      struct in_addr *localIP);

bool isSharedDataRegion(void *addr);
void initializeBarrier();
void resetBarrierInfo();
void prepareForCkpt();
void postRestart();
void waitForBarrier(const string &barrierId);

string coordHost();
uint32_t coordPort();
void getCoordAddr(struct sockaddr *addr, uint32_t *len);
void setCoordHost(struct in_addr *in);
uint64_t getCoordTimeStamp();

string getTmpDir();
char *getTmpDir(char *buf, uint32_t len);
string getInstallDir();
uint32_t getCkptInterval();
void updateGeneration(uint32_t generation);
DmtcpUniqueProcessId getCompId();
DmtcpUniqueProcessId getCoordId();

void getLocalIPAddr(struct in_addr *in);

void updateDlsymOffset(int32_t dlsymOffset, int32_t dlsymOffset_m32 = 0);
int32_t getDlsymOffset(void);
int32_t getDlsymOffset_m32(void);

int32_t getRealIPCId(int type, int32_t virt);
void setIPCIdMap(int type, int32_t virt, int32_t real);

pid_t getRealPid(pid_t virt);
void setPidMap(pid_t virt, pid_t real);

pid_t getPtraceVirtualId(pid_t tracerId);
void setPtraceVirtualId(pid_t tracerId, pid_t childId);

void getRealPtyName(const char *virt, char *out, uint32_t len);
void getVirtPtyName(const char *real, char *out, uint32_t len);
void createVirtualPtyName(const char *real, char *out, uint32_t len);
void setVirtualPtyId(uint32_t id);
uint32_t getVirtualPtyId();
void insertPtyNameMap(const char *virt, const char *real);

void registerIncomingCons(vector<const char *> &ids,
                          struct sockaddr_un receiverAddr,
                          socklen_t len);
void getMissingConMaps(struct IncomingConMap **map, uint32_t *nmaps);

void insertInodeConnIdMaps(vector<InodeConnIdMap> &maps);
bool getCkptLeaderForFile(dev_t devnum, ino_t inode, void *id);
}
}
#endif // ifndef SHARED_DATA_H
