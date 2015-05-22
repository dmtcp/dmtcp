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

#include "dmtcp.h"
#include "protectedfds.h"
#include "util.h"
#include "syscallwrappers.h"
#include "../jalib/jalib.h"

using namespace dmtcp;

extern "C" void initializeJalib()
{
  jalib::JalibFuncPtrs jalibFuncPtrs;

#define INIT_JALIB_FPTR(name) jalibFuncPtrs.name = _real_ ## name;

  jalibFuncPtrs.writeAll = Util::writeAll;
  jalibFuncPtrs.readAll = Util::readAll;

  INIT_JALIB_FPTR(open);
  INIT_JALIB_FPTR(fopen);
  INIT_JALIB_FPTR(close);
  INIT_JALIB_FPTR(fclose);
  INIT_JALIB_FPTR(dup);
  INIT_JALIB_FPTR(dup2);
  INIT_JALIB_FPTR(readlink);

  INIT_JALIB_FPTR(syscall);
  INIT_JALIB_FPTR(mmap);
  INIT_JALIB_FPTR(munmap);

  INIT_JALIB_FPTR(read);
  INIT_JALIB_FPTR(write);
  INIT_JALIB_FPTR(select);

  INIT_JALIB_FPTR(socket);
  INIT_JALIB_FPTR(connect);
  INIT_JALIB_FPTR(bind);
  INIT_JALIB_FPTR(listen);
  INIT_JALIB_FPTR(accept);
  INIT_JALIB_FPTR(setsockopt);

  INIT_JALIB_FPTR(pthread_mutex_lock);
  INIT_JALIB_FPTR(pthread_mutex_trylock);
  INIT_JALIB_FPTR(pthread_mutex_unlock);

  jalib_init(jalibFuncPtrs,
             ELF_INTERPRETER,
             PROTECTED_STDERR_FD,
             PROTECTED_JASSERTLOG_FD,
             DMTCP_FAIL_RC);
}
