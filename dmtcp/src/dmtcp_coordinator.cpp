/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include "dmtcp_coordinator.h"
#include "constants.h"
#include  "../jalib/jconvert.h"
#include "dmtcpmessagetypes.h"
#include "dmtcpworker.h"
#include <stdio.h>
#include <sys/stat.h>
#include  "../jalib/jtimer.h"
#include <algorithm>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#undef min
#undef max


int thePort = -1;

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
  "USAGE: \n"
  "   dmtcp_coordinator [OPTIONS] [port]\n\n"
  "OPTIONS:\n"
  "  --port, -p, (environment variable DMTCP_PORT):\n"
  "      Port to listen on (default: 7779)\n"
  "  --dir, -d, (environment variable DMTCP_CHECKPOINT_DIR):\n"
  "      Directory to store dmtcp_restart_script.sh (default: ./)\n"
  "  --interval, -i, (environment variable DMTCP_CHECKPOINT_INTERVAL):\n"
  "      Time in seconds between automatic checkpoints (default: 0, disabled)\n"
  "  --exit-on-last\n"
  "      Exit automatically when last client disconnects\n"
  "  --background\n"
  "      Run silently in the background\n"
  "COMMANDS:\n"
  "  (type '?<return>' at runtime for list)\n\n"
  "See http://dmtcp.sf.net/ for more information.\n"
;


static bool exitOnLast = false;
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
    for ( std::vector<jalib::JReaderInterface*>::iterator i = _dataSockets.begin()
        ; i!= _dataSockets.end()
        ; ++i )
    {
      (*i)->socket().close();
    }
    for ( std::vector<jalib::JSocket>::iterator i = _listenSockets.begin()
        ; i!= _listenSockets.end()
        ; ++i )
    {
      i->close();
    }
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

    if(exitOnLast){
      CoordinatorStatus s = getStatus();
      if(s.numPeers <= 1){
        JNOTE ("last client exited, shutting down..");
        handleUserCommand('q');
      }
    }

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

  const bool isSingleHost = (_restartFilenames.size() == 1);

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
            "\n");
  fprintf(fp, "if test -z \"$" ENV_VAR_NAME_ADDR "\"; then\n  " ENV_VAR_NAME_ADDR "=%s\nfi\n\n", hostname);
  fprintf(fp, "if test -z \"$" ENV_VAR_NAME_PORT "\"; then\n  " ENV_VAR_NAME_PORT "=%d\nfi\n\n", thePort);


  for ( host=_restartFilenames.begin(); host!=_restartFilenames.end(); ++host )
  {
    if(isSingleHost && host->first==hostname){
      fprintf ( fp, "# Because this is a single-host computation, there is only one call to dmtcp_restart.\n# If this were a multi-host computation the calls would look like this:\n#" );
    }

    fprintf ( fp, "ssh %s "DMTCP_RESTART_CMD
                  " --host \"$"ENV_VAR_NAME_ADDR"\""
                  " --port \"$"ENV_VAR_NAME_ADDR"\""
                  " --join",
                  host->first.c_str());

    if(isSingleHost && host->first==hostname) fprintf (fp, " ...\nexec "DMTCP_RESTART_CMD );

    for ( file=host->second.begin(); file!=host->second.end(); ++file )
    {
      fprintf ( fp," %s", file->c_str() );
    }
    if(!isSingleHost || host->first!=hostname) fprintf ( fp," & \n" );
  }

  fprintf ( fp,"\n\n#wait for them all to finish\nwait\n" );
  fclose ( fp );
  {
    /* Set execute permission for user. */
    struct stat buf;
    stat ( RESTART_SCRIPT_NAME, &buf );
    chmod ( RESTART_SCRIPT_NAME, buf.st_mode | S_IXUSR );
  }
  _restartFilenames.clear();
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
    std::string s = argv[0];
    if(s=="-h" || s=="--help"){
      fprintf(stderr, theUsage, DEFAULT_PORT);
      return 1;
    }else if(s=="--exit-on-last"){
      exitOnLast = true;
      shift;
    }else if(s=="--background"){
      background = true;
      shift;
    }else if(argc>1 && (s == "-i" || s == "--interval")){
      setenv(ENV_VAR_NAME_CKPT_INTR, argv[1], 1);
      shift; shift;
    }else if(argc>1 && (s == "-p" || s == "--port")){
      thePort = jalib::StringToInt( argv[1] );
      shift; shift;
    }else if(argc>1 && (s == "-d" || s == "--dir")){
      setenv(ENV_VAR_CHECKPOINT_DIR, argv[1], 1);
      shift; shift;
    }else if(argc == 1){ //last arg can be port
      thePort = jalib::StringToInt( argv[0] );
      shift;
    }else{
      fprintf(stderr, theUsage, DEFAULT_PORT);
      return 1;
    }
  }

  //parse checkpoint interval
  const char* interval = getenv ( ENV_VAR_NAME_CKPT_INTR );
  if ( interval != NULL ) theCheckpointInterval = jalib::StringToInt ( interval );

  if ( thePort <= 0 )
  {
    fprintf(stderr, theUsage, DEFAULT_PORT);
    return 1;
  }

  errno = 0;
  jalib::JServerSocket sock ( jalib::JSockAddr::ANY, thePort );
  JASSERT ( sock.isValid() ) ( thePort ) ( JASSERT_ERRNO ).Text ( "Failed to create listen socket" );

  if(background){
    JASSERT(dup2(open("/dev/null",O_RDWR), 0)==0);
    JASSERT(dup2(open("/dev/null",O_RDWR), 1)==1);
    JASSERT(dup2(open("/dev/null",O_RDWR), 2)==2);
    if(fork()>0){
      exit(0);
    }
    pid_t sid = setsid();
  }else{
    JASSERT_STDERR <<
      "dmtcp_coordinator starting..." <<
      "\n    Port: " << thePort <<
      "\n    Checkpoint Interval: ";
    if(theCheckpointInterval==0)
      JASSERT_STDERR << "disabled (checkpoint manually instead)";
    else
      JASSERT_STDERR << theCheckpointInterval;
    JASSERT_STDERR  <<
      "\n    Exit on last client: " << exitOnLast <<
      "\nType '?' for help." <<
      "\n\n";
  }

  dmtcp::DmtcpCoordinator prog;
  prog.addListenSocket ( sock );
  if(!background) prog.addDataSocket ( new jalib::JChunkReader ( STDIN_FD , 1 ) );
  prog.monitorSockets ( theCheckpointInterval > 0 ? theCheckpointInterval : 3600 );
  return 0;
}

