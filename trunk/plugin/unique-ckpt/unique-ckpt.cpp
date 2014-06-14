#include <stdlib.h>
#include <string.h>
#include <string>
#include <iomanip>
#include "dmtcpalloc.h"
#include "dmtcp.h"
#include "jfilesystem.h"

using namespace dmtcp;
#define GEN_WIDTH 5

extern "C" int dmtcp_unique_ckpt_enabled(void)
{
  return true;
}

void updateCkptDir()
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

void dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_THREADS_SUSPEND:
      updateCkptDir();
      break;

    default:
      break;
  }
  DMTCP_NEXT_EVENT_HOOK(event, data);
}
