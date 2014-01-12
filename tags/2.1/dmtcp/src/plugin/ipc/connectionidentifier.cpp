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
#include "dmtcp.h"
#include "../jalib/jassert.h"

static int _nextConId = CONNECTION_ID_START;
static int _nextConnectionId()
{
  //static int id = CONNECTION_ID_START;
  return _nextConId++;
}

dmtcp::ConnectionIdentifier::ConnectionIdentifier(int id)
{
  _upid = dmtcp_get_uniquepid();
  _id = id;
}

void dmtcp::ConnectionIdentifier::serialize (jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT ("dmtcp::ConnectionIdentifier:");
  o & _nextConId;
  JASSERT(_nextConId >= CONNECTION_ID_START);
}

dmtcp::ConnectionIdentifier dmtcp::ConnectionIdentifier::Create()
{
  return ConnectionIdentifier (_nextConnectionId());
}
dmtcp::ConnectionIdentifier dmtcp::ConnectionIdentifier::Null()
{
  static dmtcp::ConnectionIdentifier n;
  return n;
}
dmtcp::ConnectionIdentifier dmtcp::ConnectionIdentifier::Self()
{
  return ConnectionIdentifier(-1);
}

bool dmtcp::ConnectionIdentifier::operator<(const ConnectionIdentifier& that)
  const
{
#define TRY_LEQ(param) \
  if(_upid.param != that._upid.param) return _upid.param < that._upid.param;

  TRY_LEQ ( _hostid );
  TRY_LEQ ( _pid );
  TRY_LEQ ( _time );
  return _id < that._id;
}

bool dmtcp::ConnectionIdentifier::operator==(const ConnectionIdentifier& that)
  const
{
  return  _upid._hostid == that._upid._hostid &&
          _upid._pid == that._upid._pid &&
          _upid._time == that._upid._time &&
          _upid._generation == that._upid._generation &&
          _id   == that._id;
}
