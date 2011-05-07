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

#include  "../jalib/jsocket.h"
#include "../jalib/jalloc.h"
#include "dmtcpmessagetypes.h"
#include "constants.h"

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

      DmtcpCoordinatorAPI ();
      // Use default destructor

      void connectAndSendUserCommand(char c, int* result = NULL);

      void useAlternateCoordinatorFd();

      bool connectToCoordinator(bool dieOnError=true);
      bool tryConnectToCoordinator();
      void sendUserCommand(char c, int* result = NULL);

      jalib::JSocket _coordinatorSocket;

    protected:
      jalib::JSocket _restoreSocket;
  };

}

#endif
