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

#ifndef STLWRAPPER_H
#define STLWRAPPER_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef DMTCP
#include "dmtcpalloc.h"
#endif

#include <string>
#include <vector>


namespace jalib {
#ifdef DMTCP
  typedef dmtcp::string string;
  typedef dmtcp::ostringstream ostringstream;
  typedef dmtcp::vector<int> IntVector;
  typedef dmtcp::set<int> IntSet;
  typedef dmtcp::vector<string> StringVector;
  using dmtcp::vector;
#else
  typedef std::string string;
  typedef std::ostringstream ostringstream;
  typedef std::vector<int> IntVector;
  typedef std::set<int> IntSet;
  typedef std::vector<string> StringVector;
  using std::vector;
#endif
}

#endif 

