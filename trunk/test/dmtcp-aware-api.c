#include <assert.h>
#include <stdio.h>

#include "../dmtcp/src/dmtcpaware.h"

int main(int argc, char* argv[])
{
  int r;
  const char* s;
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

    printf("dmtcpGetStatus()=");
    fflush(stdout);
    DmtcpCoordinatorStatus status = dmtcpGetStatus();
    printf("{%d,%d}\n", status.numProcesses, status.isRunning);
    fflush(stdout);
    assert(status.numProcesses>0);

    printf("dmtcpGetCheckpointFilenameMtcp()=");
    fflush(stdout);
    s=dmtcpGetCheckpointFilenameMtcp();
    assert(s!=NULL);
    if(s!=NULL) printf("%s\n",s);

    printf("dmtcpGetCheckpointFilenameDmtcp()=");
    fflush(stdout);
    s=dmtcpGetCheckpointFilenameDmtcp();
    assert(s!=NULL);
    if(s!=NULL) printf("%s\n",s);

    printf("dmtcpGetUniquePid()=");
    fflush(stdout);
    s=dmtcpGetUniquePid();
    assert(s!=NULL);
    if(s!=NULL) printf("%s\n",s);

    sleep(2);

  }
  return 0;
}
