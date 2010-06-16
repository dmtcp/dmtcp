/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef DMTCPPROTECTEDFDS_H
#define DMTCPPROTECTEDFDS_H


#include "constants.h"
#include "../jalib/jalloc.h"

#define PROTECTEDFDS (dmtcp::ProtectedFDs::instance())
#define PFD(i) (PROTECTED_FD_START + (i))
#define PROTECTEDFD(i) PFD(i)
/*
   The values of PFD(5) and PFD(6) correspond to the values of DUP_STDERR_FD
   and DUP_LOG_FD in jassert.cpp. They should always be kept in sync.
*/
// DUP_STDERR_FD
#define PROTECTED_STDERR_FD PFD(5)
// DUP_LOG_FD
#define PROTECTED_JASSERTLOG_FD PFD(6)
#define PROTECTED_PIDTBL_FD PFD(8)
#define PROTECTED_PIDMAP_FD PFD(9)
#define PROTECTED_PIDMAPCNT_FD PFD(10)
#define PROTECTED_TMPDIR_FD PFD(11)

namespace dmtcp
{

  class ProtectedFDs
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      static ProtectedFDs& instance();
      static bool isProtected ( int fd );
    protected:
      ProtectedFDs();
    private:
//     bool _usageTable[PROTECTED_FD_COUNT];
  };

}

#endif
