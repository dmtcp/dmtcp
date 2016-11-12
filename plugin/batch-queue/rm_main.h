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

#include "dmtcp.h"
#include "dmtcpalloc.h"

#define _real_fork                 NEXT_FNC(fork)
#define _real_execve               NEXT_FNC(execve)
#define _real_execvp               NEXT_FNC(execvp)
#define _real_execvpe              NEXT_FNC(execvpe)
#define _real_open                 NEXT_FNC(open)
#define _real_close                NEXT_FNC(close)
#define _real_dup                  NEXT_FNC(dup)
#define _real_dup2                 NEXT_FNC(dup2)
#define _real_fcntl                NEXT_FNC(fcntl)
#define _real_pthread_mutex_lock   NEXT_FNC(pthread_mutex_lock)
#define _real_pthread_mutex_unlock NEXT_FNC(pthread_mutex_unlock)
#define _real_dlopen               NEXT_FNC(dlopen)
#define _real_dlsym                NEXT_FNC(dlsym)
#define _real_system               NEXT_FNC(system)

#define _real_socket               NEXT_FNC(socket)
#define _real_connect              NEXT_FNC(connect)
#define _real_bind                 NEXT_FNC(bind)

namespace dmtcp
{
// General
bool runUnderRMgr();
enum rmgr_type_t { Empty, None, torque, sge, lsf, slurm };

rmgr_type_t _get_rmgr_type();
void _set_rmgr_type(rmgr_type_t nval);

void _rm_clear_path(string &path);
void _rm_del_trailing_slash(string &path);

enum ResMgrFileType {
  TORQUE_IO,
  TORQUE_NODE,
  SLURM_TMPDIR
};
}

#define FWD_TO_DEV_NULL(fd)                                        \
  {                                                                \
    int tmp = open("/dev/null", O_CREAT | O_RDWR | O_TRUNC, 0666); \
    if (tmp >= 0 && tmp != fd) {                                   \
      dup2(tmp, fd);                                               \
      close(tmp);                                                  \
    }                                                              \
  }

#define CHECK_FWD_TO_DEV_NULL(fd)   \
  {                                 \
    if (fcntl(fd, F_GETFL) == -1) { \
      FWD_TO_DEV_NULL(fd)           \
    }                               \
  }

#define DMTCP_SRUN_HELPER_ADDR_ENV "DMTCP_SRUN_HELPER_ADDR"
#define DMTCP_SRUN_HELPER_SYNC_ENV "DMTCP_SRUN_HELPER_SYNCFILE"
#endif // ifndef RESOURCE_MANAGER_H
