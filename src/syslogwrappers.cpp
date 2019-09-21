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

#include <syslog.h>
#include <string>
#include "../jalib/jassert.h"
#include "dmtcpalloc.h"
#include "syscallwrappers.h"

namespace dmtcp
{
static bool _isSuspended = false;
static bool _syslogEnabled = false;
static bool _identIsNotNULL = false;
static int _option = -1;
static int _facility = -1;

static void SyslogCheckpointer_StopService();
static void SyslogCheckpointer_RestoreService();
static void SyslogCheckpointer_ResetOnFork();

static void
syslog_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_ATFORK_CHILD:
    SyslogCheckpointer_ResetOnFork();
    break;

  case DMTCP_EVENT_PRESUSPEND:
    break;

  case DMTCP_EVENT_PRECHECKPOINT:
    SyslogCheckpointer_StopService();
    break;

  case DMTCP_EVENT_RESUME:
    SyslogCheckpointer_RestoreService();
    break;

  case DMTCP_EVENT_RESTART:
    SyslogCheckpointer_RestoreService();
    break;

  default:
    break;
  }
}

static DmtcpPluginDescriptor_t syslogPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "syslog",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Syslog plugin",
  syslog_event_hook
};


DmtcpPluginDescriptor_t
dmtcp_Syslog_PluginDescr()
{
  return syslogPlugin;
}

static string&
_ident()
{
  static string t;

  return t;
}

void
SyslogCheckpointer_StopService()
{
  JASSERT(!_isSuspended);
  if (_syslogEnabled) {
    closelog();
    _isSuspended = true;
  }
}

void
SyslogCheckpointer_RestoreService()
{
  if (_isSuspended) {
    _isSuspended = false;
    JASSERT(_option >= 0 && _facility >= 0) (_option) (_facility);
    openlog((_identIsNotNULL ? _ident().c_str() : NULL),
            _option, _facility);
  }
}

void
SyslogCheckpointer_ResetOnFork()
{
  _syslogEnabled = false;
}

extern "C" void
openlog(const char *ident, int option, int facility)
{
  JASSERT(!_isSuspended);
  JTRACE("openlog") (ident);
  _real_openlog(ident, option, facility);
  _syslogEnabled = true;

  _identIsNotNULL = (ident != NULL);
  if (ident != NULL) {
    _ident() = ident;
  }
  _option = option;
  _facility = facility;
}

extern "C" void
closelog(void)
{
  JASSERT(!_isSuspended);
  JTRACE("closelog");
  _real_closelog();
  _syslogEnabled = false;
}

// FIXME:  Need to add wrappers for vsyslog() and setlogmask()
// NOTE:  openlog() is optional.  Its purpose is primarily to set default
// parameters.  If syslog() or vsyslog() is called without it,
// it will still open the log.  Hence, we need a wrapper for them
// that will set _syslogEnabled = true.
// NOTE:  We also need to save and restore the mask of setlogmask()
// NOTE:  Need a test/syslog.c to test this code.  How can we verify that
// it continues to log on restart in an automatic fashion?
}
