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

#include "dmtcpmessagetypes.h"
#include "workerstate.h"

using namespace dmtcp;

DmtcpMessage::DmtcpMessage(DmtcpMessageType t /*= DMT_NULL*/)
  : _msgSize(sizeof(DmtcpMessage))
  , extraBytes(0)
  , type(t)
  , state(WorkerState::currentState())
  , from(UniquePid::ThisProcess())
  , virtualPid(-1)
  , realPid(-1)
  , keyLen(0)
  , valLen(0)
  , numPeers(0)
  , isRunning(0)
  , coordCmd('\0')
  , coordCmdStatus(CoordCmdStatus::NOERROR)
  , coordTimeStamp(0)
  , theCheckpointInterval(DMTCPMESSAGE_SAME_CKPT_INTERVAL)
  , exitAfterCkpt(0)
{
  // struct sockaddr_storage _addr;
  // socklen_t _addrlen;
  memset(&barrier, 0, sizeof(barrier));
  memset(&compGroup, 0, sizeof(compGroup));
  memset(&ipAddr, 0, sizeof ipAddr);
  memset(nsid, 0, sizeof nsid);
  strncpy(_magicBits, DMTCP_MAGIC_STRING, sizeof(_magicBits));
}

void
DmtcpMessage::assertValid() const
{
  JASSERT(strcmp(DMTCP_MAGIC_STRING, _magicBits) == 0)(_magicBits)
  .Text("read invalid message, _magicBits mismatch."
        "  Did DMTCP coordinator die uncleanly?");
  JASSERT(_msgSize == sizeof(DmtcpMessage)) (_msgSize) (sizeof(DmtcpMessage))
  .Text("read invalid message, size mismatch.");
}

bool
DmtcpMessage::isValid() const
{
  if (strcmp(DMTCP_MAGIC_STRING, _magicBits) != 0) {
    JNOTE("read invalid message, _magicBits mismatch."
          " Closing remote connection.") (_magicBits);
    return false;
  }
  if (_msgSize != sizeof(DmtcpMessage)) {
    JNOTE("read invalid message, size mismatch. Closing remote connection.")
      (_msgSize) (sizeof(DmtcpMessage));
    return false;
  }
  return true;
}

void
DmtcpMessage::poison() { memset(_magicBits, 0, sizeof(_magicBits)); }


ostream&
dmtcp::operator<<(dmtcp::ostream &o, const DmtcpMessageType &s)
{
  // o << "DmtcpMessageType: ";
  switch (s) {
#undef OSHIFTPRINTF
#define OSHIFTPRINTF(name) case name: o << # name; break;

    OSHIFTPRINTF(DMT_NULL)
    OSHIFTPRINTF(DMT_NEW_WORKER)
    OSHIFTPRINTF(DMT_NAME_SERVICE_WORKER)
    OSHIFTPRINTF(DMT_RESTART_WORKER)
    OSHIFTPRINTF(DMT_ACCEPT)
    OSHIFTPRINTF(DMT_REJECT_NOT_RESTARTING)
    OSHIFTPRINTF(DMT_REJECT_WRONG_COMP)
    OSHIFTPRINTF(DMT_REJECT_NOT_RUNNING)

    OSHIFTPRINTF(DMT_UPDATE_PROCESS_INFO_AFTER_FORK)
    OSHIFTPRINTF(DMT_UPDATE_PROCESS_INFO_AFTER_INIT_OR_EXEC)
    OSHIFTPRINTF(DMT_GET_CKPT_DIR)
    OSHIFTPRINTF(DMT_GET_CKPT_DIR_RESULT)
    OSHIFTPRINTF(DMT_UPDATE_CKPT_DIR)

    OSHIFTPRINTF(DMT_USER_CMD)
    OSHIFTPRINTF(DMT_USER_CMD_RESULT)
    OSHIFTPRINTF(DMT_CKPT_FILENAME)
    OSHIFTPRINTF(DMT_UNIQUE_CKPT_FILENAME)

    // OSHIFTPRINTF ( DMT_RESTART_PROCESS )
    // OSHIFTPRINTF ( DMT_RESTART_PROCESS_REPLY )

    OSHIFTPRINTF(DMT_DO_CHECKPOINT)

    OSHIFTPRINTF(DMT_BARRIER)
    OSHIFTPRINTF(DMT_BARRIER_RELEASED)

    OSHIFTPRINTF(DMT_WORKER_RESUMING)

    OSHIFTPRINTF(DMT_KILL_PEER)

    OSHIFTPRINTF(DMT_REGISTER_NAME_SERVICE_DATA)
    OSHIFTPRINTF(DMT_NAME_SERVICE_QUERY)
    OSHIFTPRINTF(DMT_NAME_SERVICE_QUERY_RESPONSE)
    OSHIFTPRINTF(DMT_NAME_SERVICE_QUERY_ALL)
    OSHIFTPRINTF(DMT_NAME_SERVICE_QUERY_ALL_RESPONSE)

    OSHIFTPRINTF(DMT_NAME_SERVICE_GET_UNIQUE_ID)
    OSHIFTPRINTF(DMT_NAME_SERVICE_GET_UNIQUE_ID_RESPONSE)

  default:
    JASSERT(false) (s).Text("Invalid Message Type");

    // o << s;
  }
  return o;
}
