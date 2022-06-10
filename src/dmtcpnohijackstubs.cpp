/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include "../jalib/jassert.h"
#include "uniquepid.h"

#undef dmtcp_is_enabled
#undef dmtcp_checkpoint
#undef dmtcp_disable_ckpt
#undef dmtcp_enable_ckpt
#undef dmtcp_get_coordinator_status
#undef dmtcp_get_local_status
#undef dmtcp_get_uniquepid_str
#undef dmtcp_get_ckpt_filename

// dmtcp_launch, and dmtcp_coordinator, and dmtcp_command do not
// need to load dmtcpworker.cpp
// libdmtcpinternal.a contains code needed by dmtcpworker and the utilities
// alike.
// libnohijack.a contains stub functions (mostly empty definitions
// corresponding to definitions in libdmtcp.so.  It includes
// nosyscallsreal.c and this file (dmtcpworkerstubs.cpp).
// libdmtcp.so and libsyscallsreal.a contain the wrappers and other code
// that executes within the end user process

// libdmtcp.so defines this differently

using namespace dmtcp;

void
_dmtcp_setup_trampolines() {}

int
dmtcp_get_ckpt_signal()
{
  JASSERT(false).Text("NOT REACHED");
  return -1;
}

const char *
dmtcp_get_tmpdir()
{
  JASSERT(false).Text("NOT REACHED");
  return NULL;
}

const char *
dmtcp_get_uniquepid_str()
{
  static string uniquepid_str;

  uniquepid_str = UniquePid::ThisProcess(true).toString();
  return uniquepid_str.c_str();
}

DmtcpUniqueProcessId
dmtcp_get_uniquepid()
{
  return UniquePid::ThisProcess(true).upid();
}

DmtcpUniqueProcessId
dmtcp_get_computation_id()
{
  DmtcpUniqueProcessId id = { 0, 0, 0, 0 };

  return id;
}

int
dmtcp_is_running_state()
{
  JASSERT(false);
  return 0;
}

int
dmtcp_is_protected_fd(int fd)
{
  JASSERT(false);
  return 0;
}

int
dmtcp_no_coordinator()
{
  JASSERT(false);
  return 0;
}
