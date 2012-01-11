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

#include "dmtcpworker.h"
#include "mtcpinterface.h"
#include "dmtcpmessagetypes.h"
#include "dmtcpmodule.h"

// Initializing variable, theInstance, to an object of type DmtcpWorker,
//   with DmtcpWorker constructor called with arg, enableCheckpointing = true
// This gets executed before main().
dmtcp::DmtcpWorker dmtcp::DmtcpWorker::theInstance ( true );

void dmtcp::DmtcpWorker::resetOnFork(jalib::JSocket& coordSock)
{
  dmtcp_process_event(DMTCP_EVENT_RESET_ON_FORK, NULL);

  UniquePid coordinatorId = theInstance._coordinatorId;

  theInstance.cleanupWorker();
  shutdownMtcpEngineOnFork();

  /* If parent process had file connections and it fork()'d a child
   * process, the child process would consider the file connections as
   * pre-existing and hence wouldn't restore them. This is fixed by making sure
   * that when a child process is forked, it shouldn't be looking for
   * pre-existing connections because the parent has already done that.
   *
   * So, here while creating the instance, we do not want to execute everything
   * in the constructor since it's not relevant. All we need to call is
   * connectToCoordinatorWithHandshake() and initializeMtcpEngine().
   */
  new ( &theInstance ) DmtcpWorker ( false );

  dmtcp::DmtcpWorker::_exitInProgress = false;

  WorkerState::setCurrentState ( WorkerState::RUNNING );
  instance()._coordinatorId = coordinatorId;
  instance().informCoordinatorOfNewProcessOnFork(coordSock);

  //WRAPPER_EXECUTION_GET_EXCL_LOCK();
  initializeMtcpEngine();
  //WRAPPER_EXECUTION_RELEASE_EXCL_LOCK();

  /* Now wait for Checkpoint Thread to finish initialization
   * NOTE: This should be the last thing in this function
   */
  while (!ThreadSync::isCheckpointThreadInitialized()) {
    struct timespec sleepTime = {0, 10*1000*1000};
    nanosleep(&sleepTime, NULL);
  }
}

//to allow linking without mtcpinterface
void __attribute__ ((weak)) dmtcp::initializeMtcpEngine()
{
  JASSERT(false).Text("should not be called");
}
dmtcp::DmtcpWorker& dmtcp::DmtcpWorker::instance() { return theInstance; }

