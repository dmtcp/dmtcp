/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, Gene Cooperman,    *
 *                                                           and Rohan Garg *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu, and         *
 *                                                      rohgarg@ccs.neu.edu *
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
#ifndef CONNECTION_H
# define CONNECTION_H

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "jalloc.h"
#include "jserialize.h"
#include "config.h"
#include "connectionidentifier.h"
#include "dmtcpalloc.h"

namespace dmtcp
{
class Connection
{
  public:
# ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
# endif // ifdef JALIB_ALLOCATOR
    enum ConnectionType {
      INVALID  = 0x00000,
      TCP      = 0x10000,
      RAW      = 0x11000,
      PTY      = 0x20000,
      FILE     = 0x21000,
      STDIO    = 0x22000,
      FIFO     = 0x24000,
      EPOLL    = 0x30000,
      EVENTFD  = 0x31000,
      SIGNALFD = 0x32000,
      INOTIFY  = 0x34000,
      POSIXMQ  = 0x40000,
      TYPEMASK = TCP | RAW | PTY | FILE | STDIO | FIFO | EPOLL | EVENTFD |
        SIGNALFD | INOTIFY | POSIXMQ
    };

    Connection() {}

    virtual ~Connection() {}

    void addFd(int fd);
    void removeFd(int fd);
    void restoreDupFds(int fd);

    uint32_t numFds() const { return _fds.size(); }

    const vector<int32_t> &getFds() const { return _fds; }

    uint32_t conType() const { return _type & TYPEMASK; }

    uint32_t subType() const { return _type; }

    bool hasLock() { return _hasLock; }

    bool isStdio() { return conType() == STDIO; }

    void checkLocking();
    const ConnectionIdentifier &id() const { return _id; }

    virtual void saveOptions();
    virtual void doLocking();
    virtual void drain() = 0;
    virtual void preCkpt() {}

    virtual void refill(bool isRestart) = 0;
    virtual void resume(bool isRestart) {}

    virtual void postRestart() = 0;
    virtual bool isPreExistingCTTY() const { return false; }

    virtual void restoreOptions();

    virtual string str() = 0;

    void serialize(jalib::JBinarySerializer &o);

  protected:
    virtual void serializeSubClass(jalib::JBinarySerializer &o) = 0;

  protected:
    // only child classes can construct us...
    Connection(uint32_t t);

  protected:
    ConnectionIdentifier _id;
    uint32_t _type;
    int64_t _fcntlFlags;
    int64_t _fcntlOwner;
    int64_t _fcntlSignal;
    bool _hasLock;
    vector<int32_t> _fds;
};
}
#endif // ifndef CONNECTION_H
