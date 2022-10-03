#include <unistd.h>
#include "dmtcp.h"
#include "dmtcpmessagetypes.h"
#include "coordinatorapi.h"
#include "util.h"

using namespace dmtcp;

EXTERNC int
dmtcp_kvdb64_get(const char *id,
               int64_t key,
               int64_t *val)
{
  DmtcpMessage msg(DMT_KVDB64_GET);

  JWARNING(strlen(id) < sizeof(msg.kvdbId));
  strncpy(msg.kvdbId, id, sizeof(msg.kvdbId));

  msg.kvdb.key = key;

  CoordinatorAPI::sendMsgToCoordinator(msg);

  CoordinatorAPI::recvMsgFromCoordinator(&msg);
  msg.assertValid();
  JASSERT(msg.type == DMT_KVDB64_GET_RESPONSE ||
          msg.type == DMT_KVDB64_GET_FAILED);

  if (msg.type == DMT_KVDB64_GET_FAILED) {
    return -1;
  }

  *val = msg.kvdb.value;

  return 0;
}

EXTERNC int
dmtcp_kvdb64(DmtcpKVDBOperation_t op,
             const char *id,
             int64_t key,
             int64_t val)
{
  return dmtcp_kvdb64_r(op, DMTCP_KVDB_RESPONSE_NONE, id, key, val);
}

uint64_t dmtcp_kvdb64_r(DmtcpKVDBOperation_t op,
                   DmtcpKVDBOperationResponse_t responseType,
                   const char *id,
                   int64_t key,
                   int64_t val)
{
  DmtcpMessage msg(DMT_KVDB64_OP);

  JWARNING(strlen(id) < sizeof(msg.kvdbId));
  strncpy(msg.kvdbId, id, sizeof(msg.kvdbId));

  msg.kvdb.op = op;
  msg.kvdb.key = key;
  msg.kvdb.value = val;

  CoordinatorAPI::sendMsgToCoordinator(msg);

  if (responseType != DMTCP_KVDB_RESPONSE_NONE) {
    return 0;
  }

  CoordinatorAPI::recvMsgFromCoordinator(&msg);
  msg.assertValid();
  JASSERT(msg.type == DMT_KVDB64_OP_RESPONSE);

  return msg.kvdb.value;
}
