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

#ifndef DMTCPCONNECTIONIDENTIFIER_H
#define DMTCPCONNECTIONIDENTIFIER_H

#include "constants.h"
#include "dmtcpalloc.h"
#include "uniquepid.h"
#include "../jalib/jalloc.h"

namespace dmtcp
{
  class ConnectionIdentifier
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      static ConnectionIdentifier Create();
      static ConnectionIdentifier Null();
      static ConnectionIdentifier Self();

      static void serialize ( jalib::JBinarySerializer& o );

      int conId() const;
      const UniquePid& pid() const;

      ConnectionIdentifier ( const UniquePid& pid = UniquePid(), int id = -1 );

      bool isNull() const { return _id < 0; }
    private:
      UniquePid _pid;
      int _id;
  };


  bool operator<(const ConnectionIdentifier& a, const ConnectionIdentifier& b);
  bool operator==(const ConnectionIdentifier& a, const ConnectionIdentifier& b);
  inline bool operator!=(const ConnectionIdentifier& a,
                         const ConnectionIdentifier& b)
    { return ! ( a == b ); }

}

namespace std
{
  inline dmtcp::ostream& operator<<(dmtcp::ostream& o,
                                    const dmtcp::ConnectionIdentifier& i)
  {
    o << i.pid() << '(' << i.conId() << ')';
    return o;
  }
}

#endif
