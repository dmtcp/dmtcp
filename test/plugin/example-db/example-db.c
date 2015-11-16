/* NOTE:  This code assumes that two environment variables have
 *  been set:  EXAMPLE_DB_KEY and EXAMPLE_DB_KEY_OTHER (for the other process).
 *  We will announce our (EXAMPLE_DB_KEY, <pid>) to the coordinator, and then
 *  query the <pid> for EXAMPLE_DB_KEY_OTHER (set by the other process).
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include "dmtcp.h"
#include "config.h"

struct keyPid {
  int key;
  pid_t pid;
} mystruct, mystruct_other;

void dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  uint32_t sizeofPid;

  /* NOTE:  See warning in plugin/README about calls to printf here. */
  switch (event) {
  case DMTCP_EVENT_INIT:
    printf("The plugin containing %s has been initialized.\n", __FILE__);
    if (getenv("EXAMPLE_DB_KEY")) {
      mystruct.key = atoi(getenv("EXAMPLE_DB_KEY"));
      mystruct.pid = getpid();
      printf("  Data initialized:  My (key, pid) is: (%d, %ld).\n",
             mystruct.key, (long)mystruct.pid);
    }
    if (getenv("EXAMPLE_DB_KEY_OTHER")) {
      mystruct_other.key = atoi(getenv("EXAMPLE_DB_KEY_OTHER"));
      mystruct_other.pid = -1; /* -1 means unkonwn */
    }
    break;
  case DMTCP_EVENT_WRITE_CKPT:
    printf("\nThe plugin is being called before checkpointing.\n");
    break;
  case DMTCP_EVENT_REGISTER_NAME_SERVICE_DATA:
    /* Although one process resumes late, they will still all synchronize. */
    if (mystruct.key == 1) sleep(1);
    printf("The plugin is now resuming or restarting from checkpointing.\n");
    dmtcp_send_key_val_pair_to_coordinator("ex-db",
                                           &(mystruct.key),
                                           sizeof(mystruct.key),
                                           &(mystruct.pid),
                                           sizeof(mystruct.pid));
    printf("  Data sent:  My (key, pid) is: (%d, %ld).\n",
	   mystruct.key, (long)mystruct.pid);
    break;
  case DMTCP_EVENT_SEND_QUERIES:
    /* NOTE: DMTCP creates a barrier between
     *   DMTCP_EVENT_REGISTER_NAME_SERVICE_DATA and DMTCP_EVENT_SEND_QUERIES.
     *   The calls to send_key_val_pair and send_query require this barrier.
     *   Associating these functions with the wrong DMTCP events risks aborting
     *   the computation.  Also, calling send_query on a non-existent key
     *   risks aborting the computation.
     *     Currently, calling send_query without having previously called
     *   send_key_val_pair within the same transaction also risks an abort.
     */
    /* Set max size of the buffer &(mystruct.pid) */
    /* This process was called with an environment variable,
     *  EXAMPLE_DB_KEY_OTHER, whose value was used to set mystruct_other.key.
     */
    sizeofPid = sizeof(mystruct_other.pid);
    dmtcp_send_query_to_coordinator("ex-db",
                                    &(mystruct_other.key),
                                    sizeof(mystruct_other.key),
                                    &(mystruct_other.pid),
                                    &sizeofPid);
    printf("Data exchanged:  My (key,pid) is: (%d, %ld);  The other pid is:  "
	  "%ld.\n", mystruct.key, (long)mystruct.pid, (long)mystruct_other.pid);
    break;
  default:
    break;
  }
  DMTCP_NEXT_EVENT_HOOK(event, data);
}

static DmtcpBarrier exampledbBarriers[] = {
  {DMTCP_GLOBAL_BARRIER_RESUME, "RESUME_NS_REGISTER_DATA"},
  {DMTCP_GLOBAL_BARRIER_RESUME, "RESUME_NS_SEND_QUERIES"},

  {DMTCP_GLOBAL_BARRIER_RESTART, "RESTART_NS_REGISTER_DATA"},
  {DMTCP_GLOBAL_BARRIER_RESTART, "RESTART_NS_SEND_QUERIES"}
};

DmtcpPluginDescriptor_t example_db_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "example_db",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Example-db Plugin",
  DMTCP_DECL_BARRIERS(exampledbBarriers),
  NULL
};

DMTCP_DECL_PLUGIN(example_db_plugin);
