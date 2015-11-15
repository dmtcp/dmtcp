#include <stdio.h>
#include "dmtcp.h"
#include "config.h"


static void example_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  /* NOTE:  See warning in plugin/README about calls to printf here. */
  switch (event) {
  case DMTCP_EVENT_INIT:
    printf("The plugin containing %s has been initialized.\n", __FILE__);
    break;
  case DMTCP_EVENT_EXIT:
    printf("The plugin is being called before exiting.\n");
    break;
  default:
    break;
  }
}

static void checkpoint()
{
  printf("\n*** The plugin is being called before checkpointing. ***\n");
}

static void resume()
{
  printf("*** The application has now been checkpointed. ***\n");
}

static void restart()
{
  printf("The application is now restarting from checkpointing.\n");
}

static DmtcpBarrier barriers[] = {
  {DMTCP_GLOBAL_BARRIER_PRE_CKPT, checkpoint, "checkpoint"},
  {DMTCP_GLOBAL_BARRIER_RESUME, resume, "resume"},
  {DMTCP_GLOBAL_BARRIER_RESTART, restart, "restart"}
};

DmtcpPluginDescriptor_t example_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "example",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Example Plugin",
  DMTCP_DECL_BARRIERS(barriers),
  example_event_hook
};

DMTCP_DECL_PLUGIN(example_plugin);
