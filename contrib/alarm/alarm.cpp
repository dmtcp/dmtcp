/* FILE: alarm.cpp
 * AUTHOR: Rohan Garg
 * EMAIL: rohgarg@ccs.neu.edu
 * Copyright (C) 2015 Rohan Garg
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include "dmtcp.h"

#ifdef ALARM_PLUGIN_DEBUG
# define DEBUG
#endif
#include "jassert.h"

/* File local functions  */
static unsigned
start_stop_alarm(unsigned int timeout)
{
  return alarm(timeout);
}

extern "C" void
dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  static unsigned int l_timeleft = 0;
  switch (event) {
    case DMTCP_EVENT_INIT:
      {
        JTRACE("The plugin has been initialized.");
        break;
      }
    case DMTCP_EVENT_WRITE_CKPT:
      {
        JTRACE("*** The plugin is being called before checkpointing. ***");
        l_timeleft = start_stop_alarm(0);
        JTRACE("*** Alarm stopped. ***") (l_timeleft);
        break;
      }
    case DMTCP_EVENT_THREADS_RESUME:
      {
        if (data->resumeInfo.isRestart) {
          JTRACE("The plugin is now restarting from checkpointing.");
        } else {
          JTRACE("The process is now resuming after checkpoint.");
        }
        /* Need to restart the timer on resume/restart. */
        if (l_timeleft > 0) {
          JTRACE("*** Resuming alarm. ***") (l_timeleft);
          l_timeleft = start_stop_alarm(l_timeleft);
        }
        break;
      }
    default:
      break;
  }
  DMTCP_NEXT_EVENT_HOOK(event, data);
}
