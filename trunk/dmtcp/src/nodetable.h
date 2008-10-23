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
#ifndef DMTCPNODETABLE_H
#define DMTCPNODETABLE_H

#include "uniquepid.h"
#include "dmtcpmessagetypes.h"
#include  "../jalib/jsocket.h"

#include <map>

// namespace dmtcp {
//
// class WorkerNode{
// public:
//     void setId(const UniquePid& theValue);
//     UniquePid id() const;
//     void setState(const WorkerState& theValue);
//     WorkerState state() const;
//
//     WorkerNode():_addrlen(0),_restorePort(0),_clientNumer(-1){memset(&_addr,0,sizeof(_addr));}
//
//
//
//     void setAddr(const struct sockaddr* theValue,socklen_t len);
//
//
//     const struct sockaddr* addr() const;
//     socklen_t addrlen() const;
//
//     void setRestorePort(const int& theValue);
//
//     int restorePort() const;
//
//     void setClientNumer(const int& theValue);
//
//
//     int clientNumer() const;
//
//
//
//
//
// private:
//     UniquePid _id;
//     WorkerState _state;
//     struct sockaddr_storage _addr;
//     socklen_t               _addrlen;
//     int                     _restorePort;
//     int                     _clientNumer;
// };
//
//
// class NodeTable{
// public:
//     dmtcp::WorkerNode& operator[] (const UniquePid& id);
//     dmtcp::WorkerState minimumState() const;
//     dmtcp::WorkerState maximumState() const;
//     size_t size() const {return _table.size();}
//     void removeClient(int clientNumer);
//
//     void dbgPrint() const;
// private:
//     std::map<UniquePid, WorkerNode> _table;
// };
//
//
// }

#endif
