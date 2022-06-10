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

#include "uniquepid.h"
#include <stdlib.h>
#include <time.h>
#include "../jalib/jconvert.h"
#include "../jalib/jfilesystem.h"
#include "../jalib/jserialize.h"
#include "constants.h"
#include "protectedfds.h"
#include "shareddata.h"
#include "syscallwrappers.h"
#include "processinfo.h"

using namespace dmtcp;

uint64_t
UniquePid::HostId()
{
#ifdef USE_GETHOSTID
  return ::gethostid()
#else // ifdef USE_GETHOSTID

  // gethostid() calls socket() on some systems, which we don't want
  char buf[512];

  JASSERT(::gethostname(buf, sizeof(buf)) == 0)(JASSERT_ERRNO);

  // so return a bad hash of our hostname
  long h = 0;
  for (char *i = buf; *i != '\0'; ++i) {
    h = (*i) + (331 * h);
  }

  // make it positive for good measure
  return h > 0 ? h : -1 * h;
#endif // ifdef USE_GETHOSTID
}

uint64_t
UniquePid::Timestamp()
{
  struct timespec value;

  JASSERT(clock_gettime(CLOCK_MONOTONIC, &value) == 0);
  return (uint64_t) value.tv_sec * 1000000000L + value.tv_nsec;
}

// computation_generation field of return value may later have to be modified.
// So, it can't return a const UniquePid
UniquePid const&
UniquePid::ThisProcess()
{
  if (ProcessInfo::instance().upid().isNull()) {
    ProcessInfo::instance().setUpid(
      UniquePid(HostId(), getpid(), Timestamp()));
  }

  return ProcessInfo::instance().upid();
}

UniquePid const&
UniquePid::ParentProcess()
{
  return ProcessInfo::instance().uppid();
}

// This is called only by the DMTCP coordinator.
void
UniquePid::incrementGeneration()
{
  computation_generation++;
}

/*!
    \fn UniquePid::operator<() const
 */
bool
UniquePid::operator<(const UniquePid &that) const
{
#define TRY_LEQ(param)               \
  if (this->param != that.param) {   \
    return this->param < that.param; \
  }
  TRY_LEQ(hostid);
  TRY_LEQ(pid);
  TRY_LEQ(time);
  return false;
}

bool
UniquePid::operator==(const UniquePid &that) const
{
  return hostid == that.hostid
         && pid == that.pid
         && time == that.time;
}

ostream&
dmtcp::operator<<(dmtcp::ostream &o, const UniquePid &id)
{
  o << std::hex << id.hostid << '-' << std::dec << id.pid << '-' <<
  std::hex << id.time << std::dec;
  return o;
}

ostream&
dmtcp::operator<<(dmtcp::ostream &o, const DmtcpUniqueProcessId &id)
{
  o << std::hex << id.hostid << '-' << std::dec << id.pid << '-' <<
  std::hex << id.time << std::dec;
  return o;
}

bool
dmtcp::operator==(const DmtcpUniqueProcessId &a, const DmtcpUniqueProcessId &b)
{
  return a.hostid == b.hostid &&
         a.pid == b.pid &&
         a.time == b.time &&
         a.computation_generation == b.computation_generation;
}

bool
dmtcp::operator!=(const DmtcpUniqueProcessId &a, const DmtcpUniqueProcessId &b)
{
  return !(a == b);
}

string
UniquePid::toString() const
{
  ostringstream o;

  o << *this;
  return o.str();
}

void
UniquePid::serialize(jalib::JBinarySerializer &o)
{
  o & hostid & pid & time & computation_generation;
}

bool
UniquePid::isNull() const
{
  return !hostid && !pid && !time && !computation_generation;
}
