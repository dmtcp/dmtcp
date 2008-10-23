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
