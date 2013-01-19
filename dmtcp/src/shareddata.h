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

#include <pthread.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/un.h>

#include "constants.h"
#include "uniquepid.h"
#include "protectedfds.h"
#include "dmtcpalloc.h"
#include "dmtcpplugin.h"

#define MAX_IPC_ID_MAPS 256
#define MAX_PTY_NAME_MAPS 256
#define MAX_PTRACE_ID_MAPS 256
#define MAX_PROCESS_TREE_ROOTS 256
#define MAX_MISSING_CONNECTIONS 10240
#define CON_ID_LEN \
  (sizeof(DmtcpUniqueProcessId) + sizeof(long))

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
      dmtcp::UniquePid     processTreeRoots[MAX_PROCESS_TREE_ROOTS];
      size_t               numProcessTreeRoots;

      struct PtyNameMap    ptyNameMap[MAX_PTY_NAME_MAPS];
      size_t               numPtyNameMaps;
      size_t               nextPtyName;

      struct MissingConMap missingConMap[MAX_MISSING_CONNECTIONS];
      size_t               numMissingConMaps;
    };

    void initialize();
    void initializeHeader();
    void preCkpt();

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

    void setProcessTreeRoot();
    void getProcessTreeRoots(UniquePid **roots, size_t *numRoots);

    void getRealPtyName(const char* virt, char* out, size_t len);
    void getVirtPtyName(const char* real, char *out, size_t len);
    void createVirtualPtyName(const char* real, char *out, size_t len);
    void insertPtyNameMap(const char* virt, const char* real);
    unsigned getNextVirtualPtyId();
    void restoreNextVirtualPtyId(unsigned n);

    void registerMissingCons(vector<const char*>& ids,
                             struct sockaddr_un receiverAddr,
                             socklen_t len);
    void getMissingConMaps(struct MissingConMap **map, size_t *nmaps);
  }
}
#endif
