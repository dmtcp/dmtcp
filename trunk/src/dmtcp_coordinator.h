/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef DMTCPDMTCPCOORDINATOR_H
#define DMTCPDMTCPCOORDINATOR_H

#include "dmtcpalloc.h"
#include  "../jalib/jsocket.h"
#include "dmtcpmessagetypes.h"

namespace dmtcp
{
  class CoordClient
  {
    public:
      CoordClient(const jalib::JSocket& sock,
                  const struct sockaddr_storage *addr,
                  socklen_t len,
                  dmtcp::DmtcpMessage &hello_remote,
		  int isNSWorker = 0);

      jalib::JSocket &sock() { return _sock; }
      const dmtcp::UniquePid& identity() const { return _identity;}
      void identity(dmtcp::UniquePid upid) { _identity = upid;}
      int clientNumber() const { return _clientNumber; }
      dmtcp::string ip() const { return _ip; }
      dmtcp::WorkerState state() const { return _state; }
      void setState ( dmtcp::WorkerState value ) { _state = value; }
      void progname(dmtcp::string pname){ _progname = pname; }
      dmtcp::string progname(void) const { return _progname; }
      void hostname(dmtcp::string hname){ _hostname = hname; }
      dmtcp::string hostname(void) const { return _hostname; }
      dmtcp::string prefixDir(void) const { return _prefixDir; }
      pid_t realPid(void) const { return _realPid; }
      void realPid(pid_t pid) { _realPid = pid; }
      pid_t virtualPid(void) const { return _virtualPid; }
      void virtualPid(pid_t pid) { _virtualPid = pid; }
      int isNSWorker() {return _isNSWorker;}

      void readProcessInfo(dmtcp::DmtcpMessage& msg);

    private:
      dmtcp::UniquePid _identity;
      int _clientNumber;
      jalib::JSocket _sock;
      dmtcp::WorkerState _state;
      dmtcp::string _hostname;
      dmtcp::string _progname;
      dmtcp::string _prefixDir;
      dmtcp::string _ip;
      pid_t         _realPid;
      pid_t         _virtualPid;
      int           _isNSWorker;
  };

  class DmtcpCoordinator
  {
    public:
      typedef struct {
        dmtcp::WorkerState minimumState;
        dmtcp::WorkerState maximumState;
        bool minimumStateUnanimous;
        int numPeers;
      } ComputationStatus;

      void onTimeoutInterval();
      void onData(CoordClient *client);
      void onConnect();
      void onDisconnect(CoordClient *client);
      void eventLoop(bool daemon);

      void addDataSocket(CoordClient *client);
      void updateCheckpointInterval(uint32_t timeout);
      int  getRemainingTimeoutMS();
      void updateMinimumState(dmtcp::WorkerState oldState);
      void initializeComputation();
      void broadcastMessage(DmtcpMessageType type, int numPeers = -1);
      bool startCheckpoint();

      void handleUserCommand(char cmd, DmtcpMessage* reply = NULL);

      void processDmtUserCmd(DmtcpMessage& hello_remote,
                             jalib::JSocket& remote);
      bool validateNewWorkerProcess(DmtcpMessage& hello_remote,
                                    jalib::JSocket& remote,
                                    CoordClient *client,
                                    const struct sockaddr_storage* addr,
                                    socklen_t len);
      bool validateRestartingWorkerProcess(DmtcpMessage& hello_remote,
                                           jalib::JSocket& remote,
                                           const struct sockaddr_storage* addr,
                                           socklen_t len);

      ComputationStatus getStatus() const;
      dmtcp::WorkerState minimumState() const {
        return getStatus().minimumState;
      }

      pid_t getNewVirtualPid();

    protected:
      void writeRestartScript();
    private:
      //map from hostname to checkpoint files
      map< dmtcp::string, dmtcp::vector<dmtcp::string> > _restartFilenames;
      dmtcp::map< pid_t, CoordClient* > _virtualPidToClientMap;
  };

}

#endif
