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

#include "dmtcpaware.h"
#include "dmtcpworker.h"
#include "dmtcpmessagetypes.h"
#include "syscallwrappers.h"
#include "mtcpinterface.h"
#include <string>
#include <unistd.h>
#include <time.h>
//#include <pthread.h>

#ifndef EXTERNC
# define EXTERNC extern "C"
#endif

//global counters
static int numCheckpoints = 0;
static int numRestarts    = 0;

//user hook functions
static DmtcpFunctionPointer userHookPreCheckpoint = NULL;
static DmtcpFunctionPointer userHookPostCheckpoint = NULL;
static DmtcpFunctionPointer userHookPostRestart = NULL;

//I wish we could use pthreads for the trickery in this file, but much of our code 
//is execute before the thread we want to wake is restored.  Thus we do it the bad way.
static inline void memfence(){  asm volatile ("mfence" ::: "memory"); }

//needed for sizeof()
static const dmtcp::DmtcpMessage * const exampleMessage = NULL;

static inline void _runCoordinatorCmd(char c, int* result){
  _dmtcp_lock();
  {
    dmtcp::DmtcpWorker worker(false);
    worker.useAlternateCoordinatorFd();
    worker.connectAndSendUserCommand(c, result);
  }
  _dmtcp_unlock();
}

EXTERNC int dmtcpIsEnabled() { return 1; }

EXTERNC int dmtcpCheckpointBlocking(){
  int rv = 0;
  int oldNumRestarts    = numRestarts;
  int oldNumCheckpoints = numCheckpoints;
  memfence(); //make sure the reads above dont get reordered

  if(dmtcpRunCommand('c')){ //request checkpoint
    //and wait for the checkpoint
    while(oldNumRestarts==numRestarts && oldNumCheckpoints==numCheckpoints){
      //nanosleep should get interupped by checkpointing with an EINTR error
      //though there is a race to get to nanosleep() before the checkpoint
      struct timespec t = {1,0};
      nanosleep(&t, NULL); 
      memfence();  //make sure the loop condition doesn't get optimized
    }
    rv = (oldNumRestarts==numRestarts ? DMTCP_AFTER_CHECKPOINT : DMTCP_AFTER_RESTART);
  }
  return rv;
}

EXTERNC int dmtcpRunCommand(char command){
  int result[sizeof(exampleMessage->params)/sizeof(int)];
  _runCoordinatorCmd(command,result);
  return result[0]>=0;
}

EXTERNC const DmtcpCoordinatorStatus* dmtcpGetCoordinatorStatus(){
  int result[sizeof(exampleMessage->params)/sizeof(int)];
  _runCoordinatorCmd('s',result);

  //must be static so memory is not deleted.
  static DmtcpCoordinatorStatus status;

  status.numProcesses = result[0];
  status.isRunning = result[1];
  return &status;
}

EXTERNC const DmtcpLocalStatus* dmtcpGetLocalStatus(){
  //these must be static so there memory is not deleted.
  static std::string mtcp;
  static std::string dmtcp;
  static std::string pid;
  static DmtcpLocalStatus status;
  mtcp.reserve(1024);
  
  //get filenames
  pid=dmtcp::UniquePid::ThisProcess().toString();
  mtcp=dmtcp::UniquePid::checkpointFilename();
  dmtcp=dmtcp::UniquePid::dmtcpCheckpointFilename();
  
  status.numCheckpoints          = numCheckpoints;
  status.numRestarts             = numRestarts;
  status.checkpointFilenameMtcp  = mtcp.c_str(); 
  status.checkpointFilenameDmtcp = dmtcp.c_str();
  status.uniquePidStr            = pid.c_str();
  return &status;
}

EXTERNC void dmtcpInstallHooks( DmtcpFunctionPointer preCheckpoint
                              , DmtcpFunctionPointer postCheckpoint
                              , DmtcpFunctionPointer postRestart){
  userHookPreCheckpoint  = preCheckpoint;
  userHookPostCheckpoint = postCheckpoint;
  userHookPostRestart    = postRestart; 
}

void dmtcp::userHookTrampoline_preCkpt() {
  if(userHookPreCheckpoint != NULL) 
    (*userHookPreCheckpoint)();
}

void dmtcp::userHookTrampoline_postCkpt(bool isRestart) {
  //this function runs before other threads are resumed
  if(isRestart){
    numRestarts++;
    if(userHookPostRestart != NULL) 
      (*userHookPostRestart)();
  }else{
    numCheckpoints++;
    if(userHookPostCheckpoint != NULL) 
      (*userHookPostCheckpoint)();
  }
}