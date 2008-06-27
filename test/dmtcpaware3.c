#include <assert.h>
#include <stdio.h>

#include "../dmtcp/src/dmtcpaware.h"

// this example tests dmtcpDelayCheckpoins[Un]Lock()

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
    
    sleep(1);

    dmtcpDelayCheckpointsLock();
    printf("dmtcpDelayCheckpointsLock();\n");
    sleep(2);
    printf("dmtcpDelayCheckpointsUnlock();\n");
    dmtcpDelayCheckpointsUnlock();
  }
  return 0;
}
