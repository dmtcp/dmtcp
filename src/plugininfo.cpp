/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include "barrierinfo.h"
#include "plugininfo.h"
#include "dmtcp.h"
#include "jassert.h"
#include "shareddata.h"
#include "coordinatorapi.h"

namespace dmtcp {

PluginInfo::PluginInfo(const DmtcpPluginDescriptor_t& descr,
                       const vector<BarrierInfo*>& _preCkptBarriers,
                       const vector<BarrierInfo*>& _resumeBarriers,
                       const vector<BarrierInfo*>& _restartBarriers)
  : pluginName (descr.pluginName),
    authorName (descr.authorName),
    authorEmail (descr.authorEmail),
    description (descr.description),
    event_hook (descr.event_hook),
    preCkptBarriers (_preCkptBarriers),
    resumeBarriers (_resumeBarriers),
    restartBarriers (_restartBarriers)
{}

PluginInfo *PluginInfo::create(const DmtcpPluginDescriptor_t& descr)
{
  vector<BarrierInfo*> preCkptBarriers;
  vector<BarrierInfo*> resumeBarriers;
  vector<BarrierInfo*> restartBarriers;

  for (size_t i = 0; i < descr.numBarriers; i++) {
    BarrierInfo *barrier = new BarrierInfo(descr.pluginName, descr.barriers[i]);
    switch (barrier->type) {
      case DMTCP_GLOBAL_BARRIER_PRE_CKPT:
      case DMTCP_LOCAL_BARRIER_PRE_CKPT:
      case DMTCP_PRIVATE_BARRIER_PRE_CKPT:
        preCkptBarriers.push_back(barrier);
        break;

      case DMTCP_GLOBAL_BARRIER_RESUME:
      case DMTCP_LOCAL_BARRIER_RESUME:
      case DMTCP_PRIVATE_BARRIER_RESUME:
        resumeBarriers.push_back(barrier);
        break;

      case DMTCP_GLOBAL_BARRIER_RESTART:
      case DMTCP_LOCAL_BARRIER_RESTART:
      case DMTCP_PRIVATE_BARRIER_RESTART:
        restartBarriers.push_back(barrier);
        break;

      default:
        JASSERT(false) .Text("NOT REACHED");
    }
  }

  return new PluginInfo(descr,
      preCkptBarriers,
      resumeBarriers,
      restartBarriers);
}

void PluginInfo::eventHook (const DmtcpEvent_t event, DmtcpEventData_t *data)
{
  if (event_hook != NULL) {
    event_hook(event, data);
  }
}

void PluginInfo::processBarriers()
{
  if (WorkerState::currentState() == WorkerState::CHECKPOINTING) {
    for (int i = 0; i < preCkptBarriers.size(); i++) {
      processBarrier(preCkptBarriers[i]);
    }
  } else if (WorkerState::currentState() == WorkerState::CHECKPOINTED) {
    for (int i = 0; i < resumeBarriers.size(); i++) {
      processBarrier(resumeBarriers[i]);
    }
  } else if (WorkerState::currentState() == WorkerState::RESTARTING) {
    for (int i = 0; i < restartBarriers.size(); i++) {
      processBarrier(restartBarriers[i]);
    }
  } else {
    JASSERT(false) .Text("Not Reached");
  }
}

void PluginInfo::processBarrier(BarrierInfo *barrier)
{
  if (dmtcp_no_coordinator()) {
    // Do nothing.
  } else if (barrier->isGlobal()) {
    JTRACE("Waiting for global barrier") (barrier->toString());
    CoordinatorAPI::instance().waitForBarrier(barrier->toString());
  } else if (barrier->isLocal()) {
    JTRACE("Waiting for local barrier") (barrier->toString());
    SharedData::waitForBarrier(barrier->toString());
  }

  JTRACE("Barrier released") (barrier->toString());
  barrier->callback();
}

}
