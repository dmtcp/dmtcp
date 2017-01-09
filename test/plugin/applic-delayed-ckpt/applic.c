/* NOTE: This file must be compiled with -fPIC in order to work properly.
 *
 *       The code in this file will work both with and without DMTCP.
 *       Of course, the dmtcp.h file is needed in both cases.
 *
 * These functions are in <DMTCP_ROOT>/lib/dmtcp/libdmtcp.so and dmtcp.h
 *   int dmtcp_is_enabled() - returns 1 when running with DMTCP; 0 otherwise.
 *   int dmtcp_checkpoint() - returns DMTCP_AFTER_CHECKPOINT,
 *                                   DMTCP_AFTER_RESTART, or DMTCP_NOT_PRESENT.
 * These return 0 on success and DMTCP_NOT_PRESENT if DMTCP is not present.
 *   int dmtcp_disable_ckpt() - DMTCP will block any checkpoint requests.
 *   int dmtcp_enble_ckpt() - DMTCP will execute any blocked
 *               checkpoint requests, and will permit new checkpoint requests.
 *
 * FOR ADVANCED USERS, ONLY:
 *   dmtcp_get_local_status
 *   dmtcp_get_coordinator_status
 *   dmtcp_install_hooks
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "dmtcp.h"

int
main()
{
  if (!dmtcp_is_enabled()) {
    printf("\n *** dmtcp_is_enabled: executable seems to not be running"
           " under dmtcp_launch.\n");
  }

  int retval = dmtcp_disable_ckpt();
  if (retval == DMTCP_NOT_PRESENT) {
    printf("\n *** dmtcp_disable_ckpt: DMTCP_NOT_PRESENT."
           "  Will exit.\n");
    exit(0);
  }

  // This assumes that DMTCP is present.
  int original_generation = dmtcp_get_generation();

  printf("*** dmtcp_disable_ckpt: Checkpoints are blocked.\n");
  printf("      But a checkpoint was requested every two seconds: '-i 2'\n");
  printf("*** sleep: sleeping 3 seconds.\n\n");
  sleep(3);
  printf("*** dmtcp_enable_ckpt: Will now unblock checkpointing\n"
         "      and write ./dmtcp_restart_script.sh.\n");
  printf("*** Execute ./dmtcp_restart_script.sh to restart from here.\n");
  dmtcp_enable_ckpt();

  // Wait long enough for checkpoint to be written.
  while (dmtcp_get_generation() == original_generation) {
    sleep(1);
  }

  printf("\n*** Process done executing.  Successfully exiting.\n");
  return 0;
}
