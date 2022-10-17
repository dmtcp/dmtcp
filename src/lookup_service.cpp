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

using kvdb::KVDBRequest;
using kvdb::KVDBResponse;

void
LookupService::reset()
{
  _maps.clear();
}

void
LookupService::set(string const& id, string const& key, string const& val)
{
  _maps[id][key] = val;
}

KVDBResponse
LookupService::get(string const& id, string const& key, string *val)
{
  if (_maps.find(id) == _maps.end()) {
    JTRACE("Lookup Failed, database not found.") (id);
    return KVDBResponse::DB_NOT_FOUND;
  }

  KeyValueMap &kvmap = _maps[id];
  if (kvmap.find(key) == kvmap.end()) {
    JTRACE("Lookup Failed, Key not found.") (id) (key);
    return KVDBResponse::KEY_NOT_FOUND;
  }

  *val = kvmap[key];

  return KVDBResponse::SUCCESS;
}

void
LookupService::sendResponse(jalib::JSocket &remote,
                            KVDBResponse response)
{
  DmtcpMessage reply(DMT_KVDB_RESPONSE);
  reply.kvdbResponse = response;
  remote << reply;
}

void
LookupService::sendResponse(jalib::JSocket &remote,
                            string const& val)
{
  DmtcpMessage reply(DMT_KVDB_RESPONSE);
  reply.kvdbResponse = kvdb::KVDBResponse::SUCCESS;
  reply.valLen = val.size() + 1;
  reply.extraBytes = reply.valLen;

  remote << reply;
  remote.writeAll(val.data(), reply.valLen);
}

void
LookupService::processRequest(jalib::JSocket &remote,
                         const DmtcpMessage &msg,
                         const void *extraData)
{
  JASSERT(msg.keyLen > 0 &&
          msg.valLen > 0 &&
          (msg.keyLen + msg.valLen) == msg.extraBytes)
  (msg.keyLen)(msg.valLen)(msg.extraBytes);

  if (msg.kvdbRequest == KVDBRequest::GET) {
    processGet(remote, msg, extraData);
    return;
  }

  processSet(remote, msg, extraData);
  return;
}

void
LookupService::processGet(jalib::JSocket &remote,
                         const DmtcpMessage &msg,
                         const void *extraData)
{
  const char *key = (const char*)extraData;
  string val;

  KVDBResponse response = get(msg.kvdbId, key, &val);

  if (response == KVDBResponse::SUCCESS) {
    sendResponse(remote, val);
  } else {
    sendResponse(remote, response);
  }
  return;
}

void
LookupService::processSet(jalib::JSocket &remote,
                          const DmtcpMessage &msg,
                          const void *extraData)
{
  KeyValueMap &kvmap = _maps[msg.kvdbId];
  const char *key = (const char*)extraData;
  const char *val = key + msg.keyLen;

  string oldVal("0");
  get(msg.kvdbId, key, &oldVal);

  if (msg.kvdbRequest == KVDBRequest::SET ||
      kvmap.find(key) == kvmap.end()) {
    kvmap[key] = val;
    sendResponse(remote, oldVal);
    return;
  }

  int64_t val64 = jalib::StringToInt64(val);
  int64_t oldVal64 = jalib::StringToInt64(kvmap[key]);

  switch (msg.kvdbRequest)
  {
  case KVDBRequest::INCRBY:
    kvmap[key] = jalib::XToString(oldVal64 + val64);
    break;

  case KVDBRequest::OR:
    kvmap[key] = jalib::XToString(oldVal64 | val64);
    break;

  case KVDBRequest::XOR:
    kvmap[key] = jalib::XToString(oldVal64 ^ val64);
    break;

  case KVDBRequest::AND:
    kvmap[key] = jalib::XToString(oldVal64 & val64);
    break;

  case KVDBRequest::MIN:
    kvmap[key] = jalib::XToString(MIN(oldVal64, val64));
    break;

  case KVDBRequest::MAX:
    kvmap[key] = jalib::XToString(MAX(oldVal64, val64));
    break;

  default:
    JASSERT(false).Text("Invalid operation");
  }

  sendResponse(remote, oldVal);
  return;
}

void
LookupService::serialize(ofstream& o, string const& str)
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
LookupService::serialize(string const& file)
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

  o << "\n}";

  o.close();
}
