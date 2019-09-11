#include <stdio.h>
#include <sys/time.h>
#include "dmtcp.h"

void
print_time()
{
  struct timeval val;

  gettimeofday(&val, NULL);
  printf("%ld %ld", (long)val.tv_sec, (long)val.tv_usec);
}

unsigned int
sleep(unsigned int seconds)
{
  printf("sleep1: "); print_time(); printf(" ... ");
  unsigned int result = NEXT_FNC(sleep)(seconds);
  print_time(); printf("\n");

  return result;
}

static void
checkpoint()
{
  printf("\n*** The plugin %s is being called before checkpointing. ***\n",
         __FILE__);
}

static void
resume()
{
  printf("*** The plugin %s has now been checkpointed. ***\n", __FILE__);
}


static void
sleep1_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_PRECHECKPOINT:
    checkpoint();
    break;

  case DMTCP_EVENT_RESUME:
    resume();
    break;
  }
}

DmtcpPluginDescriptor_t sleep1_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  DMTCP_PACKAGE_VERSION,
  "sleep1",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Sleep1 plugin",
  sleep1_EventHook
};

DMTCP_DECL_PLUGIN(sleep1_plugin);
