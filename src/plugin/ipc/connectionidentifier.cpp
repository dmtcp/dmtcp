/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include "connectionidentifier.h"
#include "../jalib/jassert.h"
#include "dmtcp.h"

using namespace dmtcp;

static int _nextConId = CONNECTION_ID_START;
static int
_nextConnectionId()
{
  // static int id = CONNECTION_ID_START;
  return _nextConId++;
}

ConnectionIdentifier::ConnectionIdentifier(int id)
{
  _upid = dmtcp_get_uniquepid();
  _id = id;
}

void
ConnectionIdentifier::serialize(jalib::JBinarySerializer &o)
{
  JSERIALIZE_ASSERT_POINT("ConnectionIdentifier:");
  o &_nextConId;
  JASSERT(_nextConId >= CONNECTION_ID_START);
}

ConnectionIdentifier
ConnectionIdentifier::create()
{
  return ConnectionIdentifier(_nextConnectionId());
}

ConnectionIdentifier
ConnectionIdentifier::null()
{
  static ConnectionIdentifier n;

  return n;
}

// FIXME:  This is never used.
ConnectionIdentifier
ConnectionIdentifier::self()
{
  return ConnectionIdentifier(-1);
}

bool
ConnectionIdentifier::operator<(const ConnectionIdentifier &that)
const
{
#define TRY_LEQ(param)                     \
  if (_upid.param != that._upid.param) {   \
    return _upid.param < that._upid.param; \
  }

  TRY_LEQ(_hostid);
  TRY_LEQ(_pid);
  TRY_LEQ(_time);
  return _id < that._id;
}

bool
ConnectionIdentifier::operator==(const ConnectionIdentifier &that)
const
{
  return _upid._hostid == that._upid._hostid &&
         _upid._pid == that._upid._pid &&
         _upid._time == that._upid._time &&
         _upid._computation_generation == that._upid._computation_generation &&
         _id == that._id;
}

ostream&
dmtcp::operator<<(ostream &o, const ConnectionIdentifier &id)
{
  o << std::hex << id.hostid()
    << '-' << std::dec << id.pid()
    << '-' << std::hex << id.time()
    << std::dec << '(' << id.conId() << ')';
  return o;
}
