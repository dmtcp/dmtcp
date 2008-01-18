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
#include "nodetable.h"


#include <algorithm>
/*
dmtcp::UniquePid dmtcp::WorkerNode::id() const
{
  return _id;
}


void dmtcp::WorkerNode::setId(const UniquePid& theValue)
{
  _id = theValue;
}


dmtcp::WorkerState dmtcp::WorkerNode::state() const
{
  return _state;
}


void dmtcp::WorkerNode::setState(const WorkerState& theValue)
{
  _state = theValue;
}


dmtcp::WorkerNode& dmtcp::NodeTable::operator[] (const UniquePid& id)
{
    dmtcp::WorkerNode& node = _table[id];
    node.setId( id );
    return node;
}




socklen_t dmtcp::WorkerNode::addrlen() const
{
  return _addrlen;
}



const struct sockaddr* dmtcp::WorkerNode::addr() const
{
    return (sockaddr*)&_addr;
}


void dmtcp::WorkerNode::setAddr(const struct sockaddr* theValue,socklen_t len)
{
  memcpy(&_addr,theValue,len);
  _addrlen = len;
}


int dmtcp::WorkerNode::restorePort() const
{
  return _restorePort;
}


void dmtcp::WorkerNode::setRestorePort(const int& theValue)
{
  _restorePort = theValue;
}




int dmtcp::WorkerNode::clientNumer() const
{
  return _clientNumer;
}


void dmtcp::WorkerNode::setClientNumer(const int& theValue)
{
  _clientNumer = theValue;
}

dmtcp::WorkerState dmtcp::NodeTable::minimumState() const
{
    int t = 999999;
    for(std::map<UniquePid, WorkerNode>::const_iterator i = _table.begin()
      ;i != _table.end()
      ;++i)
    {
        t = std::min(t,(int)i->second.state().value());
    }
    return (WorkerState::eWorkerState)t;
}

dmtcp::WorkerState dmtcp::NodeTable::maximumState() const
{
    int t = 0;
    for(std::map<UniquePid, WorkerNode>::const_iterator i = _table.begin()
      ;i != _table.end()
      ;++i)
    {
       t = std::max(t,(int)i->second.state().value());
    }
    return (WorkerState::eWorkerState)t;
}

void dmtcp::NodeTable::removeClient(int clientNumer)
{
    for( std::map<UniquePid, WorkerNode>::iterator i = _table.begin()
       ; i != _table.end()
       ; ++i)
    {
        if(i->second.clientNumer() == clientNumer)
        {
            _table.erase(i);
            return;
        }
    }
    JWARNING(false)(clientNumer).Text("failed to remove client from table");
}



void dmtcp::NodeTable::dbgPrint() const
{
    JASSERT_STDERR << "Listing table entries..." << std::endl;
    for( std::map<UniquePid, WorkerNode>::const_iterator i = _table.begin()
       ; i != _table.end()
       ; ++i)
    {
        JASSERT_STDERR << "Entry: clientNumber=" << i->second.clientNumer()
                << " " <<  i->first << std::endl;
    }
}

*/
