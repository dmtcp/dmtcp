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

#ifndef DMTCPCONNECTIONREWIRER_H
#define DMTCPCONNECTIONREWIRER_H

#include "dmtcpalloc.h"
#include  "../jalib/jsocket.h"
#include "connectionidentifier.h"
#include "connection.h"
#include <map>
#include <set>
#include <vector>

namespace dmtcp
{

  class ConnectionRewirer : public jalib::JMultiSocketProgram
  {
    public:
      ConnectionRewirer() : _coordFd(-1), _restorePort(-1) {}

      void openRestoreSocket();
      void doReconnect();
      void registerIncoming(const ConnectionIdentifier& local,
                            Connection *con);
      void registerOutgoing(const ConnectionIdentifier& remote,
                            Connection *con);


    protected:

      virtual void onData(jalib::JReaderInterface* sock);
      virtual void onConnect(const jalib::JSocket& sock,
                             const struct sockaddr* /*remoteAddr*/,
                             socklen_t /*remoteLen*/);
      virtual void onDisconnect(jalib::JReaderInterface* sock);

      void finishup();

      size_t pendingCount() const
      { return _pendingIncoming.size() + _pendingOutgoing.size(); }

      void debugPrint() const;

    private:
      int _coordFd;
      int _restorePort;

      typedef map<ConnectionIdentifier, Connection*> ConnectionListT;
      typedef ConnectionListT::iterator iterator;
      typedef ConnectionListT::const_iterator const_iterator;
      ConnectionListT _pendingIncoming;
      ConnectionListT _pendingOutgoing;
  };

}

#endif
