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
 *   int dmtcp_enable_ckpt() - DMTCP will execute any blocked
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
  int dmtcp_enabled = dmtcp_is_enabled();

  if (!dmtcp_enabled) {
    printf("\n *** dmtcp_is_enabled: executable seems to not be running"
           " under dmtcp_launch.\n\n");
  }

  int original_generation;
  if (dmtcp_enabled) {
    original_generation = dmtcp_get_generation();
  }


  int retval = dmtcp_checkpoint();
  if (retval == DMTCP_AFTER_CHECKPOINT) {
    // Wait long enough for checkpoint request to be written out.
    while (dmtcp_get_generation() == original_generation) {
      sleep(1);
    }

    printf("*** dmtcp_checkpoint: This program has now invoked a checkpoint.\n"
           "      It will resume its execution next.\n");
  } else if (retval == DMTCP_AFTER_RESTART) {
    printf("*** dmtcp_checkpoint: This program is now restarting.\n");
  } else if (retval == DMTCP_NOT_PRESENT) {
    printf(" *** dmtcp_checkpoint: DMTCP is not running."
           "  Skipping checkpoint.\n");
  }

  printf("\n*** Process done executing.  Successfully exiting.\n");
  if (retval == DMTCP_AFTER_CHECKPOINT) {
    printf("*** Execute ./dmtcp_restart_script.sh to restart.\n");
  }
  return 0;
}
