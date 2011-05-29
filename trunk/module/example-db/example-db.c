/* NOTE:  This code assumes that two environment variables have
 *  been set:  EXAMPLE_DB_KEY and EXAMPLE_DB_KEY_OTHER (for the other process).
 *  We will announce our (EXAMPLE_DB_KEY, <pid>) to the coordinator, and then
 *  query the <pid> for EXAMPLE_DB_KEY_OTHER (set by the other process).
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include "dmtcpmodule.h"

struct keyPid {
  int key;
  pid_t pid;
} mystruct, mystruct_other;

void process_dmtcp_event(DmtcpEvent_t event, void* data)
{
  int sizeofPid;

  switch (event) {
  case DMTCP_EVENT_INIT:
    printf("The module containing %s has been initialized.\n", __FILE__);
    if (getenv("DMTCP_EXAMPLE_DB")) {
      mystruct.key = atoi(getenv("DMTCP_EXAMPLE_DB"));
      mystruct.pid = getpid();
    }
    if (getenv("DMTCP_EXAMPLE_DB_OTHER")) {
      mystruct_other.key = atoi(getenv("DMTCP_EXAMPLE_DB_OTHER"));
      mystruct_other.pid = -1; /* -1 means unkonwn */
    }
    break;
  case DMTCP_EVENT_PRE_CHECKPOINT:
    printf("The module is being called before checkpointing.\n");
    break;
  case DMTCP_EVENT_POST_RESTART_RESUME:
    printf("The module is now resuming or restarting from checkpointing.\n");
    send_key_val_pair_to_coordinator(&(mystruct.key), sizeof(mystruct.key),
                                     &(mystruct.pid), sizeof(mystruct.pid));
    printf("Data sent:  My (key, pid) is: (%d, %ld).\n",
	   mystruct.key, (long)mystruct.pid);
    /* NOTE: DMTCP automatically creates a barrier between all calls to
     *   send_key_val_pair and send_query.  Using these functions in the wrong
     *   order risks deadlock.  Calling send_query on a non-existent key
     *   risks aborting the computation.
     */
    /* Set max size of the buffer &(mystruct.pid) */
    sizeofPid = sizeof(mystruct_other.pid);
    send_query_to_coordinator(&(mystruct_other.key), sizeof(mystruct_other.key),
                              &(mystruct_other.pid), &sizeofPid);
    printf("Data exchanged:  My pid is: %ld;  The other pid is:  %ld.\n",
	   (long)mystruct.pid, (long)mystruct_other.pid);
    break;
  default:
    break;
  }
}
