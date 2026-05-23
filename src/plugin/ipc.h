/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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
#ifndef DMTCP_IPC_H
#define DMTCP_IPC_H

#include <dirent.h>
#include <linux/version.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include "dmtcp.h"

# define CONNECTION_ID_START        99000

# define DEV_ZERO_DELETED_STR       "/dev/zero (deleted)"
# define DEV_NULL_DELETED_STR       "/dev/null (deleted)"

# define DRAINER_CHECK_FREQ         0.1
# define DRAINER_WARNING_FREQ       10

# define _real_socket               NEXT_FNC(socket)
# define _real_bind                 NEXT_FNC(bind)
# define _real_close                NEXT_FNC(close)
# define _real_fclose               NEXT_FNC(fclose)
# define _real_closedir             NEXT_FNC(closedir)
# define _real_dup                  NEXT_FNC(dup)
# define _real_dup2                 NEXT_FNC(dup2)
# if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) && __GLIBC_PREREQ(2, 9)
#  define _real_dup3                NEXT_FNC(dup3)
# endif // if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) &&
        // __GLIBC_PREREQ(2, 9)

# define _real_fcntl                NEXT_FNC(fcntl)
# define _real_select               NEXT_FNC(select)
# define _real_poll                 NEXT_FNC(poll)
#endif // ifndef DMTCP_IPC_H
