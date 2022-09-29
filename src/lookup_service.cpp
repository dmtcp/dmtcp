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

#include "dmtcp.h"
#include "util.h"
#include "lookup_service.h"
#include "../jalib/jassert.h"
#include "../jalib/jsocket.h"

using namespace dmtcp;

void
LookupService::reset()
{
  _maps.clear();
  _maps64.clear();
}

void
LookupService::addKeyValue(string id, string key, string val)
{
  KeyValueMap &kvmap = _maps[id];

  if (kvmap.find(key) != kvmap.end()) {
    JTRACE("Duplicate key");
  }
  kvmap[key] = val;
}

void
LookupService::registerData(const DmtcpMessage &msg, const void *data)
{
  JASSERT(msg.keyLen > 0 && msg.valLen > 0 &&
          msg.keyLen + msg.valLen == msg.extraBytes)
    (msg.keyLen) (msg.valLen) (msg.extraBytes);
  const char *key = (char*) data;
  const char *val = key + msg.keyLen;
  size_t keyLen = msg.keyLen;
  size_t valLen = msg.valLen;
  addKeyValue(msg.nsid, key, val);
}

void
LookupService::respondToQuery(jalib::JSocket &remote,
                              const DmtcpMessage &msg,
                              const void *extraData)
{
  const char *key = (const char*)extraData;
  JASSERT(msg.keyLen > 0 && msg.keyLen == msg.extraBytes)
    (msg.keyLen) (msg.extraBytes);

  DmtcpMessage reply(DMT_NAME_SERVICE_QUERY_RESPONSE);

  KeyValueMap &kvmap = _maps[msg.nsid];
  string val;

  if (kvmap.find(key) == kvmap.end()) {
    JTRACE("Lookup Failed, Key not found.");
  } else {
    val = kvmap[key];
  }

  reply.keyLen = 0;
  reply.valLen = val.size() + 1;
  reply.extraBytes = reply.valLen;

  remote << reply;
  if (reply.valLen > 0) {
    remote.writeAll(val.c_str(), reply.valLen);
  }
}

void
LookupService::get64(jalib::JSocket &remote,
                     const DmtcpMessage &msg)
{
  KeyValueMap64 &kvmap = _maps64[msg.kvdbId];

  if (kvmap.find(msg.kvdb.key) == kvmap.end()) {
    JTRACE("Lookup Failed, Key not found.");
    remote << DmtcpMessage(DMT_KVDB64_GET_FAILED);
    return;
  }

  DmtcpMessage reply(DMT_KVDB64_GET_RESPONSE);
  reply.kvdb.value = kvmap[msg.kvdb.key];

  remote << reply;
}

void
LookupService::set64(jalib::JSocket &remote,
                     const DmtcpMessage &msg)
{
  DmtcpMessage reply(DMT_KVDB64_GET_RESPONSE);
  reply.kvdb.value = 0;

  KeyValueMap64 &kvmap = _maps64[msg.kvdbId];

  // If key isn't found, set the key to the given value.
  if (kvmap.find(msg.kvdb.key) == kvmap.end()) {
    if (msg.kvdb.op == DMTCP_KVDB_NOT) {
      JWARNING("Key not found for NOT operation.") (msg.kvdbId) (msg.kvdb.key);
    } else {
      kvmap[msg.kvdb.key] = msg.kvdb.value;
    }

    if (msg.kvdb.responseType == DMTCP_KVDB_RESPONSE_PREV_VAL) {
      reply.kvdb.value = 0;
      remote << reply;
    } else if (msg.kvdb.responseType == DMTCP_KVDB_RESPONSE_NEW_VAL) {
      reply.kvdb.value = msg.kvdb.value;
      remote << reply;
    }

    return;
  }

  if (msg.kvdb.op == DMTCP_KVDB_SET) {
    kvmap[msg.kvdb.key] = msg.kvdb.value;

    if (msg.kvdb.responseType == DMTCP_KVDB_RESPONSE_PREV_VAL) {
      reply.kvdb.value = 0;
      remote << reply;
    } else if (msg.kvdb.responseType == DMTCP_KVDB_RESPONSE_NEW_VAL) {
      reply.kvdb.value = msg.kvdb.value;
      remote << reply;
    }

    return;
  }

  if (msg.kvdb.responseType == DMTCP_KVDB_RESPONSE_PREV_VAL) {
    reply.kvdb.value = kvmap[msg.kvdb.key];
  }

  // If a key doesn't exist, we assume the default value (0) for all operations,
  // except for AND where we set it to the incoming value.
  switch (msg.kvdb.op)
  {
  case DMTCP_KVDB_INCRBY:
    kvmap[msg.kvdb.key] += msg.kvdb.value;
    break;

  case DMTCP_KVDB_OR:
    kvmap[msg.kvdb.key] |= msg.kvdb.value;
    break;

  case DMTCP_KVDB_XOR:
    kvmap[msg.kvdb.key] ^= msg.kvdb.value;
    break;

  case DMTCP_KVDB_NOT:
    kvmap[msg.kvdb.key] = ~kvmap[msg.kvdb.key];
    break;

  case DMTCP_KVDB_AND:
    kvmap[msg.kvdb.key] &= msg.kvdb.value;
    break;

  case DMTCP_KVDB_MIN:
    kvmap[msg.kvdb.key] = MIN(msg.kvdb.value, kvmap[msg.kvdb.key]);
    break;

  case DMTCP_KVDB_MAX:
    kvmap[msg.kvdb.key] = MAX(msg.kvdb.value, kvmap[msg.kvdb.key]);
    break;

  default:
    JASSERT(false).Text("Invalid operation");
  }

  if (msg.kvdb.responseType == DMTCP_KVDB_RESPONSE_NEW_VAL) {
    reply.kvdb.value = kvmap[msg.kvdb.key];
  }

  if (msg.kvdb.responseType != DMTCP_KVDB_RESPONSE_NONE) {
    remote << reply;
  }
}
