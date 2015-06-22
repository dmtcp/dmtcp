#include <sys/prctl.h>
#include "dmtcp.h"
#include "dmtcpalloc.h"
#include "util.h"
#include "syscallwrappers.h"
#include "../jalib/jassert.h"

using namespace dmtcp;

static const char* DMTCP_PRGNAME_PREFIX = "DMTCP:";

static map<pid_t, string> prgNameMap;

static void prctlReset();
static void prctlGetProcessName();
static void prctlRestoreProcessName();

static pthread_mutex_t prgNameMapLock = PTHREAD_MUTEX_INITIALIZER;

// No need to reset the locks on fork because the locks are grabbed/released
// only during ckpt cycle and during that time fork is guaranteed to _not_ take
// place (courtesy of wrapper-execution locks).
static void lockPrgNameMapLock()
{
  JASSERT(_real_pthread_mutex_lock(&prgNameMapLock) == 0) (JASSERT_ERRNO);
}

static void unlockPrgNameMapLock()
{
  JASSERT(_real_pthread_mutex_unlock(&prgNameMapLock) == 0) (JASSERT_ERRNO);
}

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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11)
  char name[17] = {0};
  int ret = prctl(PR_GET_NAME, name);
  if (ret != -1) {
    lockPrgNameMapLock();
    prgNameMap[dmtcp_gettid()] = name;
    unlockPrgNameMapLock();
    JTRACE("prctl(PR_GET_NAME, ...) succeeded") (name);
  } else {
    JASSERT(errno == EINVAL) (JASSERT_ERRNO)
      .Text ("prctl(PR_GET_NAME, ...) failed");
    JTRACE("prctl(PR_GET_NAME, ...) failed. Not supported on this kernel?");
  }
#endif
}

void prctlRestoreProcessName()
{
  // Although PR_SET_NAME has been supported since 2.6.9, we wouldn't use it on
  // kernel < 2.6.11 since we didn't get the process name using PR_GET_NAME
  // which is supported on >= 2.6.11
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11)
  // NOTE: We don't need to protect the access to prgNameMap with a lock
  // because all accesses during restart are guaranteed to be read-only.
  string prgName = prgNameMap[dmtcp_gettid()];
  if (!Util::strStartsWith(prgName, DMTCP_PRGNAME_PREFIX)) {
    // Add the "DMTCP:" prefix.
    prgName = DMTCP_PRGNAME_PREFIX + prgName;
  }

  if (prctl(PR_SET_NAME, prgName.c_str()) != -1) {
    JTRACE("prctl(PR_SET_NAME, ...) succeeded") (prgName);
  } else {
    JASSERT(errno == EINVAL) (prgName) (JASSERT_ERRNO)
      .Text ("prctl(PR_SET_NAME, ...) failed");
    JTRACE("prctl(PR_SET_NAME, ...) failed") (prgName);
  }
#endif
}

