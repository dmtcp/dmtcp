/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel                                 *
 *   jansel@csail.mit.edu                                                   *
 *                                                                          *
 *   This file is part of the JALIB module of DMTCP (DMTCP:dmtcp/jalib).    *
 *                                                                          *
 *  DMTCP:dmtcp/jalib is free software: you can redistribute it and/or      *
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

jalib::JTimeRecorder::JTimeRecorder ( const jalib::string& name )
    : _name ( name )
    , _isStarted ( false )
{}

namespace
{
  static const jalib::string& _testName()
  {
    static const char* env = getenv ( "TESTNAME" );
    static jalib::string tn = jalib::Filesystem::GetProgramName()
                            + jalib::XToString ( getpid() )
                            + ',' + jalib::string ( env == NULL ? "unnamed test" : env );
    return tn;
  }

  static void _writeTimerLogLine ( const jalib::string& name, double time )
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
