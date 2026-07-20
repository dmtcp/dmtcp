#include <stdlib.h>
#include <string.h>
#include <iomanip>
#include <string>
#include "jfilesystem.h"
#include "config.h"
#include "dmtcp.h"
#include "dmtcpalloc.h"
#include "pluginmanager.h"

using namespace dmtcp;
#define GEN_WIDTH 5

extern "C" int
dmtcp_unique_ckpt_enabled(void)
{
  static const int enabled =
    internalPluginEnabled(INTERNAL_PLUGIN_UNIQUE_CKPT) ? 1 : 0;
  return enabled;
}

static void
updateCkptDir()
{
  const char *ckptDir = dmtcp_get_ckpt_dir();
  string baseDir;

  if (strstr(ckptDir, dmtcp_get_computation_id_str()) != NULL) {
    baseDir = jalib::Filesystem::DirName(ckptDir);
  } else {
    baseDir = ckptDir;
  }
  ostringstream o;
  o << baseDir << "/ckpt_" << dmtcp_get_computation_id_str() << "_"
    << std::setw(GEN_WIDTH) << std::setfill('0') << dmtcp_get_generation();
  dmtcp_set_ckpt_dir(o.str().c_str());
}

static void
uniqueCkpt_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_PRECHECKPOINT:
    updateCkptDir();
    break;

  default:  // other events are not registered
    break;
  }
}

/*
 * The configure default remains authoritative, but this built-in stays
 * environment-controlled so dmtcp_launch can opt it in or out at runtime.
 * It is checkpoint naming policy, not core mechanics, so --disable-all
 * intentionally disables it too.
 */
LIB_PRIVATE DmtcpPluginDescriptor_t UniqueCkptPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "UNIQUE_CKPT",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Unique-ckpt filename plugin",
  uniqueCkpt_EventHook
};
