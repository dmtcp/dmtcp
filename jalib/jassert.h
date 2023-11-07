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

#include <errno.h>
#include <execinfo.h> /* For backtrace() */
#include <iostream>
#include <sstream>
#include <string.h>
#include <string>
#include <unistd.h>
#define BT_SIZE 50 /* Maximum size backtrace of stack */

#include "dmtcpalloc.h"
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
constexpr const char *redEscapeStr = "\033[0;31m";
constexpr const char *greenEscapeStr = "\033[0;32m";
constexpr const char *yellowEscapeStr = "\033[0;33m";
constexpr const char *clearEscapeStr = "\033[0m";

class JAssert
{
  public:
  enum class JAssertType { Error, Warning, Note, Trace, Raw };

  dmtcp::string JAssertTypeToStr(JAssertType type)
  {
    switch (type) {
      case JAssertType::Error:
        return "Error";
      case JAssertType::Warning:
        return "Warning";
      case JAssertType::Note:
        return "Note";
      case JAssertType::Trace:
        return "Trace";
      case JAssertType::Raw:
        return "";
    }

    return "";
  };

  dmtcp::string JAssertTypeToColor(JAssertType type)
  {
    switch (type) {
      case JAssertType::Error:
        return redEscapeStr;
      case JAssertType::Warning:
        return yellowEscapeStr;
      default:
        return "";
    }

    return "";
  }

#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR

    ///
    /// print a value of any type
    template<typename T>
    JAssert &Print(const T &t);
    JAssert &Print(const char *t);
    template<typename T>
    JAssert &Print(const dmtcp::vector<T> &t);

    ///
    /// print out a string in format "Message: msg"
    JAssert &Text(const char *msg);

    JAssert(JAssertType type = JAssertType::Error);
    ~JAssert();

    ///
    /// termination point for crazy macros
    JAssert &JASSERT_CONT_A;

    ///
    /// termination point for crazy macros
    JAssert &JASSERT_CONT_B;

    template<typename T>
    JAssert&operator<<(const T &t)
    { Print(t); return *this; }

  private:
    void PrintProcMaps();
    void PrintBacktrace();

    void writeToConsole(const char *);
    void writeToLog(const char *);

    ///
    /// if set true (on construction) call exit() on destruction
    JAssertType _type;
    dmtcp::ostringstream ss;
};

class JTrace : public JAssert
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR

    JTrace() : JAssert(JAssertType::Trace) {}
};

class JNote : public JAssert
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR

    JNote() : JAssert(JAssertType::Note) {}
};

class JWarning : public JAssert
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR

    JWarning() : JAssert(JAssertType::Warning) {}
};

const char *jassert_basename(const char *str);
dmtcp::ostream &output_stream();
void jassert_init();
void close_stderr();

template<typename T>
inline JAssert&
JAssert::Print(const T &t)
{
  ss << t;
  return *this;
}

inline JAssert&
JAssert::Print(const char *t)
{
  if (t != NULL) {
    ss << t;
  }
  return *this;
}

template<typename T>
inline JAssert&
JAssert::Print(const dmtcp::vector<T> &t)
{
  for (size_t i = 0; i < t.size(); i++) {
    ss << t[i] << "\n";
  }
  return *this;
}

void set_log_file(const dmtcp::string &path);
void open_log_file();
}// jassert_internal

#define JASSERT_INIT(p) (jassert_internal::jassert_init());

#define JASSERT_SET_LOG(log) (jassert_internal::set_log_file(log));

#define JASSERT_CLOSE_STDERR() (jassert_internal::close_stderr());

#define JASSERT_ERRNO     (strerror(errno))

#define JASSERT_STDERR \
  jassert_internal::JAssert(jassert_internal::JAssert::JAssertType::Raw)
#define JASSERT_STDERR_FD (jassert_internal::jassert_console_fd())

#define JASSERT_CONT(AB, term)                                               \
                              Print("     " # term " = ").Print(term).Print( \
    "\n").JASSERT_CONT_ ## AB
#define JASSERT_CONT_A(term)  JASSERT_CONT(B, term)
#define JASSERT_CONT_B(term)  JASSERT_CONT(A, term)

#define JASSERT_STRINGIFY_(x) # x
#define JASSERT_STRINGIFY(x)  JASSERT_STRINGIFY_(x)
#define JASSERT_FUNC __FUNCTION__
#define JASSERT_LINE JASSERT_STRINGIFY(__LINE__)
#define JASSERT_FILE jassert_internal::jassert_basename(__FILE__)
#define JASSERT_CONTEXT(reason)                                                 \
  Print(" at ").Print(JASSERT_FILE).Print(":" JASSERT_LINE " in ").Print(  \
    JASSERT_FUNC).Print("; REASON='" reason "'\n")

#ifdef LOGGING
# define JTRACE(msg)                                              \
  jassert_internal::JTrace().JASSERT_CONTEXT(msg).JASSERT_CONT_A
#else // ifdef LOGGING
# define JTRACE(msg)                                              \
  if (true) {                                                     \
  } else jassert_internal::JTrace().JASSERT_CONTEXT(msg).JASSERT_CONT_A
#endif // ifdef LOGGING

#define JNOTE(msg)          \
  if (jassert_quiet >= 1) { \
  } else                    \
    jassert_internal::JNote().JASSERT_CONTEXT(msg).JASSERT_CONT_A

#define JWARNING(term)                                            \
  if ((term) || jassert_quiet >= 2) {                             \
  } else                                                          \
    jassert_internal::JWarning()                                  \
    .JASSERT_CONTEXT("JWARNING(" # term ") failed").JASSERT_CONT_A

#define JASSERT(term)                                             \
  if (term) {                                                     \
  } else                                                          \
    jassert_internal::JAssert()                                   \
    .JASSERT_CONTEXT("JASSERT(" # term ") failed").JASSERT_CONT_A

#define ASSERT_EQ(expected, term)                                 \
  if ((expected) == (term)) {                                     \
  } else                                                          \
    jassert_internal::JAssert()                                   \
    .JASSERT_CONTEXT("ASSERT_EQ failed; <" #expected "> == <"     \
                     #term ">.").JASSERT_CONT_A

#define ASSERT_NE(expected, term)                                           \
  if ((expected) != (term)) {                                               \
  } else                                                                    \
    jassert_internal::JAssert()                                             \
      .JASSERT_CONTEXT("ASSERT_NE failed; <" #expected "> != <" #term ">.") \
      .JASSERT_CONT_A

#define ASSERT_NULL(term)                                         \
  if (nullptr == (term)) {                                        \
  } else                                                          \
    jassert_internal::JAssert()                                   \
    .JASSERT_CONTEXT("ASSERT_NULL failed; <"#term ">.")           \
    .JASSERT_CONT_A

#define ASSERT_NOT_NULL(term)                                     \
  if (nullptr != (term)) {                                        \
  } else                                                          \
    jassert_internal::JAssert()                                   \
    .JASSERT_CONTEXT("ASSERT_NOT_NULL failed; <"#term ">.")       \
    .JASSERT_CONT_A

#endif // ifndef JASSERT_H
