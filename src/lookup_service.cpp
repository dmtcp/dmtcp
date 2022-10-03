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

#include <iomanip>
#include <iostream>
#include <fstream>
#include "dmtcp.h"
#include "util.h"
#include "lookup_service.h"
#include "tokenize.h"
#include "../jalib/jassert.h"
#include "../jalib/jconvert.h"
#include "../jalib/jsocket.h"

using namespace dmtcp;

void
LookupService::reset()
{
  _maps.clear();
  _maps64.clear();
}

void
LookupService::addKeyValue(string id, string key, int64_t val)
{
  KeyValueMap64 &kvmap = _maps64[id];

  if (kvmap.find(key) != kvmap.end()) {
    JTRACE("Duplicate key");
  }
  kvmap[key] = val;
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
  string key = jalib::XToString(msg.kvdb.key);

  if (kvmap.find(key) == kvmap.end()) {
    JTRACE("Lookup Failed, Key not found.");
    remote << DmtcpMessage(DMT_KVDB64_GET_FAILED);
    return;
  }

  DmtcpMessage reply(DMT_KVDB64_GET_RESPONSE);
  reply.kvdb.value = kvmap[key];

  remote << reply;
}

void
LookupService::set64(jalib::JSocket &remote,
                     const DmtcpMessage &msg)
{
  DmtcpMessage reply(DMT_KVDB64_GET_RESPONSE);
  reply.kvdb.value = 0;

  KeyValueMap64 &kvmap = _maps64[msg.kvdbId];
  string key = jalib::XToString(msg.kvdb.key);

  // If key isn't found, set the key to the given value.
  if (kvmap.find(key) == kvmap.end()) {
    if (msg.kvdb.op == DMTCP_KVDB_NOT) {
      JWARNING("Key not found for NOT operation.") (msg.kvdbId) (msg.kvdb.key);
    } else {
      kvmap[key] = msg.kvdb.value;
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
    kvmap[key] = msg.kvdb.value;

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
    reply.kvdb.value = kvmap[key];
  }

  // If a key doesn't exist, we assume the default value (0) for all operations,
  // except for AND where we set it to the incoming value.
  switch (msg.kvdb.op)
  {
  case DMTCP_KVDB_INCRBY:
    kvmap[key] += msg.kvdb.value;
    break;

  case DMTCP_KVDB_OR:
    kvmap[key] |= msg.kvdb.value;
    break;

  case DMTCP_KVDB_XOR:
    kvmap[key] ^= msg.kvdb.value;
    break;

  case DMTCP_KVDB_NOT:
    kvmap[key] = ~kvmap[key];
    break;

  case DMTCP_KVDB_AND:
    kvmap[key] &= msg.kvdb.value;
    break;

  case DMTCP_KVDB_MIN:
    kvmap[key] = MIN(msg.kvdb.value, kvmap[key]);
    break;

  case DMTCP_KVDB_MAX:
    kvmap[key] = MAX(msg.kvdb.value, kvmap[key]);
    break;

  default:
    JASSERT(false).Text("Invalid operation");
  }

  if (msg.kvdb.responseType == DMTCP_KVDB_RESPONSE_NEW_VAL) {
    reply.kvdb.value = kvmap[key];
  }

  if (msg.kvdb.responseType != DMTCP_KVDB_RESPONSE_NONE) {
    remote << reply;
  }
}

void
LookupService::serialize(ofstream& o, std::string_view str)
{
  if (str.find('\n') == string::npos) {
    o << std::quoted(str);
    return;
  }

  vector<string> lines = tokenizeString(str, "\n");

  o << " [\n";
  o << "      " << std::quoted(lines[0]);

  for (size_t i = 1; i < lines.size(); i++) {
    o << ",\n      " << std::quoted(lines[i]);
  }

  o << "\n    ]";
}

void
LookupService::serialize(ofstream& o, KeyValueMap64 const& kvmap64)
{
  KeyValueMap64::const_iterator it = kvmap64.begin();

  o << "{\n";

  if (it != kvmap64.end()) {
    o << "    " << std::quoted(it->first) << ": " << it->second;
    it++;
  }

  for (; it != kvmap64.end(); it++) {
    o << ",\n    " << std::quoted(it->first) << ": " << it->second;
  }

  o << "\n  }";
}

void
LookupService::serialize(ofstream& o, KeyValueMap const& kvmap)
{
  KeyValueMap::const_iterator it = kvmap.begin();

  o << "{\n";

  if (it != kvmap.end()) {
    o << "    " << std::quoted(it->first) << ": ";
    serialize(o, it->second);
    it++;
  }

  for (; it != kvmap.end(); it++) {
    o << ",\n    " << std::quoted(it->first) << ": ";
    serialize(o, it->second);
  }

  o << "\n  }";
}

void
LookupService::serialize(std::string_view file)
{
  ofstream o;
  o.open (file.data());

  JASSERT(o.is_open());

  o << "{\n";

  map<string, KeyValueMap>::iterator it = _maps.begin();
  if (it != _maps.end()) {
    o << "  " << std::quoted(it->first) << ": ";
    serialize(o, it->second);
    it++;

    for (; it != _maps.end(); it++) {
      o << ",\n  " << std::quoted(it->first) << ": ";
      serialize(o, it->second);
    }
  }

  map<string, KeyValueMap64>::iterator it64 = _maps64.begin();
  if (it64 != _maps64.end()) {
    o << ",\n  " << std::quoted(it64->first) << ": ";
    serialize(o, it64->second);
    it64++;

    for (; it64 != _maps64.end(); it64++) {
      o << ",\n  " << std::quoted(it64->first) << ": ";
      serialize(o, it64->second);
    }
  }

  o << "\n}";

  o.close();
}
