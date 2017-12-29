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
#include "jassert.h"
#include "jfilesystem.h"
#include "jtimer.h"
#include <fstream>
#include <iostream>

jalib::JTime::JTime()
{
  JASSERT(gettimeofday(&_value, NULL) == 0);
}

double
jalib::operator-(const jalib::JTime &a, const jalib::JTime &b)
{
  double sec = 0;
  struct timeval diff;

  timersub(&a._value, &b._value, &diff);
  sec = diff.tv_sec + (diff.tv_usec / 1000000.0);
  return sec;
}

jalib::JTimeRecorder::JTimeRecorder(const dmtcp::string &name, bool printToFile)
  : _name(name)
  , _isStarted(false)
  , _printToFile(printToFile)
{}

namespace
{
static const dmtcp::string&
_testName()
{
  static const char *env = getenv("TESTNAME");
  static dmtcp::string tn = jalib::Filesystem::GetProgramName()
    + jalib::XToString(getpid())
    + ',' + dmtcp::string(env == NULL ? "unamedtest" : env);

  return tn;
}

static void
_writeTimerLogLine(const dmtcp::string &name, double time)
{
  static std::ofstream logfile("jtimings.csv", std::ios::out | std::ios::app);

  logfile << _testName() << ',' << name << ',' << time << std::endl;
  JASSERT_STDERR << "JTIMER(" << name << ") : " << time << '\n';
}
}

void
jalib::JTimeRecorder::recordTime(double time)
{
  _writeTimerLogLine(_name, time);
}
