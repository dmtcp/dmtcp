#include <sys/types.h>
#include <sys/stat.h>
#include "jassert.h"
#include "dmtcp.h"

extern "C" int dmtcp_must_ckpt_file(const char *abspath)
{
  if (strstr(abspath, "/home/") == 0) {
    return 1;
  }
  return 0;
}

extern "C" void dmtcp_get_new_file_path(const char *abspath, const char *cwd,
                                        char *newpath)
{
  // Dummy for now.
  return;
}

static void preCkpt()
{
  // Code to execute on ckpt phase.
  // You might want to update the criterion for dmtcp_must_ckpt_file.
}

static void restart()
{
  // Code to execute on restart phase.
  // You might want to update the criterion for dmtcp_get_new_file_path.
}

extern "C" void dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_THREADS_SUSPEND:
      preCkpt();
      break;

    case DMTCP_EVENT_RESTART:
      restart();
      break;

    default:
      break;
  }

  DMTCP_NEXT_EVENT_HOOK(event, data);
  return;
}
