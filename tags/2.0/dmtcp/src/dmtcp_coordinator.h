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

#include "dmtcpalloc.h"
#include  "../jalib/jsocket.h"
#include "dmtcpmessagetypes.h"

namespace dmtcp
{
  class DmtcpCoordinator : public jalib::JMultiSocketProgram
  {
    public:
      typedef struct {
        dmtcp::WorkerState minimumState;
        dmtcp::WorkerState maximumState;
        bool minimumStateUnanimous;
        int numPeers;
      } CoordinatorStatus;

      virtual void onData(jalib::JReaderInterface* sock);
      virtual void onConnect(const jalib::JSocket& sock,
                             const struct sockaddr* remoteAddr,
                             socklen_t remoteLen);
      virtual void onDisconnect(jalib::JReaderInterface* sock);
      virtual void onTimeoutInterval();

      void updateMinimumState(dmtcp::WorkerState oldState);
      void initializeComputation();
      void broadcastMessage(DmtcpMessageType type, dmtcp::UniquePid, int);
      void broadcastMessage(const DmtcpMessage& msg);
      bool startCheckpoint();

      void handleUserCommand(char cmd, DmtcpMessage* reply = NULL);

      void processDmtUserCmd(DmtcpMessage& hello_remote,
                             jalib::JSocket& remote);
      bool validateNewWorkerProcess(DmtcpMessage& hello_remote,
                                    jalib::JSocket& remote,
                                    jalib::JChunkReader *jcr,
                                    const struct sockaddr* remoteAddr,
                                    socklen_t remoteLen);
      bool validateRestartingWorkerProcess(DmtcpMessage& hello_remote,
                                           jalib::JSocket& remote,
                                           const struct sockaddr* remoteAddr,
                                           socklen_t remoteLen);

      CoordinatorStatus getStatus() const;
      dmtcp::WorkerState minimumState() const {
        return getStatus().minimumState;
      }

      pid_t getNewVirtualPid();

    protected:
      void writeRestartScript();
    private:
      typedef dmtcp::vector<jalib::JReaderInterface*>::iterator iterator;
      typedef
        dmtcp::vector<jalib::JReaderInterface*>::const_iterator const_iterator;

      //map from hostname to checkpoint files
      map< dmtcp::string, dmtcp::vector<dmtcp::string> > _restartFilenames;
      dmtcp::map< pid_t, jalib::JChunkReader* > _virtualPidToChunkReaderMap;
  };

}

#endif
