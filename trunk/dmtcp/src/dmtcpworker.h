/***************************************************************************
 *   Copyright (C) 2006 by Jason Ansel                                     *
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
#ifndef DMTCPDMTCPWORKER_H
#define DMTCPDMTCPWORKER_H

#include "jsocket.h"
#include "uniquepid.h"

namespace dmtcp
{

  class CheckpointCoordinator;

  class DmtcpWorker
  {
    public:
      static DmtcpWorker& instance();
      const dmtcp::UniquePid& coordinatorId() const;

      void waitForStage1Suspend();
      void waitForStage2Checkpoint();
      void waitForStage3Resume();
      void restoreSockets ( CheckpointCoordinator& coordinator );
      void postRestart();

      static void resetOnFork();


      DmtcpWorker ( bool shouldEnableCheckpointing );
      ~DmtcpWorker();

      void forceRestart();

      static void maskStdErr();
      static void unmaskStdErr();
      static bool isStdErrMasked() { return _stdErrMasked; }
    protected:


      void connectToCoordinator();
    private:
      static DmtcpWorker theInstance;
    private:
      jalib::JSocket _coordinatorSocket;
      UniquePid      _coordinatorId;
      jalib::JSocket _restoreSocket;
      static bool _stdErrMasked;// = false;
  };

}

#endif
