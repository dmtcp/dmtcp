/* FILE: apache.cpp
 * AUTHOR: Rohan Garg
 * EMAIL: rohgarg@ccs.neu.edu
 * Copyright (C) 2014 Rohan Garg
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "config.h"
#include "dmtcp.h"

#define DEBUG_SIGNATURE "[Apache Plugin]"
#ifdef APACHE_PLUGIN_DEBUG
# define DPRINTF(fmt, ...) \
  do { fprintf(stderr, DEBUG_SIGNATURE fmt, ## __VA_ARGS__); } while (0)
#else // ifdef APACHE_PLUGIN_DEBUG
# define DPRINTF(fmt, ...) \
  do {} while (0)
#endif // ifdef APACHE_PLUGIN_DEBUG

#define REAL_TO_VIRTUAL_SHM_ID(realId) (realId)
#define _real_shmget NEXT_FNC(shmget)


static void
apache_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  /* NOTE:  See warning in plugin/README about calls to printf here. */
  switch (event) {
  case DMTCP_EVENT_INIT:
    DPRINTF("The plugin containing %s has been initialized.\n", __FILE__);
    break;
  case DMTCP_EVENT_EXIT:
    DPRINTF("The plugin is being called before exiting.\n");
    break;
  default:
    break;
  }
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
int
shmget(key_t key, size_t size, int shmflg)
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
    DPRINTF("Creating new Shared memory segment %d %u %x %d %d",
            (key), (size),
            (shmflg), (realId),
            (virtId));
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return virtId;
}

DmtcpPluginDescriptor_t apache_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "apache",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Apache plugin",
  DMTCP_NO_PLUGIN_BARRIERS,
  apache_event_hook
};

DMTCP_DECL_PLUGIN(apache_plugin);
