#include "syscallwrappers.h"
#include "dmtcpworker.h"
#include "uniquepid.h"
#include "dmtcpmodule.h"
#include "dmtcpmessagetypes.h"
#include "dmtcpalloc.h"

// dmtcp_checkpoint, and dmtcp_coordinator, and dmtcp_command do not
//   need to load dmtcpworker.cpp
// libdmtcpinternal.a contains code needed by dmtcpworker and the utilities
//    alike.
// libnohijack.a contains stub functions (mostly empty definitions
//   corresponding to definitions in dmtcphijack.so.  It includes
//   nosyscallsreal.c and this file (dmtcpworkerstubs.cpp).
// dmtcphijack.so and libsyscallsreal.a contain the wrappers and other code
//   that executes within the end user process

// dmtcphijack.so defines this differently
void _dmtcp_setup_trampolines() {}

void process_dmtcp_event(DmtcpEvent_t id, void* data)
{
  return;
}

int  dmtcp_get_ckpt_signal()
{
  JASSERT(false) .Text ("NOT REACHED");
  return -1;
}

const char* dmtcp_get_tmpdir()
{
  static dmtcp::string tmpdir;
  tmpdir = dmtcp::UniquePid::getTmpDir();
  return tmpdir.c_str();
}

const char* dmtcp_get_uniquepid_str()
{
  static dmtcp::string uniquepid_str;
  uniquepid_str = dmtcp::UniquePid::ThisProcess(true).toString();
  return uniquepid_str.c_str();
}

int  dmtcp_is_running_state()
{
  return dmtcp::WorkerState::currentState() == dmtcp::WorkerState::RUNNING;
}

