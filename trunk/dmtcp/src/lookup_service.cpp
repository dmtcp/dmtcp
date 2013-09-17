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
