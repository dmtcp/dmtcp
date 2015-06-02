/******************************************************************
 * PLEASE NOTE:
 *   dmtcpaware is an older interface, that is now deprecated.
 * It is recommended to use the more flexible interfaces found in:
 *   <DMTCP>/test/plugin/
 * Of particular interest are the subdirectories:
 *   applic-delayed-ckpt
 *   applic-initiated-ckpt
 *   sleep1
 ******************************************************************/

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

/* Be sure to compile with -I<path>; see Makefile in this directory. */
#include "dmtcp.h"

// this example tests dmtcpDelayCheckpoins[Un]Lock()

int main(int argc, char* argv[])
{
  int count = 0;
  int numCheckpoints, numRestarts;
  while (1)
  {
    if(dmtcp_is_enabled()){
      dmtcp_get_local_status(&numCheckpoints, &numRestarts);
      printf("working... %d (status: %d checkpoints / %d restarts)\n",
             ++count, numCheckpoints, numRestarts);
    }else{
      printf("working... %d\n", ++count);
    }

    sleep(1);

    dmtcp_disable_ckpt();
    printf("dmtcp_disable_ckpt();\n");
    sleep(2);
    printf("dmtcp_enable_ckpt();\n");
    dmtcp_enable_ckpt();
  }
  return 0;
}
