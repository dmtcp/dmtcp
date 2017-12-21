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

#ifndef JCONVERT_H
#define JCONVERT_H

#include <sstream>
#include <stdlib.h>
#include <string>

#include "dmtcpalloc.h"
#include "jassert.h"

namespace jalib
{
namespace jconvert_internal
{
// about 15x faster than sringstream method
template<typename T, T(strtoT) (const char *, char **, int)>
inline T
StdLibEC(const dmtcp::string &s, bool strict)
{
  const char *begin = s.c_str();
  char *end = 0;
  T v = (*strtoT)(begin, &end, 10);

  JASSERT(end != 0 && end != begin &&
          (!strict || *end == '\0')) (end) (begin) (strict)
  .Text("conversion failed");
  return v;
}

// about 15x faster than sringstream method
template<typename T, T(strtoT) (const char *, char **)>
inline T
StdLibEC(const dmtcp::string &s, bool strict)
{
  const char *begin = s.c_str();
  char *end = 0;
  T v = (*strtoT)(begin, &end);

  JASSERT(end != 0 && end != begin &&
          (!strict || *end == '\0')) (end) (begin) (strict)
  .Text("conversion failed");
  return v;
}
}

template<typename X>
inline dmtcp::string
XToString(const X &x)
{
  dmtcp::ostringstream tmp;

  tmp << x;
  return tmp.str();
}

template<typename X>
inline X StringToX(const dmtcp::string &s, bool strict = true);

// this is too slow
// {
// jalib::istringstream tmp(s);
// X x;
// tmp >> x;
// return x;
// }

template<>
inline dmtcp::string
StringToX<dmtcp::string>(const dmtcp::string &s, bool strict)
{
  return s;
}

#define JCONVERT_DECLARE_StringToX(T, TFunc, Function)              \
  template<>                                                        \
  inline T StringToX<T>(const dmtcp::string & s, bool strict) {     \
    return jconvert_internal::StdLibEC<TFunc, Function>(s, strict); \
  }

JCONVERT_DECLARE_StringToX(short, long, strtol);
JCONVERT_DECLARE_StringToX(int, long, strtol);
JCONVERT_DECLARE_StringToX(long, long, strtol);
JCONVERT_DECLARE_StringToX(unsigned int, unsigned long, strtoul);
JCONVERT_DECLARE_StringToX(unsigned long, unsigned long, strtoul);
JCONVERT_DECLARE_StringToX(long long, long long, strtoll);
JCONVERT_DECLARE_StringToX(unsigned long long, unsigned long long, strtoull);
JCONVERT_DECLARE_StringToX(float, float, strtof);
JCONVERT_DECLARE_StringToX(double, double, strtod);
JCONVERT_DECLARE_StringToX(long double, long double, strtold)


#undef JCONVERT_DECLARE_StringToX

#define StringToInt    StringToX < int >
#define StringToDouble StringToX < double >


template<typename T>
inline bool
Between(const T &a, const T &b, const T &c)
{
  return (a <= b) && (b <= c);
}
}// namespace jalib
#endif // ifndef JCONVERT_H
