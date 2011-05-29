#include "dmtcpmodule.h"
#include "dmtcpworker.h"
#include "dmtcpmessagetypes.h"

using namespace dmtcp;

EXTERNC void process_dmtcp_event(DmtcpEvent_t id, void* data)
{
  return;
}

EXTERNC int  dmtcp_get_ckpt_signal()
{
  const int ckpt_signal = dmtcp::DmtcpWorker::determineMtcpSignal();
  return ckpt_signal;
}

EXTERNC const char* dmtcp_get_tmpdir()
{
  static dmtcp::string tmpdir;
  tmpdir = dmtcp::UniquePid::getTmpDir();
  return tmpdir.c_str();
}

EXTERNC const char* dmtcp_get_uniquepid_str()
{
  static dmtcp::string uniquepid_str;
  uniquepid_str = dmtcp::UniquePid::ThisProcess(true).toString();
  return uniquepid_str.c_str();
}

EXTERNC int  dmtcp_is_running_state()
{
  return dmtcp::WorkerState::currentState() == dmtcp::WorkerState::RUNNING;
}

EXTERNC int send_key_val_pair_to_coordinator(const void *key, size_t key_len,
                                             const void *val, size_t val_len)
{
  char *extraData = new char[key_len + val_len];
  memcpy(extraData, key, key_len);
  memcpy(extraData + key_len, val, val_len);

  DmtcpMessage msg (DMT_REGISTER_NAME_SERVICE_DATA);
  msg.keyLen = key_len;
  msg.valLen = val_len;
  msg.extraBytes = key_len + val_len;

  DmtcpWorker::instance().coordinatorSocket() << msg;
  DmtcpWorker::instance().coordinatorSocket().writeAll(extraData,
                                                       msg.extraBytes);
  delete [] extraData;
  return 1;
}

// On input, val points to a buffer in user memory and *val_len is the maximum
//   size of that buffer (the memory allocated by user).
// On output, we copy data to val, and set *val_len to the actual buffer size
//   (to the size of the data that we copied in).
EXTERNC int send_query_to_coordinator(const void *key, size_t key_len,
                                      void *val, size_t *val_len)
{
  char *extraData = new char[key_len];
  memcpy(extraData, key, key_len);

  DmtcpMessage msg (DMT_NAME_SERVICE_QUERY);
  msg.keyLen = key_len;
  msg.valLen = 0;
  msg.extraBytes = key_len;

  DmtcpWorker::instance().coordinatorSocket() << msg;
  DmtcpWorker::instance().coordinatorSocket().writeAll(extraData,
                                                       msg.extraBytes);
  delete [] extraData;

  msg.poison();

  DmtcpWorker::instance().coordinatorSocket() >> msg;
  msg.assertValid();

  JASSERT(msg.type == DMT_NAME_SERVICE_QUERY_RESPONSE &&
          msg.extraBytes > 0 && (msg.valLen + msg.keyLen) == msg.extraBytes);

  extraData = new char[msg.extraBytes];
  DmtcpWorker::instance().coordinatorSocket().readAll(extraData,
                                                      msg.extraBytes);
  //TODO: FIXME --> enforce the JASSERT
  JASSERT(msg.extraBytes <= *val_len + key_len);
  memcpy(val, extraData + key_len, msg.extraBytes-key_len);
  *val_len = msg.valLen;
  delete [] extraData;
  return 1;
}
