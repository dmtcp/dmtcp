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

using namespace dmtcp;

inline static long
theUniqueHostId()
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

static UniquePid&
nullProcess()
{
  static char buf[sizeof(UniquePid)];
  static UniquePid *t = NULL;

  if (t == NULL) {
    t = new (buf)UniquePid(0, 0, 0);
  }
  return *t;
}

static UniquePid&
theProcess()
{
  static char buf[sizeof(UniquePid)];
  static UniquePid *t = NULL;

  if (t == NULL) {
    t = new (buf)UniquePid(0, 0, 0);
  }
  return *t;
}

static UniquePid&
parentProcess()
{
  static char buf[sizeof(UniquePid)];
  static UniquePid *t = NULL;

  if (t == NULL) {
    t = new (buf)UniquePid(0, 0, 0);
  }
  return *t;
}

// _computation_generation field of return value may later have to be modified.
// So, it can't return a const UniquePid
UniquePid&
UniquePid::ThisProcess(bool disableJTrace /*=false*/)
{
  struct timespec value;
  uint64_t nsecs = 0;

  if (theProcess() == nullProcess()) {
    JASSERT(clock_gettime(CLOCK_MONOTONIC, &value) == 0);
    nsecs = value.tv_sec * 1000000000L + value.tv_nsec;
    theProcess() = UniquePid(theUniqueHostId(),
                             ::getpid(),
                             nsecs);
    if (disableJTrace == false) {
      JTRACE("recalculated process UniquePid...") (theProcess());
    }
  }

  return theProcess();
}

UniquePid&
UniquePid::ParentProcess()
{
  return parentProcess();
}

/*!
    \fn UniquePid::UniquePid()
 */
UniquePid::UniquePid()
{
  _pid = 0;
  _hostid = 0;
  memset(&_time, 0, sizeof(_time));
}

// This is called only by the DMTCP coordinator.
void
UniquePid::incrementGeneration()
{
  _computation_generation++;
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
  TRY_LEQ(_hostid);
  TRY_LEQ(_pid);
  TRY_LEQ(_time);
  return false;
}

bool
UniquePid::operator==(const UniquePid &that) const
{
  return _hostid == that.hostid()
         && _pid == that.pid()
         && _time == that.time();
}

ostream&
dmtcp::operator<<(dmtcp::ostream &o, const UniquePid &id)
{
  o << std::hex << id.hostid() << '-' << std::dec << id.pid() << '-' <<
  std::hex << id.time() << std::dec;
  return o;
}

ostream&
dmtcp::operator<<(dmtcp::ostream &o, const DmtcpUniqueProcessId &id)
{
  o << std::hex << id._hostid << '-' << std::dec << id._pid << '-' <<
  std::hex << id._time << std::dec;
  return o;
}

bool
dmtcp::operator==(const DmtcpUniqueProcessId &a, const DmtcpUniqueProcessId &b)
{
  return a._hostid == b._hostid &&
         a._pid == b._pid &&
         a._time == b._time &&
         a._computation_generation == b._computation_generation;
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
UniquePid::resetOnFork(const UniquePid &newId)
{
  // parentProcess() is for inspection tools
  parentProcess() = ThisProcess();
  JTRACE("Explicitly setting process UniquePid") (newId);
  theProcess() = newId;
}

bool
UniquePid::isNull() const
{
  return *this == nullProcess();
}

void
UniquePid::serialize(jalib::JBinarySerializer &o)
{
  // NOTE: Do not put JTRACE/JNOTE/JASSERT in here
  UniquePid theCurrentProcess, theParentProcess;

  if (o.isWriter()) {
    theCurrentProcess = ThisProcess();
    theParentProcess = ParentProcess();
  }

  o&theCurrentProcess &theParentProcess;

  if (o.isReader()) {
    theProcess() = theCurrentProcess;
    parentProcess() = theParentProcess;
  }
}
