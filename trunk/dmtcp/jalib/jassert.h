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

#ifndef JASSERT_H
#define JASSERT_H


#include "stlwrapper.h"
#include <string>
#include <iostream>
#include <sstream>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <execinfo.h> /* For backtrace() */
#define BT_SIZE 50 /* Maximum size backtrace of stack */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "jalloc.h"

extern int jassert_quiet;

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
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      ///
      /// print a value of any type
      template < typename T > JAssert& Print ( const T& t );
      JAssert& Print ( const char *t );
      template < typename T > JAssert& Print ( const dmtcp::vector<T>& t );
      ///
      /// print out a string in format "Message: msg"
      JAssert& Text ( const char* msg );
      ///
      /// prints stack backtrace and always returns true
      JAssert& jbacktrace ();
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
      bool _logLockAcquired;
      dmtcp::ostringstream ss;
  };


  const char* jassert_basename ( const char* str );
  dmtcp::ostream& jassert_output_stream();
  void jassert_safe_print ( const char* );
  void jassert_init ( const jalib::string& f );
  void close_stderr();
  bool lockLog();
  void unlockLog();

  template < typename T >
  inline JAssert& JAssert::Print ( const T& t )
  {
#ifdef JASSERT_FAST
    jassert_output_stream() << t;
#else
    ss << t;
#endif
    return *this;
  }

  inline JAssert& JAssert::Print ( const char*t )
  {
    if (t != NULL) {
#ifdef JASSERT_FAST
      jassert_output_stream() << *t;
#else
      ss << t;
#endif
    }
    return *this;
  }

  template < typename T >
  inline JAssert& JAssert::Print ( const dmtcp::vector<T>& t )
  {
    for (size_t i = 0; i < t.size(); i++) {
      ss << t[i] << "\n";
    }
    return *this;
  }

  void set_log_file ( const jalib::string& path );
  void reset_on_fork ( );

  void jassert_set_console_fd(int fd);

}//jassert_internal

#define JASSERT_INIT(p) (jassert_internal::jassert_init(p));
#define JASSERT_CLOSE_STDERR() (jassert_internal::close_stderr());

#define JASSERT_RESET_ON_FORK() (jassert_internal::reset_on_fork());

#define JASSERT_CKPT_LOCK() (jassert_internal::lockLog());
#define JASSERT_CKPT_UNLOCK() (jassert_internal::unlockLog());

#define JASSERT_ERRNO (strerror(errno))

#define JASSERT_SET_CONSOLE_FD(fd) \
  jassert_internal::jassert_set_console_fd(fd)

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

#ifdef QUIET
#define JNOTE(msg) if(true){}else jassert_internal::JAssert(false).JASSERT_CONTEXT("NOTE",msg).JASSERT_CONT_A
#else
#define JNOTE(msg) if(jassert_quiet >= 1){}else \
    jassert_internal::JAssert(false).JASSERT_CONTEXT("NOTE",msg).JASSERT_CONT_A
#endif

#ifdef QUIET
#define JWARNING(term) if(true){}else \
    jassert_internal::JAssert(false).JASSERT_CONTEXT("WARNING","JWARNING(" #term ") failed").JASSERT_CONT_A
#else
#define JWARNING(term) if((term) || jassert_quiet >= 2){}else \
    jassert_internal::JAssert(false).JASSERT_CONTEXT("WARNING","JWARNING(" #term ") failed").JASSERT_CONT_A
#endif

#ifndef DEBUG
# define JASSERT(term)  if((term)){}else \
    jassert_internal::JAssert(true) \
	.JASSERT_CONTEXT("ERROR","JASSERT(" #term ") failed").JASSERT_CONT_A
#else
# define JASSERT(term) \
    if ((term)) {} else \
      jassert_internal::JAssert(true) \
        .JASSERT_CONTEXT("ERROR","JASSERT(" #term ") failed").JASSERT_CONT_A
#endif

#define JALIB_CKPT_LOCK() do{\
  JASSERT_CKPT_LOCK();\
  JALLOC_HELPER_LOCK();\
} while(0)

#define JALIB_CKPT_UNLOCK() do{\
  JALLOC_HELPER_UNLOCK();\
  JASSERT_CKPT_UNLOCK();\
} while(0)

#define JALIB_RESET_ON_FORK() do{\
  JASSERT_RESET_ON_FORK();\
  JALLOC_HELPER_RESET_ON_FORK();\
} while(0)

#endif
