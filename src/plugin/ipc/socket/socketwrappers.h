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
#ifndef SOCKET_WRAPPERS_H
#define SOCKET_WRAPPERS_H

#include "dmtcp.h"

# define _real_socket        NEXT_FNC(socket)
# define _real_connect       NEXT_FNC(connect)
# define _real_bind          NEXT_FNC(bind)
# define _real_listen        NEXT_FNC(listen)
# define _real_accept        NEXT_FNC(accept)
# define _real_accept4       NEXT_FNC(accept4)
# define _real_setsockopt    NEXT_FNC(setsockopt)
# define _real_getsockopt    NEXT_FNC(getsockopt)
# define _real_socketpair    NEXT_FNC(socketpair)
# define _real_close         NEXT_FNC(close)
# define _real_getaddrinfo   NEXT_FNC(getaddrinfo)
# define _real_getnameinfo   NEXT_FNC(getnameinfo)
# define _real_gethostbyname NEXT_FNC(gethostbyname)
# define _real_gethostbyaddr NEXT_FNC(gethostbyaddr)
# define _real_poll          NEXT_FNC(poll)
#endif // SOCKET_WRAPPERS_H
