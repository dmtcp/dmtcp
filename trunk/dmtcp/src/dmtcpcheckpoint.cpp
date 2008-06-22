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
#include <errno.h>

static const char* theUsage = 
  "USAGE: dmtcp_checkpoint <command> [args...]\n"
  "OPTIONS (Environment variables):\n"
  "  - DMTCP_HOST=<hostname where coordinator is running> (default: localhost) \n"
  "  - DMTCP_PORT=<coordinator listener port> (default: 7779) \n"
  "  - DMTCP_GZIP=<1: enable compression of checkpoint image> \n"
  "               (default:0, compression disabled) \n"
  "  - DMTCP_CHECKPOINT_DIR=<location to store checkpoints> (default: ./)\n"
  "  - DMTCP_SIGCKPT=<signal number> (default: SIGUSR2) \n"
;

static const char* theExecFailedMsg = 
  "ERROR: Failed to exec(\"%s\"): %s\n"
  "Perhaps it is not in your $PATH? \n"
;

static std::string _stderrProcPath()
{
  return "/proc/" + jalib::XToString ( getpid() ) + "/fd/" + jalib::XToString ( fileno ( stderr ) );
}

int main ( int argc, char** argv )
{

  //setup hijack library
  std::string dmtcphjk = jalib::Filesystem::FindHelperUtility ( "dmtcphijack.so" );
  std::string searchDir = jalib::Filesystem::GetProgramDir();

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

  bool is_ssh_slave=false;
  //how many args to trim off start
  int startArg = 1;

  if( argc < 2 || strcmp(argv[1],"--help")==0 || strcmp(argv[1],"-h")==0){
    fprintf(stderr, theUsage);
    return 1;
  }

  if ( strncmp(argv[1],"--ssh-slave", strlen("--ssh-slave")) == 0 )
  {
    is_ssh_slave = true;
    startArg++;
  }

  std::string stderrDevice = jalib::Filesystem::ResolveSymlink ( _stderrProcPath() );

  //TODO:
  // When stderr is a pseudo terminal for IPC between parent/child processes,
  // this logic fails and JASSERT may write data to FD 2 (stderr)
  // this will cause problems in programs that use FD 2 (stderr) for algorithmic things...
  if ( stderrDevice.length() > 0
          && jalib::Filesystem::FileExists ( stderrDevice ) )
    setenv ( ENV_VAR_STDERR_PATH,stderrDevice.c_str(), 0 );
  else// if( is_ssh_slave )
    setenv ( ENV_VAR_STDERR_PATH, "/dev/null", 0 );

  setenv ( "LD_PRELOAD", dmtcphjk.c_str(), 1 );
  setenv ( ENV_VAR_HIJACK_LIB, dmtcphjk.c_str(), 0 );
  setenv ( ENV_VAR_UTILITY_DIR, searchDir.c_str(), 0 );
  if ( getenv(ENV_VAR_SIGCKPT) != NULL )
    setenv ( "MTCP_SIGCKPT", getenv(ENV_VAR_SIGCKPT), 1);
  else
    unsetenv("MTCP_SIGCKPT");

  //copy args into new structure
  char** newArgs = new char* [argc];
  memset ( newArgs, 0, sizeof ( char* ) *argc );
  for ( int i=0; i<argc-startArg; ++i )
    newArgs[i] = argv[i+startArg];

  //run the user program
  execvp ( newArgs[0], newArgs );

  //should be unreachable
  fprintf(stderr, theExecFailedMsg, newArgs[0], JASSERT_ERRNO);

  return -1;
}
