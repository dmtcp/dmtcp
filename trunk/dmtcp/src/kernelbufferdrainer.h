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

#ifndef DMTCPKERNELBUFFERDRAINER_H
#define DMTCPKERNELBUFFERDRAINER_H

#include <map>
#include <vector>

#include  "../jalib/jsocket.h"
#include "connectionidentifier.h"

namespace dmtcp
{

  class KernelBufferDrainer : public jalib::JMultiSocketProgram
  {
    public:
      KernelBufferDrainer() : _timeoutCount(0) {}
//     void drainAllSockets();
      void beginDrainOf ( int fd , const ConnectionIdentifier& id);
      void refillAllSockets();
      virtual void onData ( jalib::JReaderInterface* sock );
      virtual void onConnect ( const jalib::JSocket& sock, const struct sockaddr* remoteAddr,socklen_t remoteLen );
      virtual void onTimeoutInterval();
      virtual void onDisconnect ( jalib::JReaderInterface* sock );

      const std::vector<ConnectionIdentifier>& getDisconnectedSockets() const { return _disconnectedSockets; }

    private:
      std::map<int , std::vector<char> >    _drainedData;
      std::map<int , ConnectionIdentifier > _reverseLookup;
      std::vector<ConnectionIdentifier>     _disconnectedSockets;
      int _timeoutCount;
  };

}

#endif
