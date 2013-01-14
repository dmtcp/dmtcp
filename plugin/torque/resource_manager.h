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

// This file was originally contributed
// by Artem Y. Polyakov <artpol84@gmail.com>.

#ifndef RESOURCE_MANAGER_H
#define RESOURCE_MANAGER_H

#include "dmtcpalloc.h"
#include "dmtcpplugin.h"

#define _real_fork NEXT_FNC(fork)
#define _real_execvp NEXT_FNC(execvp)
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

// Torque API

int findLibTorque(dmtcp::string &libpath);
bool isTorqueFile(dmtcp::string relpath, dmtcp::string &path);
bool isTorqueIOFile(dmtcp::string &path);
bool isTorqueNodeFile(dmtcp::string &path);
bool isTorqueStdout(dmtcp::string &path);
bool isTorqueStderr(dmtcp::string &path);

#endif
