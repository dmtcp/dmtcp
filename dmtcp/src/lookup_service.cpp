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

#include "lookup_service.h"
#include "../jalib/jassert.h"
#include "../jalib/jsocket.h"

using namespace dmtcp;

void dmtcp::LookupService::reset()
{
  keyValueMapIterator it;
  for (it = _keyValueMap.begin(); it != _keyValueMap.end(); it++) {
    KeyValue *k = (KeyValue*)&(it->first);
    KeyValue *v = it->second;
    k->destroy();
    v->destroy();
    delete v;
  }
  _keyValueMap.clear();
}

void dmtcp::LookupService::addKeyValue(const void *key, size_t keyLen,
                                       const void *val, size_t valLen)
{
  KeyValue k(key, keyLen);
  KeyValue *v = new KeyValue(val, valLen);
  if (_keyValueMap.find(k) != _keyValueMap.end()) {
    JTRACE("Duplicate key");
  }
  _keyValueMap[k] = v;
}

void dmtcp::LookupService::query(const void *key, size_t keyLen,
                                 void **val, size_t *valLen)
{
  KeyValue k(key, keyLen);
  if (_keyValueMap.find(k) == _keyValueMap.end()) {
    JTRACE("Lookup Failed, Key not found.");
    *val = NULL;
    *valLen = 0;
  }

  KeyValue *v = _keyValueMap[k];
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
  addKeyValue(key, keyLen, val, valLen);
}

void dmtcp::LookupService::respondToQuery(jalib::JSocket& remote,
                                          const DmtcpMessage& msg,
                                          const void *key)
{
  JASSERT (msg.keyLen > 0 && msg.keyLen == msg.extraBytes)
    (msg.keyLen) (msg.extraBytes);
  void *val = NULL;
  size_t valLen = 0;

  query(key, msg.keyLen, &val, &valLen);

  DmtcpMessage reply (DMT_NAME_SERVICE_QUERY_RESPONSE);
  reply.keyLen = 0;
  reply.valLen = valLen;
  reply.extraBytes = reply.valLen;

  remote << reply;
  if (valLen > 0) {
    remote.writeAll((char*)val, valLen);
    delete [] (char*)val;
  }
}
