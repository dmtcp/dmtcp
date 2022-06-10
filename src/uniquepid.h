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

#pragma once
#ifndef UNIQUEPID_H
#define UNIQUEPID_H

#include <sys/types.h>
#include "../jalib/jserialize.h"
#include "dmtcp.h"


namespace dmtcp
{
struct UniquePid : public DmtcpUniqueProcessId {
  public:
    static UniquePid const& ParentProcess();
    static UniquePid const& ThisProcess();
    static uint64_t HostId();
    static uint64_t Timestamp();

    UniquePid() = default;

    UniquePid(const uint64_t &host,
              const pid_t &pd,
              const uint64_t &tm,
              const int &gen = 0)
    {
      hostid = host;
      pid = pd;
      time = tm;
      computation_generation = gen;
    }

    UniquePid(DmtcpUniqueProcessId id)
    {
      hostid = id.hostid;
      pid = id.pid;
      time = id.time;
      computation_generation = id.computation_generation;
    }

    void resetOnFork()
    {
      pid = getpid();
      time = Timestamp();
    }

    DmtcpUniqueProcessId upid() const
    {
      DmtcpUniqueProcessId up;

      up.hostid = hostid;
      up.pid = pid;
      up.time = time;
      up.computation_generation = computation_generation;
      return up;
    }

    void incrementGeneration();

    void serialize(jalib::JBinarySerializer &o);

    bool operator<(const UniquePid &that) const;
    bool operator==(const UniquePid &that) const;
    bool operator!=(const UniquePid &that) const { return !operator==(that); }

    string toString() const;

    bool isNull() const;

    static DmtcpPluginDescriptor_t pluginDescr();
};

ostream&operator<<(ostream &o, const UniquePid &id);
ostream&operator<<(ostream &o, const DmtcpUniqueProcessId &id);
bool operator==(const DmtcpUniqueProcessId &a, const DmtcpUniqueProcessId &b);
bool operator!=(const DmtcpUniqueProcessId &a, const DmtcpUniqueProcessId &b);
}
#endif // ifndef UNIQUEPID_H
