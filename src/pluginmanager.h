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
#ifdef TIMING
    static void logCkptResumeBarrierOverhead();
    static void logRestartBarrierOverhead(double ckptReadTime);
#endif

  private:
    void initializePlugins();

    vector<PluginInfo *>pluginInfos;
};
}
#endif // ifndef __PLUGINMANAGER_H__
