#include <stdio.h>
#include <sys/time.h>
#include <inttypes.h>
#include "dmtcp.h"

#define MAX_CKPT_DIR_LENGTH 128

static char *baseCkptDir;
static char newCkptDir[MAX_CKPT_DIR_LENGTH];

void dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_WRITE_CKPT:
    if (dmtcp_get_generation() == 1) {
      baseCkptDir = (char *)dmtcp_get_ckpt_dir();
    }
    snprintf(newCkptDir, MAX_CKPT_DIR_LENGTH, "%s/%s_%05"PRIu32"", baseCkptDir,
        dmtcp_get_computation_id_str(), dmtcp_get_generation());

    dmtcp_set_ckpt_dir(newCkptDir);
    dmtcp_set_coord_ckpt_dir(newCkptDir);
    break;
  case DMTCP_EVENT_RESUME:
    break;
  default:
    ;
  }

  /* Call this next line in order to pass DMTCP events to later plugins. */
  DMTCP_NEXT_EVENT_HOOK(event, data);
}
