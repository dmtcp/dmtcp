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

#ifndef JTIMER_H
#define JTIMER_H

#include <sys/time.h>
#include <time.h>

#include "dmtcpalloc.h"

#include "jalloc.h"
#include "jassert.h"
#include "jconvert.h"

#include "config.h"

#ifdef TIMING
# define JTIMER(name)                                            \
  static jalib::JTimeRecorder _jtimer_ ## name(# name, true)
# define JTIMER_NOPRINT(name)                                    \
  static jalib::JTimeRecorder _jtimer_ ## name(# name, false)
# define JTIMER_START(name) (_jtimer_ ## name.start())
# define JTIMER_STOP(name)  (_jtimer_ ## name.stop())
# define JTIMER_GETDELTA(var, name)  ((var) = _jtimer_ ## name.getDelta())
# define JTIMER_SCOPE(name)                                      \
  static jalib::JTimeRecorder _jtimer_scope_tm_ ## name(# name); \
  jalib::JScopeTimer _jtimer_scope_inst_ ## name(_jtimer_scope_tm_ ## name);
#else // ifdef TIMING
# define JTIMER(name)
# define JTIMER_NOPRINT(name)
# define JTIMER_START(name)
# define JTIMER_STOP(name)
# define JTIMER_GETDELTA(var, name)
# define JTIMER_SCOPE(name)
#endif // ifdef TIMING


namespace jalib
{
class JTime;
double operator-(const JTime &a, const JTime &b);

class JTime
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR
    JTime();
    friend double operator-(const JTime &a, const JTime &b);
    static JTime Now() { return JTime(); }

  private:
    struct timeval _value;
};

class JTimeRecorder
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR
    JTimeRecorder(const dmtcp::string &name, bool printToFile);
    void start()
    {
      JWARNING(!_isStarted) (_name);
      _start = JTime::Now();
      _end = JTime::Now();
      _isStarted = true;
    }

    void stop()
    {
      JWARNING(_isStarted) (_name);
      if (!_isStarted) { return; }
      _isStarted = false;
      _end = JTime::Now();
      if (_printToFile) {
        recordTime(_end - _start);
      }
    }

    /* The function returns the difference in seconds between
     * the call to start() and stop(). It returns 0.0 if it gets
     * called without calling stop().
     */
    double getDelta()
    {
      return _end - _start;
    }

  protected:
    void recordTime(double time);

  private:
    dmtcp::string _name;
    bool _isStarted;
    bool _printToFile;
    JTime _start;
    JTime _end;
};

class JScopeTimer
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR
    JScopeTimer(JTimeRecorder &tm) : _tm(tm) { _tm.start(); }

    ~JScopeTimer() { _tm.stop(); }

  private:
    JTimeRecorder &_tm;
};
}
#endif // ifndef JTIMER_H
