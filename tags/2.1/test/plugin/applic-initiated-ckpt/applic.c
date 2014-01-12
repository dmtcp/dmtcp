/* NOTE: This file must be compiled with -fPIC in order to work properly.
 *
 *       The code in this file will work both with and without DMTCP.
 *       Of course, the dmtcp.h file is needed in both cases.
 *
 * These functions are in <DMTCP_ROOT>/lib/dmtcp/libdmtcp.so and dmtcp.h
 *   int dmtcpIsEnabled() - returns 1 when running with DMTCP; 0 otherwise.
 *   int dmtcpCheckpoint() - returns DMTCP_AFTER_CHECKPOINT,
 *                                   DMTCP_AFTER_RESTART, or DMTCP_NOT_PRESENT.
 * These return 0 on success and DMTCP_NOT_PRESENT if DMTCP is not present.
 *   int dmtcpDelayCheckpointsLock() - DMTCP will block any checkpoint requests.
 *   int dmtcpDelayCheckpointsUnlock() - DMTCP will execute any blocked
 *               checkpoint requests, and will permit new checkpoint requests.
 *
 * FOR ADVANCED USERS, ONLY:
 *   dmtcpGetLocalStatus
 *   dmtcpGetCoordinatorStatus
 *   dmtcpInstallHooks
 *   dmtcpRunCommand
 */

#include <stdio.h>
#include <stdlib.h>

#include "dmtcp.h"

int main() {
    if ( ! dmtcp_is_enabled() ) {
      printf("\n *** dmtcpIsEnabled: executable seems to not be running"
             " under dmtcp_launch.\n\n");
    }

    int retval = dmtcp_checkpoint();
    if (retval == DMTCP_AFTER_CHECKPOINT) {
      printf("*** dmtcpCheckpoint: This program has now invoked a checkpoint.\n"
             "      It will resume its execution next.\n");
    } else if (retval == DMTCP_AFTER_RESTART) {
      printf("*** dmtcpCheckpoint: This program is now restarting.\n");
    } else if (retval == DMTCP_NOT_PRESENT) {
      printf(" *** dmtcpCheckpoint: DMTCP is not running."
             "  Skipping checkpoint.\n");
    }

    printf("\n*** Process done executing.  Successfully exiting.\n");
    if (retval == DMTCP_AFTER_CHECKPOINT) {
        printf("*** Execute ./dmtcp_restart_script.sh to restart.\n");
    }
    return 0;
}
