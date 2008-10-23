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
#ifndef JTIMER_H
#define JTIMER_H

#include <sys/time.h>
#include <time.h>

#include "jconvert.h"
#include "jassert.h"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef TIMING
#define JTIMER(name) static jalib::JTimeRecorder _jtimer_ ## name (#name);
#define JTIMER_START(name) ( _jtimer_ ## name . start() )
#define JTIMER_STOP(name) ( _jtimer_ ## name . stop() )
#define JTIMER_SCOPE(name) static jalib::JTimeRecorder _jtimer_scope_tm_ ## name (#name); \
       jalib::JScopeTimer _jtimer_scope_inst_ ## name (_jtimer_scope_tm_ ## name);
#else
#define JTIMER(name)
#define JTIMER_START(name)
#define JTIMER_STOP(name)
#define JTIMER_SCOPE(name)
#endif


namespace jalib
{

  class JTime;
  double operator- ( const JTime& a, const JTime& b );

  class JTime
  {
    public:
      JTime();
      friend double operator- ( const JTime& a, const JTime& b );
      static JTime Now() {return JTime();}
    private:
      struct timeval _value;
  };

  class JTimeRecorder
  {
    public:
      JTimeRecorder ( const std::string& name );
      void start()
      {
        JWARNING ( !_isStarted ) ( _name );
        _start = JTime::Now();
        _isStarted = true;
      }
      void stop()
      {
        JWARNING ( _isStarted ) ( _name );
        if ( !_isStarted ) return;
        _isStarted = false;
        recordTime ( JTime::Now() - _start );
      }
    protected:
      void recordTime ( double time );
    private:
      std::string _name;
      bool  _isStarted;
      JTime _start;
  };

  class JScopeTimer
  {
    public:
      JScopeTimer ( JTimeRecorder& tm ) :_tm ( tm ) { _tm.start(); }
      ~JScopeTimer() { _tm.stop(); }
    private:
      JTimeRecorder& _tm;
  };

}
#endif
