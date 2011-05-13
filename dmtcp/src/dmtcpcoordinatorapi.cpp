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
#include "protectedfds.h"
#include "syscallwrappers.h"
#include  "../jalib/jsocket.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jfilesystem.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

dmtcp::DmtcpCoordinatorAPI::DmtcpCoordinatorAPI ()
  :_coordinatorSocket ( PROTECTED_COORD_FD )
  ,_restoreSocket ( PROTECTED_RESTORE_SOCK_FD )
{ 
  return;
}

void dmtcp::DmtcpCoordinatorAPI::useAlternateCoordinatorFd(){
  _coordinatorSocket = jalib::JSocket( PROTECTED_COORD_ALT_FD );
}

void dmtcp::DmtcpCoordinatorAPI::connectAndSendUserCommand(char c, int* result /*= NULL*/)
{
  if ( tryConnectToCoordinator() == false ) {
    *result = ERROR_COORDINATOR_NOT_FOUND;
    return;
  }
  sendUserCommand(c,result);
  _coordinatorSocket.close();
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

void dmtcp::DmtcpCoordinatorAPI::startCoordinatorIfNeeded(int modes,
                                                          int isRestart)
{
  const static int CS_OK = 91;
  const static int CS_NO = 92;
  int coordinatorStatus = -1;

  if (modes & COORD_BATCH) {
    startNewCoordinator ( modes, isRestart );
    return;
  }
  //fork a child process to probe the coordinator
  if (fork()==0) {
    //fork so if we hit an error parent won't die
    dup2(2,1);                          //copy stderr to stdout
    dup2(open("/dev/null",O_RDWR), 2);  //close stderr
    int result[DMTCPMESSAGE_NUM_PARAMS];
    dmtcp::DmtcpCoordinatorAPI coordinatorAPI;
    {
      if ( coordinatorAPI.tryConnectToCoordinator() == false ) {
        JTRACE("Coordinator not found.  Will try to start a new one.");
        _real_exit(1);
      }
    }

    coordinatorAPI.sendUserCommand('s',result);
    coordinatorAPI._coordinatorSocket.close();

    // result[0] == numPeers of coord;  bool result[1] == computation is running
    if(result[0]==0 || result[1] ^ isRestart){
      if(result[0] != 0) {
        int num_processes = result[0];
        JTRACE("Joining existing computation.") (num_processes);
      }
      _real_exit(CS_OK);
    }else{
      JTRACE("Existing computation not in a running state," \
	     " perhaps checkpoint in progress?");
      _real_exit(CS_NO);
    }
  }
  errno = 0;
  // FIXME:  wait() could return -1 if a signal happened before child exits
  JASSERT(::wait(&coordinatorStatus)>0)(JASSERT_ERRNO);
  JASSERT(WIFEXITED(coordinatorStatus));

  //is coordinator running?
  if (WEXITSTATUS(coordinatorStatus) != CS_OK) {
    //is coordinator in funny state?
    if(WEXITSTATUS(coordinatorStatus) == CS_NO){
      JASSERT (false) (isRestart)
	 .Text ("Coordinator in a funny state.  Peers exist, not restarting," \
		"\n but not in a running state.  Checkpointing?" \
		"\n Or maybe restarting and running with peers existing?");
    }else{
      JTRACE("Bad result found for coordinator.  Try a new one.");
    }

    JTRACE("Coordinator not found.  Starting a new one.");
    startNewCoordinator ( modes, isRestart );

  }else{
    if (modes & COORD_FORCE_NEW) {
      JTRACE("Forcing new coordinator.  --new-coordinator flag given.");
      startNewCoordinator ( modes, isRestart );
      return;
    }
    JASSERT( modes & COORD_JOIN )
      .Text("Coordinator already running, but '--new' flag was given.");
  }
}

void dmtcp::DmtcpCoordinatorAPI::startNewCoordinator(int modes, int isRestart)
{
  int coordinatorStatus = -1;
  //get location of coordinator
  const char *coordinatorAddr = getenv ( ENV_VAR_NAME_HOST );
  if(coordinatorAddr == NULL) coordinatorAddr = DEFAULT_HOST;
  const char *coordinatorPortStr = getenv ( ENV_VAR_NAME_PORT );

  dmtcp::string s = coordinatorAddr;
  if(s != "localhost" && s != "127.0.0.1" &&
     s != jalib::Filesystem::GetCurrentHostname()){
    JASSERT(false)
      .Text("Won't automatically start coordinator because DMTCP_HOST is set to a remote host.");
    _real_exit(1);
  }

  if ( modes & COORD_BATCH || modes & COORD_FORCE_NEW ) {
    // Create a socket and bind it to an unused port.
    jalib::JServerSocket coordinatorListenerSocket ( jalib::JSockAddr::ANY, 0 );
    errno = 0;
    JASSERT ( coordinatorListenerSocket.isValid() )
      ( coordinatorListenerSocket.port() ) ( JASSERT_ERRNO )
      .Text ( "Failed to create listen socket."
          "\nIf msg is \"Address already in use\", this may be an old coordinator."
          "\nKill other coordinators and try again in a minute or so." );
    // Now dup the sockfd to
    coordinatorListenerSocket.changeFd(PROTECTED_COORD_FD);
    dmtcp::string coordPort= jalib::XToString(coordinatorListenerSocket.port());
    setenv ( ENV_VAR_NAME_PORT, coordPort.c_str(), 1 );
  }

  JTRACE("Starting a new coordinator automatically.") (coordinatorPortStr);

  if(fork()==0){
    dmtcp::string coordinator = jalib::Filesystem::FindHelperUtility("dmtcp_coordinator");
    char *modeStr = (char *)"--background";
    if ( modes & COORD_BATCH ) {
      modeStr = (char *)"--batch";
    }
    char * args[] = {
      (char*)coordinator.c_str(),
      (char*)"--exit-on-last",
      modeStr,
      NULL
    };
    execv(args[0], args);
    JASSERT(false)(coordinator)(JASSERT_ERRNO).Text("exec(dmtcp_coordinator) failed");
  } else {
    _real_close ( PROTECTED_COORD_FD );
  }

  errno = 0;

  if ( modes & COORD_BATCH ) {
    // FIXME: If running in batch Mode, we sleep here for 5 seconds to let
    // the coordinator get started up.  We need to fix this in future.
    sleep(5);
  } else {
    JASSERT(wait(&coordinatorStatus)>0)(JASSERT_ERRNO);

    JASSERT(WEXITSTATUS(coordinatorStatus) == 0)
      .Text("Failed to start coordinator, port already in use.  You may use a different port by running with \'-p 12345\'\n");
  }
}

