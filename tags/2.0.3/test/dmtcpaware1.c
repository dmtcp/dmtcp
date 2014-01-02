#include <assert.h>
#include <stdio.h>
#include <unistd.h>

/* Be sure to compile with -I<path>; see Makefile in this directory. */
#include "dmtcp.h"

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
  if(dmtcp_is_enabled())
    dmtcp_install_hooks(pre,post,restart);

  int r;
  int isRunning;
  int numPeers;
  int numCheckpoints;
  int numRestarts;
  while (1)
  {
    printf("dmtcp_is_enabled()=");
    fflush(stdout);
    printf("%d\n", r=dmtcp_is_enabled());
    assert(r);

    //printf("dmcpRunCommand('l')=");
    //fflush(stdout);
    //printf("%d\n",r=dmtcpRunCommand('l'));
    //assert(r);

    printf("dmtcp_get_local_status()=");
    fflush(stdout);
    assert(dmtcp_get_local_status(&numCheckpoints, &numRestarts) != DMTCP_NOT_PRESENT);
    printf("{\n\t %d,\n\t %d,\n\t %s,\n\t %s}\n",
           numCheckpoints, numRestarts, dmtcp_get_ckpt_filename(),
           dmtcp_get_uniquepid_str());

    printf("dmtcp_get_coordinator_status()=");
    fflush(stdout);
    assert(dmtcp_get_coordinator_status(&numPeers, &isRunning) != DMTCP_NOT_PRESENT);
    printf("{%d,%d}\n", numPeers, isRunning);
    assert(numPeers>0);

    //lock should be recursive
    dmtcp_disable_ckpt();
    dmtcp_disable_ckpt();
    dmtcp_disable_ckpt();
    dmtcp_enable_ckpt();
    dmtcp_enable_ckpt();
    dmtcp_enable_ckpt();

    sleep(2);

  }
  return 0;
}
