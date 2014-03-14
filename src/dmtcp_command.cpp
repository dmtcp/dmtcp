/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <stdio.h>

#include "coordinatorapi.h"
#include "util.h"

#define BINARY_NAME "dmtcp_command"

using namespace dmtcp;

// gcc-4.3.4 -Wformat=2 issues false positives for warnings unless the format
// string has atleast one format specifier with corresponding format argument.
// Ubuntu 9.01 uses -Wformat=2 by default.
static const char* theUsage =
  "Usage:  dmtcp_command [OPTIONS] COMMAND [COMMAND...]\n"
  "Send a command to the dmtcp_coordinator remotely.\n\n"
  "Options:\n\n"
  "  -h, --host HOSTNAME (environment variable DMTCP_HOST)\n"
  "              Hostname where dmtcp_coordinator is run (default: localhost)\n"
  "  -p, --port PORT_NUM (environment variable DMTCP_PORT)\n"
  "              Port where dmtcp_coordinator is run (default: 7779)\n"
  "  --help \n"
  "              Print this message and exit.\n"
  "  --version \n"
  "              Print version information and exit.\n"
  "\n"
  "Commands for Coordinator:\n"
  "    -s, --status:          Print status message\n"
  "    -c, --checkpoint:      Checkpoint all nodes\n"
  "    -bc, --bcheckpoint:    Checkpoint all nodes, blocking until done\n"
  //"    xc, -xc, --xcheckpoint : Checkpoint all nodes, kill all nodes when done\n"
  "    -i, --interval <val>   Update ckpt interval to <val> seconds (0=never)\n"
  "    -k, --kill             Kill all nodes\n"
  "    -q, --quit             Kill all nodes and quit\n"
  "\n"
  HELP_AND_CONTACT_INFO
  "\n"
;


//shift args
#define shift argc--,argv++

int main ( int argc, char** argv )
{
  dmtcp::string interval = "";
  dmtcp::string request = "h";

  initializeJalib();

  Util::initializeLogFile();

  //process args
  shift;
  while(argc>0){
    dmtcp::string s = argv[0];
    if((s=="--help" || s=="-h") && argc==1){
      printf("%s", theUsage);
      return 1;
    } else if ((s=="--version") && argc==1){
      printf("%s", DMTCP_VERSION_AND_COPYRIGHT_INFO);
      return 1;
    }else if(argc>1 && (s == "-h" || s == "--host")){
      setenv(ENV_VAR_NAME_HOST, argv[1], 1);
      shift; shift;
    }else if(argc>1 && (s == "-p" || s == "--port")){
      setenv(ENV_VAR_NAME_PORT, argv[1], 1);
      shift; shift;
    }else if(s == "h" || s == "-h" || s == "--help" || s == "?"){
      fprintf(stderr, theUsage, "");
      return 1;
    }else{ // else it's a request
      char* cmd = argv[0];
      //ignore leading dashes
      while(*cmd == '-') cmd++;
      s = cmd;

      if((*cmd == 'b' || *cmd == 'x') && *(cmd+1) != 'c'){
        // If blocking ckpt, next letter must be 'c'; else print the usage
        fprintf(stderr, theUsage, "");
        return 1;
      } else if (*cmd == 's' || *cmd == 'i' || *cmd == 'c' || *cmd == 'b' ||
                 *cmd == 'x' || *cmd == 'k' || *cmd == 'q') {
        request = s;
        if (*cmd == 'i') {
	  if (isdigit(cmd[1])) { // if -i5, for example
	    interval = cmd+1;
	  } else { // else -i 5
            if (argc == 1) {
              fprintf(stderr, theUsage, "");
              return 1;
            }
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

  int coordCmdStatus = CoordCmdStatus::NOERROR;
  int numPeers;
  int isRunning;
  CoordinatorAPI coordinatorAPI;
  char *cmd = (char *)request.c_str();
  switch (*cmd) {
  case 'h':
    fprintf(stderr, theUsage, "");
    return 1;
  case 'i':
    setenv(ENV_VAR_CKPT_INTR, interval.c_str(), 1);
    coordinatorAPI.connectAndSendUserCommand(*cmd, &coordCmdStatus);
    printf("Interval changed to %s\n", interval.c_str());
    break;
  case 'b':
  case 'x':
    // blocking prefix
    coordinatorAPI.connectAndSendUserCommand(*cmd, &coordCmdStatus);
    // actual command
    coordinatorAPI.connectAndSendUserCommand(*(cmd+1), &coordCmdStatus);
    break;
  case 's':
    coordinatorAPI.connectAndSendUserCommand(*cmd, &coordCmdStatus,
                                             &numPeers, &isRunning);
  case 'c':
  case 'k':
  case 'q':
    coordinatorAPI.connectAndSendUserCommand(*cmd, &coordCmdStatus);
    break;
  }

  //check for error
  if (coordCmdStatus != CoordCmdStatus::NOERROR) {
    switch(coordCmdStatus){
    case CoordCmdStatus::ERROR_COORDINATOR_NOT_FOUND:
      if (getenv("DMTCP_PORT"))
        fprintf(stderr, "Coordinator not found. Please check port and host.\n");
      else
        fprintf(stderr,
	      "Coordinator not found. Try specifying port with \'--port\'.\n");
      break;
    case CoordCmdStatus::ERROR_INVALID_COMMAND:
      fprintf(stderr,
	      "Unknown command: %c, try 'dmtcp_command --help'\n", *cmd);
      break;
    case CoordCmdStatus::ERROR_NOT_RUNNING_STATE:
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
    printf("NUM_PEERS=%d\n", numPeers);
    printf("RUNNING=%s\n", (isRunning?"yes":"no"));
  }

  return 0;
}

