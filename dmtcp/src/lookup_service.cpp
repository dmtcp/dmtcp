#include <map>
#include "lookup_service.h"
#include "../jalib/jassert.h"
#include "../jalib/jsocket.h"

using namespace dmtcp;

void dmtcp::LookupService::reset()
{
  keyValueMapIterator it;
  for (it = _keyValueMap.begin(); it != _keyValueMap.end(); it++) {
    KeyValue *k = it->first;
    KeyValue *v = it->second;
    delete k;
    delete v;
  }
  _keyValueMap.clear();
}

void dmtcp::LookupService::addKeyValue(const void *key, size_t keyLen,
                                       const void *val, size_t valLen)
{
  KeyValue *k = new KeyValue(key, keyLen);
  KeyValue *v = new KeyValue(val, valLen);
  if (_keyValueMap.find(k) != _keyValueMap.end()) {
    JTRACE("Duplicate key");
  }
  _keyValueMap[k] = v;
}

const void* dmtcp::LookupService::query(const void *key, size_t keyLen,
                                        void **val, size_t *valLen)
{
  KeyValue *k = new KeyValue(key, keyLen);
  if (_keyValueMap.find(k) == _keyValueMap.end()) {
    JTRACE("Lookup Failed, Key not found.");
    return NULL;
  }

  KeyValue *v = _keyValueMap[k];
  *val = v->data();
  *valLen = v->len();
  return *val;
}

void dmtcp::LookupService::registerData(const dmtcp::UniquePid& upid,
                                        const DmtcpMessage& msg,
                                        const char *data)
{
  JASSERT (msg.keyLen > 0 && msg.valLen > 0 &&
           msg.keyLen + msg.valLen == msg.extraBytes)
    (msg.keyLen) (msg.valLen) (msg.extraBytes) (upid);
  const void *key = data;
  const void *val = (char *)key + msg.keyLen;
  size_t keyLen = msg.keyLen;
  size_t valLen = msg.valLen;
  addKeyValue(key, keyLen, val, valLen);
}

void dmtcp::LookupService::respondToQuery(const dmtcp::UniquePid& upid,
                                          jalib::JSocket& remote,
                                          const DmtcpMessage& msg,
                                          const char *data)

{
  JASSERT (msg.keyLen > 0 && msg.keyLen == msg.extraBytes)
    (msg.keyLen) (msg.extraBytes) (upid);
  const void *key = data;
  size_t keyLen = msg.keyLen;
  void *val = NULL;
  size_t valLen = 0;
  JASSERT(query(key, msg.keyLen, &val, &valLen) != NULL);

  char *extraData = 0;
  extraData = new char[msg.extraBytes];
  memcpy(extraData, key, keyLen);
  memcpy(extraData + keyLen, val, valLen);

  DmtcpMessage reply (DMT_NAME_SERVICE_QUERY_RESPONSE);
  reply.keyLen = msg.keyLen;
  reply.valLen = valLen;
  reply.extraBytes = reply.keyLen + reply.valLen;

  remote << reply;
  remote.writeAll(extraData, reply.extraBytes);

  delete [] extraData;
}
