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

#include "../jalib/jsocket.h"
#include "dmtcpalloc.h"
#include "dmtcpmessagetypes.h"

namespace dmtcp
{
class CoordClient
{
  public:
    CoordClient(const jalib::JSocket &sock,
                const struct sockaddr_storage *addr,
                socklen_t len,
                DmtcpMessage &hello_remote,
                int isNSWorker = 0);

    jalib::JSocket &sock() { return _sock; }

    const UniquePid &identity() const { return _identity; }

    void identity(UniquePid upid) { _identity = upid; }

    int clientNumber() const { return _clientNumber; }

    string ip() const { return _ip; }

    WorkerState::eWorkerState state() const { return _state; }

    void setState(WorkerState::eWorkerState value) { _state = value; }

    string barrier() const { return _barrier; }

    void setBarrier(const string &value) { _barrier = value; }

    void progname(string pname) { _progname = pname; }

    string progname(void) const { return _progname; }

    void hostname(string hname) { _hostname = hname; }

    string hostname(void) const { return _hostname; }

    pid_t realPid(void) const { return _realPid; }

    void realPid(pid_t pid) { _realPid = pid; }

    pid_t virtualPid(void) const { return _virtualPid; }

    void virtualPid(pid_t pid) { _virtualPid = pid; }

    int isNSWorker() { return _isNSWorker; }

    void readProcessInfo(DmtcpMessage &msg);

  private:
    UniquePid _identity;
    int _clientNumber;
    jalib::JSocket _sock;
    WorkerState::eWorkerState _state;
    string _hostname;
    string _progname;
    string _ip;
    string _barrier;
    pid_t _realPid;
    pid_t _virtualPid;
    int _isNSWorker;
};

class DmtcpCoordinator
{
  public:
    typedef struct {
      WorkerState::eWorkerState minimumState;
      WorkerState::eWorkerState maximumState;
      bool minimumStateUnanimous;
      int numPeers;
    } ComputationStatus;

    void onData(CoordClient *client);
    void onConnect();
    void onDisconnect(CoordClient *client);
    void eventLoop(bool daemon);

    void addDataSocket(CoordClient *client);
    void updateCheckpointInterval(uint32_t timeout);
    void updateMinimumState();
    void initializeComputation();
    void broadcastMessage(DmtcpMessageType type,
                          size_t extraBytes = 0,
                          const void *extraData = NULL);

    void processBarrier(const string &barrier);
    void releaseBarrier(const string &barrier);

    bool startCheckpoint();
    void recordCkptFilename(CoordClient *client, const char *barrierList);

    void handleUserCommand(char cmd, DmtcpMessage *reply = NULL);
    void printStatus(size_t numPeers, bool isRunning);
    string printList();

    void processDmtUserCmd(DmtcpMessage &hello_remote, jalib::JSocket &remote);
    bool validateNewWorkerProcess(DmtcpMessage &hello_remote,
                                  jalib::JSocket &remote,
                                  CoordClient *client,
                                  const struct sockaddr_storage *addr,
                                  socklen_t len);
    bool validateRestartingWorkerProcess(DmtcpMessage &hello_remote,
                                         jalib::JSocket &remote,
                                         const struct sockaddr_storage *addr,
                                         socklen_t len);

    ComputationStatus getStatus() const;
    WorkerState::eWorkerState minimumState() const
    {
      return getStatus().minimumState;
    }

    pid_t getNewVirtualPid();

    void writeRestartScript();

  private:
    size_t _numCkptWorkers;
    size_t _numRestartFilenames;

    // Store whether rsh/ssh was used
    map< string, vector<string> > _rshCmdFileNames;
    map< string, vector<string> > _sshCmdFileNames;

    // map from hostname to checkpoint files
    map<string, vector<string> >_restartFilenames;
    map<pid_t, CoordClient *>_virtualPidToClientMap;
};
}
#endif // ifndef DMTCPDMTCPCOORDINATOR_H
