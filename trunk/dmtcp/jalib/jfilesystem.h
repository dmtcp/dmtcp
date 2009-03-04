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

#ifndef JALIBJFILESYSTEM_H
#define JALIBJFILESYSTEM_H

#include <string>
#include <vector>
#include "stlwrapper.h"

namespace jalib
{

  namespace Filesystem
  {

    //true if a given file exists
    bool FileExists ( const jalib::string& str );

    //search for a given utility in many different places
    jalib::string FindHelperUtility ( const jalib::string& name, bool dieOnError = true );

    jalib::string GetProgramDir();
    jalib::string GetProgramName();
    jalib::string GetProgramPath();

    jalib::string ResolveSymlink ( const jalib::string& file );

    StringVector GetProgramArgs();

    IntVector ListOpenFds();

    jalib::string GetCurrentTty();

    jalib::string GetCurrentHostname();

  }

}

#endif
