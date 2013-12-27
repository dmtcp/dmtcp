#include <assert.h>
#include <stdio.h>
#include <unistd.h>

/* Be sure to compile with -I<path>; see Makefile in this directory. */
#include "dmtcpplugin.h"

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
