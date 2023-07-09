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
#include "kvdb.h"

namespace dmtcp
{
enum CoordinatorMode {
  COORD_INVALID   = 0x0000,
  COORD_JOIN      = 0x0001,
  COORD_NEW       = 0x0002,
  COORD_ANY       = 0x0010
};

namespace CoordinatorAPI
{

void eventHook(DmtcpEvent_t event, DmtcpEventData_t *data);
DmtcpPluginDescriptor_t pluginDescr();

void atForkPrepare();
void atForkParent();
void atForkChild();
void vforkChild();

void getCoordHostAndPort(CoordinatorMode mode, string *host, int *port);

void connectToCoordOnStartup(CoordinatorMode  mode,
                             string           progname,
                             DmtcpUniqueProcessId *compId,
                             CoordinatorInfo *coordInfo,
                             struct in_addr  *localIP);
int  createNewConnectionBeforeFork(string& progname);
void connectToCoordOnRestart(CoordinatorMode  mode,
                             string progname,
                             UniquePid compGroup,
                             int np,
                             CoordinatorInfo *coordInfo,
                             struct in_addr *localIP);

void sendMsgToCoordinator(DmtcpMessage msg,
                          const void *extraData = NULL,
                          size_t len = 0);
void sendMsgToCoordinator(const DmtcpMessage &msg, const string &data);
void recvMsgFromCoordinator(DmtcpMessage *msg, void **extraData = NULL);
bool waitForBarrier(const string& barrier, uint32_t *numPeers = NULL);
char *connectAndSendUserCommand(char c,
                                int *coordCmdStatus = NULL,
                                int *numPeers = NULL,
                                int *isRunning = NULL,
                                int *ckptInterval = NULL);

void sendCkptFilename();


kvdb::KVDBResponse
kvdbRequest(DmtcpMessage const& msg,
            string const& key,
            string const& val,
            string *oldVal);

} // namespace CoordinatorAPI
} // namespace dmtcp
#endif // ifndef COORDINATORAPI_H
