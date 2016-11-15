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
struct UniquePid : private DmtcpUniqueProcessId {
  public:
    static UniquePid &ParentProcess();
    static UniquePid &ThisProcess(bool disableJTrace = false);
    UniquePid();

    UniquePid(const uint64_t &host,
              const pid_t &pd,
              const uint64_t &tm,
              const int &gen = 0)
    {
      _hostid = host;
      _pid = pd;
      _time = tm;
      _computation_generation = gen;
    }

    UniquePid(DmtcpUniqueProcessId id)
    {
      _hostid = id._hostid;
      _pid = id._pid;
      _time = id._time;
      _computation_generation = id._computation_generation;
    }

    uint64_t hostid() const { return _hostid; }

    pid_t pid() const { return _pid; }

    int computationGeneration() const { return _computation_generation; }

    uint64_t time() const { return _time; }

    DmtcpUniqueProcessId upid() const
    {
      DmtcpUniqueProcessId up;

      up._hostid = _hostid;
      up._pid = _pid;
      up._time = _time;
      up._computation_generation = _computation_generation;
      return up;
    }

    void incrementGeneration();

    static void serialize(jalib::JBinarySerializer &o);

    bool operator<(const UniquePid &that) const;
    bool operator==(const UniquePid &that) const;
    bool operator!=(const UniquePid &that) const { return !operator==(that); }

    static void restart();
    static void resetOnFork(const UniquePid &newId);

    string toString() const;

    bool isNull() const;
};

ostream&operator<<(ostream &o, const UniquePid &id);
ostream&operator<<(ostream &o, const DmtcpUniqueProcessId &id);
bool operator==(const DmtcpUniqueProcessId &a, const DmtcpUniqueProcessId &b);
bool operator!=(const DmtcpUniqueProcessId &a, const DmtcpUniqueProcessId &b);
}
#endif // ifndef UNIQUEPID_H
