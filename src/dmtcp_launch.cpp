/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include "launch.h"
#include "../jalib/jassert.h"

using namespace dmtcp;

int
main(int argc, const char **argv)
{
  Launch::initializeLaunch(&argc, &argv);

  if (argc > 0) {
    JTRACE("dmtcp_launch starting new program:")(argv[0]);
  }

  Launch::validateLaunchEnvironment(argc, argv);

  const char **newArgv = Launch::patchArgvForSetuid(argc, argv);

  Launch::setLDPreloadLibs(argc, newArgv);

  execvp(newArgv[0], (char* const*) newArgv);

  // should be unreachable
  JASSERT_STDERR <<
    "ERROR: Failed to exec(\"" << argv[0] << "\"): " << JASSERT_ERRNO << "\n"
                 << "Perhaps it is not in your $PATH?\n"
                 << "See `dmtcp_launch --help` for usage.\n";

  return -1;
}
