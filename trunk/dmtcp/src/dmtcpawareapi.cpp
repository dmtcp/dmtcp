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

#include "dmtcpaware.h"
#include "dmtcpcoordinatorapi.h"
#include "dmtcpworker.h"
#include "dmtcpmessagetypes.h"
#include "dmtcp_coordinator.h"
#include "syscallwrappers.h"
#include "mtcpinterface.h"
#include "dmtcpalloc.h"
#include <string>
#include <unistd.h>
#include <time.h>
//#include <pthread.h>
#ifdef RECORD_REPLAY
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include  "../jalib/jfilesystem.h"
#include "synchronizationlogging.h"
#include "log.h"
#endif

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

#ifdef RECORD_REPLAY
static bool remap_logs = false;
#endif

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

/*
 * ___real_dmtcpXXX: Does the real work expected by dmtcpXXX
 * __dyn_dmtcpXXX:   Dummy trampolines to support static linking of user code
 *                   to libdmtcpaware.a
 * dmtcpXXX:         Dummy trampolines to support dynamic linking of user code
 *                   to libdmtcpaware.so
 *
 * NOTE: We do not want __dyn_dmtcpXXX for call dmtcpXXX functions directly
 * because if the user binary is compiled with -rdynamic and libdmtcpaware.a,
 * this would result in a call to libdmtcpaware.a:dmtcpXXX, thus creating an
 * infinite loop.
 */

#ifdef RECORD_REPLAY
int __real_dmtcp_userSynchronizedEvent()
{
  userSynchronizedEvent();
  return 1;
}

EXTERNC int dmtcp_userSynchronizedEventBegin()
{
  userSynchronizedEventBegin();
  return 1;
}

EXTERNC int dmtcp_userSynchronizedEventEnd()
{
  userSynchronizedEventEnd();
  return 1;
}
#endif

int __real_dmtcpIsEnabled() { return 1; }

int __real_dmtcpCheckpoint(){
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

int __real_dmtcpRunCommand(char command){
  int result[sizeof(exampleMessage->params)/sizeof(int)];
  int i = 0;
  while (i < 100) {
    _runCoordinatorCmd(command, result);
  // if we got error result - check it
	// There is possibility that checkpoint thread
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

const DmtcpCoordinatorStatus* __real_dmtcpGetCoordinatorStatus(){
  int result[sizeof(exampleMessage->params)/sizeof(int)];
  _runCoordinatorCmd('s',result);

  //must be static so memory is not deleted.
  static DmtcpCoordinatorStatus status;

  status.numProcesses = result[0];
  status.isRunning = result[1];
  return &status;
}

const DmtcpLocalStatus* __real_dmtcpGetLocalStatus(){
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

int __real_dmtcpInstallHooks( DmtcpFunctionPointer preCheckpoint
                              , DmtcpFunctionPointer postCheckpoint
                              , DmtcpFunctionPointer postRestart){
  userHookPreCheckpoint  = preCheckpoint;
  userHookPostCheckpoint = postCheckpoint;
  userHookPostRestart    = postRestart;
  return 1;
}

int __real_dmtcpDelayCheckpointsLock(){
  dmtcp::DmtcpWorker::delayCheckpointsLock();
  return 1;
}

int __real_dmtcpDelayCheckpointsUnlock(){
  dmtcp::DmtcpWorker::delayCheckpointsUnlock();
  return 1;
}

void dmtcp::userHookTrampoline_preCkpt() {
#ifdef RECORD_REPLAY
  if (SYNC_IS_REPLAY) {
    /* Checkpointing during replay -- we will truncate the log to the current
       position, and begin recording again. */
    truncate_all_logs();
  }
  set_sync_mode(SYNC_NOOP);
  log_all_allocs = 0;
  // Write the logs to disk, if any are in memory, and unmap them.
  close_all_logs();
  // Remove the threads which aren't alive anymore.
  {
    dmtcp::map<clone_id_t, pthread_t>::iterator it;
    dmtcp::vector<clone_id_t> stale_clone_ids;
    for (it = clone_id_to_tid_table.begin(); it != clone_id_to_tid_table.end(); it++) {
      if (_real_pthread_kill(it->second, 0) != 0) {
        stale_clone_ids.push_back(it->first);
      }
    }
    for (size_t i = 0; i < stale_clone_ids.size(); i++) {
      clone_id_to_tid_table.erase(stale_clone_ids[i]);
      clone_id_to_log_table.erase(stale_clone_ids[i]);
    }
  }
#endif
  if(userHookPreCheckpoint != NULL)
    (*userHookPreCheckpoint)();
}

void dmtcp::userHookTrampoline_postCkpt(bool isRestart) {
  //this function runs before other threads are resumed
#ifdef RECORD_REPLAY
    recordDataStackLocations();
#endif
  if(isRestart){
#ifdef RECORD_REPLAY
    set_sync_mode(SYNC_REPLAY);
    initLogsForRecordReplay();
    log_all_allocs = 1;
#endif
    numRestarts++;
    if(userHookPostRestart != NULL)
      (*userHookPostRestart)();
  }else{
#ifdef RECORD_REPLAY
    set_sync_mode(SYNC_RECORD);
    initLogsForRecordReplay();
    log_all_allocs = 1;
#endif
    numCheckpoints++;
    if(userHookPostCheckpoint != NULL)
      (*userHookPostCheckpoint)();
  }
}

extern "C" int __dynamic_dmtcpIsEnabled(){
  return 3;
}

//These dummy trampolines support static linking of user code to libdmtcpaware.a
//See dmtcpaware.c .
#ifdef RECORD_REPLAY
EXTERNC int __dyn_dmtcp_userSynchronizedEvent()
{
  return __real_dmtcp_userSynchronizedEvent();
}

EXTERNC int __dyn_dmtcp_userSynchronizedEventBegin()
{
  return dmtcp_userSynchronizedEventBegin();
}

EXTERNC int __dyn_dmtcp_userSynchronizedEventEnd()
{
  return dmtcp_userSynchronizedEventEnd();
}
#endif
EXTERNC int __dyn_dmtcpIsEnabled(){
  return __real_dmtcpIsEnabled();
}
EXTERNC int __dyn_dmtcpCheckpoint(){
  return __real_dmtcpCheckpoint();
}
EXTERNC int __dyn_dmtcpRunCommand(char command){
  return __real_dmtcpRunCommand(command);
}
EXTERNC int __dyn_dmtcpDelayCheckpointsLock(){
  return __real_dmtcpDelayCheckpointsLock();
}
EXTERNC int __dyn_dmtcpDelayCheckpointsUnlock(){
  return __real_dmtcpDelayCheckpointsUnlock();
}
EXTERNC int __dyn_dmtcpInstallHooks( DmtcpFunctionPointer preCheckpoint
                                    ,  DmtcpFunctionPointer postCheckpoint
                                    ,  DmtcpFunctionPointer postRestart){
  return __real_dmtcpInstallHooks(preCheckpoint, postCheckpoint, postRestart);
}
EXTERNC const DmtcpCoordinatorStatus* __dyn_dmtcpGetCoordinatorStatus(){
  return __real_dmtcpGetCoordinatorStatus();
}
EXTERNC const DmtcpLocalStatus* __dyn_dmtcpGetLocalStatus(){
  return __real_dmtcpGetLocalStatus();
}


//These dummy trampolines support dynamic linking of user code to libdmtcpaware.so
#ifdef RECORD_REPLAY
EXTERNC int dmtcp_userSynchronizedEvent()
{
  return __real_dmtcp_userSynchronizedEvent();
}
#endif
EXTERNC int dmtcpIsEnabled(){
  return __real_dmtcpIsEnabled();
}
EXTERNC int dmtcpCheckpoint(){
  return __real_dmtcpCheckpoint();
}
EXTERNC int dmtcpRunCommand(char command){
  return __real_dmtcpRunCommand(command);
}
EXTERNC int dmtcpDelayCheckpointsLock(){
  return __real_dmtcpDelayCheckpointsLock();
}
EXTERNC int dmtcpDelayCheckpointsUnlock(){
  return __real_dmtcpDelayCheckpointsUnlock();
}
EXTERNC int dmtcpInstallHooks( DmtcpFunctionPointer preCheckpoint,
                               DmtcpFunctionPointer postCheckpoint,
                               DmtcpFunctionPointer postRestart){
  return __real_dmtcpInstallHooks(preCheckpoint, postCheckpoint, postRestart);
}
EXTERNC const DmtcpCoordinatorStatus* dmtcpGetCoordinatorStatus(){
  return __real_dmtcpGetCoordinatorStatus();
}
EXTERNC const DmtcpLocalStatus* dmtcpGetLocalStatus(){
  return __real_dmtcpGetLocalStatus();
}
