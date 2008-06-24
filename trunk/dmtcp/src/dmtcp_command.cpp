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
#include "dmtcpcoordinator.h"
#include "dmtcpmessagetypes.h"
#include "mtcpinterface.h"

using namespace dmtcp;

static const DmtcpMessage * exampleMessage = NULL;
static const char* theUsage = 
  "USAGE: dmtcp_command COMMAND\n"
  "  where COMMAND is one of:\n"
  "    s : Print status message\n"
  "    c : Checkpoint all nodes\n"
  "    f : Force a restart even if there are missing nodes (debugging only)\n"
  "    k : Kill all nodes\n"
  "    q : Kill all nodes and quit\n"
;

int main ( int argc, char** argv )
{
  if( argc != 2 ){
    fprintf(stderr, theUsage);
    return 1;
  }

  const char* cmd = argv[1];
  //ignore leading dashes
  while(*cmd == '-') cmd++;

  if(*cmd == 'h' || *cmd == '\0' || *cmd == '?'){
    fprintf(stderr, theUsage);
    return 1;
  }
  
  int result[sizeof(exampleMessage->params)/sizeof(int)];
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

//needed to link
void dmtcp::initializeMtcpEngine()
{
  JASSERT ( "false" ).Text ( "should not be called in dmtcp_restart" );
}
