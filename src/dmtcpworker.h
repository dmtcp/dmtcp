/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef DMTCPDMTCPWORKER_H
#define DMTCPDMTCPWORKER_H

#include "jalloc.h"
#include "dmtcpalloc.h"
#include "dmtcpmessagetypes.h"

void restoreUserLDPRELOAD();

namespace dmtcp
{
  class DmtcpWorker
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      DmtcpWorker();
      ~DmtcpWorker();
      static DmtcpWorker& instance();

      static void waitForCoordinatorMsg(string signalStr,
                                        DmtcpMessageType type);
      static void informCoordinatorOfRUNNINGState();
      static void waitForStage1Suspend();
      static void waitForStage2Checkpoint();
      static void waitForStage3Refill(bool isRestart);
      static void waitForStage4Resume(bool isRestart);
      static void restoreVirtualPidTable();
      static void postRestart();

      static void resetOnFork();
      static void cleanupWorker();

      static int determineCkptSignal();

      static void setExitInProgress() { _exitInProgress = true; };
      static bool exitInProgress() { return _exitInProgress; };
      static void interruptCkpthread();

      static void writeCheckpointPrefix(int fd);

      static void eventHook(DmtcpEvent_t id, DmtcpEventData_t *data);

    protected:
      static void sendUserCommand(char c, int* result = NULL);
    private:
      static DmtcpWorker theInstance;
      static bool _exitInProgress;
  };
}

#endif
