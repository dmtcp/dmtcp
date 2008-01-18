/***************************************************************************
 *   Copyright (C) 2006 by Jason Ansel                                     *
 *   jansel@ccs.neu.edu                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "syslogcheckpointer.h"
#include "syscallwrappers.h"
#include <string>
#include "jassert.h"
#include <syslog.h>

namespace
{
  bool         _isSuspended = false;
  bool         _syslogEnabled = false;
  std::string& _ident() {static std::string t; return t;}
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


