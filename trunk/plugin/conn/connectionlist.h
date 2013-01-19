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
#ifndef CONNECTIONLIST_H
#define CONNECTIONLIST_H

#include "dmtcpalloc.h"
#include "connection.h"
#include "../jalib/jserialize.h"
#include "../jalib/jalloc.h"

namespace dmtcp
{
  class ConnectionList
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      ConnectionList() {}
      typedef dmtcp::map<ConnectionIdentifier, Connection*>::iterator iterator;
      iterator begin() { return _connections.begin(); }
      iterator end() { return _connections.end(); }
      static ConnectionList& instance();
      void resetOnFork();
      void deleteStaleConnections();
      void erase(iterator i);
      void erase(ConnectionIdentifier& key);
      Connection& operator[](const ConnectionIdentifier& id);
      Connection *getConnection(const ConnectionIdentifier &id);
      Connection *getConnection(int fd);
      dmtcp::vector<int>& getFds(const ConnectionIdentifier& c);
      void processClose(int fd);
      void processDup(int oldfd, int newfd);
      void processFileConnection(int fd, const char *path, int flags,
                                 mode_t mode);
      void list();
      void serialize(jalib::JBinarySerializer& o);

      //examine /proc/self/fd for unknown connections
      void scanForPreExisting();
      void add(int fd, Connection* c);

      // Moved from ConnectionState
      void preLockSaveOptions();
      void preCheckpointFdLeaderElection();
      void preCheckpointDrain();
      void preCheckpointHandshakes();
      void refill(bool isRestart);
      void resume(bool isRestart);
      void postRestart();
      void doReconnect();
      void registerNSData();
      void sendQueries();

      void registerMissingCons();
      void sendReceiveMissingFds();

    private:
      typedef map<ConnectionIdentifier, Connection*> ConnectionMapT;
      ConnectionMapT _connections;

      typedef map<int, Connection*> FdToConMapT;
      FdToConMapT _fdToCon;
  };
}
#endif
