#include <stdio.h>
#include "dmtcp.h"
#include "config.h"


static void applic_initiated_ckpt_event_hook(DmtcpEvent_t event,
                                      DmtcpEventData_t *data)
{
  /* NOTE:  See warning in plugin/README about calls to printf here. */
  switch (event) {
  case DMTCP_EVENT_INIT:
    printf("Plugin(%s:%d): initialization of plugin is complete.\n",
	   __FILE__, __LINE__);
    break;
  case DMTCP_EVENT_WRITE_CKPT:
    printf("Plugin(%s:%d): about to checkpoint.\n", __FILE__, __LINE__);
    break;
  case DMTCP_EVENT_RESUME:
    printf("Plugin(%s:%d): done checkpointing.\n", __FILE__, __LINE__);
    break;
  case DMTCP_EVENT_RESTART:
    printf("Plugin(%s:%d): done restarting from checkpoint image.\n",
           __FILE__, __LINE__);
    break;
  case DMTCP_EVENT_EXIT:
    printf("Plugin(%s:%d): exiting.\n", __FILE__, __LINE__);
    break;
  /* These events are unused and could be omitted.  See dmtcp.h for
   * complete list.
   */
  case DMTCP_EVENT_THREADS_RESUME:
  case DMTCP_EVENT_ATFORK_CHILD:
  case DMTCP_EVENT_THREADS_SUSPEND:
  case DMTCP_EVENT_LEADER_ELECTION:
  case DMTCP_EVENT_DRAIN:
  default:
    break;
  }
}

DmtcpPluginDescriptor_t applic_initiated_ckpt_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "applic_initiated_ckpt",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Application initiated ckpt plugin",
  DMTCP_NO_PLUGIN_BARRIERS,
  applic_initiated_ckpt_event_hook
};

DMTCP_DECL_PLUGIN(applic_initiated_ckpt_plugin);
