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

#ifndef THREADLIST_H
#define THREADLIST_H

#include <signal.h>
#include <sys/types.h>
#include <ucontext.h>
#include "threadinfo.h"

namespace dmtcp
{
namespace ThreadList
{
pid_t _real_pid();
pid_t _real_tid();
int _real_tgkill(pid_t tgid, pid_t tid, int sig);

void init();
void initThread(Thread *th, int (*fn)(
                  void *), void *arg, int flags, int *ptid, int *ctid);
void updateTid(Thread *);
void resetOnFork();
void threadExit();

Thread *getNewThread();
void addToActiveList(Thread *th);
void threadIsDead(Thread *thread);
void emptyFreeList();

void suspendThreads();
void resumeThreads();
void waitForAllRestored(Thread *thisthread);
void writeCkpt();
void postRestart(double readTime = 0.0);
void postRestartDebug(double readTime, int restartPause);
}
}
#endif // ifndef THREADLIST_H
