/****************************************************************************
 *   Copyright (C) 2006-2012 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef THREADSYNC_H
#define THREADSYNC_H

#include "dmtcpworker.h"
#include "wrapperlock.h"

struct Thread;

namespace dmtcp
{

namespace ThreadSync
{
void acquireLocks();
void releaseLocks();
void resetLocks(bool resetPresuspendEventHookLock = true);
void initMotherOfAll();

void wrapperExecutionLockLock();
void wrapperExecutionLockUnlock();
void wrapperExecutionLockLockExcl();
bool wrapperExecutionLockLockForNewThread();
void wrapperExecutionLockUnlockForNewThread();

bool libdlLockLock();
void libdlLockUnlock();

void presuspendEventHookLockLock();
void presuspendEventHookLockUnlock();
}

}
#endif // ifndef THREADSYNC_H
