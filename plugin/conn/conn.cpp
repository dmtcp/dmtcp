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

#include "connectionlist.h"
#include "dmtcpplugin.h"
#include "../jalib/jassert.h"
#include "../jalib/jserialize.h"

using namespace dmtcp;
// This is the first program after dmtcp_checkpoint
static bool freshProcess = true;

extern "C"
void dmtcp_process_event(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_INIT:
      if (freshProcess) {
        ConnectionList::instance().scanForPreExisting();
      }
      break;

    case DMTCP_EVENT_WAIT_FOR_SUSPEND_MSG:
      break;

    case DMTCP_EVENT_PRE_EXEC:
      {
        jalib::JBinarySerializeWriterRaw wr("", data->serializerInfo.fd);
        ConnectionList::instance().serialize(wr);
      }
      break;

    case DMTCP_EVENT_POST_EXEC:
      {
        freshProcess = false;
        jalib::JBinarySerializeReaderRaw rd("", data->serializerInfo.fd);
        ConnectionList::instance().serialize(rd);
        ConnectionList::instance().deleteStaleConnections();
      }
      break;

    case DMTCP_EVENT_POST_RESTART:
      ConnectionList::instance().postRestart();

      break;

    case DMTCP_EVENT_PRE_FORK:
      break;

    case DMTCP_EVENT_SUSPENDED:
      ConnectionList::instance().preLockSaveOptions();
      break;

    case DMTCP_EVENT_LEADER_ELECTION:
      JTRACE("locking...");
      ConnectionList::instance().preCheckpointFdLeaderElection();
      JTRACE("locked");
      break;

    case DMTCP_EVENT_DRAIN:
      JTRACE("draining...");
      ConnectionList::instance().preCheckpointDrain();
      JTRACE("drained");
      break;

    case DMTCP_EVENT_PRE_CKPT:
#if HANDSHAKE_ON_CHECKPOINT == 1
      //handshake is done after one barrier after drain
      JTRACE("beginning handshakes");
      ConnectionList::instance().preCheckpointHandshakes();
      JTRACE("handshaking done");
#endif
      break;

    case DMTCP_EVENT_REFILL:
      ConnectionList::instance().refill(data->refillInfo.isRestart);
      break;

    case DMTCP_EVENT_RESUME:
      ConnectionList::instance().resume(data->resumeInfo.isRestart);
      break;

    case DMTCP_EVENT_REGISTER_NAME_SERVICE_DATA:
      if (data->nameserviceInfo.isRestart) {
        ConnectionList::instance().registerNSData();
      }
      break;

    case DMTCP_EVENT_SEND_QUERIES:
      if (data->nameserviceInfo.isRestart) {
        ConnectionList::instance().sendQueries();
      }
      break;

    default:
      break;
  }

  NEXT_DMTCP_PROCESS_EVENT(event, data);
  return;
}

