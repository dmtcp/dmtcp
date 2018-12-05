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

#pragma once
#ifndef PTY_WRAPPERS_H
#define PTY_WRAPPERS_H

#include "dmtcp.h"

# define _real_xstat        NEXT_FNC(__xstat)
# define _real_xstat64      NEXT_FNC(__xstat64)
# define _real_lxstat       NEXT_FNC(__lxstat)
# define _real_lxstat64     NEXT_FNC(__lxstat64)
# define _real_readlink     NEXT_FNC(readlink)
# define _real_ptsname_r    NEXT_FNC(ptsname_r)
# define _real_ttyname_r    NEXT_FNC(ttyname_r)
# define _real_getpt        NEXT_FNC(getpt)
# define _real_posix_openpt NEXT_FNC(posix_openpt)
# define _real_access       NEXT_FNC(access)

// NOTE:  realpath is a versioned symbol, and we should be using
// NEXT_FNC.  But that interferes with libdl.so (e.g., dlopen).
// and other functions that use gettid() -> __tls_get_addr()
// for some unknown reason.
# define _real_realpath NEXT_FNC(realpath)
#endif // PTY_WRAPPERS_H
