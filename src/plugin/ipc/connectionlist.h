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
# define CONNECTIONLIST_H

#include "jalloc.h"
#include "jserialize.h"
#include "connection.h"
#include "dmtcpalloc.h"
#include "protectedfds.h"

namespace dmtcp
{
class ConnectionList
{
  public:
# ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
# endif // ifdef JALIB_ALLOCATOR
    typedef map<ConnectionIdentifier, Connection *>::iterator iterator;

    ConnectionList()
    {
      numIncomingCons = 0;
      DmtcpMutexInit(&_lock, DMTCP_MUTEX_NORMAL);
    }

    virtual ~ConnectionList();

    void resetOnFork();
    void deleteStaleConnections();

    void add(int fd, Connection *c);
    void erase(iterator i);
    void erase(ConnectionIdentifier &key);
    Connection *getConnection(const ConnectionIdentifier &id);
    Connection *getConnection(int fd);
    void processClose(int fd);
    void processDup(int oldfd, int newfd);
    void list();
    void serialize(jalib::JBinarySerializer &o);

    void eventHook(DmtcpEvent_t event, DmtcpEventData_t *data);
    virtual void scanForPreExisting() {}

    virtual void preLockSaveOptions();
    virtual void preCkptFdLeaderElection();
    virtual void drain();
    virtual void preCkpt();
    virtual void postRestart();
    virtual void registerNSData() {}

    virtual void sendQueries() {}

    virtual void refill(bool isRestart);
    virtual void resume(bool isRestart);

    void ckptRefill() { refill(false); }

    void ckptResume() { resume(false); }

    void postRestartRefill() { refill(true); }

    void postRestartResume() { resume(true); }

    void registerIncomingCons();
    void determineOutgoingCons();
    void sendReceiveMissingFds();
    virtual int protectedFd() = 0;

  protected:
    virtual Connection *createDummyConnection(int type) = 0;
    iterator begin() { return _connections.begin(); }

    iterator end() { return _connections.end(); }

  private:
    void processCloseWork(int fd);
    void _lock_tbl()
    {
      JASSERT(DmtcpMutexLock(&_lock) == 0);
    }

    void _unlock_tbl()
    {
      JASSERT(DmtcpMutexUnlock(&_lock) == 0);
    }

    DmtcpMutex _lock;
    typedef map<ConnectionIdentifier, Connection *>ConnectionMapT;
    ConnectionMapT _connections;

    typedef map<int, Connection *>FdToConMapT;
    FdToConMapT _fdToCon;

    size_t numIncomingCons;
};
}
#endif // ifndef CONNECTIONLIST_H
