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


#include <unistd.h>
#include <stdlib.h>
#include <string>
#include <stdio.h>
#include "jassert.h"
#include "jfilesystem.h"
#include "jconvert.h"
#include "constants.h"
#include "dmtcpworker.h"
#include "dmtcpmessagetypes.h"
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static const char* theUsage = 
  "USAGE: \n"
  "  dmtcp_checkpoint [OPTIONS] <command> [args...]\n\n"
  "OPTIONS:\n"
  "  --host, -h, (environment variable DMTCP_HOST):\n"
  "      Hostname where dmtcp_coordinator is run (default: localhost)\n"
  "  --port, -p, (environment variable DMTCP_PORT):\n"
  "      Port where dmtcp_coordinator is run (default: 7779)\n"
  "  --gzip, --no-gzip, (environment variable DMTCP_GZIP=[01]):\n"
  "      Enable/disable compression of checkpoint images (default: 1)\n"
  "  --dir, -d, (environment variable DMTCP_CHECKPOINT_DIR):\n"
  "      Directory to store checkpoint images (default: ./)\n"
  "  --join, -j:\n"
  "      Join an existing coordinator, do not create one automatically\n"
  "  --new, -n:\n"
  "      Create a new coordinator, raise error if one already exists\n"
  "  --no-check:\n"
  "      Skip check for valid coordinator and never start one automatically\n\n"
  "See http://dmtcp.sf.net/ for more information.\n"
;

static const char* theExecFailedMsg = 
  "ERROR: Failed to exec(\"%s\"): %s\n"
  "Perhaps it is not in your $PATH?\n"
  "See `dmtcp_checkpoint --help` for usage.\n"
;

static std::string _stderrProcPath()
{
  return "/proc/" + jalib::XToString ( getpid() ) + "/fd/" + jalib::XToString ( fileno ( stderr ) );
}

static void startCoordinatorIfNeeded(int modes);

enum {
  COORD_JOIN = 1,
  COORD_NEW  = 2,
  COORD_ANY  = COORD_JOIN | COORD_NEW
};

//shift args
#define shift argc--; argv++

int main ( int argc, char** argv )
{
  bool isSSHSlave=false;
  bool autoStartCoordinator=true;
  int allowedModes = COORD_ANY;

  //process args 
  shift;
  while(true){
    std::string s = argc>0 ? argv[0] : "--help";
    if(s=="--help"){
      fprintf(stderr, theUsage);
      return 1;
    }else if(s=="--ssh-slave"){
      isSSHSlave = true;
      shift;
    }else if(s == "--no-check"){
      autoStartCoordinator = false;
      shift;
    }else if(s == "-j" || s == "--join"){
      allowedModes = COORD_JOIN;
      shift;
    }else if(s == "--gzip"){
      setenv(ENV_VAR_COMPRESSION, "1", 1);
      shift;
    }else if(s == "--no-gzip"){
      setenv(ENV_VAR_COMPRESSION, "0", 1);
      shift;
    }else if(s == "-n" || s == "--new"){
      allowedModes = COORD_NEW;
      shift;
    }else if(argc>1 && (s == "-h" || s == "--host")){
      setenv(ENV_VAR_NAME_ADDR, argv[1], 1);
      shift; shift;
    }else if(argc>1 && (s == "-p" || s == "--port")){
      setenv(ENV_VAR_NAME_PORT, argv[1], 1);
      shift; shift;
    }else if(argc>1 && (s == "-d" || s == "--dir")){
      setenv(ENV_VAR_CHECKPOINT_DIR, argv[1], 1);
      shift; shift;
    }else if(argc>1 && s=="--"){
      shift;
      break;
    }else{
      break;
    }
  }

  if(autoStartCoordinator) startCoordinatorIfNeeded(allowedModes);

  //Detect important paths
  std::string dmtcphjk = jalib::Filesystem::FindHelperUtility ( "dmtcphijack.so" );
  std::string searchDir = jalib::Filesystem::GetProgramDir();

  // Initialize JASSERT library here
  JASSERT_INIT();

  //setup CHECKPOINT_DIR
  if(getenv(ENV_VAR_CHECKPOINT_DIR) == NULL){
    const char* ckptDir = get_current_dir_name();
    if(ckptDir != NULL ){
      //copy to private buffer
      static std::string _buf = ckptDir;
      ckptDir = _buf.c_str();
    }else{
      ckptDir=".";
    }
    setenv ( ENV_VAR_CHECKPOINT_DIR, ckptDir, 0 );
    JTRACE("setting " ENV_VAR_CHECKPOINT_DIR)(ckptDir);
  }

  std::string stderrDevice = jalib::Filesystem::ResolveSymlink ( _stderrProcPath() );

  //TODO:
  // When stderr is a pseudo terminal for IPC between parent/child processes,
  // this logic fails and JASSERT may write data to FD 2 (stderr)
  // this will cause problems in programs that use FD 2 (stderr) for algorithmic things...
  if ( stderrDevice.length() > 0
          && jalib::Filesystem::FileExists ( stderrDevice ) )
    setenv ( ENV_VAR_STDERR_PATH,stderrDevice.c_str(), 0 );
  else// if( isSSHSlave )
    setenv ( ENV_VAR_STDERR_PATH, "/dev/null", 0 );

  setenv ( "LD_PRELOAD", dmtcphjk.c_str(), 1 );
  setenv ( ENV_VAR_HIJACK_LIB, dmtcphjk.c_str(), 0 );
  setenv ( ENV_VAR_UTILITY_DIR, searchDir.c_str(), 0 );
  if ( getenv(ENV_VAR_SIGCKPT) != NULL )
    setenv ( "MTCP_SIGCKPT", getenv(ENV_VAR_SIGCKPT), 1);
  else
    unsetenv("MTCP_SIGCKPT");

  //copy args into new structure
  //char** newArgs = new char* [argc];
  //memset ( newArgs, 0, sizeof ( char* ) *argc );
  //for ( int i=0; i<argc-startArg; ++i )
  //  newArgs[i] = argv[i+startArg];

  //run the user program
  execvp ( argv[0], argv );

  //should be unreachable
  fprintf(stderr, theExecFailedMsg, argv[0], JASSERT_ERRNO);

  return -1;
}

static void startCoordinatorIfNeeded(int modes){
  const static int CS_OK = 91;
  const static int CS_NO = 92;
  int coordinatorStatus = -1;
  //fork a child process to probe the coordinator
  if(fork()==0){
    //fork so if we hit an error parent wont die
    dup2(2,1);
    close(2);
    int result[DMTCPMESSAGE_NUM_PARAMS];
    dmtcp::DmtcpWorker worker(false);
    worker.connectAndSendUserCommand('s', result);
    if(result[0]==0 || result[1]){
      if(result[0] != 0)
        printf("[DMTCP] Joining existing computation of %d processes.\n", result[0]);
      exit(CS_OK);
    }else{
      printf("[DMTCP] ERROR: existing computation not in a running state, perhaps checkpoint in progress?\n");
      exit(CS_NO);
    }
  }
  JASSERT(::wait(&coordinatorStatus)>0)(JASSERT_ERRNO);

  //is coordinator running?
  if(WEXITSTATUS(coordinatorStatus) != CS_OK){
    //is coordinator in funny state?
    if(WEXITSTATUS(coordinatorStatus) == CS_NO){
      exit(1);
    }
    
    //get location of coordinator
    const char * coordinatorAddr = getenv ( ENV_VAR_NAME_ADDR );
    if(coordinatorAddr==NULL) coordinatorAddr = "localhost";
    const char * coordinatorPortStr = getenv ( ENV_VAR_NAME_PORT );
    int coordinatorPort = coordinatorPortStr==NULL ? DEFAULT_PORT : jalib::StringToInt(coordinatorPortStr);
    
    fprintf(stderr, "[DMTCP] Coordinator not found at %s:%d.\n", coordinatorAddr, coordinatorPort);

    if((modes&COORD_NEW) == 0){
      fprintf(stderr, "[DMTCP] Won't automatically start coordinator because '--join' flag is specified.\n");
      exit(1);
    }

    if(coordinatorAddr!="localhost"){
      std::string s=coordinatorAddr;
      if(s!="127.0.0.1" && s!="localhost" && s!=jalib::Filesystem::GetCurrentHostname()){
        fprintf(stderr, "[DMTCP] Won't automatically start coordinator because DMTCP_HOST is set to a remote host.\n");
        exit(1); 
      }
    }

    fprintf(stderr, "[DMTCP] Starting a new coordinator automatically.\n");

    if(fork()==0){
      std::string coordinator = jalib::Filesystem::FindHelperUtility("dmtcp_coordinator");
      char * args[] = {
        (char*)coordinator.c_str(),
        (char*)"--exit-on-last",
        (char*)"--background",
        NULL
      };
      execv(args[0], args);
      JASSERT(false)(coordinator)(JASSERT_ERRNO).Text("exec(dmtcp_coordinator) failed");
    }
    JASSERT(::wait(&coordinatorStatus)>0)(JASSERT_ERRNO);
    if(WEXITSTATUS(coordinatorStatus) != 0){
      printf("[DMTCP] ERROR: Failed to start coordinator, port already in use.\n[DMTCP] You may use a different port by running: `dmtcp_checkpoint -p 12345 ...`.\n");
      exit(1);
    }
  }else{
    if((modes&COORD_JOIN) == 0){
      fprintf(stderr, "[DMTCP] ERROR: Coordinator already running, but '--new' flag was given.\n");
      exit(1);
    }
  }
}

