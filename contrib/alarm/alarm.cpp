/* FILE: alarm.cpp
 * AUTHOR: Rohan Garg
 * EMAIL: rohgarg@ccs.neu.edu
 * Copyright (C) 2015 Rohan Garg
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

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include "dmtcp.h"
#include "jassert.h"

#ifdef ALARM_PLUGIN_DEBUG
# undef JTRACE
# define JTRACE JNOTE
#endif

#define _next_alarm NEXT_FNC(alarm)

bool g_usealarm = false; 

/* File local functions  */
static unsigned
start_stop_alarm(unsigned int timeout)
{
  return alarm(timeout);
}

/* Wrapper to determine if the application is using alarm */
extern "C" unsigned int
alarm(unsigned int seconds)
{
  g_usealarm = true;
  return _next_alarm(seconds);
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
        if (g_usealarm) {
          l_timeleft = start_stop_alarm(0);
          JTRACE("*** Alarm stopped. ***") (l_timeleft);
        }
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
        if (g_usealarm) {
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
