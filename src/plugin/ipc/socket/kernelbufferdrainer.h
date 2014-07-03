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
#ifndef KERNELBUFFERDRAINER_H
#define KERNELBUFFERDRAINER_H

#include <map>
#include <vector>

#include "dmtcpalloc.h"
#include "connectionidentifier.h"
#include "../jalib/jsocket.h"

namespace dmtcp
{

  class KernelBufferDrainer : public jalib::JMultiSocketProgram
  {
    public:
      KernelBufferDrainer() : _timeoutCount(0) {}
      static KernelBufferDrainer& instance();

//     void drainAllSockets();
      void beginDrainOf(int fd , const ConnectionIdentifier& id);
      void refillAllSockets();
      virtual void onData(jalib::JReaderInterface* sock);
      virtual void onConnect(const jalib::JSocket& sock, const struct sockaddr* remoteAddr,socklen_t remoteLen);
      virtual void onTimeoutInterval();
      virtual void onDisconnect(jalib::JReaderInterface* sock);

      const dmtcp::map<ConnectionIdentifier, vector<char> >&
        getDisconnectedSockets() const { return _disconnectedSockets; }

      const vector<char>& getDrainedData(ConnectionIdentifier id);

    private:
      dmtcp::map<int , dmtcp::vector<char> >    _drainedData;
      dmtcp::map<int , ConnectionIdentifier > _reverseLookup;
      dmtcp::map<ConnectionIdentifier, vector<char> > _disconnectedSockets;
      int _timeoutCount;
  };

}

#endif
