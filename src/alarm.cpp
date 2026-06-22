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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "config.h"
#include "dmtcp.h"

#include "dmtcp_assert.h"

namespace dmtcp
{
static unsigned int timeLeft = 0;

static void
checkpoint()
{
  timeLeft = alarm(0);
  TRACE("*** Alarm stopped. *** timeLeft={}", timeLeft);
}

static void
resume()
{
  /* Need to restart the timer on resume/restart. */
  if (timeLeft > 0) {
    TRACE("*** Resuming alarm. *** timeLeft={}", timeLeft);
    timeLeft = alarm(timeLeft);
  }
}

static void
alarm_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_PRECHECKPOINT:
    checkpoint();
    break;

  case DMTCP_EVENT_RESUME:
  case DMTCP_EVENT_RESTART:
    resume();
    break;

  default:
    break;
  }
}

LIB_PRIVATE DmtcpPluginDescriptor_t alarmPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "ALARM",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Alarm plugin",
  alarm_EventHook
};
}
