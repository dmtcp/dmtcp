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
#define __USE_BSD
#include "jtimer.h"
#include <iostream>
#include <fstream>
#include "jassert.h"
#include "jfilesystem.h"

jalib::JTime::JTime()
{
  JASSERT ( gettimeofday ( &_value,NULL ) == 0 );
}

double jalib::operator- ( const jalib::JTime& a, const jalib::JTime& b )
{
  double sec = a._value.tv_sec - b._value.tv_sec;
  sec += ( a._value.tv_usec-b._value.tv_usec ) /1000000.0;
  if ( sec < 0 ) sec *= -1;
  return sec;
}

jalib::JTimeRecorder::JTimeRecorder ( const std::string& name )
    : _name ( name )
    , _isStarted ( false )
{}

namespace
{
  static const std::string& _testName()
  {
    static const char* env = getenv ( "TESTNAME" );
    static std::string tn = jalib::Filesystem::GetProgramName()
                            + jalib::XToString ( getpid() )
                            + ',' + std::string ( env == NULL ? "unamedtest" : env );
    return tn;
  }

  static void _writeTimerLogLine ( const std::string& name, double time )
  {
    static std::ofstream logfile ( "jtimings.csv", std::ios::out | std::ios::app );
    logfile << _testName() <<  ',' << name << ',' << time << std::endl;
    JASSERT_STDERR << "JTIMER(" <<  name << ") : " << time << '\n';
  }
}

void jalib::JTimeRecorder::recordTime ( double time )
{
  _writeTimerLogLine ( _name,time );
}
