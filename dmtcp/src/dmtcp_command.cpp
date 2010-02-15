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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "dmtcpworker.h"
#include "dmtcp_coordinator.h"
#include "dmtcpmessagetypes.h"
#include "mtcpinterface.h"

using namespace dmtcp;

// gcc-4.3.4 -Wformat=2 issues false positives for warnings unless the format
// string has atleast one format specifier with corresponding format argument.
// Ubuntu 9.01 uses -Wformat=2 by default.
static const char* theUsage =
  "%sPURPOSE:\n"
  "  Send a command to the dmtcp_coordinator remotely.\n\n"
  "USAGE:\n"
  "  dmtcp_command [OPTIONS] COMMAND [COMMAND...]\n\n"
  "OPTIONS:\n"
  "  --host, -h, (environment variable DMTCP_HOST):\n"
  "      Hostname where dmtcp_coordinator is run (default: localhost)\n"
  "  --port, -p, (environment variable DMTCP_PORT):\n"
  "      Port where dmtcp_coordinator is run (default: 7779)\n"
  "  --quiet:\n"
  "      Skip copyright notice\n\n"
  "COMMANDS:\n"
  "    s, -s, --status : Print status message\n"
  "    c, -c, --checkpoint : Checkpoint all nodes\n"
  "    bc, -bc, --bcheckpoint : Checkpoint all nodes, blocking until done\n"
  "    f, -f, --force : Force restart even with missing nodes (for debugging)\n"
  "    k, -k, --kill : Kill all nodes\n"
  "    q, -q, --quit : Kill all nodes and quit\n\n"
  "See http://dmtcp.sf.net/ for more information.\n"
;


//shift args
#define shift argc--,argv++

int main ( int argc, char** argv )
{
  bool quiet = false;

  //process args
  shift;
  while(true){
    dmtcp::string s = argc>0 ? argv[0] : "--help";
    if(s=="--help" || s=="-h" && argc==1){
      fprintf(stderr, theUsage, "");
      return 1;
    }else if(argc>1 && (s == "-h" || s == "--host")){
      setenv(ENV_VAR_NAME_ADDR, argv[1], 1);
      shift; shift;
    }else if(argc>1 && (s == "-p" || s == "--port")){
      setenv(ENV_VAR_NAME_PORT, argv[1], 1);
      shift; shift;
    }else if(s == "--quiet"){
      quiet = true;
      shift;
    }else{
      break;
    }
  }

  if (! quiet)
    printf("DMTCP/MTCP  Copyright (C) 2006-2008  Jason Ansel, Michael Rieker,\n"
           "                                       Kapil Arya, and Gene Cooperman\n"
           "This program comes with ABSOLUTELY NO WARRANTY.\n"
           "This is free software, and you are welcome to redistribute it\n"
           "under certain conditions; see COPYING file for details.\n"
           "(Use flag \"--quiet\" to hide this message.)\n\n");

  for( ; argc>0; shift){

    char* cmd = argv[0];
    //ignore leading dashes
    while(*cmd == '-') cmd++;

    if(*cmd == 'b' && *(cmd+1) != 'c')
      *cmd = 'h';  // If blocking ckpt, next letter must be 'c'; else print usage

    if(*cmd == 'h' || *cmd == '\0' || *cmd == '?'){
      fprintf(stderr, theUsage, "");
      return 1;
    }

    int result[DMTCPMESSAGE_NUM_PARAMS];
    DmtcpWorker worker(false);
    if (*cmd == 'b') {
      worker.connectAndSendUserCommand(*cmd, result);     // blocking prefix
      worker.connectAndSendUserCommand(*(cmd+1), result); // actual command
    } else {
      worker.connectAndSendUserCommand(*cmd, result);
    }

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

  }

  return 0;
}

