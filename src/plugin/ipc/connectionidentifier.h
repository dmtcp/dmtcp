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
#ifndef CONNECTIONIDENTIFIER_H
#define CONNECTIONIDENTIFIER_H

#include <stdint.h>
#include "jalloc.h"
#include "jserialize.h"
#include "dmtcp.h"
#include "dmtcpalloc.h"
#include "ipc.h"

namespace dmtcp
{
class ConnectionIdentifier
{
  public:
# ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
# endif // ifdef JALIB_ALLOCATOR
    static ConnectionIdentifier create();
    static ConnectionIdentifier null();
    static ConnectionIdentifier self();

    static void serialize(jalib::JBinarySerializer &o);

    uint64_t hostid() const { return _upid._hostid; }

    pid_t pid() const { return _upid._pid; }

    uint64_t time() const { return _upid._time; }

    int64_t conId() const { return _id; }

    // int conId() const;
    // const UniquePid& pid() const;

    ConnectionIdentifier(int id = -1);
    ConnectionIdentifier(DmtcpUniqueProcessId id)
    {
      _upid = id;
      _id = -1;
    }

    bool isNull() const { return _id < 0; }

    bool operator==(const ConnectionIdentifier &that) const;
    bool operator<(const ConnectionIdentifier &that) const;
    bool operator!=(const ConnectionIdentifier &that) const
    { return !(*this == that); }

  private:
    DmtcpUniqueProcessId _upid;
    int64_t _id;
};

ostream&operator<<(ostream &o, const ConnectionIdentifier &id);
}
#endif // ifndef CONNECTIONIDENTIFIER_H
