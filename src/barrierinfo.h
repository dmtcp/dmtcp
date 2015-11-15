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

#ifndef __BARRIERINFO_H__
#define __BARRIERINFO_H__

#include "dmtcpalloc.h"
#include "dmtcp.h"

namespace dmtcp
{
  class BarrierInfo
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      BarrierInfo(string _pluginName, const DmtcpBarrier& barrier)
        : type (barrier.type),
          callback (barrier.callback),
          id (barrier.id),
          pluginName (_pluginName)
      {}

      string toString() const
      {
        return pluginName + "::" + id;
      }

      bool operator == (const BarrierInfo& that) const
      {
        return type == that.type &&
               id == that.id &&
               pluginName == that.pluginName;
      }

      bool isGlobal() const {
        return
          type == DMTCP_GLOBAL_BARRIER_PRE_CKPT ||
          type == DMTCP_GLOBAL_BARRIER_RESUME ||
          type == DMTCP_GLOBAL_BARRIER_RESTART;
      }

      const DmtcpBarrierType type;
      const string id;
      void (*callback)();
      const string pluginName;
  };

  static inline ostream& operator << (ostream& o, const BarrierInfo& info)
  {
    return o << info.toString();
  }

}

#endif
