/* NOTE:  if you just want to insert your own code at the time of checkpoint
 *  and restart, there are two simpler additional mechanisms:
 *  dmtcpaware, and the MTCP special hook functions:
 *    mtcpHookPreCheckpoint, mtcpHookPostCheckpoint, mtcpHookRestart
 */

#include <stdio.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include "dmtcp.h"

#define DEBUG_SIGNATURE "[Apache Plugin]"
#ifdef APACHE_PLUGIN_DEBUG
# define DPRINTF(fmt, ...) \
  do { fprintf(stderr, DEBUG_SIGNATURE fmt, ## __VA_ARGS__); } while (0)
#else
# define DPRINTF(fmt, ...) \
  do { } while (0)
#endif

#define REAL_TO_VIRTUAL_SHM_ID(realId) (realId)
#define _real_shmget NEXT_FNC(shmget)


void dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  /* NOTE:  See warning in plugin/README about calls to printf here. */
  switch (event) {
  case DMTCP_EVENT_INIT:
    DPRINTF("The plugin containing %s has been initialized.\n", __FILE__);
    break;
  case DMTCP_EVENT_WRITE_CKPT:
    DPRINTF("\n*** The plugin is being called before checkpointing. ***\n");
    break;
  case DMTCP_EVENT_RESUME:
    DPRINTF("*** The plugin has now been checkpointed. ***\n");
    break;
  case DMTCP_EVENT_THREADS_RESUME:
    if (data->resumeInfo.isRestart) {
      DPRINTF("The plugin is now resuming or restarting from checkpointing.\n");
    } else {
      DPRINTF("The process is now resuming after checkpoint.\n");
    }
    break;
  case DMTCP_EVENT_EXIT:
    DPRINTF("The plugin is being called before exiting.\n");
    break;
  /* These events are unused and could be omitted.  See dmtcp.h for
   * complete list.
   */
  case DMTCP_EVENT_RESTART:
  case DMTCP_EVENT_ATFORK_CHILD:
  case DMTCP_EVENT_THREADS_SUSPEND:
  case DMTCP_EVENT_LEADER_ELECTION:
  case DMTCP_EVENT_DRAIN:
  default:
    break;
  }
  DMTCP_NEXT_EVENT_HOOK(event, data);
}

/*
 * Wrapper functions
 */

/*
 * This wrapper serves two purposes when trying to checkpoint-restart Apache
 * with PHP-FastCGI. See notes below.
 *
 * NOTE: This wrapper assumes that DMTCP has been loaded with the core sysv IPC
 *       plugin.
 */
int shmget(key_t key, size_t size, int shmflg)
{
  int realId = -1;
  int virtId = -1;
  DMTCP_PLUGIN_DISABLE_CKPT();
  DPRINTF("Before if: shmget %d %u %x", (key), (size), (shmflg));
  /*
   * Apache starts as a root process, opens shared memory areas as owner RW,
   * and then drops privileges. This causes problems while checkpointing; in
   * particular, when checking for stale shm connections, the shmctl(IPC_STAT)
   * will fail because the new process will not have enough privileges.
   *
   * This checks if the flag bits are only owner RW and then sets them to RW
   * for all.
   * XXX: Need a better fix.
   */
  if (shmflg & 0600) {
    shmflg |= 0666;
    DPRINTF("In if: shmget", (key), (size), (shmflg));
  }
  /* The IPC_EXCL bit can cause problems during the restart if Apache does not
   * clean up the shm on shutdown. During the ShmSegment::postRestart() the
   * shmget() call will fail if the IPC_EXCL bit is set but the shm has not
   * been reaped.
   *
   * This checks and removes the IPC_EXCL bit from the flags.
   * XXX: Need a better fix.
   */
  if (shmflg & IPC_EXCL) {
     DPRINTF("Removing the IPC_EXCL bit from the shm flags %x", (shmflg));
     shmflg &= (~IPC_EXCL);
     DPRINTF("Removed the IPC_EXCL bit from the shm flags %x", (shmflg));
  }
  DPRINTF("After if: shmget", (key), (size), (shmflg));
  realId = _real_shmget(key, size, shmflg);
  if (realId != -1) {
    /*
     * NOTE: This doesn't need to do anything now because the core sysv
     *       IPC plugin takes care of the virtualization.
     */
    virtId = REAL_TO_VIRTUAL_SHM_ID(realId);
    DPRINTF ("Creating new Shared memory segment %d %u %x %d %d",
             (key), (size),
             (shmflg), (realId),
             (virtId));
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return virtId;
}
