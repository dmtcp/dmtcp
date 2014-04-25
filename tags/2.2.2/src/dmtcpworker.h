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
      DmtcpWorker ( bool shouldEnableCheckpointing );
      ~DmtcpWorker();
      static DmtcpWorker& instance();

      void waitForCoordinatorMsg(dmtcp::string signalStr,
                                 DmtcpMessageType type);
      void informCoordinatorOfRUNNINGState();
      void waitForStage1Suspend();
      void waitForStage2Checkpoint();
      void waitForStage3Refill(bool isRestart);
      void waitForStage4Resume(bool isRestart);
      void restoreVirtualPidTable();
      void postRestart();

      static void resetOnFork();
      void cleanupWorker();

      static int determineCkptSignal();

      static void setExitInProgress() { _exitInProgress = true; };
      static bool exitInProgress() { return _exitInProgress; };
      void interruptCkpthread();

      void writeCheckpointPrefix(int fd);

      static void eventHook(DmtcpEvent_t id, DmtcpEventData_t *data);

    protected:
      void sendUserCommand(char c, int* result = NULL);
    private:
      static DmtcpWorker theInstance;
      static bool _exitInProgress;
  };
}

#endif
