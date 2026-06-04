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
#include "json.h"
#include "workerstate.h"

#include <cstring>

using namespace dmtcp;

namespace {

const int JSON_SCHEMA_VERSION = 1;

bool
messageMagicIsValid(const char (&magic)[16])
{
  static_assert(sizeof(DMTCP_MAGIC_STRING) <= sizeof(magic),
                "DMTCP_MAGIC_STRING must not exceed magic buffer");
  return memcmp(magic, DMTCP_MAGIC_STRING, sizeof(DMTCP_MAGIC_STRING)) == 0;
}

} // namespace

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
  , coordCmd(DMT_INVALID_COORDINATOR_COMMAND)
  , coordCmdStatus(DMT_COORD_SUCCESS)
  , coordTimeStamp(0)
  , theCheckpointInterval(DMTCPMESSAGE_SAME_CKPT_INTERVAL)
  , exitAfterCkpt(0)
{
  // struct sockaddr_storage _addr;
  // socklen_t _addrlen;
  memset(&barrier, 0, sizeof(barrier));
  compGroup = UniquePid();
  memset(&ipAddr, 0, sizeof ipAddr);
  memset(nsid, 0, sizeof nsid);
  strncpy(_magicBits, DMTCP_MAGIC_STRING, sizeof(_magicBits));
}

void
DmtcpMessage::assertValid() const
{
  JASSERT(messageMagicIsValid(_magicBits))
  .Text("read invalid message, _magicBits mismatch."
        "  Did DMTCP coordinator die uncleanly?");
  JASSERT(_msgSize == sizeof(DmtcpMessage)) (_msgSize) (sizeof(DmtcpMessage))
  .Text("read invalid message, size mismatch.");
}

bool
DmtcpMessage::isValid() const
{
  if (!messageMagicIsValid(_magicBits)) {
    JNOTE("read invalid message, _magicBits mismatch."
          " Closing remote connection.");
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

string
DmtcpMessage::toCoordinatorCmdJson(const char *coordHost,
                                   int coordPort,
                                   const char *extraData) const
{
  Json json;

  json.appendField("schema_version", JSON_SCHEMA_VERSION);
  json.appendField("command", coordinatorCmdName(coordCmd));
  json.appendField("command_status",
                   coordinatorCmdStatusName(coordCmdStatus));
  json.appendField("coordinator_host", coordHost);
  json.appendField("coordinator_port", coordPort);

  if (coordCmdStatus != DMT_COORD_SUCCESS) {
    return json.str();
  }

  if (coordCmd == DMT_STATUS) {
    json.appendField("num_peers", numPeers);
    json.appendField("running", isRunning != 0);
    json.appendField("checkpoint_interval", theCheckpointInterval);
  } else if (coordCmd == DMT_LIST) {
    json.appendField("workers", extraData != NULL ? extraData : "");
  } else if (coordCmd == DMT_CHECKPOINT ||
             coordCmd == DMT_BLOCKING_CKPT ||
             coordCmd == DMT_KILL_AFTER_CKPT) {
    json.appendField("num_peers", numPeers);
  } else if (coordCmd == DMT_UPDATE_CKPT_INTERVAL) {
    json.appendField("checkpoint_interval", theCheckpointInterval);
  }

  return json.str();
}

const char *
dmtcp::coordinatorCmdName(CoordinatorCmd command)
{
  switch (command) {
  case DMT_INVALID_COORDINATOR_COMMAND:
    return "DMT_INVALID_COORDINATOR_COMMAND";
  case DMT_STATUS:
    return "DMT_STATUS";
  case DMT_LIST:
    return "DMT_LIST";
  case DMT_CHECKPOINT:
    return "DMT_CHECKPOINT";
  case DMT_BLOCKING_CKPT:
    return "DMT_BLOCKING_CKPT";
  case DMT_KILL_AFTER_CKPT:
    return "DMT_KILL_AFTER_CKPT";
  case DMT_UPDATE_CKPT_INTERVAL:
    return "DMT_UPDATE_CKPT_INTERVAL";
  case DMT_KILL:
    return "DMT_KILL";
  case DMT_QUIT:
    return "DMT_QUIT";
  case DMT_HELP:
    return "DMT_HELP";
  default:
    return "DMT_INVALID_COORDINATOR_COMMAND";
  }
}

const char *
dmtcp::coordinatorCmdStatusName(CoordinatorCmdStatus response)
{
  switch (response) {
  case DMT_COORD_SUCCESS:
    return "DMT_COORD_SUCCESS";
  case DMT_COORD_INVALID_COMMAND:
    return "DMT_COORD_INVALID_COMMAND";
  case DMT_COORD_NOT_RUNNING:
    return "DMT_COORD_NOT_RUNNING";
  case DMT_COORD_NOT_FOUND:
    return "DMT_COORD_NOT_FOUND";
  default:
    return "DMT_COORD_INVALID_COMMAND";
  }
}

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
    OSHIFTPRINTF(DMT_REJECT_RESTART_PEER_MISMATCH)

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

    OSHIFTPRINTF(DMT_KVDB_REQUEST)
    OSHIFTPRINTF(DMT_KVDB_RESPONSE)

  default:
    JASSERT(false) (s).Text("Invalid Message Type");

    // o << s;
  }
  return o;
}
