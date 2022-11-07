#include <unistd.h>
#include "dmtcp.h"
#include "dmtcpmessagetypes.h"
#include "coordinatorapi.h"
#include "util.h"
#include "jconvert.h"
#include "kvdb.h"
#include "threadinfo.h"

namespace dmtcp
{
namespace kvdb
{

KVDBResponse request(KVDBRequest request,
                string const& id,
                string const& key,
                string const& val,
                string *oldVal)
{
  DmtcpMessage msg(DMT_KVDB_REQUEST);

  msg.kvdbRequest = request;

  if (id.empty() || key.empty()) {
    return KVDBResponse::INVALID_REQUEST;
  }

  if (val.empty() && request != kvdb::KVDBRequest::GET) {
    return KVDBResponse::INVALID_REQUEST;
  }

  JWARNING(id.length() < sizeof(msg.kvdbId));
  strncpy(msg.kvdbId, id.data(), sizeof msg.kvdbId);
  msg.keyLen = key.length() + 1;
  msg.valLen = val.length() + 1;
  msg.extraBytes = msg.keyLen + msg.valLen;

  if (dmtcp_is_running_state() && !dmtcp_is_ckpt_thread()) {
    return CoordinatorAPI::kvdbRequest(msg, key, val, oldVal, true);
  }

  return CoordinatorAPI::kvdbRequest(msg, key, val, oldVal);
}

KVDBResponse
request64(KVDBRequest req,
             string const& id,
             string const& key,
             int64_t val,
             int64_t *oldVal)
{
  string valStr(jalib::XToString(val));
  string oldValStr;

  KVDBResponse response = request(req, id, key, valStr, &oldValStr);
  if (response == KVDBResponse::SUCCESS && oldVal != NULL) {
    *oldVal = jalib::StringToInt64(oldValStr);
  }

  return response;
}

KVDBResponse
get64(string const& id, string const& key, int64_t *val)
{
  return request64(KVDBRequest::GET, id, key, 0, val);
}

KVDBResponse
set64(string const& id, string const& key, int64_t val, int64_t *oldVal)
{
  return request64(KVDBRequest::SET, id, key, val, oldVal);
}

KVDBResponse
get(string const& id, string const& key, string *val)
{
  return request(KVDBRequest::GET, id, key, string(), val);
}

KVDBResponse
set(string const& id, string const& key, string const& val, string *oldVal)
{
  return request(KVDBRequest::SET, id, key, val, oldVal);
}

ostream &
operator<<(ostream &o, const KVDBRequest &id)
{
  switch (id) {
    case KVDBRequest::GET:
      o << "KVDBRequest::GET";
      break;
    case KVDBRequest::SET:
      o << "KVDBRequest::SET";
      break;
    case KVDBRequest::INCRBY:
      o << "KVDBRequest::INCRBY";
      break;
    case KVDBRequest::AND:
      o << "KVDBRequest::AND";
      break;
    case KVDBRequest::OR:
      o << "KVDBRequest::OR";
      break;
    case KVDBRequest::XOR:
      o << "KVDBRequest::XOR";
      break;
    case KVDBRequest::MIN:
      o << "KVDBRequest::MIN";
      break;
    case KVDBRequest::MAX:
      o << "KVDBRequest::MAX";
      break;
  }

  return o;
}

ostream &
operator<<(ostream &o, const KVDBResponse &id)
{
  switch (id) {
    case KVDBResponse::SUCCESS:
      o << "KVDBResponse::SUCCESS";
      break;
    case KVDBResponse::INVALID_REQUEST:
      o << "KVDBResponse::INVALID_REQUEST";
      break;
    case KVDBResponse::DB_NOT_FOUND:
      o << "KVDBResponse::DB_NOT_FOUND";
      break;
    case KVDBResponse::KEY_NOT_FOUND:
      o << "KVDBResponse::KEY_NOT_FOUND";
      break;
  }

  return o;
}

}
}
