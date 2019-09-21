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

#ifndef __PLUGININFO_H__
#define __PLUGININFO_H__

#include "jassert.h"
#include "dmtcp.h"
#include "dmtcpalloc.h"
#include "dmtcpmessagetypes.h"

namespace dmtcp
{
class PluginInfo
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR

    PluginInfo(const DmtcpPluginDescriptor_t &descr)
      : pluginName(descr.pluginName),
        authorName(descr.authorName),
        authorEmail(descr.authorEmail),
        description(descr.description),
        event_hook(descr.event_hook)
    {}

    const string pluginName;
    const string authorName;
    const string authorEmail;
    const string description;
    void(*const event_hook)(const DmtcpEvent_t event, DmtcpEventData_t * data);
};
}
#endif // ifndef __PLUGININFO_H__
