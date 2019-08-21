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
#ifndef TIMER_WRAPPERS_H
#define TIMER_WRAPPERS_H

#include <signal.h>
#include <time.h>
#include "dmtcp.h"

# define _real_timer_create           NEXT_FNC(timer_create)
# define _real_timer_delete           NEXT_FNC(timer_delete)
# define _real_timer_gettime          NEXT_FNC(timer_gettime)
# define _real_timer_settime          NEXT_FNC(timer_settime)
# define _real_timer_getoverrun       NEXT_FNC(timer_getoverrun)

# define _real_clock_getcpuclockid   NEXT_FNC(clock_getcpuclockid)
# define _real_pthread_getcpuclockid NEXT_FNC(pthread_getcpuclockid)
# define _real_clock_getres          NEXT_FNC(clock_getres)
# define _real_clock_gettime         NEXT_FNC(clock_gettime)
# define _real_clock_settime         NEXT_FNC(clock_settime)

int timer_create_sigev_thread(clockid_t clock_id,
                              struct sigevent *evp,
                              timer_t *timerid,
                              struct sigevent *sevOut);
#endif // ifndef TIMER_WRAPPERS_H
