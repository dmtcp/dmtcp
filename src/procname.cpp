#include <sys/prctl.h>
#include "dmtcp.h"
#include "dmtcpalloc.h"
#include "syscallwrappers.h"
#include "../jalib/jassert.h"

using namespace dmtcp;

#define DMTCP_PRGNAME_PREFIX "DMTCP:"

// FIXME: Linux prctl for PR_GET_NAME/PR_SET_NAME is on a per-thread basis.
//   If we want to be really accurate, we should make this thread-local.
struct PrgName {
  char str[16+sizeof(DMTCP_PRGNAME_PREFIX)-1];
};

static map<pid_t, struct PrgName> prgNameMap;

static void prctlReset();
static void prctlGetProcessName();
static void prctlRestoreProcessName();

void dmtcp_ProcName_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_WAIT_FOR_SUSPEND_MSG:
      prctlReset();
      break;

    case DMTCP_EVENT_THREADS_SUSPEND:
    case DMTCP_EVENT_PRE_SUSPEND_USER_THREAD:
      prctlGetProcessName();
      break;

    case DMTCP_EVENT_RESUME_USER_THREAD:
      if (data->resumeUserThreadInfo.isRestart) {
        prctlRestoreProcessName();
      }
      break;

    case DMTCP_EVENT_RESTART:
      prctlRestoreProcessName();
      break;

    default:
      break;
  }
}

void prctlReset()
{
  prgNameMap.clear();
}

void prctlGetProcessName()
{
  struct PrgName &prgName = prgNameMap[gettid()];
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11)
  if (prgName.str[0] == '\0') {
    memset(prgName.str, 0, sizeof(prgName.str));
    strcpy(prgName.str, DMTCP_PRGNAME_PREFIX);
    int ret = prctl(PR_GET_NAME, &prgName.str[strlen(DMTCP_PRGNAME_PREFIX)]);
    if (ret != -1) {
      JTRACE("prctl(PR_GET_NAME, ...) succeeded") (prgName.str);
    } else {
      JASSERT(errno == EINVAL) (JASSERT_ERRNO)
        .Text ("prctl(PR_GET_NAME, ...) failed");
      JTRACE("prctl(PR_GET_NAME, ...) failed. Not supported on this kernel?");
    }
  }
#endif
}

void prctlRestoreProcessName()
{
  struct PrgName &prgName = prgNameMap[gettid()];
  // Although PR_SET_NAME has been supported since 2.6.9, we wouldn't use it on
  // kernel < 2.6.11 since we didn't get the process name using PR_GET_NAME
  // which is supported on >= 2.6.11
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11)
    if (prctl(PR_SET_NAME, prgName.str) != -1) {
      JTRACE("prctl(PR_SET_NAME, ...) succeeded") (prgName.str);
    } else {
      JASSERT(errno == EINVAL) (prgName.str) (JASSERT_ERRNO)
        .Text ("prctl(PR_SET_NAME, ...) failed");
      JTRACE("prctl(PR_SET_NAME, ...) failed") (prgName.str);
    }
#endif
}

