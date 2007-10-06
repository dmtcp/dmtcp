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
#ifndef DMTCPCONNECTIONREWIRER_H
#define DMTCPCONNECTIONREWIRER_H

#include "jsocket.h"
#include "connectionidentifier.h"
#include <map>
#include <set>
#include <vector>

namespace dmtcp {

class ConnectionRewirer : public jalib::JMultiSocketProgram  {
public:
    ConnectionRewirer() : _masterFd( -1 ){}

    void setMasterFd(const int& theValue);
    int masterFd() const;

    void doReconnect();
    
    void registerIncoming(const ConnectionIdentifier& local
                        , const std::vector<int>& fds);
    
    void registerOutgoing(const ConnectionIdentifier& remote
                        , const std::vector<int>& fds);
    
    
protected:
    
    virtual void onData(jalib::JReaderInterface* sock);
     
    virtual void onConnect(const jalib::JSocket& sock,  const struct sockaddr* /*remoteAddr*/,socklen_t /*remoteLen*/);
  
    virtual void onDisconnect(jalib::JReaderInterface* sock);
    
    void finishup();
      
    size_t pendingCount() const { return _pendingIncoming.size() + _pendingOutgoing.size(); }
    
    void debugPrint() const;
            
private:
   int _masterFd;
   std::map<ConnectionIdentifier, std::vector<int> > _pendingIncoming;
   std::map<ConnectionIdentifier, std::vector<int> > _pendingOutgoing;
   typedef std::map<ConnectionIdentifier, std::vector<int> >::iterator iterator;
   typedef std::map<ConnectionIdentifier, std::vector<int> >::const_iterator const_iterator;
};

}

#endif
