/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef __WORKER_STATE_H__
#define __WORKER_STATE_H__

#include "dmtcpalloc.h"

namespace dmtcp
{
namespace WorkerState
{
enum eWorkerState {
  UNKNOWN,
  RUNNING,
  PRESUSPEND,
  SUSPENDED,
  CHECKPOINTING,
  CHECKPOINTED,
  RESTARTING,
  _MAX
};

void setCurrentState(const eWorkerState &value);
eWorkerState currentState();

ostream&operator<<(ostream &o, const eWorkerState &s);
} // namespace WorkerState
} // namespace dmtcp
#endif // #ifndef __WORKER_STATE_H__
