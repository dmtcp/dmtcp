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
#ifndef DMTCPCHECKPOINTSTATE_H
#define DMTCPCHECKPOINTSTATE_H

#include "kernelbufferdrainer.h"
#include "connectionmanager.h"
#include "connectionrewirer.h"
#include  "../jalib/jsocket.h"

namespace dmtcp
{

  /**
  *  State of open connections, stored in checkpoint image
  */
  class ConnectionState
  {
    public:
      ConnectionState ( const ConnectionToFds& ctfd = ConnectionToFds() );

      void preCheckpointLock();
      void preCheckpointDrain();
      void preCheckpointHandshakes(const UniquePid& coordinator);
      void postCheckpoint();
      void outputDmtcpConnectionTable(int fd);

      void postRestart();
      void doReconnect ( jalib::JSocket& coordinator, jalib::JSocket& restoreListen );

    private:
      KernelBufferDrainer _drain;
      ConnectionToFds     _conToFds;
      ConnectionRewirer   _rewirer;
  };

}

#endif
