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
#ifndef SYSVIPC_WRAPPERS_H
#define SYSVIPC_WRAPPERS_H

#include "dmtcp.h"

# define _real_shmget               NEXT_FNC(shmget)
# define _real_shmat                NEXT_FNC(shmat)
# define _real_shmdt                NEXT_FNC(shmdt)
# define _real_shmctl               NEXT_FNC(shmctl)

# define _real_semget               NEXT_FNC(semget)
# define _real_semctl               NEXT_FNC(semctl)
# define _real_semop                NEXT_FNC(semop)
# define _real_semtimedop           NEXT_FNC(semtimedop)

# define _real_msgget               NEXT_FNC(msgget)
# define _real_msgctl               NEXT_FNC(msgctl)
# define _real_msgsnd               NEXT_FNC(msgsnd)
# define _real_msgrcv               NEXT_FNC(msgrcv)

# define _real_dlsym                NEXT_FNC(dlsym)
#endif // ifndef SYSVIPC_WRAPPERS_H
