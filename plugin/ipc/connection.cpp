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

#include <fcntl.h>
#include "connection.h"
#include "../jalib/jassert.h"
#include "../jalib/jserialize.h"

dmtcp::Connection::Connection(uint32_t t)
  : _id(ConnectionIdentifier::Create())
  , _type((ConnectionType) t)
  , _fcntlFlags(-1)
  , _fcntlOwner(-1)
  , _fcntlSignal(-1)
  , _hasLock(false)
{}

void dmtcp::Connection::addFd(int fd)
{
  _fds.push_back(fd);
}

void dmtcp::Connection::removeFd(int fd)
{
  JASSERT(_fds.size() > 0);
  if (_fds.size() == 1) {
    JASSERT(_fds[0] == fd);
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

void dmtcp::Connection::saveOptions()
{
  errno = 0;
  _fcntlFlags = fcntl(_fds[0],F_GETFL);
  JASSERT(_fcntlFlags >= 0) (_fds[0]) (_fcntlFlags) (_type) (JASSERT_ERRNO);
  errno = 0;
  _fcntlOwner = fcntl(_fds[0],F_GETOWN);
  JASSERT(_fcntlOwner != -1) (_fcntlOwner) (JASSERT_ERRNO);
  errno = 0;
  _fcntlSignal = fcntl(_fds[0],F_GETSIG);
  JASSERT(_fcntlSignal >= 0) (_fcntlSignal) (JASSERT_ERRNO);
}

void dmtcp::Connection::restoreOptions()
{
  //restore F_GETFL flags
  JASSERT(_fcntlFlags >= 0) (_fcntlFlags);
  JASSERT(_fcntlOwner != -1) (_fcntlOwner);
  JASSERT(_fcntlSignal >= 0) (_fcntlSignal);
  errno = 0;
  JASSERT(fcntl(_fds[0], F_SETFL, (int)_fcntlFlags) == 0)
    (_fds[0]) (_fcntlFlags) (JASSERT_ERRNO);

  errno = 0;
  JASSERT(fcntl(_fds[0], F_SETOWN, (int)_fcntlOwner) == 0)
   (_fds[0]) (_fcntlOwner) (JASSERT_ERRNO);

  // This JASSERT will almost always trigger until we fix the above mentioned
  // bug.
  //JASSERT(fcntl(_fds[0], F_GETOWN) == _fcntlOwner)
  //(fcntl(_fds[0], F_GETOWN)) (_fcntlOwner) (VIRTUAL_TO_REAL_PID(_fcntlOwner));

  errno = 0;
  JASSERT(fcntl(_fds[0], F_SETSIG, (int)_fcntlSignal) == 0)
    (_fds[0]) (_fcntlSignal) (JASSERT_ERRNO);
}

void dmtcp::Connection::doLocking()
{
  errno = 0;
  _hasLock = false;
  JASSERT(fcntl(_fds[0], F_SETOWN, getpid()) == 0)
   (_fds[0]) (JASSERT_ERRNO);
}

void dmtcp::Connection::checkLock()
{
  pid_t pid = fcntl(_fds[0], F_GETOWN);
  JASSERT(pid != -1);
  _hasLock = pid == getpid();
}

void dmtcp::Connection::serialize(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("dmtcp::Connection");
  o & _id & _type & _fcntlFlags & _fcntlOwner & _fcntlSignal;
  o.serializeVector(_fds);
  serializeSubClass(o);
}
