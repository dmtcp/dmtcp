/*
 * The following functions are defined in <DMTCP_ROOT>/lib/dmtcp/libdmtcp.so :
 *   dmtcpCheckpoint
 *   dmtcpGetLocalStatus
 *   dmtcpGetCoordinatorStatus
 *   dmtcpDelayCheckpointsLock
 *   dmtcpDelayCheckpointsUnlock
 *   dmtcpInstallHooks
 *   dmtcpRunCommand
 */

#include <stdio.h>
#include <dlfcn.h>

#include "dmtcpaware.h"
;
int main() {
    void *libdmtcp = dlopen("libdmtcp.so", RTLD_LAZY);
    // This type declaration corresponds to dmtcpCheckpoint() in dmtcpaware.h
    int (*dmtcpCheckpointPtr)();
    dmtcpCheckpointPtr = dlsym(libdmtcp, "dmtcpCheckpoint");
    int result = (*dmtcpCheckpointPtr)();
    if (result == DMTCP_AFTER_CHECKPOINT) {
      printf("*** This process has now been checkpointed.\n"
             "*** It will resume its execution next.\n");
    } else if (result == DMTCP_AFTER_RESTART) {
      printf("*** This process is now restarting.\n");
    }

    printf("\n*** Process done executing.  Successfully exiting.\n");
    if (result == DMTCP_AFTER_CHECKPOINT) {
        printf("*** Execute ./dmtcp_restart_script.sh to restart.\n");
    }
    return 0;
}
