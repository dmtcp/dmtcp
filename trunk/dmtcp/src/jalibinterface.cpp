/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
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

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/prctl.h>

#include "dmtcpmodule.h"
#include "protectedfds.h"
#include "util.h"
#include "syscallwrappers.h"
#include "../jalib/jalib.h"

extern "C" void initializeJalib()
{
  jalib::JalibFuncPtrs jalibFuncPtrs;

#define INIT_JALIB_FPTR(name) jalibFuncPtrs.name = _real_ ## name;

  jalibFuncPtrs.dmtcp_get_tmpdir = dmtcp_get_tmpdir;
  jalibFuncPtrs.dmtcp_get_uniquepid_str = dmtcp_get_uniquepid_str;
  jalibFuncPtrs.writeAll = dmtcp::Util::writeAll;
  jalibFuncPtrs.readAll = dmtcp::Util::readAll;

  INIT_JALIB_FPTR(open);
  INIT_JALIB_FPTR(fopen);
  INIT_JALIB_FPTR(close);
  INIT_JALIB_FPTR(fclose);

  INIT_JALIB_FPTR(syscall);

  INIT_JALIB_FPTR(read);
  INIT_JALIB_FPTR(write);
  INIT_JALIB_FPTR(select);

  INIT_JALIB_FPTR(socket);
  INIT_JALIB_FPTR(connect);
  INIT_JALIB_FPTR(bind);
  INIT_JALIB_FPTR(listen);
  INIT_JALIB_FPTR(accept);

  INIT_JALIB_FPTR(pthread_mutex_lock);
  INIT_JALIB_FPTR(pthread_mutex_trylock);
  INIT_JALIB_FPTR(pthread_mutex_unlock);

  jalib_init(jalibFuncPtrs, PROTECTED_STDERR_FD, PROTECTED_JASSERTLOG_FD);
}
