#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <inttypes.h>
#include "dmtcp.h"
#include "config.h"

#define MAX_CKPT_DIR_LENGTH 256
#define MAX_HOST_NAME_LENTGH 128

static char *baseCkptDir;
static char newCkptDir[MAX_CKPT_DIR_LENGTH];
static char hostname[MAX_HOST_NAME_LENTGH];

static void pre_ckpt()
{
  if (dmtcp_get_generation() == 1) {
    baseCkptDir = (char *)dmtcp_get_ckpt_dir();
    gethostname(hostname, MAX_HOST_NAME_LENTGH - 1);
  }
  snprintf(newCkptDir, MAX_CKPT_DIR_LENGTH - 1, "%s/%s/%05"PRIu32"", baseCkptDir,
           hostname, dmtcp_get_generation());

  dmtcp_set_ckpt_dir(newCkptDir);
  dmtcp_set_coord_ckpt_dir(newCkptDir);
}

static void snooze_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  return;
}

static DmtcpBarrier snoozeBarriers[] = {
  {DMTCP_GLOBAL_BARRIER_PRE_CKPT, pre_ckpt, "checkpoint"}
};

DmtcpPluginDescriptor_t snooze_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "snooze",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Snooze Plugin",
  DMTCP_DECL_BARRIERS(snoozeBarriers),
  snooze_event_hook
};

DMTCP_DECL_PLUGIN(snooze_plugin);
