#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "../dmtcp/src/dmtcpaware.h"

// this example tests dmtcpDelayCheckpoins[Un]Lock()

int main(int argc, char* argv[])
{
  int count = 0;
  const DmtcpLocalStatus * ls;
  while (1)
  {
    if(dmtcpIsEnabled()){ 
      ls = dmtcpGetLocalStatus();
      printf("working... %d (status: %d checkpoints / %d restarts)\n", ++count,ls->numCheckpoints, ls->numRestarts);
    }else{
      printf("working... %d\n", ++count);
    }
    
    sleep(1);

    dmtcpDelayCheckpointsLock();
    printf("dmtcpDelayCheckpointsLock();\n");
    sleep(2);
    printf("dmtcpDelayCheckpointsUnlock();\n");
    dmtcpDelayCheckpointsUnlock();
  }
  return 0;
}
