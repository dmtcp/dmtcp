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

#pragma once
#ifndef CONNECTIONREWIRER_H
#define CONNECTIONREWIRER_H

#include <sys/socket.h>
#include "dmtcpalloc.h"
#include "connectionidentifier.h"
#include "connection.h"

namespace dmtcp
{

  class ConnectionRewirer
  {
    public:
      struct RemoteAddr {
        struct sockaddr_storage addr;
        socklen_t len;
        Connection *con;
      };

      static ConnectionRewirer& instance();
      static void destroy();

      void openRestoreSocket();
      void registerIncoming(const ConnectionIdentifier& local,
                            Connection *con);
      void registerOutgoing(const ConnectionIdentifier& remote,
                            Connection *con);
      void registerNSData();
      void sendQueries();
      void doReconnect();
      void checkForPendingIncoming();

      void debugPrint() const;

    private:
      struct sockaddr_storage _restoreAddr;
      socklen_t               _restoreAddrlen;

      typedef map<ConnectionIdentifier, Connection*> ConnectionListT;
      typedef ConnectionListT::iterator iterator;
      typedef ConnectionListT::const_iterator const_iterator;
      typedef map<ConnectionIdentifier, struct RemoteAddr> RemoteInfoT;
      typedef RemoteInfoT::iterator remoteInfoIter;

      ConnectionListT _pendingIncoming;
      ConnectionListT _pendingOutgoing;
      RemoteInfoT     _remoteInfo;
  };

}

#endif
