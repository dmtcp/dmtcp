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

#include "dmtcpaware.h"
#include "dmtcpcoordinatorapi.h"
#include "dmtcpworker.h"
#include "dmtcpmessagetypes.h"
#include "dmtcp_coordinator.h"
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

//I wish we could use pthreads for the trickery in this file, but much of our
//code is executed before the thread we want to wake is restored.  Thus we do
//it the bad way.
static inline void memfence(){  asm volatile ("mfence" ::: "memory"); }

//needed for sizeof()
static const dmtcp::DmtcpMessage * const exampleMessage = NULL;

static inline void _runCoordinatorCmd(char c, int* result){
  _dmtcp_lock();
  {
    dmtcp::DmtcpCoordinatorAPI coordinatorAPI;
    coordinatorAPI.useAlternateCoordinatorFd();
    coordinatorAPI.connectAndSendUserCommand(c, result);
  }
  _dmtcp_unlock();
}

EXTERNC int dmtcpIsEnabled() { return 1; }

EXTERNC int dmtcpCheckpoint(){
  int rv = 0;
  int oldNumRestarts    = numRestarts;
  int oldNumCheckpoints = numCheckpoints;
  memfence(); //make sure the reads above don't get reordered

  if(dmtcpRunCommand('c')){ //request checkpoint
    //and wait for the checkpoint
    while(oldNumRestarts==numRestarts && oldNumCheckpoints==numCheckpoints){
      //nanosleep should get interrupted by checkpointing with an EINTR error
      //though there is a race to get to nanosleep() before the checkpoint
      struct timespec t = {1,0};
      nanosleep(&t, NULL);
      memfence();  //make sure the loop condition doesn't get optimized
    }
    rv = (oldNumRestarts==numRestarts ? DMTCP_AFTER_CHECKPOINT : DMTCP_AFTER_RESTART);
  }else{
  	/// TODO: Maybe we need to process it in some way????
    /// EXIT????
    /// -- Artem
    //	printf("\n\n\nError requesting checkpoint\n\n\n");
  }

  return rv;
}

EXTERNC int dmtcpRunCommand(char command){
  int result[sizeof(exampleMessage->params)/sizeof(int)];
  int i = 0;
  while (i < 100) {
    _runCoordinatorCmd(command, result);
  // if we got error result - check it
	// There is posibility that checkpoint thread
	// did not send state=RUNNING yet or Coordinator did not receive it
	// -- Artem
    if (result[0] == dmtcp::DmtcpCoordinator::ERROR_NOT_RUNNING_STATE) {
      struct timespec t;
      t.tv_sec = 0;
      t.tv_nsec = 1000000;
      nanosleep(&t, NULL);
      //printf("\nWAIT FOR CHECKPOINT ABLE\n\n");
    } else {
//      printf("\nEverything is OK - return\n");
      break;
    }
    i++;
  }
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
  //these must be static so their memory is not deleted.
  static dmtcp::string ckpt;
  static dmtcp::string pid;
  static DmtcpLocalStatus status;
  ckpt.reserve(1024);

  //get filenames
  pid=dmtcp::UniquePid::ThisProcess().toString();
  ckpt=dmtcp::UniquePid::checkpointFilename();

  status.numCheckpoints          = numCheckpoints;
  status.numRestarts             = numRestarts;
  status.checkpointFilename      = ckpt.c_str();
  status.uniquePidStr            = pid.c_str();
  return &status;
}

EXTERNC int dmtcpInstallHooks( DmtcpFunctionPointer preCheckpoint
                              , DmtcpFunctionPointer postCheckpoint
                              , DmtcpFunctionPointer postRestart){
  userHookPreCheckpoint  = preCheckpoint;
  userHookPostCheckpoint = postCheckpoint;
  userHookPostRestart    = postRestart;
  return 1;
}

EXTERNC int dmtcpDelayCheckpointsLock(){
  dmtcp::DmtcpWorker::delayCheckpointsLock();
  return 1;
}

EXTERNC int dmtcpDelayCheckpointsUnlock(){
  dmtcp::DmtcpWorker::delayCheckpointsUnlock();
  return 1;
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

extern "C" int __dynamic_dmtcpIsEnabled(){
  return 3;
}

//these dummy trampolines support static linking libdmtcpaware.a
//see dmtcpaware.c
EXTERNC int __dyn_dmtcpIsEnabled(){
  return dmtcpIsEnabled();
}
EXTERNC int __dyn_dmtcpCheckpoint(){
  return dmtcpCheckpoint();
}
EXTERNC int __dyn_dmtcpRunCommand(char command){
  return dmtcpRunCommand(command);
}
EXTERNC int __dyn_dmtcpDelayCheckpointsLock(){
  return dmtcpDelayCheckpointsLock();
}
EXTERNC int __dyn_dmtcpDelayCheckpointsUnlock(){
  return dmtcpDelayCheckpointsUnlock();
}
EXTERNC int __dyn_dmtcpInstallHooks( DmtcpFunctionPointer preCheckpoint
                                    ,  DmtcpFunctionPointer postCheckpoint
                                    ,  DmtcpFunctionPointer postRestart){
  return dmtcpInstallHooks(preCheckpoint, postCheckpoint, postRestart);
}
EXTERNC const DmtcpCoordinatorStatus* __dyn_dmtcpGetCoordinatorStatus(){
  return dmtcpGetCoordinatorStatus();
}
EXTERNC const DmtcpLocalStatus* __dyn_dmtcpGetLocalStatus(){
  return dmtcpGetLocalStatus();
}


