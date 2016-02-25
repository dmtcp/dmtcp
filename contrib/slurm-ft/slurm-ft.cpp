#include <signal.h>

#include "jassert.h"
#include "dmtcp.h"

#define DEFAULT_SLURM_SIGNAL SIGRTMIN+2

static void
startCheckpoint(int sig, siginfo_t *si, void *uc)
{
  dmtcp_checkpoint();
}

static void
setup_signal_handler()
{
  struct sigaction sa;
  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = startCheckpoint;
  sigemptyset(&sa.sa_mask);
  if (sigaction(DEFAULT_SLURM_SIGNAL, &sa, NULL) == -1) {
      JWARNING(false)(JASSERT_ERRNO).Text("Error setting up signal handler for SLURM");
  }
}

extern "C" void
dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_INIT:
      {
        setup_signal_handler();
      }
      break;
    default:
      break;
  }

  DMTCP_NEXT_EVENT_HOOK(event, data);
  return;
}
