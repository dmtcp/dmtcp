/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef __COORDINATOR_PLUGIN_H__
#define __COORDINATOR_PLUGIN_H__

#include "../jalib/jsocket.h"
#include "../jalib/jconvert.h"
#include "dmtcpalloc.h"
#include "dmtcpmessagetypes.h"
#include "dmtcp_coordinator.h"

namespace dmtcp
{
class CoordinatorPlugin
{
  public:
    virtual void tick(ComputationStatus const& status) {}
    virtual void writeStatusToStream(ostream *o) {}

    virtual void resumeAfterCkpt(ComputationStatus const& status) {}
    virtual void resumeAfterRestart(ComputationStatus const& status) {}
    virtual void userCmd(DmtcpMessage const& msg, ComputationStatus status, DmtcpMessage *reply) {}
    virtual void clientConnected(CoordClient *client, DmtcpMessage const& msg, ComputationStatus status) {}
    virtual void clientDisconnected(CoordClient *client, ComputationStatus status) {}

    // virtual void startCkpt() {}
    // virtual void clientReachedBarrier(CoordClient *client, string const& barrier);
    // virtual void barrierReleased(string const& barrier);
};

/* If dmtcp_launch/dmtcp_restart specifies '-i', theCheckpointInterval
 * will be reset accordingly (valid for current computation).  If dmtcp_command
 * specifies '-i' (or if user interactively invokes 'i' in coordinator),
 * then both theCheckpointInterval and theDefaultCheckpointInterval are set.
 * A value of '0' means:  never checkpoint (manual checkpoint only).
 */
class CkptIntervalManager : public CoordinatorPlugin
{
  public:
    uint32_t theCheckpointInterval; /* Current checkpoint interval */
    uint32_t theDefaultCheckpointInterval; /* Reset to this on new comp*/
    struct timespec nextCkptTimeout;

    CkptIntervalManager(CoordFlags flags)
    : theCheckpointInterval(0),
      theDefaultCheckpointInterval(0)
    {
      if (flags.interval != 0) {
        theDefaultCheckpointInterval = flags.interval;
        theCheckpointInterval = flags.interval;
      }

      nextCkptTimeout = {0, 0};
    }

    void resetCkptTimer(ComputationStatus status)
    {
      if (status.numPeers == 0) {
        nextCkptTimeout = {0, 0};
      } else if (theCheckpointInterval > 0) {
        nextCkptTimeout.tv_sec = status.timestamp.tv_sec + theCheckpointInterval;
        JNOTE("Time before next ckpt") (theCheckpointInterval);
      }
    }

    void updateCheckpointInterval(int interval, ComputationStatus const& status)
    {
      if (interval != -1) {
        theCheckpointInterval = interval;
      }

      resetCkptTimer(status);
    }

    virtual void tick(ComputationStatus const& status) override
    {
      if (status.numPeers == 0) {
        return;
      }

      // If we are not in the running state, nothing to do.
      if (status.minimumState != WorkerState::RUNNING || !status.minimumStateUnanimous) {
        return;
      }

      if (nextCkptTimeout.tv_sec != 0 && status.timestamp.tv_sec > nextCkptTimeout.tv_sec) {
        nextCkptTimeout.tv_sec = 0;
        JTRACE("Next ckpt timeout expired; triggering ckpt");
        DmtcpCoordinator::queueCheckpoint();
      }
    }

    virtual void resumeAfterCkpt(ComputationStatus const& status) override
    {
      resetCkptTimer(status);
    }

    virtual void resumeAfterRestart(ComputationStatus const& status) override
    {
      resetCkptTimer(status);
    }

    virtual void userCmd(DmtcpMessage const& msg, ComputationStatus status, DmtcpMessage *reply) override
    {
      if (msg.coordCmd != 'i') {
        return;
      }

      theDefaultCheckpointInterval = msg.theCheckpointInterval;
      theCheckpointInterval = theDefaultCheckpointInterval;

      JTRACE("Setting checkpoint interval...");
      updateCheckpointInterval(theCheckpointInterval, status);

      if (theCheckpointInterval == 0) {
        JNOTE("Current Checkpoint Interval:"
              " Disabled (checkpoint manually instead)");
      } else {
        JNOTE("Current Checkpoint Interval:") (theCheckpointInterval);
      }
    };

    virtual void clientConnected(CoordClient *client, DmtcpMessage const& msg, ComputationStatus status) override
    {
      if (msg.theCheckpointInterval != -1) {
        theCheckpointInterval = msg.theCheckpointInterval;
      }

      if (status.numPeers == 1 && (client->state() == WorkerState::UNKNOWN || client->state() == WorkerState::RUNNING)) {
        resetCkptTimer(status);
      }
    }

    virtual void clientDisconnected(CoordClient *client, ComputationStatus status) override
    {
      if (status.numPeers == 0) {
        resetCkptTimer(status);
      }
    }
    virtual void writeStatusToStream(ostream *o) override
    {
      *o << "Checkpoint Interval: ";
      if (theCheckpointInterval == 0) {
          *o << "disabled (checkpoint manually instead)" << std::endl;
      } else {
        *o << theCheckpointInterval << std::endl;
      }
    }

};

class StaleTimeoutManager : public CoordinatorPlugin
{
  public:
    timespec stopTime;
    uint32_t theDefaultStaleTimeout;
    uint32_t theStaleTimeout;

    StaleTimeoutManager(CoordFlags flags)
    : theDefaultStaleTimeout(8 * 60 * 60)
    {
      if (flags.staleTimeout != 0) {
        theStaleTimeout = flags.staleTimeout;
      } else {
        theStaleTimeout = theDefaultStaleTimeout;
      }

      stopTime = {0, 0};
    }

    void resetTimeout()
    {
      stopTime = {0, 0};
    }

    virtual void tick(ComputationStatus const &status)
    {
      if (status.numPeers > 0) {
        stopTime.tv_sec = 0;
        return;
      }

      if (theStaleTimeout != 0 && stopTime.tv_sec == 0) {
        // Set stop time.
        stopTime.tv_sec = status.timestamp.tv_sec + theStaleTimeout;
        JNOTE("No active clients; starting stale timeout")(theStaleTimeout);
      }

      if (stopTime.tv_sec != 0 && status.timestamp.tv_sec > stopTime.tv_sec) {
        JNOTE("*** dmtcp_coordinator:  --stale-timeout timed out") (stopTime.tv_sec) (theStaleTimeout);
        exit(1);
      }
    }
};

class TimeoutManager : public CoordinatorPlugin
{
  public:
    timespec stopTime;
    uint32_t theTimeout;

    TimeoutManager(CoordFlags flags)
    {
      theTimeout = flags.timeout;
      stopTime = {0, 0};
    }

    virtual void tick(ComputationStatus const &status)
    {
      if (theTimeout != 0 && stopTime.tv_sec == 0) {
        // initialization is done here to ensure we have the correct timestamp
        // from status.
        stopTime.tv_sec = status.timestamp.tv_sec + theTimeout;
      }

      if (stopTime.tv_sec != 0 && status.timestamp.tv_sec > stopTime.tv_sec) {
        JNOTE("*** dmtcp_coordinator:  --timeout timed out") (theTimeout);
        exit(1);
      }
    }
};

class CoordPluginMgr
{
  public:
    static void initialize(CoordFlags const& flags)
    {
      staleTimeoutManager = new StaleTimeoutManager(flags);
      timeoutManager = new TimeoutManager(flags);
      ckptIntervalManager = new CkptIntervalManager(flags);

      plugins.push_back(staleTimeoutManager);
      plugins.push_back(timeoutManager);
      plugins.push_back(ckptIntervalManager);
    }

    static void tick(ComputationStatus const& status)
    {
      for (CoordinatorPlugin *plugin : plugins) {
        plugin->tick(status);
      }
    }

    static void clientDisconnected(CoordClient *client, ComputationStatus status)
    {
      for (CoordinatorPlugin *plugin : plugins) {
        plugin->clientDisconnected(client, status);
      }
    }

    static void clientConnected(CoordClient *client, DmtcpMessage const& msg, ComputationStatus status)
    {
      for (CoordinatorPlugin *plugin : plugins) {
        plugin->clientConnected(client, msg, status);
      }
    }

    static void resumeAfterCkpt(ComputationStatus const& status)
    {
      for (CoordinatorPlugin *plugin : plugins) {
        plugin->resumeAfterCkpt(status);
      }
    }

    static void resumeAfterRestart(ComputationStatus const& status)
    {
      for (CoordinatorPlugin *plugin : plugins) {
        plugin->resumeAfterRestart(status);
      }
    }

    static void writeStatusToStream(ostream *o)
    {
       for (CoordinatorPlugin *plugin : plugins) {
        plugin->writeStatusToStream(o);
      }
    }

  public:
    // Defined in dmtcp_coordinator.cpp.
    // These variables are needed until we can further refactor the coordinator
    // code to strip out state related to these plugins (e.g., ckpt interval).
    // Once the plugin-specific state is completely removed from the rest of the
    // coordinator logic, we can have these variables local to the plugin
    // manager.
    static StaleTimeoutManager *staleTimeoutManager;
    static TimeoutManager *timeoutManager;
    static CkptIntervalManager *ckptIntervalManager;

  private:
    static vector<CoordinatorPlugin*> plugins;
};

}
#endif // ifndef __COORDINATOR_PLUGIN_H__
