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

// this example tests dmtcpCheckpointBlocking()

int main(int argc, char* argv[])
{
  int count = 0;
  int r;
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

    if(count%10==0){
      printf("10 iteration, time to checkpoint... ");
      fflush(stdout);
      if(dmtcp_is_enabled()){
        printf("\n");
        r = dmtcp_checkpoint();
        if(r<=0)
          printf("Error, checkpointing failed: %d\n",r);
        if(r==1)
          printf("***** after checkpoint *****\n");
        if(r==2)
          printf("***** after restart *****\n");
      }else{
        printf(" dmtcp disabled -- nevermind\n");
      }
    }

    sleep(1);
  }
  return 0;
}
