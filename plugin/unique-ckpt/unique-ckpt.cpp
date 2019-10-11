#include <stdlib.h>
#include <string.h>
#include <iomanip>
#include <string>
#include "jfilesystem.h"
#include "config.h"
#include "dmtcp.h"
#include "dmtcpalloc.h"

using namespace dmtcp;
#define GEN_WIDTH 5

extern "C" int
dmtcp_unique_ckpt_enabled(void)
{
  return true;
}

void
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
uniqueckpt_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_PRECHECKPOINT:
    updateCkptDir();
    break;

  default:  // other events are not registered
    break;
  }
}

DmtcpPluginDescriptor_t unique_ckpt_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "unique-ckpt",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Unique-ckpt filename plugin",
  uniqueckpt_EventHook
};

DMTCP_DECL_PLUGIN(unique_ckpt_plugin);
