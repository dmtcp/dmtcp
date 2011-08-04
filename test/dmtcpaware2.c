#include <assert.h>
#include <stdio.h>
#include <unistd.h>

/* Be sure to compile with -I<path>; see Makefile in this directory. */
#include "dmtcpaware.h"

// this example tests dmtcpCheckpointBlocking()

int main(int argc, char* argv[])
{
  int count = 0;
  int r;
  const DmtcpLocalStatus * ls;
  while (1)
  {
    if(dmtcpIsEnabled()){
      ls = dmtcpGetLocalStatus();
      printf("working... %d (status: %d checkpoints / %d restarts)\n", ++count,ls->numCheckpoints, ls->numRestarts);
    }else{
      printf("working... %d\n", ++count);
    }

    if(count%10==0){
      printf("10 iteration, time to checkpoint... ");
      fflush(stdout);
      if(dmtcpIsEnabled()){
        printf("\n");
        r = dmtcpCheckpoint();
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
