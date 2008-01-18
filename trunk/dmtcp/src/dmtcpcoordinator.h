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
#ifndef DMTCPDMTCPCOORDINATOR_H
#define DMTCPDMTCPCOORDINATOR_H

#include "jsocket.h"
#include "nodetable.h"
#include "dmtcpmessagetypes.h"

namespace dmtcp
{

  class DmtcpCoordinator : public jalib::JMultiSocketProgram
  {
    public:
      virtual void onData ( jalib::JReaderInterface* sock );
      virtual void onConnect ( const jalib::JSocket& sock, const struct sockaddr* remoteAddr,socklen_t remoteLen );
      virtual void onDisconnect ( jalib::JReaderInterface* sock );
      virtual void onTimeoutInterval();
      void broadcastMessage ( DmtcpMessageType type );
      void broadcastMessage ( const DmtcpMessage& msg );
      void startCheckpoint();
      dmtcp::WorkerState minimumState() const;
    protected:
      void writeRestartScript();
    private:
      typedef std::vector<jalib::JReaderInterface*>::iterator iterator;
      typedef std::vector<jalib::JReaderInterface*>::const_iterator const_iterator;
//     NodeTable _table;
      std::vector< DmtcpMessage > _restoreWaitingMessages;

      //map from hostname to checkpoint files
      std::map< std::string, std::vector<std::string> > _restartFilenames;

  };

}

#endif
