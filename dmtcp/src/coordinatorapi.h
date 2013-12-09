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

#ifndef COORDINATORAPI_H
#define COORDINATORAPI_H

#include "constants.h"
#include "protectedfds.h"
#include "dmtcpmessagetypes.h"
#include "../jalib/jsocket.h"
#include "../jalib/jalloc.h"

namespace dmtcp
{
  class CoordinatorAPI
  {
    public:
      enum CoordinatorMode {
        COORD_INVALID   = 0x0000,
        COORD_JOIN      = 0x0001,
        COORD_NEW       = 0x0002,
        COORD_NONE      = 0x0004,
        COORD_ANY       = 0x0010
      };

#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      CoordinatorAPI (int sockfd = PROTECTED_COORD_FD);
      // Use default destructor

      static CoordinatorAPI& instance();
      static void resetOnFork(CoordinatorAPI& coordAPI);

      void closeConnection() { _coordinatorSocket.close(); }

      void sendMsgToCoordinator(const DmtcpMessage &msg,
                                const void *ch = NULL, size_t len = 0);
      void recvMsgFromCoordinator(DmtcpMessage *msg, void **str = NULL);

      jalib::JSocket& coordinatorSocket() { return _coordinatorSocket; }
      const DmtcpUniqueProcessId& coordinatorId() const { return _coordinatorId; }
      void setCoordinatorId(DmtcpUniqueProcessId id) { _coordinatorId = id; }
      uint64_t coordTimeStamp() const { return _coordTimeStamp; }

      bool isValid() { return _coordinatorSocket.isValid(); }

      void connectAndSendUserCommand(char c,
                                     int *coordCmdStatus = NULL,
                                     int *numPeers = NULL,
                                     int *isRunning = NULL);

      void useAlternateCoordinatorFd();

      bool connectToCoordinator(bool dieOnError=true);
      bool tryConnectToCoordinator();
      void connectToCoordinatorWithHandshake();
      void connectToCoordinatorWithoutHandshake();
      void sendUserCommand(char c,
                           int *coordCmdStatus = NULL,
                           int *numPeers = NULL,
                           int *isRunning = NULL);

      pid_t virtualPid() const { return _virtualPid; }
      pid_t getVirtualPidFromCoordinator();
      void updateCoordTimeStamp();
      void updateCoordCkptDir(const char *dir);
      void createNewConnectionBeforeFork(dmtcp::string& progName);

      // np > -1  means it is restarting a process that have np processes in its
      //           computation group
      // np == -1 means it is a new pure process, so coordinator needs to
      //           generate compGroup ID for it
      // np == -2 means it is a service connection from dmtcp_restart
      //           - ignore it
      void sendCoordinatorHandshake(const dmtcp::string& procName,
                                    UniquePid compGroup = UniquePid(),
                                    int np = -1,
                                    DmtcpMessageType msgType =
                                      DMT_HELLO_COORDINATOR,
                                    bool preForkHandshake = false);
      void recvCoordinatorHandshake();
      void sendCkptFilename();
      void updateHostAndPortEnv();
      void getLocalIPAddr(struct in_addr *in);

      static void setupVirtualCoordinator();
      static void waitForCheckpointCommand();
      static bool noCoordinator();
      static void startCoordinatorIfNeeded(CoordinatorMode mode, int isRestart = 0);
      static void startNewCoordinator(CoordinatorMode mode);

      int sendKeyValPairToCoordinator(const char *id,
                                      const void *key, uint32_t key_len,
                                      const void *val, uint32_t val_len);
      int sendQueryToCoordinator(const char *id,
                                 const void *key, uint32_t key_len,
                                 void *val, uint32_t *val_len);

    private:
      jalib::JSocket createNewConnectionToCoordinator(bool dieOnError = true);

    protected:
      DmtcpUniqueProcessId    _coordinatorId;
      jalib::JSocket          _coordinatorSocket;
      struct sockaddr_storage _coordAddr;
      socklen_t               _coordAddrLen;
      uint64_t                _coordTimeStamp;
      struct in_addr          _localIPAddr;
      pid_t                   _virtualPid;
  };
}

#endif
