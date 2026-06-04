/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include "connection.h"
#include <fcntl.h>
#include "../jalib/jserialize.h"

#include "plugin/pid/pidhelpers.h"
#include "syscallwrappers.h"
#include "util.h"
#include "util_assert.h"

using namespace dmtcp;

Connection::Connection(uint32_t t)
  : _id(ConnectionIdentifier::create())
  , _type(t)
  , _fcntlFlags(-1)
  , _fcntlOwner(-1)
  , _fcntlSignal(-1)
  , _hasLock(false)
{}

void
Connection::addFd(int fd)
{
  _fds.push_back(fd);
}

void
Connection::removeFd(int fd)
{
  ASSERT(_fds.size() > 0, "connection has no fds during remove: fd={}", fd);
  if (_fds.size() == 1) {
    ASSERT(_fds[0] == fd,
           "removing wrong sole connection fd: expected={} actual={}",
           _fds[0], fd);
    _fds.clear();
  } else {
    for (size_t i = 0; i < _fds.size(); i++) {
      if (_fds[i] == fd) {
        _fds.erase(_fds.begin() + i);
        break;
      }
    }
  }
}

void
Connection::restoreDupFds(int fd)
{
  Util::changeFd(fd, _fds[0]);
  for (size_t i = 1; i < _fds.size(); i++) {
    ASSERT_ERRNO(_real_dup2(_fds[0], _fds[i]) == _fds[i],
                 "dup2 failed while restoring shared fd: old_fd={} new_fd={}",
                 _fds[0], _fds[i]);
  }
}

void
Connection::saveOptions()
{
  errno = 0;
  _fcntlFlags = fcntl(_fds[0], F_GETFL);
  ASSERT_ERRNO(_fcntlFlags >= 0,
               "fcntl(F_GETFL) failed: fd={} flags={} type={}", _fds[0],
               _fcntlFlags, _type);
  errno = 0;
  _fcntlOwner = fcntl(_fds[0], F_GETOWN);
  ASSERT_ERRNO(_fcntlOwner != -1,
               "fcntl(F_GETOWN) failed: fd={} flags={} type={} owner={}",
               _fds[0], _fcntlFlags, _type, _fcntlOwner);
  errno = 0;
  _fcntlSignal = fcntl(_fds[0], F_GETSIG);
  ASSERT_ERRNO(_fcntlSignal >= 0,
               "fcntl(F_GETSIG) failed: fd={} signal={}", _fds[0],
               _fcntlSignal);
}

void
Connection::restoreOptions()
{
  // restore F_GETFL flags
  ASSERT(_fcntlFlags >= 0, "invalid saved fcntl flags: flags={}",
         _fcntlFlags);
  ASSERT(_fcntlOwner != -1, "invalid saved fcntl owner: owner={}",
         _fcntlOwner);
  ASSERT(_fcntlSignal >= 0, "invalid saved fcntl signal: signal={}",
         _fcntlSignal);
  errno = 0;
  ASSERT_ERRNO(fcntl(_fds[0], F_SETFL, (int)_fcntlFlags) == 0,
               "fcntl(F_SETFL) failed: fd={} flags={}", _fds[0],
               _fcntlFlags);

  errno = 0;
  // Check to see if the owner is alive; if so, try to restore fd ownership.
  if (kill(_fcntlOwner, 0) == 0) {
    ASSERT_ERRNO(fcntl(_fds[0], F_SETOWN, (int)_fcntlOwner) == 0,
                 "fcntl(F_SETOWN) failed: fd={} owner={}", _fds[0],
                 _fcntlOwner);
  }

  // FIXME:  The comment below seems to be obsolete now.
  // This ASSERT would almost always trigger until we fix the above mentioned
  // bug.
  // ASSERT(fcntl(_fds[0], F_GETOWN) == _fcntlOwner,
  //        "fd owner mismatch");

  errno = 0;
  ASSERT_ERRNO(fcntl(_fds[0], F_SETSIG, (int)_fcntlSignal) == 0,
               "fcntl(F_SETSIG) failed: fd={} signal={}", _fds[0],
               _fcntlSignal);
}

void
Connection::doLocking()
{
  pid_t realPid = dmtcp_pid_virtual_to_real(getpid());

  errno = 0;
  _hasLock = false;
  ASSERT_ERRNO(_real_fcntl(_fds[0], F_SETOWN, realPid) == 0,
               "fcntl(F_SETOWN) failed during leader election: fd={} "
               "real_pid={}",
               _fds[0], realPid);
}

void
Connection::checkLocking()
{
  pid_t pid = _real_fcntl(_fds[0], F_GETOWN);
  pid_t realPid = dmtcp_pid_virtual_to_real(getpid());

  ASSERT_ERRNO(pid != -1,
               "fcntl(F_GETOWN) failed during leader check: fd={}", _fds[0]);
  _hasLock = pid == realPid;
}

void
Connection::serialize(jalib::JBinarySerializer &o)
{
  JSERIALIZE_ASSERT_POINT("Connection");
  o&_id&_type&_fcntlFlags&_fcntlOwner &_fcntlSignal & _fds;
  serializeSubClass(o);
}
