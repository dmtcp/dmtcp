/***************************************************************************
 *   Copyright (C) 2006 by Jason Ansel                                     *
 *   jansel@ccs.neu.edu                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "dmtcpcoordinator.h"
#include "constants.h"
#include "jconvert.h"
#include "dmtcpmessagetypes.h"
#include "dmtcpworker.h"
#include <stdio.h>
#include <sys/stat.h>
#include "jtimer.h"
#include <algorithm>
#undef min
#undef max


static const char* theHelpMessage =
  "COMMANDS:\n"
  "  l : List connected nodes\n"
  "  s : Print status message\n"
  "  c : Checkpoint all nodes\n"
  "  f : Force a restart even if there are missing nodes (debugging only)\n"
  "  k : Kill all nodes\n"
  "  q : Kill all nodes and quit\n"
  "  ? : Show this message\n"
  "\n"
;

static const char* theUsage =
  "USAGE: dmtcp_coordinator [port]\n"
  "OPTIONS: (Environment variables):\n"
  "  - DMTCP_CHECKPOINT_INTERVAL=<time in seconds> (default: 0, disabled)\n"
  "  - DMTCP_CHECKPOINT_DIR=<where restart script is written> (default: ./)\n"
  "  - DMTCP_PORT=<coordinator listener port> (default: %d)\n"
  "COMMANDS:\n"
  "  (type '?<return>' at runtime for list)\n"
;


int theCheckpointInterval = -1;

const int STDIN_FD = fileno ( stdin );

JTIMER ( checkpoint );
JTIMER ( restart );

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
      void progname(std::string pname){ _progname = pname; }
      std::string progname(void) const { return _progname; }
      void hostname(std::string hname){ _hostname = hname; }
      std::string hostname(void) const { return _hostname; }
    private:
      dmtcp::UniquePid _identity;
      int _clientNumber;
      dmtcp::WorkerState _state;
      struct sockaddr_storage _addr;
      socklen_t               _addrlen;
      int _restorePort;
      std::string _hostname;
      std::string _progname;
  };
}

void dmtcp::DmtcpCoordinator::handleUserCommand(char cmd, DmtcpMessage* reply /*= NULL*/){
  int * replyParams;
  if(reply!=NULL){
    replyParams = reply->params;
  }else{
    static int dummy[sizeof(reply->params)/sizeof(int)];
    replyParams = dummy;
  }

  JASSERT(sizeof(reply->params)/sizeof(int) >= 2); //this should be compiled out
  //default reply is 0
  replyParams[0] = NOERROR;
  replyParams[1] = NOERROR;

  switch ( cmd ){
  case 'c': case 'C':
    if(startCheckpoint()){
      replyParams[0] = getStatus().numPeers;
    }else{
      replyParams[0] = ERROR_NOT_RUNNING_STATE;
      replyParams[1] = ERROR_NOT_RUNNING_STATE;
    }
    break;
  case 'l': case 'L':
  case 't': case 'T':
    JASSERT_STDERR << "Client List:\n";
    JASSERT_STDERR << "#, PROG[PID]@HOST, DMTCP-UNIQUEPID, STATE\n";
    for ( std::vector<jalib::JReaderInterface*>::iterator i = _dataSockets.begin()
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
    JASSERT_STDERR << "exiting... (per request)\n";
    exit ( 0 );
    break;
  case 'k': case 'K':
    JNOTE ( "Killing all connected Peers..." );
    broadcastMessage ( DMT_KILL_PEER );
    break;
  case 'h': case 'H': case '?':
    JASSERT_STDERR << theHelpMessage;
    break;
  case 's': case 'S':
    {
      CoordinatorStatus s = getStatus();
      bool running= s.minimumStateUnanimous && s.minimumState==WorkerState::RUNNING;
      if(reply==NULL){
        printf("Status...\n");
        printf("NUM_PEERS=%d\n", s.numPeers);
        printf("RUNNING=%s\n", (running?"yes":"no"));
        fflush(stdout);
        if(!running) JTRACE("raw status")(s.minimumState)(s.minimumStateUnanimous);
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
    replyParams[0] = ERROR_INVALID_COMMAND;
    replyParams[1] = ERROR_INVALID_COMMAND;
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

        JTRACE ("got DMT_OK message")( msg.from )( msg.state )( oldState )( newState );

        if ( oldState == WorkerState::RUNNING
                && newState == WorkerState::SUSPENDED )
        {
          JNOTE ( "locking all nodes" );
          broadcastMessage ( DMT_DO_LOCK_FDS );
        }
        if ( oldState == WorkerState::SUSPENDED
                && newState == WorkerState::LOCKED )
        {
          JNOTE ( "draining all nodes" );
          broadcastMessage ( DMT_DO_DRAIN );
        }
        if ( oldState == WorkerState::LOCKED
                && newState == WorkerState::DRAINED )
        {
          JNOTE ( "checkpointing all nodes" );
          broadcastMessage ( DMT_DO_CHECKPOINT );
        }
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
        {
          JNOTE ( "restarting all nodes" );
          broadcastMessage ( DMT_DO_RESUME );

          JTIMER_STOP ( checkpoint );
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
        JASSERT ( restMsg.restorePort > 0 ) ( restMsg.restorePort ) ( client->identity() );
        JASSERT ( restMsg.restoreAddrlen > 0 ) ( restMsg.restoreAddrlen ) ( client->identity() );
        JASSERT ( restMsg.restorePid != ConnectionIdentifier::Null() ) ( client->identity() );
        JTRACE ( "broadcasting RESTORE_WAITING" )( restMsg.restorePid )( restMsg.restoreAddrlen )( restMsg.restorePort );
        _restoreWaitingMessages.push_back ( restMsg );
        broadcastMessage ( restMsg );
        break;
      }
      case DMT_CKPT_FILENAME:
      {
        JASSERT ( extraData!=0 ).Text ( "extra data expected with DMT_CKPT_FILENAME message" );
        std::string ckptFilename;
        std::string hostname;
        ckptFilename = extraData;
        hostname = extraData + ckptFilename.length() + 1;

        JTRACE ( "recording restart info" ) ( ckptFilename ) ( hostname );
        _restartFilenames[hostname].push_back ( ckptFilename );
      }
      break;
      case DMT_USER_CMD:
        {
          JTRACE("got user command from client")(msg.params[0])(client->identity());
          DmtcpMessage reply;
          reply.type = DMT_USER_CMD_RESULT;
          handleUserCommand( msg.params[0], &reply );
          sock->socket() << reply;
          //alternately, we could do the write without blocking:
          //addWrite(new jalib::JChunkWriter(sock->socket(), (char*)&msg, sizeof(DmtcpMessage)));
        }
        break;
      default:
        JASSERT ( false ) ( msg.from ) ( msg.type ).Text ( "unexpected message from worker" );
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


//         int clientNumber = ((NamedChunkReader*)sock)->clientNumber();
//         JASSERT(clientNumber >= 0)(clientNumber);
//         _table.removeClient(clientNumber);
  }
}

void dmtcp::DmtcpCoordinator::onConnect ( const jalib::JSocket& sock,  const struct sockaddr* remoteAddr,socklen_t remoteLen )
{
  if ( _dataSockets.size() <= 1 )
  {
    if ( _dataSockets.size() == 0
            || _dataSockets[0]->socket().sockfd() == STDIN_FD )
    {
      //this is the first connection

      JTRACE ( "resetting _restoreWaitingMessages" )
      ( _restoreWaitingMessages.size() );
      _restoreWaitingMessages.clear();

      JTIMER_START ( restart );
    }
  }



  jalib::JSocket remote ( sock );
  dmtcp::DmtcpMessage hello_local, hello_remote;
  hello_remote.poison();
  hello_local.type = dmtcp::DMT_HELLO_WORKER;
  JTRACE("Reading from incoming connection...");
  remote >> hello_remote;
  hello_remote.assertValid();

  //dmtcp_command doesn't hanshake (it is antisocial)
  if(hello_remote.type == DMT_USER_CMD){
    JTRACE("got user command from dmtcp_command")(hello_remote.params[0]);
    DmtcpMessage reply;
    reply.type = DMT_USER_CMD_RESULT;
    handleUserCommand( hello_remote.params[0], &reply );
    remote << reply;
    remote.close();
    return;
  }

  remote << hello_local;
  JASSERT ( hello_remote.type == dmtcp::DMT_HELLO_COORDINATOR );
  JNOTE ( "worker connected" )
  ( hello_remote.from );
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
    std::string hostname = extraData;
    std::string progname = extraData + hostname.length() + 1;
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
}

void dmtcp::DmtcpCoordinator::onTimeoutInterval()
{
  if ( theCheckpointInterval > 0 )
    startCheckpoint();
}


bool dmtcp::DmtcpCoordinator::startCheckpoint()
{
  CoordinatorStatus s = getStatus();
  if ( s.minimumState == WorkerState::RUNNING )
  {
    JTIMER_START ( checkpoint );
    _restartFilenames.clear();
    JNOTE ( "starting checkpoint, suspending all nodes" )( s.numPeers );
    broadcastMessage ( DMT_DO_SUSPEND );
    return true;
  }
  else
  {
    JTRACE ( "delaying checkpoint, workers not ready" ) ( s.minimumState )( s.numPeers );
    return false;
  }
}
dmtcp::DmtcpWorker& dmtcp::DmtcpWorker::instance()
{
  JASSERT ( false ).Text ( "This method is only available on workers" );
  return * ( ( DmtcpWorker* ) 0 );
}
const dmtcp::UniquePid& dmtcp::DmtcpWorker::coordinatorId() const
{
  JASSERT ( false ).Text ( "This method is only available on workers" );
  return * ( ( UniquePid* ) 0 );
}



void dmtcp::DmtcpCoordinator::broadcastMessage ( DmtcpMessageType type )
{
  DmtcpMessage msg;
  msg.type = type;
  broadcastMessage ( msg );
}

void dmtcp::DmtcpCoordinator::broadcastMessage ( const DmtcpMessage& msg )
{
  for ( std::vector<jalib::JReaderInterface*>::iterator i = _dataSockets.begin()
          ;i!= _dataSockets.end()
          ;++i )
  {
    if ( ( *i )->socket().sockfd() != STDIN_FD )
      addWrite ( new jalib::JChunkWriter ( ( *i )->socket(), ( char* ) &msg,sizeof ( DmtcpMessage ) ) );
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

  status.minimumState = (m==INITIAL ? WorkerState::UNKNOWN : ( WorkerState::eWorkerState ) m);
  status.minimumStateUnanimous = unanimous;
  status.numPeers = count;
  return status;
}

void dmtcp::DmtcpCoordinator::writeRestartScript()
{
  const char* dir = getenv ( ENV_VAR_CHECKPOINT_DIR );
  if(dir==NULL) dir = ".";
  std::string filename = std::string(dir)+"/"+RESTART_SCRIPT_NAME;

  std::map< std::string, std::vector<std::string> >::const_iterator host;
  std::vector<std::string>::const_iterator file;
  char hostname[80];
  gethostname ( hostname,80 );
  JTRACE ( "writing restart script" ) ( filename );
  FILE* fp = fopen ( filename.c_str(),"w" );
  JASSERT ( fp!=0 ) ( filename ).Text ( "failed to open file" );
  fprintf ( fp, "%s", "#!/bin/bash \nset -m # turn on job control\n\n"
            "#This script launches all the restarts in the background.\n"
            "#Suggestions for editing:\n"
            "#  1. For those processes executing on the localhost, remove 'ssh <hostname>' from the start of the line. \n"
            "#  2. If using ssh, verify that ssh does not require passwords or other prompts.\n"
            "#  3. Verify that the dmtcp_restart command is in your path on all hosts.\n"
            "#  4. Verify DMTCP_HOST and DMTCP_PORT match the location of the dmtcp_coordinator.\n"
            "#     If necessary, add 'DMTCP_PORT=<dmtcp_coordinator port>' after 'DMTCP_HOST=<...>'.\n"
            "#  5. Remove the '&' from a line if that process reads STDIN.\n"
            "#     If multiple processes read STDIN then prefix the line with 'xterm -hold -e' and put '&' at the end of the line.\n"
            "#  6. Processes on same host can be restarted with single dmtcp_restart command.\n"
            "\n"
            "\n"
             );

  for ( host=_restartFilenames.begin(); host!=_restartFilenames.end(); ++host )
  {
    fprintf ( fp,"ssh %s env " ENV_VAR_NAME_ADDR "=%s " DMTCP_RESTART_CMD " ",
              host->first.c_str(), hostname );
    for ( file=host->second.begin(); file!=host->second.end(); ++file )
    {
      fprintf ( fp," %s", file->c_str() );
    }
    fprintf ( fp," & \n" );
  }

  fprintf ( fp,"\n#wait for them all to finish\nwait" );
  fclose ( fp );
  {
    /* Set execute permission for user. */
    struct stat buf;
    stat ( RESTART_SCRIPT_NAME, &buf );
    chmod ( RESTART_SCRIPT_NAME, buf.st_mode | S_IXUSR );
  }
  _restartFilenames.clear();
}

int main ( int argc, char** argv )
{
  dmtcp::DmtcpMessage::setDefaultCoordinator ( dmtcp::UniquePid::ThisProcess() );

  //parse port
  int port = DEFAULT_PORT;
  const char* portStr = getenv ( ENV_VAR_NAME_PORT );
  if ( portStr != NULL ) port = jalib::StringToInt ( portStr );
  if ( argc > 1 ) port = atoi ( argv[1] );

  //parse checkpoint interval
  const char* interval = getenv ( ENV_VAR_NAME_CKPT_INTR );
  if ( interval != NULL ) theCheckpointInterval = jalib::StringToInt ( interval );

  if ( port <= 0 )
  {
    fprintf(stderr, theUsage, DEFAULT_PORT);
    return 1;
  }

  JASSERT_STDERR <<
    "dmtcp_coordinator starting..." << 
    "\n    Port: " << port <<
    "\n    Checkpoint Interval: " << ( theCheckpointInterval > 0 ? "" + theCheckpointInterval : "0 (checkpoint manually instead)" ) <<
    "\n'dmtcp_coordinator -h' for help." <<
    "\n\n";

  jalib::JServerSocket sock ( jalib::JSockAddr::ANY,port );
  JASSERT ( sock.isValid() ) ( port ).Text ( "Failed to create listen socket" );
  dmtcp::DmtcpCoordinator prog;
  prog.addListenSocket ( sock );
  prog.addDataSocket ( new jalib::JChunkReader ( STDIN_FD , 1 ) );
  prog.monitorSockets ( theCheckpointInterval > 0 ? theCheckpointInterval : 3600 );
  return 0;
}

