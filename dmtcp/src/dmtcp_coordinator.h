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

#ifndef DMTCPDMTCPCOORDINATOR_H
#define DMTCPDMTCPCOORDINATOR_H

#include  "../jalib/jsocket.h"
#include "nodetable.h"
#include "dmtcpmessagetypes.h"

namespace dmtcp
{



  class DmtcpCoordinator : public jalib::JMultiSocketProgram
  {
    public:
      enum  ErrorCodes {
        NOERROR                 =  0,
        ERROR_INVALID_COMMAND   = -1,
        ERROR_NOT_RUNNING_STATE = -2
      };

      typedef struct { dmtcp::WorkerState minimumState; bool minimumStateUnanimous; int numPeers; } CoordinatorStatus;

      virtual void onData ( jalib::JReaderInterface* sock );
      virtual void onConnect ( const jalib::JSocket& sock, const struct sockaddr* remoteAddr,socklen_t remoteLen );
      virtual void onDisconnect ( jalib::JReaderInterface* sock );
      virtual void onTimeoutInterval();
      void broadcastMessage ( DmtcpMessageType type );
      void broadcastMessage ( const DmtcpMessage& msg );
      bool startCheckpoint();

      void handleUserCommand(char cmd, DmtcpMessage* reply = NULL);

      CoordinatorStatus getStatus() const;
      dmtcp::WorkerState minimumState() const { return getStatus().minimumState; }

    protected:
      void writeRestartScript();
    private:
      typedef std::vector<jalib::JReaderInterface*>::iterator iterator;
      typedef std::vector<jalib::JReaderInterface*>::const_iterator const_iterator;
//     NodeTable _table;
      std::vector< DmtcpMessage > _restoreWaitingMessages;

      //map from hostname to checkpoint files
      std::map< std::string, std::vector<std::string> > _restartFilenames;
  };

}

#endif
