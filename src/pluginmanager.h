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

#ifndef __PLUGINMANAGER_H__
#define __PLUGINMANAGER_H__

#include "dmtcp.h"
#include "dmtcpalloc.h"
#include "plugininfo.h"

namespace dmtcp
{
/*
 * Built-in plugins use DmtcpPluginDescriptor_t::pluginName as their internal
 * PluginManager id.  Keep these names identical to the owning descriptor's
 * all-caps pluginName; enablement variables are derived as
 * DMTCP_<PLUGIN>_PLUGIN.
 */
static const char INTERNAL_PLUGIN_PATHVIRT[] = "PATHVIRT";
static const char INTERNAL_PLUGIN_SYSLOG[] = "SYSLOG";
static const char INTERNAL_PLUGIN_RLIMIT_FLOAT[] = "RLIMIT_FLOAT";
static const char INTERNAL_PLUGIN_ALARM[] = "ALARM";
static const char INTERNAL_PLUGIN_TERMINAL[] = "TERMINAL";
static const char INTERNAL_PLUGIN_COORDINATOR_API[] = "COORDINATOR_API";
static const char INTERNAL_PLUGIN_PROCESS_INFO[] = "PROCESS_INFO";
static const char INTERNAL_PLUGIN_UNIQUE_PID[] = "UNIQUE_PID";
static const char INTERNAL_PLUGIN_UNIQUE_CKPT[] = "UNIQUE_CKPT";
static const char INTERNAL_PLUGIN_SSH[] = "SSH";
static const char INTERNAL_PLUGIN_EVENT[] = "EVENT";
static const char INTERNAL_PLUGIN_FILE[] = "FILE";
static const char INTERNAL_PLUGIN_PTY[] = "PTY";
static const char INTERNAL_PLUGIN_SOCKET[] = "SOCKET";
static const char INTERNAL_PLUGIN_SVIPC[] = "SVIPC";
static const char INTERNAL_PLUGIN_TIMER[] = "TIMER";
static const char INTERNAL_PLUGIN_PID[] = "PID";
static const char INTERNAL_PLUGIN_ALLOC[] = "ALLOC";
static const char INTERNAL_PLUGIN_DL[] = "DL";

bool internalPluginEnabled(const char *pluginName);

class PluginManager
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR
    PluginManager();

    void registerPlugin(DmtcpPluginDescriptor_t descr);

    static void initialize();
    static void registerBarriersWithCoordinator();
    static void processPreSuspendBarriers();
    static void processCkptBarriers();
    static void processResumeBarriers();
    static void processRestartBarriers();
    static void eventHook(DmtcpEvent_t event, DmtcpEventData_t *data = NULL);
  private:
    vector<PluginInfo *>pluginInfos;
};
}
#endif // ifndef __PLUGINMANAGER_H__
