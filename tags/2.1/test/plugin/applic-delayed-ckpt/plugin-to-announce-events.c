/* NOTE:  if you just want to insert your own code at the time of checkpoint
 *  and restart, there are two simpler additional mechanisms:
 *  dmtcpaware, and the MTCP special hook functions:
 *    mtcpHookPreCheckpoint, mtcpHookPostCheckpoint, mtcpHookRestart
 */

#include <stdio.h>
#include "dmtcp.h"


void dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
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
  DMTCP_NEXT_EVENT_HOOK(event, data);
}
