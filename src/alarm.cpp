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
#include "config.h"

#include "jassert.h"

namespace dmtcp {
static unsigned int timeLeft = 0;

static void checkpoint()
{
  timeLeft = alarm(0);
  JTRACE("*** Alarm stopped. ***") (timeLeft);
}

static void resume()
{
  /* Need to restart the timer on resume/restart. */
  if (timeLeft > 0) {
    JTRACE("*** Resuming alarm. ***") (timeLeft);
    timeLeft = alarm(timeLeft);
  }
}

static DmtcpBarrier alarmBarriers[] = {
  {DMTCP_LOCAL_BARRIER_PRE_CKPT, checkpoint, "checkpoint"},
  {DMTCP_LOCAL_BARRIER_RESUME, resume, "resume"},
  {DMTCP_LOCAL_BARRIER_RESTART, resume, "restart"}
};

static DmtcpPluginDescriptor_t alarmPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "alarm",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Alarm plugin",
  DMTCP_DECL_BARRIERS(alarmBarriers),
  NULL
};


DmtcpPluginDescriptor_t dmtcp_Alarm_PluginDescr()
{
  return alarmPlugin;
}
}
