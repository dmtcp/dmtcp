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

#include <stdlib.h>
#include <stdio.h>

#include "dmtcpcoordinatorapi.h"
#include "util.h"
#include "syscallwrappers.h"

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
  "    i, -i, --interval <val> : Update ckpt interval to <val> seconds"
						   		" (0=never)\n"
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
  dmtcp::string interval = "";
  dmtcp::string request = "h";

  initializeJalib();

  Util::initializeLogFile();

  //process args
  shift;
  while(argc>0){
    dmtcp::string s = argv[0];
    if((s=="--help" || s=="-h") && argc==1){
      fprintf(stderr, theUsage, "");
      return 1;
    }else if(argc>1 && (s == "-h" || s == "--host")){
      setenv(ENV_VAR_NAME_HOST, argv[1], 1);
      shift; shift;
    }else if(argc>1 && (s == "-p" || s == "--port")){
      setenv(ENV_VAR_NAME_PORT, argv[1], 1);
      shift; shift;
    }else if(s == "--quiet"){
      quiet = true;
      shift;
    }else if(s == "h" || s == "-h" || s == "--help" || s == "?"){
      fprintf(stderr, theUsage, "");
      return 1;
    }else{ // else it's a request
      char* cmd = argv[0];
      //ignore leading dashes
      while(*cmd == '-') cmd++;
      s = cmd;

      if(*cmd == 'b' && *(cmd+1) != 'c'){
        // If blocking ckpt, next letter must be 'c'; else print the usage
        fprintf(stderr, theUsage, "");
        return 1;
      } else if (*cmd == 's' || *cmd == 'i' || *cmd == 'c' || *cmd == 'b'
		 || *cmd == 'f' || *cmd == 'k' || *cmd == 'q') {
        request = s;
        if (*cmd == 'i') {
	  if (isdigit(cmd[1])) { // if -i5, for example
	    interval = cmd+1;
	  } else { // else -i 5
	    interval = argv[1];
	    shift;
	  }
        }
        shift;
      }else{
	fprintf(stderr, theUsage, "");
	return 1;
      }
    }
  }

  if (! quiet)
    printf("DMTCP/MTCP  Copyright (C) 2006-2010  Jason Ansel, Michael Rieker,\n"
           "                                       Kapil Arya, and Gene Cooperman\n"
           "This program comes with ABSOLUTELY NO WARRANTY.\n"
           "This is free software, and you are welcome to redistribute it\n"
           "under certain conditions; see COPYING file for details.\n"
           "(Use flag \"--quiet\" to hide this message.)\n\n");

  int result[DMTCPMESSAGE_NUM_PARAMS];
  DmtcpCoordinatorAPI coordinatorAPI;
  char *cmd = (char *)request.c_str();
  switch (*cmd) {
  case 'h':
    fprintf(stderr, theUsage, "");
    return 1;
  case 'i':
    setenv(ENV_VAR_CKPT_INTR, interval.c_str(), 1);
    coordinatorAPI.connectAndSendUserCommand(*cmd, result);
    printf("Interval changed to %s\n", interval.c_str());
    break;
  case 'b':
    // blocking prefix
    coordinatorAPI.connectAndSendUserCommand(*cmd, result);
    // actual command
    coordinatorAPI.connectAndSendUserCommand(*(cmd+1), result);
    break;
  case 's':
  case 'c':
  case 'f':
  case 'k':
  case 'q':
    coordinatorAPI.connectAndSendUserCommand(*cmd, result);
    break;
  }

  //check for error
  if(result[0]<0){
    switch(result[0]){
    case DmtcpCoordinatorAPI::ERROR_COORDINATOR_NOT_FOUND:
      if (getenv("DMTCP_PORT"))
        fprintf(stderr,
	        "Coordinator not found.  Please check port and host.\n");
      else
        fprintf(stderr,
	      "Coordinator not found.  Try specifying port with \'--port\'.\n");
      break;
    case DmtcpCoordinatorAPI::ERROR_INVALID_COMMAND:
      fprintf(stderr,
	      "Unknown command: %c, try 'dmtcp_command --help'\n", *cmd);
      break;
    case DmtcpCoordinatorAPI::ERROR_NOT_RUNNING_STATE:
      fprintf(stderr, "Error, computation not in running state."
	      "  Either a checkpoint is\n"
	      " currently happening or there are no connected processes.\n");
      break;
    default:
      fprintf(stderr, "Unknown error\n");
      break;
    }
    return 2;
  }

  if(*cmd == 's'){
    if (getenv(ENV_VAR_NAME_HOST))
      printf("  Host: %s\n", getenv(ENV_VAR_NAME_HOST));
    printf("  Port: %s\n", getenv(ENV_VAR_NAME_PORT));
    printf("Status...\n");
    printf("NUM_PEERS=%d\n", result[0]);
    printf("RUNNING=%s\n", (result[1]?"yes":"no"));
  }

  return 0;
}

