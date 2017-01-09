/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef COORDINATORAPI_H
#define COORDINATORAPI_H

#include "../jalib/jalloc.h"
#include "constants.h"
#include "dmtcpmessagetypes.h"
#include "protectedfds.h"
#include "shareddata.h"
#include "syscallwrappers.h"

namespace dmtcp
{
enum CoordinatorMode {
  COORD_INVALID   = 0x0000,
  COORD_JOIN      = 0x0001,
  COORD_NEW       = 0x0002,
  COORD_NONE      = 0x0004,
  COORD_ANY       = 0x0010
};

class CoordinatorAPI
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR
    CoordinatorAPI(void) : _coordinatorSocket(-1), _nsSock(-1) {}

    // Use default destructor

    static CoordinatorAPI &instance();
    static void init();
    static void restart();
    static void resetOnFork(CoordinatorAPI &coordAPI);

    static void getCoordHostAndPort(CoordinatorMode mode,
                                    string &host,
                                    int *port);
    static void setCoordPort(int port);

    void setupVirtualCoordinator(CoordinatorInfo *coordInfo,
                                 struct in_addr *localIP);
    void waitForCheckpointCommand();
    static bool noCoordinator();

    void connectToCoordOnStartup(CoordinatorMode mode,
                                 string progname,
                                 DmtcpUniqueProcessId *compId,
                                 CoordinatorInfo *coordInfo,
                                 struct in_addr *localIP);
    void createNewConnectionBeforeFork(string &progname);
    void connectToCoordOnRestart(CoordinatorMode mode,
                                 string progname,
                                 UniquePid compGroup,
                                 int np,
                                 CoordinatorInfo *coordInfo,
                                 const char *host,
                                 int port,
                                 struct in_addr *localIP);
    void closeConnection() { _real_close(_coordinatorSocket); }

    void updateSockFd();

    bool isValid() { return _coordinatorSocket != -1; }

    void sendMsgToCoordinator(DmtcpMessage msg,
                              const void *extraData = NULL,
                              size_t len = 0);
    void sendMsgToCoordinator(const DmtcpMessage &msg, const string &data);
    void recvMsgFromCoordinator(DmtcpMessage *msg, void **extraData = NULL);
    void waitForBarrier(const string &barrierId);
    char *connectAndSendUserCommand(char c,
                                    int *coordCmdStatus = NULL,
                                    int *numPeers = NULL,
                                    int *isRunning = NULL,
                                    int *ckptInterval = NULL);

    void updateCoordCkptDir(const char *dir);
    string getCoordCkptDir(void);

    void sendCkptFilename();

    int sendKeyValPairToCoordinator(const char *id,
                                    const void *key,
                                    uint32_t key_len,
                                    const void *val,
                                    uint32_t val_len);
    int sendQueryToCoordinator(const char *id,
                               const void *key,
                               uint32_t key_len,
                               void *val,
                               uint32_t *val_len);

  private:
    void startNewCoordinator(CoordinatorMode mode);
    void createNewConnToCoord(CoordinatorMode mode);
    DmtcpMessage sendRecvHandshake(DmtcpMessage msg,
                                   string progname,
                                   UniquePid *compId = NULL);

    int _coordinatorSocket;
    int _nsSock;
};
}
#endif // ifndef COORDINATORAPI_H
