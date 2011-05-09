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

// CAN REMOVE BOOL enableCheckpointing ARG OF DmtcpWorker WHEN WE'RE DONE.
// DmtcpWorker CAN INHERIT THIS CLASS, DmtcpCoordinatorAPI

// dmtcp::DmtcpWorker::connectToCoordinator calls this but doesn't seem to use it.
// Can we get rid of it?
//    dmtcp::UniquePid zeroGroup;

#include "dmtcpcoordinatorapi.h"
#include "dmtcpworker.h"
#include <stdlib.h>
#include "mtcpinterface.h"
#include <unistd.h>
#include "sockettable.h"
#include  "../jalib/jsocket.h"
#include <map>
#include "kernelbufferdrainer.h"
#include  "../jalib/jfilesystem.h"
#include "syscallwrappers.h"
#include "protectedfds.h"
#include "connectionidentifier.h"
#include "connectionmanager.h"
#include "connectionstate.h"
#include "dmtcp_coordinator.h"
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

dmtcp::DmtcpCoordinatorAPI::DmtcpCoordinatorAPI ()
  :_coordinatorSocket ( PROTECTEDFD ( 1 ) )
  ,_restoreSocket ( PROTECTEDFD ( 3 ) )
{  return;
}

void dmtcp::DmtcpCoordinatorAPI::useAlternateCoordinatorFd(){
  _coordinatorSocket = jalib::JSocket( PROTECTEDFD( 4 ) );
}

void dmtcp::DmtcpCoordinatorAPI::connectAndSendUserCommand(char c, int* result /*= NULL*/)
{
  //prevent checkpoints from starting
  DmtcpWorker::delayCheckpointsLock();
  {
    if ( tryConnectToCoordinator() == false ) {
      *result = ERROR_COORDINATOR_NOT_FOUND;
      return;
    }
    sendUserCommand(c,result);
    _coordinatorSocket.close();
  }
  DmtcpWorker::delayCheckpointsUnlock();
}


/*!
    \fn dmtcp::DmtcpCoordinatorAPI::connectAndSendUserCommand()
 */
bool dmtcp::DmtcpCoordinatorAPI::tryConnectToCoordinator()
{
  return connectToCoordinator ( false );
}

// THIS ONE IS COMMON TO DmtcpWorker AND DmtcpCoordinatorAPI
// Maybe make it a static method of DmtcpWorker so that anybody can use it,
//   with a comment that DmtcpCoordinatorAPI also uses it.
bool dmtcp::DmtcpCoordinatorAPI::connectToCoordinator(bool dieOnError /*= true*/)
{

  const char * coordinatorAddr = getenv ( ENV_VAR_NAME_HOST );
  const char * coordinatorPortStr = getenv ( ENV_VAR_NAME_PORT );

  if ( coordinatorAddr == NULL ) coordinatorAddr = DEFAULT_HOST;
  int coordinatorPort = coordinatorPortStr==NULL ? DEFAULT_PORT : jalib::StringToInt ( coordinatorPortStr );

  jalib::JSocket oldFd = _coordinatorSocket;

  _coordinatorSocket = jalib::JClientSocket ( coordinatorAddr,coordinatorPort );

  if ( ! _coordinatorSocket.isValid() && ! dieOnError ) {
    return false;
  }

  JASSERT ( _coordinatorSocket.isValid() )
    ( coordinatorAddr ) ( coordinatorPort )
    .Text ( "Failed to connect to DMTCP coordinator" );

  JTRACE ( "connected to dmtcp coordinator, no handshake" )
    ( coordinatorAddr ) ( coordinatorPort );

  if ( oldFd.isValid() )
  {
    JTRACE ( "restoring old coordinatorsocket fd" )
      ( oldFd.sockfd() ) ( _coordinatorSocket.sockfd() );

    _coordinatorSocket.changeFd ( oldFd.sockfd() );
  }
  return true;
}

//tell the coordinator to run given user command
void dmtcp::DmtcpCoordinatorAPI::sendUserCommand(char c, int* result /*= NULL*/)
{
  DmtcpMessage msg, reply;

  //send
  msg.type = DMT_USER_CMD;
  msg.params[0] = c;

  if (c == 'i') {
    const char* interval = getenv ( ENV_VAR_CKPT_INTR );
    if ( interval != NULL )
      msg.theCheckpointInterval = jalib::StringToInt ( interval );
  }

  _coordinatorSocket << msg;

  //the coordinator will violently close our socket...
  if(c=='q' || c=='Q'){
    result[0]=0;
    return;
  }

  //receive REPLY
  reply.poison();
  _coordinatorSocket >> reply;
  reply.assertValid();
  JASSERT ( reply.type == DMT_USER_CMD_RESULT );

  if(result!=NULL){
    memcpy( result, reply.params, sizeof(reply.params) );
  }
}
