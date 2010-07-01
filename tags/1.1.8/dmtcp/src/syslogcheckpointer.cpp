/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include "syslogcheckpointer.h"
#include "syscallwrappers.h"
#include <string>
#include  "../jalib/jassert.h"
#include <syslog.h>

namespace
{
  bool         _isSuspended = false;
  bool         _syslogEnabled = false;
  dmtcp::string& _ident() {static dmtcp::string t; return t;}
  int          _option = -1;
  int          _facility = -1;
}

void dmtcp::SyslogCheckpointer::stopService()
{
  JASSERT ( !_isSuspended );
  if ( _syslogEnabled )
  {
    closelog();
    _isSuspended = true;
  }
}

void dmtcp::SyslogCheckpointer::restoreService()
{
  if ( _isSuspended )
  {
    _isSuspended = false;
    JASSERT ( _option>=0 && _facility>=0 ) ( _option ) ( _facility );
    openlog ( _ident().c_str(),_option,_facility );
  }
}

void dmtcp::SyslogCheckpointer::resetOnFork()
{
  _syslogEnabled = false;
}

extern "C" void openlog ( const char *ident, int option, int facility )
{
  JASSERT ( ident != NULL );
  JASSERT ( !_isSuspended );
  JTRACE ( "openlog" ) ( ident );
  _real_openlog ( ident, option, facility );
  _syslogEnabled = true;

  _ident() = ident;
  _option = option;
  _facility = facility;
}

extern "C" void closelog ( void )
{
  JASSERT ( !_isSuspended );
  JTRACE ( "closelog" );
  _real_closelog();
  _syslogEnabled = false;
}


