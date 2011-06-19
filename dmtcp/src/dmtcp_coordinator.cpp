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

/****************************************************************************
 * Coordinator code logic:                                                  *
 * main calls monitorSockets, which acts as a top level event loop.         *
 * monitorSockets calls:  onConnect, onData, onDisconnect, onTimeoutInterval*
 *   when client or dmtcp_command talks to coordinator.                     *
 * onConnect and onData receive a socket parameter reads msg and passes to: *
 *   handleUserCommand, which takes single char arg ('s', 'c', 'k', 'q', ...)*
 * handleUserCommand calls broadcastMessage to send data back               *
 * any message sent by broadcastMessage takes effect only on returning      *
 *   back up to top level monitorSockets                                    *
 * Hence, even for checkpoint, handleUserCommand just changes state,        *
 *   broadcasts an initial checkpoint command, and then returns to top      *
 *   level.  Replies from clients then driver further state changes.        *
 * The prefix command 'b' (blocking) from dmtcp_command modifies behavior   *
 *   of 'c' so that the reply to dmtcp_command happens only when clients    *
 *   are back in RUNNING state.                                             *
 * The states for a worker (client) are:                                    *
 * Checkpoint: RUNNING -> SUSPENDED -> FD_LEADER_ELECTION -> DRAINED        *
 *       	  -> CHECKPOINTED -> REFILLED -> RUNNING		    *
 * Restart:    RESTARTING -> CHECKPOINTED -> REFILLED -> RUNNING	    *
 * If debugging, set gdb breakpoint on:					    *
 *   dmtcp::DmtcpCoordinator::onConnect					    *
 *   dmtcp::DmtcpCoordinator::onData					    *
 *   dmtcp::DmtcpCoordinator::handleUserCommand				    *
 *   dmtcp::DmtcpCoordinator::broadcastMessage				    *
 ****************************************************************************/

#include "dmtcp_coordinator.h"
#include "constants.h"
#include "protectedfds.h"
#include "dmtcpmessagetypes.h"
#include "dmtcpcoordinatorapi.h"
#include "lookup_service.h"
#include "util.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jtimer.h"
#include  "../jalib/jfilesystem.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <algorithm>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#undef min
#undef max


static int thePort = -1;

static const char* theHelpMessage =
  "COMMANDS:\n"
  "  l : List connected nodes\n"
  "  s : Print status message\n"
  "  c : Checkpoint all nodes\n"
  "  i : Print current checkpoint interval\n"
  "      (To change checkpoint interval, use dmtcp_command)\n"
  "  f : Force a restart even if there are missing nodes (debugging only)\n"
  "  k : Kill all nodes\n"
  "  q : Kill all nodes and quit\n"
  "  ? : Show this message\n"
  "\n"
;

static const char* theUsage =
  "USAGE: \n"
  "   dmtcp_coordinator [OPTIONS] [port]\n\n"
  "OPTIONS:\n"
  "  --port, -p, (environment variable DMTCP_PORT):\n"
  "      Port to listen on (default: 7779)\n"
  "  --ckptdir, -c, (environment variable DMTCP_CHECKPOINT_DIR):\n"
  "      Directory to store dmtcp_restart_script.sh (default: ./)\n"
  "  --tmpdir, -t, (environment variable DMTCP_TMPDIR):\n"
  "      Directory to store temporary files (default: env var TMDPIR or /tmp)\n"
  "  --exit-on-last\n"
  "      Exit automatically when last client disconnects\n"
  "  --background\n"
  "      Run silently in the background (mutually exclusive with --batch)\n"
  "  --batch\n"
  "      Run in batch mode (mutually exclusive with --background)\n"
  "      The checkpoint interval is set to 3600 seconds (1 hr) by default\n"
  "  --interval, -i, (environment variable DMTCP_CHECKPOINT_INTERVAL):\n"
  "      Time in seconds between automatic checkpoints\n"
  "      (default: 0, disabled)\n"
  "COMMANDS:\n"
  "  (type '?<return>' at runtime for list)\n\n"
  "See http://dmtcp.sf.net/ for more information.\n"
;


static const char* theRestartScriptHeader =
  "#!/bin/bash \n"
  "set -m # turn on job control\n\n"
  "#This script launches all the restarts in the background.\n"
  "#Suggestions for editing:\n"
  "#  1. For those processes executing on the localhost, remove 'ssh <hostname>' from the start of the line. \n"
  "#  2. If using ssh, verify that ssh does not require passwords or other prompts.\n"
  "#  3. Verify that the dmtcp_restart command is in your path on all hosts.\n"
  "#  4. Verify DMTCP_HOST and DMTCP_PORT match the location of the dmtcp_coordinator.\n"
  "#     If necessary, add 'DMTCP_PORT=<dmtcp_coordinator port>' after 'DMTCP_HOST=<...>'.\n"
  "#  5. Remove the '&' from a line if that process reads STDIN.\n"
  "#     If multiple processes read STDIN then prefix the line with 'xterm -hold -e' and put '&' at the end of the line.\n"
  "#  6. Processes on same host can be restarted with single dmtcp_restart command.\n\n\n"
;

static const char* theRestartScriptUsage =
  "usage_str='USAGE:\n"
  "  dmtcp_restart_script.sh [OPTIONS]\n\n"
  "OPTIONS:\n"
  "  --host, -h, (environment variable DMTCP_HOST):\n"
  "      Hostname where dmtcp_coordinator is running\n"
  "  --port, -p, (environment variable DMTCP_PORT):\n"
  "      Port where dmtcp_coordinator is running\n"
  "  --hostfile <arg0> :\n"
  "      Provide a hostfile (One host per line, \"#\" indicates comments)\n"
  "  --restartdir, -d, (environment variable DMTCP_RESTART_DIR):\n"
  "      Directory to read checkpoint images from\n"
  "  --batch, -b:\n"
  "      Enable batch mode for dmtcp_restart\n"
  "  --disable-batch, -b:\n"
  "      Disable batch mode for dmtcp_restart (if previously enabled)\n"
  "  --interval, -i, (environment variable DMTCP_CHECKPOINT_INTERVAL):\n"
  "      Time in seconds between automatic checkpoints\n"
  "      (Default: Use pre-checkpoint value)\n"
  "  --help:\n"
  "      Print this message\'\n\n\n"
;

static const char* theRestartScriptCmdlineArgHandler =
  "if [ $# -gt 0 ]; then\n"
  "  while [ $# -gt 0 ]\n"
  "  do\n"
  "    if [ $1 = \"--help\" ]; then\n"
  "      echo \"$usage_str\"\n"
  "      exit\n"
  "    elif [ $1 = \"--batch\" -o $1 = \"-b\" ]; then\n"
  "      maybebatch='--batch'\n"
  "      shift\n"
  "    elif [ $1 = \"--disable-batch\" ]; then\n"
  "      maybebatch=\n"
  "      shift\n"
  "    elif [ $# -ge 2 ]; then\n"
  "      case \"$1\" in \n"
  "        --host|-h)\n"
  "          coord_host=\"$2\";;\n"
  "        --port|-p)\n"
  "          coord_port=\"$2\";;\n"
  "        --hostfile)\n"
  "          hostfile=\"$2\"\n"
  "          if [ ! -f \"$hostfile\" ]; then\n"
  "            echo \"ERROR: hostfile $hostfile not found\"\n"
  "            exit\n"
  "          fi;;\n"
  "        --restartdir|-d)\n"
  "          DMTCP_RESTART_DIR=$2;;\n"
  "        --interval|-i)\n"
  "          checkpoint_interval=$2;;\n"
  "        *)\n"
  "          echo \"$0: unrecognized option \'$1\'. See correct usage below\"\n"
  "          echo \"$usage_str\"\n"
  "          exit;;\n"
  "      esac\n"
  "      shift\n"
  "      shift\n"
  "    elif [ $1 = \"--help\" ]; then\n"
  "      echo \"$usage_str\"\n"
  "      exit\n"
  "    else\n"
  "      echo \"$0: Incorrect usage.  See correct usage below\"\n"
  "      echo\n"
  "      echo \"$usage_str\"\n"
  "      exit\n"
  "    fi\n"
  "  done\n"
  "fi\n\n"
;

static bool exitOnLast = false;
static bool blockUntilDone = false;
static int blockUntilDoneRemote = -1;
static dmtcp::DmtcpMessage blockUntilDoneReply;
#ifdef EXTERNAL_SOCKET_HANDLING
static int numWorkersWithExternalSockets = 0;
static dmtcp::vector<dmtcp::ConnectionIdentifier> workersWithExternalSockets;
#endif

static dmtcp::DmtcpCoordinator prog;

/* The coordinator can receive a second checkpoint request while processing the
 * first one.  If the second request comes at a point where the coordinator has
 * broadcasted DMTCP_DO_SUSPEND message but the workers haven't replied, the
 * coordinator sends another DMTCP_DO_SUSPEND message.  The workers having
 * replied to the first DMTCP_DO_SUSPEND message (by suspending all the user
 * threads) are waiting for the next message (DMT_DO_FD_LEADER_ELECTION or
 * DMT_KILL_PEER), however they receive DMT_DO_SUSPEND message and thus exit()
 * indicating an error.
 * The fix to this problem is to introduce a global
 * variable "workersRunningAndSuspendMsgSent" which, as the name implies,
 * indicates that the DMT_DO_SUSPEND message has been sent and the coordinator
 * is waiting for replies from the workers.  If this variable is set, the
 * coordinator will not process another checkpoint request.
*/
static bool workersRunningAndSuspendMsgSent = false;

static int theCheckpointInterval = 0; /* Default is manual checkpoint only */
static bool batchMode = false;
static bool isRestarting = false;

const int STDIN_FD = fileno ( stdin );

JTIMER ( checkpoint );
JTIMER ( restart );

static dmtcp::UniquePid curCompGroup = dmtcp::UniquePid();
static int numPeers = -1;
static int curTimeStamp = -1;

static dmtcp::LookupService lookupService;

namespace
{
  static int theNextClientNumber = 1;

  class NamedChunkReader : public jalib::JChunkReader
  {
    public:
      NamedChunkReader ( const jalib::JSocket& sock
                         ,const dmtcp::UniquePid& identity
                         ,dmtcp::WorkerState state
                         ,const struct sockaddr * remote
                         ,socklen_t len
                         ,int restorePort )
          : jalib::JChunkReader ( sock,sizeof ( dmtcp::DmtcpMessage ) )
          , _identity ( identity )
          , _clientNumber ( theNextClientNumber++ )
          , _state ( state )
          , _addrlen ( len )
          , _restorePort ( restorePort )
      {
        memset ( &_addr, 0, sizeof _addr );
        memcpy ( &_addr, remote, len );
      }
      const dmtcp::UniquePid& identity() const { return _identity;}
      int clientNumber() const { return _clientNumber; }
      dmtcp::WorkerState state() const { return _state; }
      const struct sockaddr_storage* addr() const { return &_addr; }
      socklen_t addrlen() const { return _addrlen; }
      int restorePort() const { return _restorePort; }
      void setState ( dmtcp::WorkerState value ) { _state = value; }
      void progname(dmtcp::string pname){ _progname = pname; }
      dmtcp::string progname(void) const { return _progname; }
      void hostname(dmtcp::string hname){ _hostname = hname; }
      dmtcp::string hostname(void) const { return _hostname; }
    private:
      dmtcp::UniquePid _identity;
      int _clientNumber;
      dmtcp::WorkerState _state;
      struct sockaddr_storage _addr;
      socklen_t               _addrlen;
      int _restorePort;
      dmtcp::string _hostname;
      dmtcp::string _progname;
  };
}

#ifdef EXTERNAL_SOCKET_HANDLING
void dmtcp::DmtcpCoordinator::sendUnidentifiedPeerNotifications()
{
  _socketPeerLookupMessagesIterator it;
  for ( it = _socketPeerLookupMessages.begin();
        it != _socketPeerLookupMessages.end();
        ++it ) {
    DmtcpMessage msg (DMT_UNKNOWN_PEER);
    msg.conId = it->conId;
    jalib::JSocket remote(_workerSocketTable[it->from]);
    remote << msg;
    //*(it->second) << msg;

    vector<dmtcp::ConnectionIdentifier>::iterator i;
    for ( i  = workersWithExternalSockets.begin();
          i != workersWithExternalSockets.end();
          ++i) {
      if ( *i == it->from ) {
        break;
      }
    }
    if ( i == workersWithExternalSockets.end() ) {
      workersWithExternalSockets.push_back ( it->from );
    }
  }
  _socketPeerLookupMessages.clear();
}
#endif

void dmtcp::DmtcpCoordinator::handleUserCommand(char cmd, DmtcpMessage* reply /*= NULL*/)
{
  int * replyParams;
  if(reply!=NULL){
    replyParams = reply->params;
  }else{
    static int dummy[sizeof(reply->params)/sizeof(int)];
    replyParams = dummy;
  }

  JASSERT(sizeof(reply->params)/sizeof(int) >= 2); //this should be compiled out
  //default reply is 0
  replyParams[0] = DmtcpCoordinatorAPI::NOERROR;
  replyParams[1] = DmtcpCoordinatorAPI::NOERROR;

  switch ( cmd ){
  case 'b': case 'B':  // prefix blocking command, prior to checkpoint command
    JTRACE ( "blocking checkpoint beginning..." );
    blockUntilDone = true;
    replyParams[0] = 0;  // reply from prefix command will be ignored
    break;
  case 'c': case 'C':
    JTRACE ( "checkpointing..." );
    if(startCheckpoint()){
      replyParams[0] = getStatus().numPeers;
    }else{
      replyParams[0] = DmtcpCoordinatorAPI::ERROR_NOT_RUNNING_STATE;
      replyParams[1] = DmtcpCoordinatorAPI::ERROR_NOT_RUNNING_STATE;
    }
    break;
  case 'i': case 'I':
    JTRACE("setting timeout interval...");
    setTimeoutInterval ( theCheckpointInterval );
    if (theCheckpointInterval == 0)
      printf("Checkpoint Interval: Disabled (checkpoint manually instead)\n");
    else
      printf("Checkpoint Interval: %d\n", theCheckpointInterval);
    break;
  case 'l': case 'L':
  case 't': case 'T':
    JASSERT_STDERR << "Client List:\n";
    JASSERT_STDERR << "#, PROG[PID]@HOST, DMTCP-UNIQUEPID, STATE\n";
    for ( dmtcp::vector<jalib::JReaderInterface*>::iterator i = _dataSockets.begin()
            ;i!= _dataSockets.end()
            ;++i )
    {
      if ( ( *i )->socket().sockfd() != STDIN_FD )
      {
        const NamedChunkReader& cli = *((NamedChunkReader*)(*i));
        JASSERT_STDERR << cli.clientNumber()
                       << ", " << cli.progname() << "["  << cli.identity().pid() << "]@"  << cli.hostname()
                       << ", " << cli.identity()
                       << ", " << cli.state().toString()
                       << '\n';
      }
    }
    break;
  case 'f': case 'F':
    JNOTE ( "forcing restart..." );
    broadcastMessage ( DMT_FORCE_RESTART );
    break;
  case 'q': case 'Q':
  {
    JNOTE ( "killing all connected peers and quitting ..." );
    broadcastMessage ( DMT_KILL_PEER );
    /* Call to broadcastMessage only puts the messages into the write queue.
     * We actually want the messages to be written out to the respective sockets
     * so that we can then close the sockets and exit gracefully.  The following
     * loop is taken from the implementation of monitorSocket() implementation
     * in jsocket.cpp.
     *
     * Once the messages have been written out, the coordinator closes all the
     * connections and calls exit().
     */
    for ( size_t i=0; i<_writes.size(); ++i )
    {
      int fd = _writes[i]->socket().sockfd();
      if ( fd >= 0 ) {
        _writes[i]->writeOnce();
      }
    }
    JASSERT_STDERR << "DMTCP coordinator exiting... (per request)\n";
    for ( dmtcp::vector<jalib::JReaderInterface*>::iterator i = _dataSockets.begin()
        ; i!= _dataSockets.end()
        ; ++i )
    {
      (*i)->socket().close();
    }
    for ( dmtcp::vector<jalib::JSocket>::iterator i = _listenSockets.begin()
        ; i!= _listenSockets.end()
        ; ++i )
    {
      i->close();
    }
    JTRACE ("Exiting ...");
    exit ( 0 );
    break;
  }
  case 'k': case 'K':
    JNOTE ( "Killing all connected Peers..." );
    //XXX: What happens if a 'k' command is followed by a 'c' command before
    //     the *real* broadcast takes place?         --Kapil
    broadcastMessage ( DMT_KILL_PEER );
    break;
  case 'h': case 'H': case '?':
    JASSERT_STDERR << theHelpMessage;
    break;
  case 's': case 'S':
    {
      CoordinatorStatus s = getStatus();
      bool running = s.minimumStateUnanimous &&
		     s.minimumState==WorkerState::RUNNING;
      if (reply==NULL){
        printf("Status...\n");
        printf("NUM_PEERS=%d\n", s.numPeers);
        printf("RUNNING=%s\n", (running?"yes":"no"));
        fflush(stdout);
        if (!running) {
          JTRACE("raw status")(s.minimumState)(s.minimumStateUnanimous);
        }
      }else{
        replyParams[0]=s.numPeers;
        replyParams[1]=running;
      }
    }
    break;
  case ' ': case '\t': case '\n': case '\r':
    //ignore whitespace
    break;
  default:
    JTRACE("unhandled user command")(cmd);
    replyParams[0] = DmtcpCoordinatorAPI::ERROR_INVALID_COMMAND;
    replyParams[1] = DmtcpCoordinatorAPI::ERROR_INVALID_COMMAND;
  }
  return;
}

void dmtcp::DmtcpCoordinator::onData ( jalib::JReaderInterface* sock )
{
  if ( sock->socket().sockfd() == STDIN_FD )
  {
    handleUserCommand(sock->buffer()[0]);
    return;
  }
  else
  {
    NamedChunkReader * client= ( NamedChunkReader* ) sock;
    DmtcpMessage& msg = * ( DmtcpMessage* ) sock->buffer();
    msg.assertValid();
    char * extraData = 0;

    if ( msg.extraBytes > 0 )
    {
      extraData = new char[msg.extraBytes];
      sock->socket().readAll ( extraData, msg.extraBytes );
    }

    switch ( msg.type )
    {
      case DMT_OK:
      {
        WorkerState oldState = client->state();
        client->setState ( msg.state );
        WorkerState newState = minimumState();

        JTRACE ("got DMT_OK message")
          ( msg.from )( msg.state )( oldState )( newState );

        if ( oldState == WorkerState::RUNNING
                && newState == WorkerState::SUSPENDED )
        {
          // All the workers are in SUSPENDED state, now it is safe to reset
          // this flag.
          workersRunningAndSuspendMsgSent = false;

          JNOTE ( "locking all nodes" );
          broadcastMessage ( DMT_DO_FD_LEADER_ELECTION );
        }
#ifdef EXTERNAL_SOCKET_HANDLING
        if ( oldState == WorkerState::SUSPENDED
                && newState == WorkerState::FD_LEADER_ELECTION )
        {
          JNOTE ( "performing peerlookup for all sockets" );
          broadcastMessage ( DMT_DO_PEER_LOOKUP );
        }
        if ( oldState == WorkerState::FD_LEADER_ELECTION
                && newState == WorkerState::PEER_LOOKUP_COMPLETE )
        {
          if ( _socketPeerLookupMessages.empty() ) {
            JNOTE ( "draining all nodes" );
            broadcastMessage ( DMT_DO_DRAIN );
          } else {
            sendUnidentifiedPeerNotifications();
            JNOTE ( "Not all socket peers were Identified, "
                    " resuming computation without checkpointing" );
            broadcastMessage ( DMT_DO_RESUME );
          }
        }
        if ( oldState == WorkerState::PEER_LOOKUP_COMPLETE
                && newState == WorkerState::DRAINED )
        {
          JNOTE ( "checkpointing all nodes" );
          broadcastMessage ( DMT_DO_CHECKPOINT );
        }
#else
        if ( oldState == WorkerState::SUSPENDED
                && newState == WorkerState::FD_LEADER_ELECTION )
        {
          JNOTE ( "draining all nodes" );
          broadcastMessage ( DMT_DO_DRAIN );
        }
        if ( oldState == WorkerState::FD_LEADER_ELECTION
                && newState == WorkerState::DRAINED )
        {
          JNOTE ( "checkpointing all nodes" );
          broadcastMessage ( DMT_DO_CHECKPOINT );
        }
#endif

#ifdef COORD_NAMESERVICE
        if ( oldState == WorkerState::DRAINED
                && newState == WorkerState::CHECKPOINTED )
        {
          writeRestartScript();
          JNOTE ( "building name service database" );
          lookupService.reset();
          broadcastMessage ( DMT_DO_REGISTER_NAME_SERVICE_DATA );
        }
        if ( oldState == WorkerState::RESTARTING
                && newState == WorkerState::CHECKPOINTED )
        {
          JTRACE ( "resetting _restoreWaitingMessages" )
          ( _restoreWaitingMessages.size() );
          _restoreWaitingMessages.clear();

          JTIMER_STOP ( restart );

          lookupService.reset();
          JNOTE ( "building name service database (after restart)" );
          broadcastMessage ( DMT_DO_REGISTER_NAME_SERVICE_DATA );
        }
        if ( oldState == WorkerState::CHECKPOINTED
                && newState == WorkerState::NAME_SERVICE_DATA_REGISTERED ){
          JNOTE ( "entertaining queries now" );
          broadcastMessage ( DMT_DO_SEND_QUERIES );
        }
        if ( oldState == WorkerState::NAME_SERVICE_DATA_REGISTERED
                && newState == WorkerState::DONE_QUERYING ){
          JNOTE ( "refilling all nodes" );
          broadcastMessage ( DMT_DO_REFILL );
        }
        if ( oldState == WorkerState::DONE_QUERYING
                && newState == WorkerState::REFILLED )
#else
        if ( oldState == WorkerState::DRAINED
                && newState == WorkerState::CHECKPOINTED )
        {
          JNOTE ( "refilling all nodes" );
          broadcastMessage ( DMT_DO_REFILL );
          writeRestartScript();
        }
        if ( oldState == WorkerState::RESTARTING
                && newState == WorkerState::CHECKPOINTED )
        {
          JTRACE ( "resetting _restoreWaitingMessages" )
          ( _restoreWaitingMessages.size() );
          _restoreWaitingMessages.clear();

          JTIMER_STOP ( restart );

          JNOTE ( "refilling all nodes (after checkpoint)" );
          broadcastMessage ( DMT_DO_REFILL );
        }
        if ( oldState == WorkerState::CHECKPOINTED
                && newState == WorkerState::REFILLED )
#endif
        {
          JNOTE ( "restarting all nodes" );
          broadcastMessage ( DMT_DO_RESUME );

          JTIMER_STOP ( checkpoint );
          isRestarting = false;

          setTimeoutInterval( theCheckpointInterval );

          if (blockUntilDone) {
            JNOTE ( "replying to dmtcp_command:  we're done" );
	    // These were set in dmtcp::DmtcpCoordinator::onConnect in this file
	    jalib::JSocket remote ( blockUntilDoneRemote );
            remote << blockUntilDoneReply;
            remote.close();
            blockUntilDone = false;
            blockUntilDoneRemote = -1;
          }
        }
        break;
      }
      case DMT_RESTORE_WAITING:
      {
        DmtcpMessage restMsg = msg;
        restMsg.type = DMT_RESTORE_WAITING;
        memcpy ( &restMsg.restoreAddr,client->addr(),client->addrlen() );
        restMsg.restoreAddrlen = client->addrlen();
        restMsg.restorePort = client->restorePort();
        JASSERT ( restMsg.restorePort > 0 )
          ( restMsg.restorePort ) ( client->identity() );
        JASSERT ( restMsg.restoreAddrlen > 0 )
          ( restMsg.restoreAddrlen ) ( client->identity() );
        JASSERT ( restMsg.restorePid != ConnectionIdentifier::Null() )
          ( client->identity() );
        JTRACE ( "broadcasting RESTORE_WAITING" )
          (restMsg.restorePid) (restMsg.restoreAddrlen) (restMsg.restorePort);
        _restoreWaitingMessages.push_back ( restMsg );
        broadcastMessage ( restMsg );
        break;
      }
      case DMT_CKPT_FILENAME:
      {
        JASSERT ( extraData!=0 )
          .Text ( "extra data expected with DMT_CKPT_FILENAME message" );
        dmtcp::string ckptFilename;
        dmtcp::string hostname;
        ckptFilename = extraData;
        hostname = extraData + ckptFilename.length() + 1;

        JTRACE ( "recording restart info" ) ( ckptFilename ) ( hostname );
        _restartFilenames[hostname].push_back ( ckptFilename );
      }
      break;
      case DMT_USER_CMD:  // dmtcpaware API being used
        {
          JTRACE("got user command from client")
            (msg.params[0])(client->identity());
	  // Checkpointing commands should always block, to prevent
	  //   dmtcpaware checkpoint call from returning prior to checkpoint.
	  if (msg.params[0] == 'c')
            handleUserCommand( 'b', NULL );
          DmtcpMessage reply;
          reply.type = DMT_USER_CMD_RESULT;
          if (msg.params[0] == 'i' &&  msg.theCheckpointInterval > 0 ) {
            theCheckpointInterval = msg.theCheckpointInterval;
          }
          handleUserCommand( msg.params[0], &reply );
          sock->socket() << reply;
          //alternately, we could do the write without blocking:
          //addWrite(new jalib::JChunkWriter(sock->socket(), (char*)&msg,
          //                                 sizeof(DmtcpMessage)));
        }
        break;
#ifdef EXTERNAL_SOCKET_HANDLING
      case DMT_PEER_LOOKUP:
      {
        JTRACE ( "received PEER_LOOKUP msg" ) ( msg.conId );
        JASSERT ( msg.localAddrlen > 0 )
          ( msg.localAddrlen ) ( client->identity() );
        _socketPeerLookupMessagesIterator i;
        bool foundPeer = false;
        for ( i = _socketPeerLookupMessages.begin();
              i != _socketPeerLookupMessages.end();
              ++i ) {
          if ( ( msg.localAddrlen == i->localAddrlen ) &&
               ( memcmp ( (void*) &msg.localAddr,
                          (void*) &(i->remoteAddr),
                          msg.localAddrlen ) == 0 ) ) {
            _socketPeerLookupMessages.erase(i);
            foundPeer = true;
            break;
          }
        }
        if ( !foundPeer ) {
          _socketPeerLookupMessages.push_back(msg);
          _workerSocketTable[msg.from] = sock->socket().sockfd();
        }
      }
      break;
      case DMT_EXTERNAL_SOCKETS_CLOSED:
      {
        vector<dmtcp::ConnectionIdentifier>::iterator i;
        for ( i  = workersWithExternalSockets.begin();
              i != workersWithExternalSockets.end();
              ++i) {
          if ( *i == msg.from ) {
            break;
          }
        }
        JASSERT ( i != workersWithExternalSockets.end() ) ( msg.from )
          .Text ( "DMT_EXTERNAL_SOCKETS_CLOSED msg received from worker"
		  " but it never had one" );

        workersWithExternalSockets.erase(i);
        JTRACE ("(Known) External Sockets closed by worker") (msg.from);

        client->setState ( msg.state );

        if (workersWithExternalSockets.empty() == true) {
          JTRACE ( "External Sockets on all workers are closed now."
		   "  Trying to checkpoint." );
          handleUserCommand('c');
        }
      }
      break;
#endif

#ifdef COORD_NAMESERVICE
      case DMT_REGISTER_NAME_SERVICE_DATA:
      {
        JTRACE ("received REGISTER_NAME_SERVICE_DATA msg") (client->identity());
        lookupService.registerData(client->identity(), msg,
                                   (const char*) extraData);
      }
      break;
      case DMT_NAME_SERVICE_QUERY:
      {
        JTRACE ("received NAME_SERVICE_QUERY msg") (client->identity());
        lookupService.respondToQuery(client->identity(), sock->socket(), msg,
                                     (const char*) extraData);
      }
      break;
#endif
      default:
        JASSERT ( false ) ( msg.from ) ( msg.type )
		.Text ( "unexpected message from worker" );
    }

    delete[] extraData;
  }
}

void dmtcp::DmtcpCoordinator::onDisconnect ( jalib::JReaderInterface* sock )
{
  if ( sock->socket().sockfd() == STDIN_FD )
  {
    JTRACE ( "stdin closed" );
  }
  else
  {
    NamedChunkReader& client = * ( ( NamedChunkReader* ) sock );
    JNOTE ( "client disconnected" ) ( client.identity() );

    CoordinatorStatus s = getStatus();
    if( s.numPeers <= 1 ){
      if(exitOnLast){
        JNOTE ("last client exited, shutting down..");
        handleUserCommand('q');
      }
    }

//         int clientNumber = ((NamedChunkReader*)sock)->clientNumber();
//         JASSERT(clientNumber >= 0)(clientNumber);
//         _table.removeClient(clientNumber);
  }
}

void dmtcp::DmtcpCoordinator::onConnect ( const jalib::JSocket& sock,
                                          const struct sockaddr* remoteAddr,
                                          socklen_t remoteLen )
{
  // If no client is connected to Coordinator, then there can be only zero data
  // sockets OR there can be one data socket and that should be STDIN.
  if ( _dataSockets.size() == 0 ||
       ( _dataSockets.size() == 1
	 && _dataSockets[0]->socket().sockfd() == STDIN_FD ) )
  {
      //this is the first connection, do some initializations
      workersRunningAndSuspendMsgSent = false;

      setTimeoutInterval( theCheckpointInterval );

     // drop current computation group to 0
      curCompGroup = dmtcp::UniquePid(0,0,0);
      curTimeStamp = 0; // Drop timestamp to 0
      numPeers = -1; // Drop number of peers to unknown

      JTRACE ( "resetting _restoreWaitingMessages" )
        ( _restoreWaitingMessages.size() );
      _restoreWaitingMessages.clear();

      JTIMER_START ( restart );
  }

  jalib::JSocket remote ( sock );
  dmtcp::DmtcpMessage hello_remote;
  hello_remote.poison();
  JTRACE("Reading from incoming connection...");
  remote >> hello_remote;
  hello_remote.assertValid();

  if ( hello_remote.type == DMT_USER_CMD ) {
    processDmtUserCmd ( hello_remote, remote );
    return;
  } else if ( hello_remote.type == DMT_RESTART_PROCESS ) {
    if ( validateDmtRestartProcess ( hello_remote, remote ) == false )
      return;
    isRestarting = true;
  } else if ( hello_remote.type == DMT_HELLO_COORDINATOR ) {
    if ( validateWorkerProcess ( hello_remote, remote ) == false )
      return;
  } else {
    JASSERT ( false )
      .Text ( "Connect request from Unknown Remote Process Type" );
  }

  JNOTE ( "worker connected" ) ( hello_remote.from );

  if ( hello_remote.theCheckpointInterval >= 0 ) {
    int oldInterval = theCheckpointInterval;
    theCheckpointInterval = hello_remote.theCheckpointInterval;
    setTimeoutInterval ( theCheckpointInterval );
    JNOTE ( "CheckpointInterval Updated" ) ( oldInterval )
	  ( theCheckpointInterval );
  }
//     _table[hello_remote.from.pid()].setState(hello_remote.state);

  NamedChunkReader * ds = new NamedChunkReader (
      sock
      ,hello_remote.from.pid()
      ,hello_remote.state
      ,remoteAddr
      ,remoteLen
      ,hello_remote.restorePort );

  if( hello_remote.extraBytes > 0 ){
    char* extraData = new char[hello_remote.extraBytes];
    remote.readAll(extraData, hello_remote.extraBytes);
    dmtcp::string hostname = extraData;
    dmtcp::string progname = extraData + hostname.length() + 1;
    ds->progname(progname);
    ds->hostname(hostname);
    delete [] extraData;
  }


  //add this client as a chunk reader
  // in this case a 'chunk' is sizeof(DmtcpMessage)
  addDataSocket ( ds );

  if ( hello_remote.state == WorkerState::RESTARTING
          &&  _restoreWaitingMessages.size() >0 )
  {
    JTRACE ( "updating missing broadcasts for new connection" )
    ( hello_remote.from.pid() )
    ( _restoreWaitingMessages.size() );
    for ( size_t i=0; i<_restoreWaitingMessages.size(); ++i )
    {
      addWrite (
          new jalib::JChunkWriter ( sock
                                    , ( char* ) &_restoreWaitingMessages[i]
                                    , sizeof ( DmtcpMessage ) )
      );
    }
  }

  JTRACE( "END" )
  ( _dataSockets.size() ) ( _dataSockets[0]->socket().sockfd() == STDIN_FD );
}

//     WorkerNode& node = _table[hello_remote.from.pid()];
//     node.setClientNumer( ds->clientNumber() );
  /*
      if(hello_remote.state == WorkerState::RESTARTING)
      {
          node.setAddr(remoteAddr, remoteLen);
          node.setRestorePort(hello_remote.restorePort);

          JASSERT(node.addrlen() > 0)(node.addrlen());
          JASSERT(node.restorePort() > 0)(node.restorePort());
          DmtcpMessage msg;
          msg.type = DMT_RESTORE_WAITING;
          memcpy(&msg.restoreAddr,node.addr(),node.addrlen());
          msg.restoreAddrlen = node.addrlen();
          msg.restorePid.id = node.id();
          msg.restorePort = node.restorePort();
          broadcastMessage( msg );
      }*/
//}

void dmtcp::DmtcpCoordinator::processDmtUserCmd( DmtcpMessage& hello_remote,
						 jalib::JSocket& remote )
{
  //dmtcp_command doesn't handshake (it is antisocial)
  JTRACE("got user command from dmtcp_command")(hello_remote.params[0]);
  DmtcpMessage reply;
  reply.type = DMT_USER_CMD_RESULT;
  // if previous 'b' blocking prefix command had set blockUntilDone
  if (blockUntilDone && blockUntilDoneRemote == -1  &&
      hello_remote.params[0] == 'c') {
    // Reply will be done in dmtcp::DmtcpCoordinator::onData in this file.
    blockUntilDoneRemote = remote.sockfd();
    blockUntilDoneReply = reply;
    handleUserCommand( hello_remote.params[0], &reply );
  } else if ( (hello_remote.params[0] == 'i' || hello_remote.params[1] == 'I')
               && hello_remote.theCheckpointInterval >= 0 ) {
    theCheckpointInterval = hello_remote.theCheckpointInterval;
    handleUserCommand( hello_remote.params[0], &reply );
    remote << reply;
    remote.close();
  } else {
    handleUserCommand( hello_remote.params[0], &reply );
    remote << reply;
    remote.close();
  }
  return;
}

bool dmtcp::DmtcpCoordinator::validateDmtRestartProcess
	 ( DmtcpMessage& hello_remote, jalib::JSocket& remote )
{
  // This is dmtcp_restart process, connecting to get timestamp
  // and set current compGroup.

  JASSERT ( hello_remote.params[0] > 0 );

  dmtcp::DmtcpMessage hello_local ( dmtcp::DMT_RESTART_PROCESS_REPLY );

  if( curCompGroup == dmtcp::UniquePid(0,0,0) ){
    JASSERT ( minimumState() == WorkerState::UNKNOWN )
      .Text ( "Coordinator should be idle at this moment" );
    // Coordinator is free at this moment - setup all the things
    curCompGroup = hello_remote.compGroup;
    numPeers = hello_remote.params[0];
    curTimeStamp = time(NULL);
    hello_local.params[1] = 1;
    JNOTE ( "FIRST dmtcp_restart connection.  Set numPeers. Generate timestamp" )
      ( numPeers ) ( curTimeStamp ) ( curCompGroup );
  } else if ( curCompGroup != hello_remote.compGroup ) {
    // Coordinator already serving some other computation group - reject this process.
    JNOTE ("Reject incoming dmtcp_restart connection"
           " since it is not from current computation")
      ( curCompGroup ) ( hello_remote.compGroup );
    hello_local.type = dmtcp::DMT_REJECT;
    remote << hello_local;
    remote.close();
    return false;
  } else if ( numPeers != hello_remote.params[0] ) {
    // Sanity check
    JNOTE  ( "Invalid numPeers reported by dmtcp_restart process, Rejecting" )
      ( numPeers ) ( hello_remote.params[0] );

    hello_local.type = dmtcp::DMT_REJECT;
    remote << hello_local;
    remote.close();
    return false;
  } else {
    // This is a second or higher dmtcp_restart process connecting to the coordinator.
    // FIXME: Should the following be a JASSERT instead?      -- Kapil
    JWARNING ( minimumState() == WorkerState::RESTARTING );
    hello_local.params[1] = 0;
  }

  // Sent generated timestamp in local massage for dmtcp_restart process.
  hello_local.params[0] = curTimeStamp;

  remote << hello_local;

  return true;
}

bool dmtcp::DmtcpCoordinator::validateWorkerProcess
	 ( DmtcpMessage& hello_remote, jalib::JSocket& remote )
{
  dmtcp::DmtcpMessage hello_local ( dmtcp::DMT_HELLO_WORKER );

  if ( hello_remote.state == WorkerState::RESTARTING ) {
    if ( minimumState() != WorkerState::RESTARTING &&
         minimumState() != WorkerState::CHECKPOINTED ) {
      JNOTE ("Computation not in RESTARTING or CHECKPOINTED state."
	     "  Reject incoming restarting computation process.")
        ( curCompGroup ) ( hello_remote.compGroup );
      hello_local.type = dmtcp::DMT_REJECT;
      remote << hello_local;
      remote.close();
      return false;
    } else if ( hello_remote.compGroup != curCompGroup) {
      JNOTE ("Reject incoming restarting computation process"
	     " since it is not from current computation")
        ( curCompGroup ) ( hello_remote.compGroup );
      hello_local.type = dmtcp::DMT_REJECT;
      remote << hello_local;
      remote.close();
      return false;
    }
    // dmtcp_restart already connected and compGroup created.
    // Computation process connection
    JASSERT ( curTimeStamp != 0 );

    JTRACE("Connection from (restarting) computation process")
      ( curCompGroup ) ( hello_remote.compGroup ) ( minimumState() );

    remote << hello_local;

    // NOTE: Sending the same message twice. We want to make sure that the
    // worker process receives/processes the first messages as soon as it
    // connects to the coordinator. The second message will be processed in
    // postRestart routine in DmtcpWorker.
    //
    // The reason to do this is the following. The dmtcp_restart process
    // connects to the coordinator at a very early stage. Later on, before
    // exec()'ing into mtcp_restart, it reconnects to the coordinator using
    // it's original UniquiePid and closes the earlier socket connection.
    // However, the coordinator might process the disconnect() before it
    // processes the connect() which would lead to a situation where the
    // coordinator is not connected to any worker processes. The coordinator
    // would now process the connect() and may reject the worker because the
    // worker state is RESTARTING, but the minimumState() is UNKNOWN.
    remote << hello_local;

  } else if ( hello_remote.state == WorkerState::RUNNING ) {
    CoordinatorStatus s = getStatus();
    // If some of the processes are not in RUNNING state OR if the SUSPEND
    // message has been sent, REJECT.
    if ( s.numPeers > 0 &&
         ( s.minimumState != WorkerState::RUNNING ||
           s.minimumStateUnanimous == false       ||
           workersRunningAndSuspendMsgSent == true) ) {
      JNOTE  ( "Current Computation not in RUNNING state."
	       "  Refusing to accept new connections.")
        ( curCompGroup ) ( hello_remote.from.pid() );
      hello_local.type = dmtcp::DMT_REJECT;
      remote << hello_local;
      remote.close();
      return false;
    } else if ( hello_remote.compGroup != UniquePid() ) {
      // New Process trying to connect to Coordinator but already has compGroup
      JNOTE  ( "New Process, but already has computation group,\n"
               "OR perhaps a different DMTCP_PREFIX_ID.  Rejecting." )
        (hello_remote.compGroup);

      hello_local.type = dmtcp::DMT_REJECT;
      remote << hello_local;
      remote.close();
      return false;
    } else {
      // If first process, create the new computation group
      if ( curCompGroup == UniquePid(0,0,0) ) {
        // Connection of new computation.
        curCompGroup = hello_remote.from.pid();
        curTimeStamp = 0;
        numPeers = -1;
        JTRACE ( "First process connected.  Creating new computation group" )
	       (curCompGroup );
      } else {
        JTRACE ( "New Process Connected" ) ( hello_remote.from.pid() );
      }
      remote << hello_local;
    }
  } else {
    JASSERT ( false ) .Text ( "Invalid Worker Type" );
    return false;
  }

  return true;
}

void dmtcp::DmtcpCoordinator::onTimeoutInterval()
{
  if ( theCheckpointInterval > 0 )
    startCheckpoint();
}


bool dmtcp::DmtcpCoordinator::startCheckpoint()
{
  CoordinatorStatus s = getStatus();
  if ( s.minimumState == WorkerState::RUNNING
       && !workersRunningAndSuspendMsgSent )
  {
    JTIMER_START ( checkpoint );
    _restartFilenames.clear();
    JNOTE ( "starting checkpoint, suspending all nodes" )( s.numPeers );
    // Pass number of connected peers to all clients
    broadcastMessage ( DMT_DO_SUSPEND , curCompGroup, getStatus().numPeers );

    // Suspend Message has been sent but the workers are still in running
    // state.  If the coordinator receives another checkpoint request from user
    // at this point, it should fail.
    workersRunningAndSuspendMsgSent = true;
    return true;
  } else {
    if (s.numPeers > 0) {
      JTRACE ( "delaying checkpoint, workers not ready" ) ( s.minimumState )
	     ( s.numPeers );
    }
    return false;
  }
}

void dmtcp::DmtcpCoordinator::broadcastMessage ( DmtcpMessageType type,
    dmtcp::UniquePid compGroup = dmtcp::UniquePid(), int param1 = -1 )
{
  DmtcpMessage msg;
  msg.type = type;
  if( param1 > 0 ){
    msg.params[0] = param1;
    msg.compGroup = compGroup;
  }
  broadcastMessage ( msg );
  JTRACE ("sending message")( type );
}

void dmtcp::DmtcpCoordinator::broadcastMessage ( const DmtcpMessage& msg )
{
  for ( dmtcp::vector<jalib::JReaderInterface*>::iterator i
	= _dataSockets.begin() ; i!= _dataSockets.end() ; i++ )
  {
    if ( ( *i )->socket().sockfd() != STDIN_FD )
      addWrite ( new jalib::JChunkWriter ( ( *i )->socket(),
					   ( char* ) &msg,
					   sizeof ( DmtcpMessage ) ) );
  }
}

dmtcp::DmtcpCoordinator::CoordinatorStatus dmtcp::DmtcpCoordinator::getStatus() const
{
  CoordinatorStatus status;
  const static int INITIAL = WorkerState::_MAX;
  int m = INITIAL;
  int count = 0;
  bool unanimous = true;
  for ( const_iterator i = _dataSockets.begin()
      ; i != _dataSockets.end()
      ; ++i )
  {
    if ( ( *i )->socket().sockfd() != STDIN_FD )
    {
      int cliState = ((NamedChunkReader*)*i)->state().value();
      count++;
      unanimous = unanimous && (m==cliState || m==INITIAL);
      if ( cliState < m ) m = cliState;
    }
  }

  status.minimumState = ( m==INITIAL ? WorkerState::UNKNOWN
			  : (WorkerState::eWorkerState)m );
  if( status.minimumState == WorkerState::CHECKPOINTED &&
      isRestarting && count < numPeers ){
    JTRACE("minimal state counted as CHECKPOINTED but not all processes"
	   " are connected yet.  So we wait.") ( numPeers ) ( count );
    status.minimumState = WorkerState::RESTARTING;
  }
  status.minimumStateUnanimous = unanimous;
  status.numPeers = count;
  return status;
}

void dmtcp::DmtcpCoordinator::writeRestartScript()
{
  const char* dir = getenv ( ENV_VAR_CHECKPOINT_DIR );
  if(dir==NULL) dir = ".";
  dmtcp::ostringstream o1, o2;
  dmtcp::string filename, uniqueFilename;

  o1 << dmtcp::string(dir) << "/"
     << RESTART_SCRIPT_BASENAME << RESTART_SCRIPT_EXT;
  filename = o1.str();

  o2 << dmtcp::string(dir) << "/"
     << RESTART_SCRIPT_BASENAME << "_" << curCompGroup << RESTART_SCRIPT_EXT;
  uniqueFilename = o2.str();

  const bool isSingleHost = (_restartFilenames.size() == 1);

  dmtcp::map< dmtcp::string, dmtcp::vector<dmtcp::string> >::const_iterator host;
  dmtcp::vector<dmtcp::string>::const_iterator file;

  char hostname[80];
  gethostname ( hostname, 80 );

  JTRACE ( "writing restart script" ) ( uniqueFilename );

  FILE* fp = fopen ( uniqueFilename.c_str(),"w" );
  JASSERT ( fp!=0 )(JASSERT_ERRNO)( uniqueFilename )
    .Text ( "failed to open file" );

  fprintf ( fp, "%s", theRestartScriptHeader );
  fprintf ( fp, "%s", theRestartScriptUsage );

  fprintf ( fp, "coord_host=$"ENV_VAR_NAME_HOST"\n"
                "if test -z \"$" ENV_VAR_NAME_HOST "\"; then\n"
                "  coord_host=%s\nfi\n\n", hostname );
  fprintf ( fp, "coord_port=$"ENV_VAR_NAME_PORT"\n"
                "if test -z \"$" ENV_VAR_NAME_PORT "\"; then\n"
                "  coord_port=%d\nfi\n\n", thePort );
  fprintf ( fp, "checkpoint_interval=$"ENV_VAR_CKPT_INTR"\n"
                "if test -z \"$" ENV_VAR_CKPT_INTR "\"; then\n"
                "  checkpoint_interval=%d\nfi\n\n", theCheckpointInterval );
  if ( batchMode )
    fprintf ( fp, "maybebatch='--batch'\n\n" );
  else
    fprintf ( fp, "maybebatch=\n\n" );

  fprintf ( fp, "# Number of hosts in the computation = %zd\n",
            _restartFilenames.size() );
  fprintf ( fp, "# Number of processes in the computation = %d\n\n",
            getStatus().numPeers );

  if ( isSingleHost ) {
    JTRACE ( "Single HOST");

    host=_restartFilenames.begin();
    dmtcp::ostringstream o;
    for ( file=host->second.begin(); file!=host->second.end(); ++file ) {
      o << " " << *file;
    }

    fprintf ( fp, "%s", theRestartScriptCmdlineArgHandler );
    fprintf ( fp, "DMTCP_RESTART=dmtcp_restart\n" );
    fprintf ( fp, "which dmtcp_restart > /dev/null \\\n" \
                  " || DMTCP_RESTART=%s/dmtcp_restart\n\n",
                  jalib::Filesystem::GetProgramDir().c_str());
    fprintf ( fp, "if [ ! -z \"$DMTCP_RESTART_DIR\" ]; then\n"
                  "  new_ckpt_names=\"\"\n"
                  "  names=\"%s\"\n"
                  "  for tmp in $names; do\n"
                  "    new_ckpt_names=\"$DMTCP_RESTART_DIR/`basename $tmp` $new_ckpt_names\"\n"
                  "  done\n"
                  "fi\n", o.str().c_str());

    fprintf ( fp,
              "if [ ! -z \"$maybebatch\" ]; then\n"
              "  if [ ! -z \"$DMTCP_RESTART_DIR\" ]; then\n"
              "    exec $DMTCP_RESTART $maybebatch $maybejoin --interval \"$checkpoint_interval\"\\\n"
              "       $new_ckpt_names\n"
              "  else\n"
              "    exec $DMTCP_RESTART $maybebatch $maybejoin --interval \"$checkpoint_interval\"\\\n"
              "       %s\n"
              "  fi\n"
              "else\n"
              "  if [ ! -z \"$DMTCP_RESTART_DIR\" ]; then\n"
              "    exec $DMTCP_RESTART --host \"$coord_host\" --port \"$coord_port\" $maybebatch\\\n"
              "      $maybejoin --interval \"$checkpoint_interval\"\\\n"
              "        $new_ckpt_names\n"
              "  else\n"
              "    exec $DMTCP_RESTART --host \"$coord_host\" --port \"$coord_port\" $maybebatch\\\n"
              "      $maybejoin --interval \"$checkpoint_interval\"\\\n"
              "        %s\n"
              "  fi\n"
              "fi\n", o.str().c_str(), o.str().c_str() );
  }
  else
  {
    fprintf ( fp, "%s",
              "worker_ckpts_regexp=\'[^:]*::[ \\t\\n]*\\([^ \\t\\n]\\+\\)[ \\t\\n]*:\\([a-z]\\+\\):[ \\t\\n]*\\([^:]\\+\\)\'\n\n"
              "# SYNTAX:\n"
              "#  :: <HOST> :<MODE>: <CHECKPOINT_IMAGE> ...\n"
              "# Host names and filenames must not include \':\'\n"
              "# At most one fg (foreground) mode allowed; it must be last.\n"
              "# \'maybexterm\' and \'maybebg\' are set from <MODE>.\n"
              "worker_ckpts=\'" );

    for ( host=_restartFilenames.begin(); host!=_restartFilenames.end(); ++host )
    {
      fprintf ( fp, "\n :: %s :bg:", host->first.c_str() );
      for ( file=host->second.begin(); file!=host->second.end(); ++file )
      {
        fprintf ( fp," %s", file->c_str() );
      }
    }

    fprintf ( fp, "%s", "\n\'\n\n\n" );


    fprintf ( fp, "%s", theRestartScriptCmdlineArgHandler );

    fprintf ( fp, "%s",
              "worker_hosts=\\\n"
              "`echo $worker_ckpts | sed -e \'s/\'\"$worker_ckpts_regexp\"\'/\\1 /g\'`\n"
              "restart_modes=\\\n"
              "`echo $worker_ckpts | sed -e \'s/\'\"$worker_ckpts_regexp\"\'/: \\2/g\'`\n"
              "ckpt_files_groups=\\\n"
              "`echo $worker_ckpts | sed -e \'s/\'\"$worker_ckpts_regexp\"\'/: \\3/g\'`\n"
              "\n"
              "if [ ! -z \"$hostfile\" ]; then\n"
              "  worker_hosts=`cat \"$hostfile\" | sed -e \'s/#.*//\' -e \'s/[ \\t\\r]*//\' -e \'/^$/ d\'`\n"
              "fi\n\n"

              "localhost_ckpt_files_group=\n\n"

              "num_worker_hosts=`echo $worker_hosts | wc -w`\n\n"

              "maybejoin=\n"
              "if [ \"$num_worker_hosts\" != \"1\" ]; then\n"
              "  maybejoin='--join'\n"
              "fi\n\n"

              "for worker_host in $worker_hosts\n"
              "do\n\n"
              "  ckpt_files_group=`echo $ckpt_files_groups | sed -e \'s/[^:]*:[ \\t\\n]*\\([^:]*\\).*/\\1/\'`\n"
              "  ckpt_files_groups=`echo $ckpt_files_groups | sed -e \'s/[^:]*:[^:]*//\'`\n"
              "\n"
              "  mode=`echo $restart_modes | sed -e \'s/[^:]*:[ \\t\\n]*\\([^:]*\\).*/\\1/\'`\n"
              "  restart_modes=`echo $restart_modes | sed -e \'s/[^:]*:[^:]*//\'`\n\n"
              "  maybexterm=\n"
              "  maybebg=\n"
              "  case $mode in\n"
              "    bg) maybebg=\'bg\';;\n"
              "    xterm) maybexterm=xterm;;\n"
              "    fg) ;;\n"
              "    *) echo \"WARNING: Unknown Mode\";;\n"
              "  esac\n\n"
              "  if [ -z \"$ckpt_files_group\" ]; then\n"
              "    break;\n"
              "  fi\n\n"

              "  new_ckpt_files_group=\"\"\n"
              "  for tmp in $ckpt_files_group\n"
              "  do\n"
              "      if  [ ! -z \"$DMTCP_RESTART_DIR\" ]; then\n"
              "        tmp=$DMTCP_RESTART_DIR/`basename $tmp`\n"
              "      fi\n"
              "      new_ckpt_files_group=\"$new_ckpt_files_group $tmp\"\n"
              "  done\n\n"

              "  if [ `hostname` == \"$worker_host\" -o \"$num_worker_hosts\" == \"1\" ]; then\n"
              "    localhost_ckpt_files_group=\"$new_ckpt_files_group\"\n"
              "    continue\n"
              "  fi\n\n"

              "  if [ -z $maybebg ]; then\n"
              "    $maybexterm /usr/bin/ssh -t \"$worker_host\" \\\n"
              "      "DMTCP_RESTART_CMD" --host \"$coord_host\" --port \"$coord_port\" $maybebatch\\\n"
              "        --join --interval \"$checkpoint_interval\" $new_ckpt_files_group\n"
              "  else\n"
              "    $maybexterm /usr/bin/ssh \"$worker_host\" \\\n"
              // In OpenMPI 1.4, without this (sh -c ...), orterun hangs at the
              // end of the computation until user presses enter key.
              "      \"/bin/sh -c \'"DMTCP_RESTART_CMD" --host $coord_host --port $coord_port $maybebatch\\\n"
              "        --join --interval \"$checkpoint_interval\" $new_ckpt_files_group\'\" &\n"
              "  fi\n\n"
              "done\n\n");

    fprintf ( fp, "DMTCP_RESTART=dmtcp_restart\n" );
    fprintf ( fp, "which dmtcp_restart > /dev/null \\\n" \
                  " || DMTCP_RESTART=%s/dmtcp_restart\n\n",
                  jalib::Filesystem::GetProgramDir().c_str());

    fprintf ( fp, "%s",
              "if [ -n \"$localhost_ckpt_files_group\" ]; then\n"
              "exec dmtcp_restart --host \"$coord_host\" --port \"$coord_port\" $maybebatch\\\n"
              "  $maybejoin --interval \"$checkpoint_interval\" $localhost_ckpt_files_group\n"
              "fi\n\n"


              "#wait for them all to finish\n"
              "wait\n");
  }

  fclose ( fp );
  {
    /* Set execute permission for user. */
    struct stat buf;
    stat ( uniqueFilename.c_str(), &buf );
    chmod ( uniqueFilename.c_str(), buf.st_mode | S_IXUSR );
    // Create a symlink from
    //   dmtcp_restart_script.sh -> dmtcp_restart_script_<curCompId>.sh
    unlink ( filename.c_str() );
    JTRACE("linking \"dmtcp_restart_script.sh\" filename to uniqueFilename")
	  (filename)(uniqueFilename);
    // FIXME:  Handle error case of symlink()
    JWARNING( 0 == symlink ( uniqueFilename.c_str(), filename.c_str() ) );
  }
  _restartFilenames.clear();
}

static void SIGINTHandler(int signum)
{
  prog.handleUserCommand('q');
}

static void setupSIGINTHandler()
{
  struct sigaction action;
  action.sa_handler = SIGINTHandler;
  sigemptyset ( &action.sa_mask );
  action.sa_flags = 0;
  sigaction ( SIGINT, &action, NULL );
}

#define shift argc--; argv++

int main ( int argc, char** argv )
{
  dmtcp::DmtcpMessage::setDefaultCoordinator ( dmtcp::UniquePid::ThisProcess() );

  //parse port
  thePort = DEFAULT_PORT;
  const char* portStr = getenv ( ENV_VAR_NAME_PORT );
  if ( portStr != NULL ) thePort = jalib::StringToInt ( portStr );

  bool background = false;

  shift;
  while(argc > 0){
    dmtcp::string s = argv[0];
    if(s=="-h" || s=="--help"){
      fprintf(stderr, theUsage, DEFAULT_PORT);
      return 1;
    }else if(s=="--exit-on-last"){
      exitOnLast = true;
      shift;
    }else if(s=="--background"){
      background = true;
      shift;
    }else if(s=="--batch"){
      batchMode = true;
      shift;
    }else if(argc>1 && (s == "-i" || s == "--interval")){
      setenv(ENV_VAR_CKPT_INTR, argv[1], 1);
      shift; shift;
    }else if(argc>1 && (s == "-p" || s == "--port")){
      thePort = jalib::StringToInt( argv[1] );
      shift; shift;
    }else if(argc>1 && (s == "-c" || s == "--ckptdir")){
      setenv(ENV_VAR_CHECKPOINT_DIR, argv[1], 1);
      shift; shift;
    }else if(argc>1 && (s == "-t" || s == "--tmpdir")){
      setenv(ENV_VAR_TMPDIR, argv[1], 1);
      shift; shift;
    }else if(argc == 1){ //last arg can be port
      char *endptr;
      long x = strtol(argv[0], &endptr, 10);
      if ((ssize_t)strlen(argv[0]) != endptr - argv[0]) {
        fprintf(stderr, theUsage, DEFAULT_PORT);
        return 1;
      } else {
        thePort = jalib::StringToInt( argv[0] );
        shift;
      }
      x++, x--; // to suppress unused variable warning
    }else{
      fprintf(stderr, theUsage, DEFAULT_PORT);
      return 1;
    }
  }

  JASSERT ( ! (background && batchMode) )
    .Text ( "--background and --batch can't be specified together");

  dmtcp::UniquePid::setTmpDir(getenv(ENV_VAR_TMPDIR));

  dmtcp::Util::initializeLogFile();

  JTRACE ( "New DMTCP coordinator starting." )
    ( dmtcp::UniquePid::ThisProcess() );

  if ( thePort < 0 )
  {
    fprintf(stderr, theUsage, DEFAULT_PORT);
    return 1;
  }

  jalib::JServerSocket* sock;
  /*Test if the listener socket is already open*/
  if ( fcntl(PROTECTED_COORD_FD, F_GETFD) != -1 ) {
    sock = new jalib::JServerSocket ( PROTECTED_COORD_FD );
    JASSERT ( sock->port() != -1 ) .Text ( "Invalid listener socket" );
    JTRACE ( "Using already created listener socker" ) ( sock->port() );
  } else {

    errno = 0;
    sock = new jalib::JServerSocket ( jalib::JSockAddr::ANY, thePort );
    JASSERT ( sock->isValid() ) ( thePort ) ( JASSERT_ERRNO )
      .Text ( "Failed to create listen socket."
       "\nIf msg is \"Address already in use\", this may be an old coordinator."
       "\nKill default coordinator and try again:  dmtcp_command -q"
       "\nIf that fails, \"pkill -9 dmtcp_coord\","
       " and try again in a minute or so." );
  }

  thePort = sock->port();

  if ( batchMode && getenv ( ENV_VAR_CKPT_INTR ) == NULL ) {
    setenv(ENV_VAR_CKPT_INTR, "3600", 1);
  }
  //parse checkpoint interval
  const char* interval = getenv ( ENV_VAR_CKPT_INTR );
  if ( interval != NULL )
    theCheckpointInterval = jalib::StringToInt ( interval );

#if 0
  JASSERT_STDERR <<
    "dmtcp_coordinator starting..." <<
    "\n    Port: " << thePort <<
    "\n    Checkpoint Interval: ";
  if(theCheckpointInterval==0)
    JASSERT_STDERR << "disabled (checkpoint manually instead)";
  else
    JASSERT_STDERR << theCheckpointInterval;
  JASSERT_STDERR  <<
    "\n    Exit on last client: " << exitOnLast << "\n";
#else
    fprintf(stderr, "dmtcp_coordinator starting..."
    "\n    Port: %d"
    "\n    Checkpoint Interval: ", thePort);
  if(theCheckpointInterval==0)
    fprintf(stderr, "disabled (checkpoint manually instead)");
  else
    fprintf(stderr, "%d", theCheckpointInterval);
  fprintf(stderr, "\n    Exit on last client: %d\n", exitOnLast);
#endif

  if(background){
    JASSERT_STDERR  << "Backgrounding...\n";
    JASSERT(dup2(open("/dev/null",O_RDWR), 0)==0);
    fflush(stdout);
    JASSERT(close(1)==0);
    JASSERT(open("/dev/null", O_WRONLY)==1);
    fflush(stderr);
    JASSERT (close(2) == 0 && dup2(1,2) == 2) .Text( "Can't print to stderr");
    close(JASSERT_STDERR_FD);
    dup2(2, JASSERT_STDERR_FD);
    if(fork()>0){
      JTRACE ( "Parent Exiting after fork()" );
      exit(0);
    }
    //pid_t sid = setsid();
  } else if ( batchMode ) {
    JASSERT_STDERR  << "Going into Batch Mode...\n";
    close(0);
    close(1);
    close(2);
    close(JASSERT_STDERR_FD);

    JASSERT(open("/dev/null", O_WRONLY)==0);

    JASSERT(dup2(0, 1) == 1);
    JASSERT(dup2(0, 2) == 2);
    JASSERT(dup2(0, JASSERT_STDERR_FD) == JASSERT_STDERR_FD);

  } else {
    JASSERT_STDERR  <<
      "Type '?' for help." <<
      "\n\n";
  }

  /* We setup the signal handler for SIGINT so that it would send the
   * DMT_KILL_PEER message to all the connected peers before exiting.
   */
  setupSIGINTHandler();
  prog.addListenSocket ( *sock );
  if(!background && !batchMode)
    prog.addDataSocket ( new jalib::JChunkReader ( STDIN_FD , 1 ) );

  prog.monitorSockets ( theCheckpointInterval );
  return 0;
}
