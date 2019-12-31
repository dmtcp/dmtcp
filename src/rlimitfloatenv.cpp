#include <fenv.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "../jalib/jassert.h"
#include "shareddata.h"
#include "util.h"
#include "config.h"
#include "dmtcp.h"

/*************************************************************************
 *
 *  Save and restore rlimit and float exception settings.
 *
 *************************************************************************/

namespace dmtcp
{

static int roundingMode = -1;
static fenv_t envp;
static rlim_t rlim_cur_as = 0;
static rlim_t rlim_cur_core = 0;
static rlim_t rlim_cur_cpu = 0;
static rlim_t rlim_cur_data = 0;
static rlim_t rlim_cur_fsize = 0;
static rlim_t rlim_cur_nice = 0;
static rlim_t rlim_cur_nofile = 0;
static rlim_t rlim_cur_nproc = 0;
static rlim_t rlim_cur_stack = 0;

static void
save_rlimit_float_settings()
{
  roundingMode = fegetround();
  fegetenv(&envp);

  struct rlimit rlim = {0, 0};
#define SAVE_RLIMIT(_RLIMIT, _rlim_cur) \
  getrlimit(_RLIMIT, &rlim); \
  _rlim_cur = rlim.rlim_cur;

  SAVE_RLIMIT(RLIMIT_AS, rlim_cur_as);
  SAVE_RLIMIT(RLIMIT_CORE, rlim_cur_core);
  SAVE_RLIMIT(RLIMIT_CPU, rlim_cur_cpu);
  SAVE_RLIMIT(RLIMIT_DATA, rlim_cur_data);
  SAVE_RLIMIT(RLIMIT_FSIZE, rlim_cur_fsize);
  SAVE_RLIMIT(RLIMIT_NICE, rlim_cur_nice);
  SAVE_RLIMIT(RLIMIT_NOFILE, rlim_cur_nofile);
  SAVE_RLIMIT(RLIMIT_NPROC, rlim_cur_nproc);
  SAVE_RLIMIT(RLIMIT_STACK, rlim_cur_stack);
}

static void
restore_rlimit_float_settings()
{
  fesetenv(&envp);
  fesetround(roundingMode);

  struct rlimit rlim = {0, 0};

#define RESTORE_RLIMIT(_RLIMIT, _rlim_cur) \
  if (_rlim_cur != RLIM_INFINITY) { \
    getrlimit(_RLIMIT, &rlim); \
    JWARNING(_rlim_cur <= rlim.rlim_max) (_rlim_cur) (rlim.rlim_max) \
      .Text("Prev. soft limit of " #_RLIMIT " lowered to new hard limit"); \
    rlim.rlim_cur = _rlim_cur; \
    JASSERT(setrlimit(_RLIMIT, &rlim) == 0) (JASSERT_ERRNO); \
  }

  RESTORE_RLIMIT(RLIMIT_AS, rlim_cur_as);
  RESTORE_RLIMIT(RLIMIT_CORE, rlim_cur_core);
  RESTORE_RLIMIT(RLIMIT_CPU, rlim_cur_cpu);
  RESTORE_RLIMIT(RLIMIT_DATA, rlim_cur_data);
  RESTORE_RLIMIT(RLIMIT_FSIZE, rlim_cur_fsize);
  RESTORE_RLIMIT(RLIMIT_NICE, rlim_cur_nice);
  RESTORE_RLIMIT(RLIMIT_NOFILE, rlim_cur_nofile);
  RESTORE_RLIMIT(RLIMIT_NPROC, rlim_cur_nproc);
  RESTORE_RLIMIT(RLIMIT_STACK, rlim_cur_stack);
}

static void
rlimitfloat_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_PRECHECKPOINT:
    save_rlimit_float_settings();
    break;

  case DMTCP_EVENT_RESTART:
    restore_rlimit_float_settings();
    break;

  default:
    break;
  }
}

static DmtcpPluginDescriptor_t rlimitFloatPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "rlimit_float",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Rlimit/floating point plugin",
  rlimitfloat_EventHook
};


DmtcpPluginDescriptor_t
dmtcp_Rlimit_Float_PluginDescr()
{
  return rlimitFloatPlugin;
}
}
