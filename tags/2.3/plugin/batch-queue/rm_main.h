/****************************************************************************
 *  Copyright (C) 2012-2014 by Artem Y. Polyakov <artpol84@gmail.com>       *
 *                                                                          *
 *  This file is part of the RM plugin for DMTCP                            *
 *                                                                          *
 *  RM plugin is free software: you can redistribute it and/or              *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  RM plugin is distributed in the hope that it will be useful,            *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#ifndef RESOURCE_MANAGER_H
#define RESOURCE_MANAGER_H

#include "dmtcpalloc.h"
#include "dmtcp.h"

#define _real_fork NEXT_FNC(fork)
#define _real_execve NEXT_FNC(execve)
#define _real_execvp NEXT_FNC(execvp)
#define _real_execvpe NEXT_FNC(execvpe)
#define _real_open NEXT_FNC(open)
#define _real_close NEXT_FNC(close)
#define _real_dup NEXT_FNC(dup)
#define _real_dup2 NEXT_FNC(dup2)
#define _real_pthread_mutex_lock NEXT_FNC(pthread_mutex_lock)
#define _real_pthread_mutex_unlock NEXT_FNC(pthread_mutex_unlock)
#define _real_dlopen NEXT_FNC(dlopen)
#define _real_dlsym NEXT_FNC(dlsym)
#define _real_system NEXT_FNC(system)

// General
bool runUnderRMgr();
enum rmgr_type_t { Empty, None, torque, sge, lsf, slurm };

rmgr_type_t _get_rmgr_type();
void _set_rmgr_type(rmgr_type_t nval);

void _rm_clear_path(dmtcp::string &path);
void _rm_del_trailing_slash(dmtcp::string &path);

enum ResMgrFileType
{
  TORQUE_IO,
  TORQUE_NODE,
  SLURM_TMPDIR
};

#endif
