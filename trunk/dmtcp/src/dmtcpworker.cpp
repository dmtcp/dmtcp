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
#include "dmtcpworker.h"
#include "constants.h"
#include "jconvert.h"
#include "dmtcpmessagetypes.h"
#include <stdlib.h>
#include "mtcpinterface.h"
#include <unistd.h>
#include "sockettable.h"
#include "jsocket.h"
#include <map>
#include "kernelbufferdrainer.h"
#include "jfilesystem.h"
#include "syscallwrappers.h"
#include "protectedfds.h"
#include "connectionidentifier.h"
#include "connectionmanager.h"
#include "checkpointcoordinator.h"

bool dmtcp::DmtcpWorker::_stdErrMasked = false;

void dmtcp::DmtcpWorker::maskStdErr()
{
	if ( _stdErrMasked == true ) return;
    int newfd = PROTECTED_STDERR_FD;
    JASSERT(_real_dup2(2, newfd) == newfd);
    JASSERT(_real_dup2(JASSERT_STDERR_FD, 2) == 2);
	_stdErrMasked = true;
}

void dmtcp::DmtcpWorker::unmaskStdErr()
{
	if ( _stdErrMasked == false ) return;
    int oldfd = PROTECTED_STDERR_FD;
    JASSERT(_real_dup2(oldfd, 2) == 2);
    _real_close(oldfd);
	_stdErrMasked = false;
}

// static dmtcp::KernelBufferDrainer* theDrainer = 0;
static dmtcp::CheckpointCoordinator* theCoordinator = 0;
static int theRestorPort = RESTORE_PORT_START;


//called before user main()
dmtcp::DmtcpWorker::DmtcpWorker(bool enableCheckpointing)
    :_coordinatorSocket(PROTECTEDFD(1))
    ,_restoreSocket(PROTECTEDFD(3))
{
    if(!enableCheckpointing) return;
    
    JASSERT_SET_LOGFILE("/tmp/jassertlog." + jalib::XToString(getpid()));
    JTRACE("dmtcphijack.so:  Running ")(jalib::Filesystem::GetProgramName())(getenv("LD_PRELOAD"));
    JTRACE("dmtcphijack.so:  Child of pid ")(getppid());
    
    if(jalib::Filesystem::GetProgramName() == "ssh")
    {
        //make sure coordinator connection is closed
        _real_close(PROTECTEDFD(1));
       
        //get prog args
        std::vector<std::string> args = jalib::Filesystem::GetProgramArgs();
        JASSERT(args.size() >= 3)(args.size()).Text("ssh must have at least 3 args to be wrapped (ie: ssh host cmd)");
        
        //find command part
        size_t commandStart = 2;
        for(size_t i = 1; i < args.size(); ++i)
        {
            if(args[i][0] != '-')
            {
                commandStart = i + 1;
                break;
            }
        }
        JASSERT(commandStart < args.size() && args[commandStart][0] != '-')
                (commandStart)(args.size())(args[commandStart])
                .Text("failed to parse ssh command line");
        
        //find the start of the command
        std::string& cmd = args[commandStart];
        
        const char * coordinatorAddr = getenv(ENV_VAR_NAME_ADDR);
        const char * coordinatorPortStr = getenv(ENV_VAR_NAME_PORT);
        
        //modify the command
        std::string prefix = "env ";
        if(coordinatorAddr != NULL) prefix += std::string() + ENV_VAR_NAME_ADDR "=" + coordinatorAddr + " ";
        if(coordinatorPortStr != NULL) prefix += std::string() +  ENV_VAR_NAME_PORT "=" + coordinatorPortStr + " ";
        prefix += DMTCP_CHECKPOINT_CMD " ";
        cmd = prefix + cmd;
        
        //now repack args
        std::string newCommand = "";
        char** argv = new char*[args.size()+2];
        memset(argv,0,sizeof(char*)*(args.size()+2));
        
        for(size_t i=0; i< args.size(); ++i)
        {
            argv[i] = (char*)args[i].c_str();
            newCommand += args[i] + ' ';
        }
        
        
        
        //we dont want to get into an infinite loop now do we?
        unsetenv("LD_PRELOAD");
        
        JNOTE("re-running SSH with checkpointing")(newCommand);
        
        //now re-call ssh
        execvp(argv[0], argv);

        //should be unreachable        
        JASSERT(false)(cmd)(JASSERT_ERRNO).Text("exec() failed"); 
    }
    
    WorkerState::setCurrentState( WorkerState::RUNNING );
    
    connectToCoordinator();
    
    initializeMtcpEngine();
    
    const char* serialFile = getenv(ENV_VAR_SERIALFILE_INITIAL);
    if(serialFile != NULL)
    {
        JTRACE("loading initial socket table from file...")(serialFile);
        
        //reset state
//         ConnectionList::Instance() = ConnectionList();
//         KernelDeviceToConnection::Instance() = KernelDeviceToConnection();
    
        //load file
        jalib::JBinarySerializeReader rd(serialFile);
        KernelDeviceToConnection::Instance().serialize( rd );
        
#ifdef DEBUG
        JTRACE("initial socket table:");
        KernelDeviceToConnection::Instance().dbgSpamFds();
#endif
        
        unsetenv(ENV_VAR_SERIALFILE_INITIAL);
    }
    else
    {
        JTRACE("root of processes tree, checking for pre-existing sockets");
//        ConnectionList::Instance().scanForPreExisting();
    }

    
// #ifdef DEBUG
//     JTRACE("listing fds");
//     KernelDeviceToConnection::Instance().dbgSpamFds();
// #endif
}

//called after user main()
dmtcp::DmtcpWorker::~DmtcpWorker()
{
    JTRACE("disconnecting from dmtcp coordinator");
    _coordinatorSocket.close();
}




const dmtcp::UniquePid& dmtcp::DmtcpWorker::coordinatorId() const
{
  return _coordinatorId;
}

void dmtcp::DmtcpWorker::waitForStage1Suspend()
{
    JTRACE("running");
    WorkerState::setCurrentState( WorkerState::RUNNING );
    {
        dmtcp::DmtcpMessage msg;
        msg.type = DMT_OK;
        msg.state = WorkerState::RUNNING;
        _coordinatorSocket << msg;
    }
    JTRACE("waiting for SUSPEND signal");
    {
        dmtcp::DmtcpMessage msg;
        msg.poison();
        while(msg.type != dmtcp::DMT_DO_SUSPEND)
        {
            _coordinatorSocket >> msg;
            msg.assertValid();
            JTRACE("got MSG from coordinator")(msg.type);
        }
    }    
    JTRACE("got SUSPEND signal");
}

void dmtcp::DmtcpWorker::waitForStage2Checkpoint()
{
    JTRACE("suspended");
    WorkerState::setCurrentState( WorkerState::SUSPENDED );
    {
        dmtcp::DmtcpMessage msg;
        msg.type = DMT_OK;
        msg.state = WorkerState::SUSPENDED;
        _coordinatorSocket << msg;
    }
    JTRACE("waiting for lock signal");
    {
        dmtcp::DmtcpMessage msg;
        msg.poison();
        _coordinatorSocket >> msg;
        msg.assertValid();
        JASSERT(msg.type == dmtcp::DMT_DO_LOCK_FDS)(msg.type);
    }
    JTRACE("locking...");
    JASSERT(theCoordinator == 0);
    theCoordinator = new CheckpointCoordinator();
    theCoordinator->preCheckpointLock();
    JTRACE("locked");
    WorkerState::setCurrentState( WorkerState::LOCKED );
    {
        dmtcp::DmtcpMessage msg;
        msg.type = DMT_OK;
        msg.state = WorkerState::LOCKED;
        _coordinatorSocket << msg;
    }
    JTRACE("waiting for drain signal");
    {
        dmtcp::DmtcpMessage msg;
        msg.poison();
        _coordinatorSocket >> msg;
        msg.assertValid();
        JASSERT(msg.type == dmtcp::DMT_DO_DRAIN)(msg.type);
    }
    JTRACE("draining...");
    theCoordinator->preCheckpointDrain();
    JTRACE("drained");
    WorkerState::setCurrentState( WorkerState::DRAINED );
    {
        dmtcp::DmtcpMessage msg;
        msg.type = DMT_OK;
        msg.state = WorkerState::DRAINED;
        _coordinatorSocket << msg;
    }
    JTRACE("waiting for checkpoint signal");
    {
        dmtcp::DmtcpMessage msg;
        msg.poison();
        _coordinatorSocket >> msg;
        msg.assertValid();
        JASSERT(msg.type == dmtcp::DMT_DO_CHECKPOINT)(msg.type);
    }
    JTRACE("got checkpoint signal");
    
    
    JTRACE("masking stderr from mtcp");
    //because MTCP spams, and the user may have a socket for stderr
    maskStdErr();
}

void dmtcp::DmtcpWorker::waitForStage3Resume()
{
    JTRACE("unmasking stderr");
    unmaskStdErr();
    
    {
      // Tell coordinator to record our filename in the restart script
      std::string ckptFilename = dmtcp::UniquePid::checkpointFilename();
      std::string hostname = jalib::Filesystem::GetCurrentHostname();
      JTRACE("recording filenames")(ckptFilename)(hostname);
      dmtcp::DmtcpMessage msg;
      msg.type = DMT_CKPT_FILENAME;
      msg.extraBytes = ckptFilename.length()+1 + hostname.length()+1;
      _coordinatorSocket << msg;
      _coordinatorSocket.writeAll( ckptFilename.c_str(), ckptFilename.length()+1 );
      _coordinatorSocket.writeAll( hostname.c_str(),     hostname.length()+1 );
    }
    
    JTRACE("checkpointed");
    WorkerState::setCurrentState( WorkerState::CHECKPOINTED );
    {
      // Tell coordinator we are done checkpointing
      dmtcp::DmtcpMessage msg;
      msg.type = DMT_OK;
      msg.state = WorkerState::CHECKPOINTED;
      _coordinatorSocket << msg;
    }

    JTRACE("waiting for refill signal");
    {
        dmtcp::DmtcpMessage msg;
        do{
            msg.poison();
            _coordinatorSocket >> msg;
            msg.assertValid();
        }while(msg.type == DMT_RESTORE_WAITING || msg.type == DMT_FORCE_RESTART);
        JASSERT(msg.type == dmtcp::DMT_DO_REFILL)(msg.type);
    }
    JASSERT(theCoordinator != 0);  
    theCoordinator->postCheckpoint();
    delete theCoordinator;
    theCoordinator = 0; 
    JTRACE("refilled");
    WorkerState::setCurrentState( WorkerState::REFILLED );
    {
        dmtcp::DmtcpMessage msg;
        msg.type = DMT_OK;
        msg.state = WorkerState::REFILLED;
        _coordinatorSocket << msg;
    }
    JTRACE("waiting for resume signal");
    {
        dmtcp::DmtcpMessage msg;
        msg.poison();
        _coordinatorSocket >> msg;
        msg.assertValid();
        JASSERT(msg.type == dmtcp::DMT_DO_RESUME)(msg.type);
    }
    JTRACE("got resume signal");
}


void dmtcp::DmtcpWorker::postRestart()
{
    JTRACE("postRestart begin");
   	
	JTRACE("umasking stderr");
    unmaskStdErr();
	

    //reconnect to our coordinator
    WorkerState::setCurrentState( WorkerState::RESTARTING );
    connectToCoordinator();
    
    JASSERT(theCoordinator != NULL);
    theCoordinator->postRestart();
    
    JTRACE("masking stderr from mtcp");
    //because MTCP spams, and the user may have a socket for stderr
    maskStdErr();

    JTRACE("postRestart end");
}

void dmtcp::DmtcpWorker::restoreSockets(CheckpointCoordinator& coordinator)
{
    JTRACE("restoreSockets begin");

    theRestorPort = RESTORE_PORT_START;
    
    //open up restore socket
    {    
        jalib::JSocket restorSocket(-1);
        while(!restorSocket.isValid() && theRestorPort < RESTORE_PORT_STOP)
        {
            restorSocket = jalib::JServerSocket(jalib::JSockAddr::ANY, ++theRestorPort); 
            JTRACE("open listen socket attempt")(theRestorPort);
        }
        JASSERT(restorSocket.isValid())(RESTORE_PORT_START).Text("failed to open listen socket");
        restorSocket.changeFd(_restoreSocket.sockfd());
        JTRACE("openning listen sockets")(_restoreSocket.sockfd())(restorSocket.sockfd());
        _restoreSocket = restorSocket;
    }
    
    //reconnect to our coordinator
    WorkerState::setCurrentState( WorkerState::RESTARTING );
    connectToCoordinator();
    
    coordinator.doReconnect(_coordinatorSocket,_restoreSocket);
    
    JTRACE("sockets restored!");

}

//tell the coordinator is should broadcast DMT_FORCE_RESTART
void dmtcp::DmtcpWorker::forceRestart()
{
  connectToCoordinator();
  dmtcp::DmtcpMessage msg;
  msg.type = DMT_FORCE_RESTART;
  _coordinatorSocket << msg;
  _coordinatorSocket.close();
}


/*!
    \fn dmtcp::DmtcpWorker::connectToCoordinator()
 */
void dmtcp::DmtcpWorker::connectToCoordinator()
{
  
    const char * coordinatorAddr = getenv(ENV_VAR_NAME_ADDR);
    const char * coordinatorPortStr = getenv(ENV_VAR_NAME_PORT);
    
    if(coordinatorAddr == NULL) coordinatorAddr = "localhost";
    int coordinatorPort = coordinatorPortStr==NULL ? DEFAULT_PORT : jalib::StringToInt(coordinatorPortStr);
            
    jalib::JSocket oldFd = _coordinatorSocket;
    
    _coordinatorSocket = jalib::JClientSocket(coordinatorAddr,coordinatorPort);
    
    JASSERT(_coordinatorSocket.isValid())
            (coordinatorAddr)
            (coordinatorPort)
            .Text("Failed to connect to DMTCP coordinator");
    
    if(oldFd.isValid())
    {
        JTRACE("restoring old coordinatorsocket fd")
               (oldFd.sockfd())
               (_coordinatorSocket.sockfd());
        
        _coordinatorSocket.changeFd(oldFd.sockfd());
    }
    

    
    {
        dmtcp::DmtcpMessage hello_local, hello_remote;
        hello_remote.poison();
        hello_local.type = dmtcp::DMT_HELLO_COORDINATOR;
        hello_local.restorePort = theRestorPort;
//         hello_local.restorePid.id = UniquePid::ThisProcess();
        _coordinatorSocket >> hello_remote;
        _coordinatorSocket << hello_local;
        hello_remote.assertValid();
        JASSERT(hello_remote.type == dmtcp::DMT_HELLO_WORKER)(hello_remote.type);
        _coordinatorId = hello_remote.coordinator;
   
        DmtcpMessage::setDefaultCoordinator( _coordinatorId );
        
        JTRACE("connected to dmtcp coordinator")(coordinatorAddr)(coordinatorPort)(_coordinatorId)(hello_local.from)(UniquePid::checkpointFilename())(jalib::Filesystem::GetProgramPath());
        
    }
}
