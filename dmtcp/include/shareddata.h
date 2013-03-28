/****************************************************************************
 *   Copyright (C) 2012 by Kapil Arya <kapil@ccs.neu.edu>                   *
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

#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <sys/types.h>
#include <sys/un.h>
#include <netdb.h>
#include "dmtcpplugin.h"
#include "dmtcpalloc.h"

#define PTS_PATH_MAX 32
#define MAX_IPC_ID_MAPS 256
#define MAX_PTY_NAME_MAPS 256
#define MAX_PTRACE_ID_MAPS 256
#define MAX_MISSING_CONNECTIONS 10240
#define MAX_INODE_PID_MAPS 10240
#define CON_ID_LEN \
  (sizeof(DmtcpUniqueProcessId) + sizeof(long))

#define SHM_VERSION_STR "DMTCP_GLOBAL_AREA_V0.99"
#define VIRT_PTS_PREFIX_STR "/dev/pts/v"

namespace dmtcp {
  namespace SharedData {
    struct IPCIdMap {
      pid_t virt;
      pid_t real;
    };

    struct PtyNameMap {
      char virt[PTS_PATH_MAX];
      char real[PTS_PATH_MAX];
    };

    struct MissingConMap {
      char                 id[CON_ID_LEN];
      struct sockaddr_un   addr;
      socklen_t            len;
    };

    struct PtraceIdMaps {
      pid_t tracerId;
      pid_t childId;
    };

    typedef struct InodeConnIdMap {
      dev_t devnum;
      ino_t inode;
      char  id[CON_ID_LEN];
    } InodeConnIdMap;

    struct Header {
      bool                 initialized;
      char                 versionStr[32];
      char                 coordHost[NI_MAXHOST];
      int                  coordPort;
      int                  ckptInterval;
      struct IPCIdMap      ipcIdMap[MAX_IPC_ID_MAPS];
      size_t               numIPCIdMaps;
      struct PtraceIdMaps  ptraceIdMap[MAX_PTRACE_ID_MAPS];
      size_t               numPtraceIdMaps;

      struct PtyNameMap    ptyNameMap[MAX_PTY_NAME_MAPS];
      size_t               numPtyNameMaps;
      size_t               nextPtyName;
      size_t               nextVirtualPtyId;

      struct MissingConMap missingConMap[MAX_MISSING_CONNECTIONS];
      size_t               numMissingConMaps;

      InodeConnIdMap       inodeConnIdMap[MAX_INODE_PID_MAPS];
      size_t               numInodeConnIdMaps;
    };

    void initialize();
    void initializeHeader();
    void suspended();
    void preCkpt();
    void refill();

    string getCoordHost();
    void setCoordHost(const char *host);

    int  getCoordPort();
    void setCoordPort(int port);

    int  getCkptInterval();
    void setCkptInterval(int interval);

    int  getRealIPCId(int virt);
    void setIPCIdMap(int virt, int real);

    pid_t getPtraceVirtualId(pid_t tracerId);
    void setPtraceVirtualId(pid_t tracerId, pid_t childId);

    void getRealPtyName(const char* virt, char* out, size_t len);
    void getVirtPtyName(const char* real, char *out, size_t len);
    void createVirtualPtyName(const char* real, char *out, size_t len);
    void insertPtyNameMap(const char* virt, const char* real);

    void registerMissingCons(vector<const char*>& ids,
                             struct sockaddr_un receiverAddr,
                             socklen_t len);
    void getMissingConMaps(struct MissingConMap **map, size_t *nmaps);

    void insertInodeConnIdMaps(vector<SharedData::InodeConnIdMap>& maps);
    bool getCkptLeaderForFile(dev_t devnum, ino_t inode, void *id);
  }
}
#endif
