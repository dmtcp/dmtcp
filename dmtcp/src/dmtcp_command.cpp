/***************************************************************************
 *   Copyright (C) 2008 by Jason Ansel                                     *
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "dmtcpworker.h"
#include "dmtcp_coordinator.h"
#include "dmtcpmessagetypes.h"
#include "mtcpinterface.h"

using namespace dmtcp;


static const char* theUsage = 
  "PURPOSE:\n"
  "  Send a command to the dmtcp_coordinator remotely.\n\n"
  "USAGE:\n"
  "  dmtcp_command [OPTIONS] COMMAND\n\n"
  "OPTIONS:\n"
  "  --host, -h, (environment variable DMTCP_HOST):\n"
  "      Hostname where dmtcp_coordinator is run (default: localhost)\n"
  "  --port, -p, (environment variable DMTCP_PORT):\n"
  "      Port where dmtcp_coordinator is run (default: 7779)\n\n"
  "COMMANDS:\n"
  "    s : Print status message\n"
  "    c : Checkpoint all nodes\n"
  "    f : Force a restart even if there are missing nodes (debugging only)\n"
  "    k : Kill all nodes\n"
  "    q : Kill all nodes and quit\n\n"
  "See http://dmtcp.sf.net/ for more information.\n"
;


//shift args
#define shift argc--; argv++

int main ( int argc, char** argv )
{
  //process args 
  shift;
  while(true){
    std::string s = argc>0 ? argv[0] : "--help";
    if(s=="--help"){
      fprintf(stderr, theUsage);
      return 1;
    }else if(argc>1 && (s == "-h" || s == "--host")){
      setenv(ENV_VAR_NAME_ADDR, argv[1], 1);
      shift; shift;
    }else if(argc>1 && (s == "-p" || s == "--port")){
      setenv(ENV_VAR_NAME_PORT, argv[1], 1);
      shift; shift;
    }else{
      break;
    }
  }

  const char* cmd = argv[0];
  //ignore leading dashes
  while(*cmd == '-') cmd++;

  if(*cmd == 'h' || *cmd == '\0' || *cmd == '?'){
    fprintf(stderr, theUsage);
    return 1;
  }
  
  int result[DMTCPMESSAGE_NUM_PARAMS];
  DmtcpWorker worker(false);
  worker.connectAndSendUserCommand(*cmd, result);

  //check for error
  if(result[0]<0){
    switch(result[0]){
    case DmtcpCoordinator::ERROR_INVALID_COMMAND:
      fprintf(stderr, "Unknown command: %c, try 'dmtcp_command --help'\n", *cmd);
      break;
    case DmtcpCoordinator::ERROR_NOT_RUNNING_STATE:
      fprintf(stderr, "Error, computation not in running state.  Either a checkpoint is currently happening or there are no connected processes.\n");
      break;
    default:
      fprintf(stderr, "Unknown error\n");
      break;
    }
    return 2;
  }

  if(*cmd == 's'){
      printf("Status...\n");
      printf("NUM_PEERS=%d\n", result[0]);
      printf("RUNNING=%s\n", (result[1]?"yes":"no"));
  }

  return 0;
}

