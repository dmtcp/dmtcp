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

static dmtcp::UniquePid theDefaultCoordinator;

void dmtcp::DmtcpMessage::setDefaultCoordinator ( const UniquePid& id ) {theDefaultCoordinator = id;}

dmtcp::DmtcpMessage::DmtcpMessage ( DmtcpMessageType t /*= DMT_NULL*/ )
    :_msgSize ( sizeof ( DmtcpMessage ) )
    ,type ( t )
    ,from ( ConnectionIdentifier::Self() )
    ,coordinator ( theDefaultCoordinator )
    ,state ( WorkerState::currentState() )
    ,compGroup ( UniquePid(0,0,0) )
    ,restorePid ( ConnectionIdentifier::Null() )
    ,restoreAddrlen ( 0 )
    ,restorePort ( -1 )
    ,keyLen ( 0 )
    ,valLen ( 0 )
    ,theCheckpointInterval ( 0 )
    ,extraBytes ( 0 )
{
//     struct sockaddr_storage _addr;
//         socklen_t _addrlen;
  strncpy ( _magicBits,DMTCP_MAGIC_STRING,sizeof ( _magicBits ) );
  memset ( &params,0,sizeof ( params ) );
  memset ( &restoreAddr,0,sizeof ( restoreAddr ) );
}

void dmtcp::DmtcpMessage::assertValid() const
{
  JASSERT ( strcmp ( DMTCP_MAGIC_STRING,_magicBits ) == 0 )( _magicBits )
	  .Text ( "read invalid message, _magicBits mismatch."
		  "  Did DMTCP coordinator die uncleanly?" );
  JASSERT ( _msgSize == sizeof ( DmtcpMessage ) ) ( _msgSize ) ( sizeof ( DmtcpMessage ) )
	  .Text ( "read invalid message, size mismatch." );
}

void dmtcp::DmtcpMessage::poison() { memset ( _magicBits,0,sizeof ( _magicBits ) ); }


dmtcp::WorkerState::eWorkerState dmtcp::WorkerState::value() const
{
  return _state;
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
#ifdef EXTERNAL_SOCKET_HANDLING
      OSHIFTPRINTF ( PEER_LOOKUP_COMPLETE )
#endif
#ifdef IBV
      OSHIFTPRINTF ( NAME_SERVICE_DATA_REGISTERED)
      OSHIFTPRINTF ( DONE_QUERYING)
#endif
      OSHIFTPRINTF ( DRAINED )
      OSHIFTPRINTF ( RESTARTING )
      OSHIFTPRINTF ( CHECKPOINTED )
      OSHIFTPRINTF ( REFILLED )
    default:
      JASSERT ( false ) .Text ( "Invalid WorkerState" );
      o << s.value();
  }
  return o;
}

const char* dmtcp::WorkerState::toString() const{
  switch(_state){
  case UNKNOWN:      return "UNKNOWN";
  case RUNNING:      return "RUNNING";
  case SUSPENDED:    return "SUSPENDED";
  case FD_LEADER_ELECTION:  return "FD_LEADER_ELECTION";
#ifdef EXTERNAL_SOCKET_HANDLING
  case PEER_LOOKUP_COMPLETE:  return "PEER_LOOKUP_COMPLETE";
#endif
#ifdef IBV
  case NAME_SERVICE_DATA_REGISTERED: return "NAME_SERVICE_DATA_REGISTERED";
  case DONE_QUERYING: return "DONE_QUERYING";
#endif
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
      OSHIFTPRINTF ( DMT_HELLO_PEER )
      OSHIFTPRINTF ( DMT_HELLO_COORDINATOR )
      OSHIFTPRINTF ( DMT_HELLO_WORKER )

      OSHIFTPRINTF ( DMT_USER_CMD )
      OSHIFTPRINTF ( DMT_USER_CMD_RESULT )

      OSHIFTPRINTF ( DMT_RESTART_PROCESS )
      OSHIFTPRINTF ( DMT_RESTART_PROCESS_REPLY )

      OSHIFTPRINTF ( DMT_DO_SUSPEND )
      OSHIFTPRINTF ( DMT_DO_RESUME )
      OSHIFTPRINTF ( DMT_DO_FD_LEADER_ELECTION )
#ifdef EXTERNAL_SOCKET_HANDLING
      OSHIFTPRINTF ( DMT_DO_PEER_LOOKUP )
#endif
      OSHIFTPRINTF ( DMT_DO_DRAIN )
      OSHIFTPRINTF ( DMT_DO_CHECKPOINT )
      OSHIFTPRINTF ( DMT_DO_REFILL )

#ifdef EXTERNAL_SOCKET_HANDLING
      OSHIFTPRINTF ( DMT_PEER_LOOKUP )
      OSHIFTPRINTF ( DMT_UNKNOWN_PEER )
      OSHIFTPRINTF ( DMT_EXTERNAL_SOCKETS_CLOSED )
#endif
//#ifdef IBV
      OSHIFTPRINTF ( DMT_REGISTER_NAME_SERVICE_DATA )
      OSHIFTPRINTF ( DMT_NAME_SERVICE_QUERY )
      OSHIFTPRINTF ( DMT_NAME_SERVICE_QUERY_RESPONSE )
//#endif

      OSHIFTPRINTF ( DMT_RESTORE_RECONNECTED )
      OSHIFTPRINTF ( DMT_RESTORE_WAITING )

      OSHIFTPRINTF ( DMT_PEER_ECHO )
      OSHIFTPRINTF ( DMT_OK )
      OSHIFTPRINTF ( DMT_CKPT_FILENAME )
      OSHIFTPRINTF ( DMT_FORCE_RESTART )
      OSHIFTPRINTF ( DMT_KILL_PEER )
      OSHIFTPRINTF ( DMT_REJECT )

    default:
      JASSERT ( false ) ( s ) .Text ( "Invalid Message Type" );
      //o << s;
  }
  return o;
}

