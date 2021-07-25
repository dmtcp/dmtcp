#include <signal.h>

#include "lower_half_api.h"
#include "split_process.h"
#include "p2p_log_replay.h"
#include "p2p_drain_send_recv.h"
#include "record-replay.h"
#include "two-phase-algo.h"

#include "config.h"
#include "dmtcp.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "protectedfds.h"
#include "procselfmaps.h"

using namespace dmtcp;

/* Global variables */

int g_numMmaps = 0;
MmapInfo_t *g_list = NULL;

// #define DEBUG

#undef dmtcp_skip_memory_region_ckpting

static inline int
regionContains(const void *haystackStart,
               const void *haystackEnd,
               const void *needleStart,
               const void *needleEnd)
{
  return needleStart >= haystackStart && needleEnd <= haystackEnd;
}

EXTERNC int
dmtcp_skip_memory_region_ckpting(const ProcMapsArea *area)
{
  if (area->addr == lh_info.startText ||
      strstr(area->name, "/dev/zero") ||
      strstr(area->name, "/dev/kgni") ||
      // FIXME: must comment out for VASP 5/RPA jobs on 2 knl nodes,
      // don't know why.
      strstr(area->name, "/SYSV") ||
      strstr(area->name, "/dev/xpmem") ||
      strstr(area->name, "/dev/shm") ||
      area->addr == lh_info.startData) {
    JTRACE("Ignoring region")(area->name)((void*)area->addr);
    return 1;
  }
  if (!g_list) return 0;
  for (int i = 0; i < g_numMmaps; i++) {
    void *lhMmapStart = g_list[i].addr;
    void *lhMmapEnd = (VA)g_list[i].addr + g_list[i].len;
    if (!g_list[i].unmapped &&
        regionContains(lhMmapStart, lhMmapEnd, area->addr, area->endAddr)) {
      JTRACE("Ignoring region")
           (area->name)((void*)area->addr)(area->size)
           (lhMmapStart)(lhMmapEnd);
      return 1;
    } else if (!g_list[i].unmapped &&
               regionContains(area->addr, area->endAddr,
                              lhMmapStart, lhMmapEnd)) {
      JTRACE("Unhandled case")
           (area->name)((void*)area->addr)(area->size)
           (lhMmapStart)(lhMmapEnd);
    }
  }
  return 0;
}

// Handler for SIGSEGV: forces the code into an infinite loop for attaching
// GDB and debugging
void
segvfault_handler(int signum, siginfo_t *siginfo, void *context)
{
  int dummy = 0;
  JNOTE("Caught a segmentation fault. Attach gdb to inspect...");
  while (!dummy);
}

// Installs a handler for SIGSEGV; useful for debugging crashes
void
initialize_segv_handler()
{
  static struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_flags = SA_SIGINFO;
  action.sa_sigaction = segvfault_handler;
  sigemptyset(&action.sa_mask);

  JASSERT(sigaction(SIGSEGV, &action, NULL) != -1)
    (JASSERT_ERRNO).Text("Could not set up the segfault handler");
}


static void
mpi_plugin_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_INIT:
    {
      JTRACE("*** DMTCP_EVENT_INIT");
      initialize_segv_handler();
      JASSERT(!splitProcess()).Text("Failed to create, initialize lower haf");
      break;
    }
    case DMTCP_EVENT_EXIT:
      JTRACE("*** DMTCP_EVENT_EXIT");
      break;
    default:
      break;
  }
}

// Sets the global 'g_list' pointer to the beginning of the MmapInfo_t array
// in the lower half
static void
getLhMmapList()
{
  getMmappedList_t fnc = (getMmappedList_t)lh_info.getMmappedListFptr;
  if (fnc) {
    g_list = fnc(&g_numMmaps);
  }
  JTRACE("Lower half region info")(g_numMmaps);
  for (int i = 0; i < g_numMmaps; i++) {
    JTRACE("Lh region")(g_list[i].addr)(g_list[i].len)(g_list[i].unmapped);
  }
}

// Sets the lower half's __environ variable to point to upper half's __environ
static void
updateLhEnviron()
{
  updateEnviron_t fnc = (updateEnviron_t)lh_info.updateEnvironFptr;
  fnc(__environ);
}

static DmtcpBarrier mpiPluginBarriers[] = {
  { DMTCP_GLOBAL_BARRIER_PRE_SUSPEND, NULL,
    "Drain-MPI-Collectives", drainMpiCollectives},
  { DMTCP_PRIVATE_BARRIER_PRE_CKPT, logIbarrierIfInTrivBarrier,
    "Log-MPI_Ibarrier-if-in-trivial-barrier"},
  { DMTCP_PRIVATE_BARRIER_PRE_CKPT, getLhMmapList,
    "GetLocalLhMmapList"},
  { DMTCP_PRIVATE_BARRIER_PRE_CKPT, getLocalRankInfo,
    "GetLocalRankInfo"},
  { DMTCP_GLOBAL_BARRIER_PRE_CKPT, updateCkptDirByRank,
    "update-ckpt-dir-by-rank" },
  { DMTCP_GLOBAL_BARRIER_PRE_CKPT, registerLocalSendsAndRecvs,
    "Register-local-sends-and-receives" },
  { DMTCP_GLOBAL_BARRIER_PRE_CKPT, drainSendRecv,
    "Drain-Send-Recv" },
  { DMTCP_PRIVATE_BARRIER_RESUME, clearPendingCkpt,
    "Clear-Pending-Ckpt-Msg"},
  { DMTCP_PRIVATE_BARRIER_RESUME, resetDrainCounters,
    "Reset-Drain-Send-Recv-Counters"},
  { DMTCP_PRIVATE_BARRIER_RESTART, save2pcGlobals,
    "save-global-variables-in-2pc" },
  { DMTCP_PRIVATE_BARRIER_RESTART, updateLhEnviron,
    "updateEnviron" },
  { DMTCP_PRIVATE_BARRIER_RESTART, clearPendingCkpt,
    "Clear-Pending-Ckpt-Msg-Post-Restart"},
  { DMTCP_PRIVATE_BARRIER_RESTART, resetDrainCounters,
    "Reset-Drain-Send-Recv-Counters"},
  { DMTCP_GLOBAL_BARRIER_RESTART, restoreMpiLogState,
    "restoreMpiLogState"},
  { DMTCP_GLOBAL_BARRIER_RESTART, replayMpiP2pOnRestart,
    "replay-async-receives" },
  { DMTCP_PRIVATE_BARRIER_RESTART, restore2pcGlobals,
    "restore-global-variables-in-2pc" },
};

DmtcpPluginDescriptor_t mpi_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "mpi_plugin",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "MPI Proxy Plugin",
  DMTCP_DECL_BARRIERS(mpiPluginBarriers),
  mpi_plugin_event_hook
};

DMTCP_DECL_PLUGIN(mpi_plugin);
