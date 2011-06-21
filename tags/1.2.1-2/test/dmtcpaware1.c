#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "../dmtcp/src/dmtcpaware.h"

void pre(){
  printf("HOOK: preCheckpoint\n");
}

void post(){
  printf("HOOK: postCheckpoint\n");
}

void restart(){
  printf("HOOK: postRestart\n");
}

int main(int argc, char* argv[])
{
  if(dmtcpIsEnabled())
    dmtcpInstallHooks(pre,post,restart);

  int r;
  const DmtcpCoordinatorStatus* cs;
  const DmtcpLocalStatus * ls;
  while (1)
  {
    printf("dmtcpIsEnabled()=");
    fflush(stdout);
    printf("%d\n", r=dmtcpIsEnabled());
    assert(r);

    printf("dmcpRunCommand('l')=");
    fflush(stdout);
    printf("%d\n",r=dmtcpRunCommand('l'));
    assert(r);

    printf("dmtcpGetLocalStatus()=");
    fflush(stdout);
    ls = dmtcpGetLocalStatus();
    assert(ls!=NULL);
    printf("{\n\t %d,\n\t %d,\n\t %s,\n\t %s}\n", 
        ls->numCheckpoints, ls->numRestarts, ls->checkpointFilename, ls->uniquePidStr);

    printf("dmtcpGetCoordinatorStatus()=");
    fflush(stdout);
    cs = dmtcpGetCoordinatorStatus();
    assert(cs!=NULL);
    printf("{%d,%d}\n", cs->numProcesses, cs->isRunning);
    assert(cs->numProcesses>0);

    //lock should be recursive 
    dmtcpDelayCheckpointsLock();
    dmtcpDelayCheckpointsLock();
    dmtcpDelayCheckpointsLock();
    dmtcpDelayCheckpointsUnlock();
    dmtcpDelayCheckpointsUnlock();
    dmtcpDelayCheckpointsUnlock();

    sleep(2);

  }
  return 0;
}
