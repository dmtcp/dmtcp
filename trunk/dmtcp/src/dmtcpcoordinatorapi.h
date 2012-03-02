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

#ifndef DMTCPDMTCPCOMMANDAPI_H
#define DMTCPDMTCPCOMMANDAPI_H

#include "constants.h"
#include "protectedfds.h"
#include "dmtcpmessagetypes.h"
#include "connectionstate.h"
#include "../jalib/jsocket.h"
#include "../jalib/jalloc.h"

namespace dmtcp
{

  class ConnectionState;

  class DmtcpCoordinatorAPI
  {
    public:
      enum  ErrorCodes {
        NOERROR                 =  0,
        ERROR_INVALID_COMMAND   = -1,
        ERROR_NOT_RUNNING_STATE = -2,
        ERROR_COORDINATOR_NOT_FOUND = -3
      };

      enum CoordinatorModes {
        COORD_JOIN      = 0x0001,
        COORD_NEW       = 0x0002,
        COORD_FORCE_NEW = 0x0004,
        COORD_BATCH     = 0x0008,
        COORD_ANY       = COORD_JOIN | COORD_NEW
      };

      DmtcpCoordinatorAPI (int sockfd = PROTECTED_COORD_FD);
      // Use default destructor

      void closeConnection() { _coordinatorSocket.close(); }

      jalib::JSocket& coordinatorSocket() { return _coordinatorSocket; }

      void connectAndSendUserCommand(char c, int* result = NULL);

      void useAlternateCoordinatorFd();

      bool connectToCoordinator(bool dieOnError=true);
      bool tryConnectToCoordinator();
      void connectToCoordinatorWithHandshake();
      void connectToCoordinatorWithoutHandshake();
      void sendUserCommand(char c, int* result = NULL);

      pid_t virtualPid() const { return _virtualPid; }
      pid_t getVirtualPidFromCoordinator();
      jalib::JSocket createNewConnectionToCoordinator(bool dieOnError = true);
      void createNewConnectionBeforeFork(dmtcp::string& progName);
      void informCoordinatorOfNewProcessOnFork(jalib::JSocket& coordSock);

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
                                      DMT_HELLO_COORDINATOR);
      void recvCoordinatorHandshake(int *param1 = NULL);

      jalib::JSocket& openRestoreSocket();

      static void startCoordinatorIfNeeded(int modes, int isRestart = 0);
      static void startNewCoordinator(int modes, int isRestart = 0);

    protected:
      UniquePid      _coordinatorId;
      jalib::JSocket _coordinatorSocket;
      jalib::JSocket _restoreSocket;
      pid_t          _virtualPid;
    private:
  };

}

#endif
