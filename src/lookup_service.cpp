/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include "lookup_service.h"
#include "../jalib/jassert.h"
#include "../jalib/jsocket.h"

using namespace dmtcp;

void dmtcp::LookupService::reset()
{
  MapIterator i;
  for (i = _maps.begin(); i != _maps.end(); i++) {
    KeyValueMap &kvmap = i->second;
    KeyValueMap::iterator it;
    for (it = kvmap.begin(); it != kvmap.end(); it++) {
      KeyValue *k = (KeyValue*)&(it->first);
      KeyValue *v = it->second;
      k->destroy();
      v->destroy();
      delete v;
    }
    kvmap.clear();
  }
  _maps.clear();
}

void dmtcp::LookupService::addKeyValue(string id,
                                       const void *key, size_t keyLen,
                                       const void *val, size_t valLen)
{
  KeyValueMap &kvmap = _maps[id];

  KeyValue k(key, keyLen);
  KeyValue *v = new KeyValue(val, valLen);
  if (kvmap.find(k) != kvmap.end()) {
    JTRACE("Duplicate key");
  }
  kvmap[k] = v;
}

void dmtcp::LookupService::query(string id,
                                 const void *key, size_t keyLen,
                                 void **val, size_t *valLen)
{
  KeyValueMap &kvmap = _maps[id];
  KeyValue k(key, keyLen);
  if (kvmap.find(k) == kvmap.end()) {
    JTRACE("Lookup Failed, Key not found.");
    *val = NULL;
    *valLen = 0;
    return;
  }

  KeyValue *v = kvmap[k];
  *valLen = v->len();
  *val = new char[v->len()];
  memcpy(*val, v->data(), *valLen);
}

void dmtcp::LookupService::registerData(const DmtcpMessage& msg,
                                        const void *data)
{
  JASSERT (msg.keyLen > 0 && msg.valLen > 0 &&
           msg.keyLen + msg.valLen == msg.extraBytes)
    (msg.keyLen) (msg.valLen) (msg.extraBytes);
  const void *key = data;
  const void *val = (char *)key + msg.keyLen;
  size_t keyLen = msg.keyLen;
  size_t valLen = msg.valLen;
  addKeyValue(msg.nsid, key, keyLen, val, valLen);
}

void dmtcp::LookupService::respondToQuery(jalib::JSocket& remote,
                                          const DmtcpMessage& msg,
                                          const void *key)
{
  JASSERT (msg.keyLen > 0 && msg.keyLen == msg.extraBytes)
    (msg.keyLen) (msg.extraBytes);
  void *val = NULL;
  size_t valLen = 0;

  query(msg.nsid, key, msg.keyLen, &val, &valLen);

  DmtcpMessage reply (DMT_NAME_SERVICE_QUERY_RESPONSE);
  reply.keyLen = 0;
  reply.valLen = valLen;
  reply.extraBytes = reply.valLen;

  remote << reply;
  if (valLen > 0) {
    remote.writeAll((char*)val, valLen);
  }
  delete [] (char*)val;
}
