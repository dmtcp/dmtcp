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

#ifndef DMTCPNODETABLE_H
#define DMTCPNODETABLE_H

#include "dmtcpalloc.h"
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
//     dmtcp::map<UniquePid, WorkerNode> _table;
// };
//
//
// }

#endif
