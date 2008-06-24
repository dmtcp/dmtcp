#undef NDEBUG
#include <assert.h>
#include <stdio.h>

#include "../dmtcp/src/dmtcpaware.h"

int main(int argc, char* argv[])
{
  int r;
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
    DmtcpCoordinatorStatus s = dmtcpGetStatus();
    printf("{%d,%d}\n", s.numProcesses, s.isRunning);
    fflush(stdout);
    assert(s.numProcesses>0);

    sleep(2);

  }
  return 0;
}
