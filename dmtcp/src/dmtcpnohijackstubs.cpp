#include "syscallwrappers.h"

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
