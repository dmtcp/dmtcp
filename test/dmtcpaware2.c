#include <assert.h>
#include <stdio.h>

#include "../dmtcp/src/dmtcpaware.h"

// this example tests dmtcpCheckpointBlocking()

int main(int argc, char* argv[])
{
  int count = 0;
  int r;
  const char* s;
  const DmtcpLocalStatus * ls;
  while (1)
  {
    ls = dmtcpGetLocalStatus();
    printf("working... %d (status: %d checkpoints / %d restarts)\n", ++count,ls->numCheckpoints, ls->numRestarts);
    
        

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
