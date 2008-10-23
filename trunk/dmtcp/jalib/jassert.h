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
#ifndef JASSERT_H
#define JASSERT_H


#include <iostream>
#include <sstream>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

/**  USAGE EXAMPLE:
 *
 * int a=1,b=2,c=3,d=4;
 *
 * code:
 *     JASSERT(a==b)(a)(b)(c).Text("Error a!=b program will exit");
 * outputs:
 *     ERROR at main.cpp:15 in main
 *     Reason: JASSERT(a=b) failed
 *       a = 1
 *       b = 2
 *       c = 3
 *     Message: Error a!=b program will exit
 *     Terminating...
 *
 *
 * code:
 *     JWARNING(a==b)(a)(b)(d).Text("Warning a!=b program will continue");
 * outputs:
 *     WARNING at main.cpp:15 in main
 *     Reason: JWARNING(a=b) failed
 *       a = 1
 *       b = 2
 *       d = 4
 *     Message: Warning a!=b program will continue
 *
 *
 * code:
 *     JNOTE("Values of abcd (in the form 'a=1') will be printed below this text.")(a)(b)(c)(d);
 * outputs:
 *     JNOTE at main.cpp:15 in main
 *     Reason: Values of abcd (in the form 'a=1') will be printed below this text.
 *       a = 1
 *       b = 2
 *       c = 3
 *       d = 4
 *
 *
 * It has the ability to output any variable understood by std::ostream.
 *
 */

namespace jassert_internal
{

  class JAssert
  {
    public:
      ///
      /// print a value of any type
      template < typename T > JAssert& Print ( const T& t );
      ///
      /// print out a string in format "Message: msg"
      JAssert& Text ( const char* msg );
      ///
      /// constructor: sets members
      JAssert ( bool exitWhenDone );
      ///
      /// destructor: exits program if exitWhenDone is set
      ~JAssert();
      ///
      /// termination point for crazy macros
      JAssert& JASSERT_CONT_A;
      ///
      /// termination point for crazy macros
      JAssert& JASSERT_CONT_B;

      template < typename T > JAssert& operator << ( const T& t )
      { Print ( t ); return *this; }
    private:
      ///
      /// if set true (on construction) call exit() on destruction
      bool _exitWhenDone;
  };


  const char* jassert_basename ( const char* str );
  std::ostream& jassert_output_stream();
  void jassert_safe_print ( const char* );

  template < typename T >
  inline JAssert& JAssert::Print ( const T& t )
  {
#ifdef JASSERT_FAST
    jassert_output_stream() << t;
#else
    std::ostringstream ss;
    ss << t;
    jassert_safe_print ( ss.str().c_str() );
#endif
    return *this;
  }

  void set_log_file ( const std::string& path );

  int jassert_console_fd();

}//jassert_internal

#define JASSERT_INIT() jassert_internal::jassert_safe_print("")

#define JASSERT_SET_LOGFILE(p) (jassert_internal::set_log_file(p));

#define JASSERT_ERRNO (strerror(errno))

#define JASSERT_PRINT(str) jassert_internal::JAssert(false).Print(str)
#define JASSERT_STDERR      jassert_internal::JAssert(false)
#define JASSERT_STDERR_FD   (jassert_internal::jassert_console_fd())

#define JASSERT_CONT(AB,term) Print("     " #term " = ").Print(term).Print("\n").JASSERT_CONT_##AB
#define JASSERT_CONT_A(term) JASSERT_CONT(B,term)
#define JASSERT_CONT_B(term) JASSERT_CONT(A,term)

#define JASSERT_STRINGIFY_(x) #x
#define JASSERT_STRINGIFY(x) JASSERT_STRINGIFY_(x)
#define JASSERT_FUNC __FUNCTION__
#define JASSERT_LINE JASSERT_STRINGIFY(__LINE__)
#define JASSERT_FILE jassert_internal::jassert_basename(__FILE__)
#define JASSERT_CONTEXT(type,reason) Print('[').Print(getpid()).Print("] " type " at ").Print(JASSERT_FILE).Print(":" JASSERT_LINE " in ").Print(JASSERT_FUNC).Print("; REASON='" reason "'\n")

#ifdef DEBUG
#define JTRACE(msg) jassert_internal::JAssert(false).JASSERT_CONTEXT("TRACE",msg).JASSERT_CONT_A
#else
#define JTRACE(msg) if(true){}else jassert_internal::JAssert(false).JASSERT_CONTEXT("NOTE",msg).JASSERT_CONT_A
#endif

#define JNOTE(msg) jassert_internal::JAssert(false).JASSERT_CONTEXT("NOTE",msg).JASSERT_CONT_A

#define JWARNING(term) if((term)){}else \
    jassert_internal::JAssert(false).JASSERT_CONTEXT("WARNING","JWARNING(" #term ") failed").JASSERT_CONT_A

#define JASSERT(term)  if((term)){}else \
    jassert_internal::JAssert(true).JASSERT_CONTEXT("ERROR","JASSERT(" #term ") failed").JASSERT_CONT_A

#endif
