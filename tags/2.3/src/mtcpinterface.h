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

#ifndef DMTCPMTCPINTERFACE_H
#define DMTCPMTCPINTERFACE_H

#include <sys/types.h>

namespace dmtcp
{
  void initializeMtcpEngine();
  void killCkpthread();

  //these next two are defined in dmtcpplugin.cpp
  void userHookTrampoline_preCkpt();
  void userHookTrampoline_postCkpt(bool isRestart);

  void callbackSleepBetweenCheckpoint(int sec);
  void callbackPreCheckpoint();
  void callbackPostCheckpoint(int isRestart, char* mtcpRestoreArgvStartAddr);
  void callbackPreSuspendUserThread();
  void callbackPreResumeUserThread(int isRestart);
  void callbackHoldsAnyLocks(int *retval);
  void prepareForCkpt();
}
#endif
