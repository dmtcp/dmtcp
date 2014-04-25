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

static dmtcp::WorkerState theState ( dmtcp::WorkerState::RUNNING );

dmtcp::WorkerState dmtcp::WorkerState::currentState()
{
  return theState;
}

void dmtcp::WorkerState::setCurrentState ( const dmtcp::WorkerState& theValue )
{
  theState = theValue;
}

dmtcp::DmtcpMessage::DmtcpMessage ( DmtcpMessageType t /*= DMT_NULL*/ )
    :_msgSize ( sizeof ( DmtcpMessage ) )
    ,extraBytes ( 0 )
    ,type ( t )
    ,state ( WorkerState::currentState() )
    ,from ( UniquePid::ThisProcess() )
    ,virtualPid ( -1 )
    ,realPid ( -1 )
    ,keyLen ( 0 )
    ,valLen ( 0 )
    ,numPeers(0)
    ,isRunning(0)
    ,coordCmd('\0')
    ,coordCmdStatus(CoordCmdStatus::NOERROR)
    ,coordTimeStamp(0)
    ,theCheckpointInterval ( DMTCPMESSAGE_SAME_CKPT_INTERVAL )
{
//     struct sockaddr_storage _addr;
//         socklen_t _addrlen;
  memset(&compGroup, 0, sizeof(compGroup));
  memset(&ipAddr, 0, sizeof ipAddr);
  memset(nsid, 0, sizeof nsid);
  strncpy ( _magicBits,DMTCP_MAGIC_STRING,sizeof ( _magicBits ) );
}

void dmtcp::DmtcpMessage::assertValid() const
{
  JASSERT ( strcmp ( DMTCP_MAGIC_STRING,_magicBits ) == 0 )( _magicBits )
	  .Text ( "read invalid message, _magicBits mismatch."
		  "  Did DMTCP coordinator die uncleanly?" );
  JASSERT ( _msgSize == sizeof ( DmtcpMessage ) ) ( _msgSize ) ( sizeof ( DmtcpMessage ) )
	  .Text ( "read invalid message, size mismatch." );
}

bool dmtcp::DmtcpMessage::isValid() const
{
  if (strcmp(DMTCP_MAGIC_STRING, _magicBits) == 0) {
    JNOTE("read invalid message, _magicBits mismatch."
          " Closing remote connn") (_magicBits);
    return false;
  }
  if (_msgSize == sizeof(DmtcpMessage)) {
    JNOTE("read invalid message, size mismatch. Closing remote connection.")
      (_msgSize) (sizeof(DmtcpMessage));
    return false;
  }
  return true;
}

void dmtcp::DmtcpMessage::poison() { memset ( _magicBits,0,sizeof ( _magicBits ) ); }


dmtcp::WorkerState::eWorkerState dmtcp::WorkerState::value() const
{
  JASSERT(_state < _MAX) (_state);
  return (eWorkerState) _state;
}

dmtcp::ostream& dmtcp::operator << ( dmtcp::ostream& o, const dmtcp::WorkerState& s )
{
  o << "WorkerState::";
  switch ( s.value() )
  {
#define OSHIFTPRINTF(name) case WorkerState::name: o << #name; break;

      OSHIFTPRINTF ( UNKNOWN )
      OSHIFTPRINTF ( RUNNING )
      OSHIFTPRINTF ( SUSPENDED )
      OSHIFTPRINTF ( FD_LEADER_ELECTION )
      OSHIFTPRINTF ( NAME_SERVICE_DATA_REGISTERED)
      OSHIFTPRINTF ( DONE_QUERYING)
      OSHIFTPRINTF ( DRAINED )
      OSHIFTPRINTF ( RESTARTING )
      OSHIFTPRINTF ( CHECKPOINTED )
      OSHIFTPRINTF ( REFILLED )
    default:
      JASSERT ( false ) .Text ( "Invalid WorkerState" );
      o << (int)s.value();
  }
  return o;
}

const char* dmtcp::WorkerState::toString() const{
  switch(_state){
  case UNKNOWN:      return "UNKNOWN";
  case RUNNING:      return "RUNNING";
  case SUSPENDED:    return "SUSPENDED";
  case FD_LEADER_ELECTION:  return "FD_LEADER_ELECTION";
  case NAME_SERVICE_DATA_REGISTERED: return "NAME_SERVICE_DATA_REGISTERED";
  case DONE_QUERYING: return "DONE_QUERYING";
  case DRAINED:      return "DRAINED";
  case RESTARTING:   return "RESTARTING";
  case CHECKPOINTED: return "CHECKPOINTED";
  case REFILLED:     return "REFILLED";
  default:           return "???";
  }
}

dmtcp::ostream& dmtcp::operator << ( dmtcp::ostream& o, const dmtcp::DmtcpMessageType & s )
{
  // o << "DmtcpMessageType: ";
  switch ( s )
  {
#undef OSHIFTPRINTF
#define OSHIFTPRINTF(name) case name: o << #name; break;

      OSHIFTPRINTF ( DMT_NULL )
      OSHIFTPRINTF ( DMT_NEW_WORKER )
      OSHIFTPRINTF ( DMT_NAME_SERVICE_WORKER )
      OSHIFTPRINTF ( DMT_RESTART_WORKER )
      OSHIFTPRINTF ( DMT_ACCEPT )
      OSHIFTPRINTF ( DMT_REJECT_NOT_RESTARTING )
      OSHIFTPRINTF ( DMT_REJECT_WRONG_COMP )
      OSHIFTPRINTF ( DMT_REJECT_NOT_RUNNING )
      OSHIFTPRINTF ( DMT_REJECT_WRONG_PREFIX )

      OSHIFTPRINTF ( DMT_UPDATE_PROCESS_INFO_AFTER_FORK )
      OSHIFTPRINTF ( DMT_UPDATE_PROCESS_INFO_AFTER_EXEC )
      OSHIFTPRINTF ( DMT_GET_CKPT_DIR )
      OSHIFTPRINTF ( DMT_GET_CKPT_DIR_RESULT )
      OSHIFTPRINTF ( DMT_UPDATE_CKPT_DIR )

      OSHIFTPRINTF ( DMT_USER_CMD )
      OSHIFTPRINTF ( DMT_USER_CMD_RESULT )
      OSHIFTPRINTF ( DMT_CKPT_FILENAME )
      OSHIFTPRINTF ( DMT_UNIQUE_CKPT_FILENAME )

      //OSHIFTPRINTF ( DMT_RESTART_PROCESS )
      //OSHIFTPRINTF ( DMT_RESTART_PROCESS_REPLY )

      OSHIFTPRINTF ( DMT_DO_SUSPEND )
      OSHIFTPRINTF ( DMT_DO_FD_LEADER_ELECTION )
      OSHIFTPRINTF ( DMT_DO_DRAIN )
      OSHIFTPRINTF ( DMT_DO_CHECKPOINT )
#ifdef COORD_NAMESERVICE
      OSHIFTPRINTF ( DMT_DO_REGISTER_NAME_SERVICE_DATA )
      OSHIFTPRINTF ( DMT_DO_SEND_QUERIES )
#endif
      OSHIFTPRINTF ( DMT_DO_REFILL )
      OSHIFTPRINTF ( DMT_DO_RESUME )
      OSHIFTPRINTF ( DMT_KILL_PEER )

#ifdef COORD_NAMESERVICE
      OSHIFTPRINTF ( DMT_REGISTER_NAME_SERVICE_DATA )
      OSHIFTPRINTF ( DMT_REGISTER_NAME_SERVICE_DATA_SYNC )
      OSHIFTPRINTF ( DMT_REGISTER_NAME_SERVICE_DATA_SYNC_RESPONSE )
      OSHIFTPRINTF ( DMT_NAME_SERVICE_QUERY )
      OSHIFTPRINTF ( DMT_NAME_SERVICE_QUERY_RESPONSE )
#endif

      OSHIFTPRINTF ( DMT_OK )

    default:
      JASSERT ( false ) ( s ) .Text ( "Invalid Message Type" );
      //o << s;
  }
  return o;
}

