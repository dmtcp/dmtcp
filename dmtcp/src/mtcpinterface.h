/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef DMTCPMTCPINTERFACE_H
#define DMTCPMTCPINTERFACE_H

#include <sys/types.h>

namespace dmtcp
{
  void __attribute__ ((weak)) initializeMtcpEngine();
  void killCkpthread();

  void shutdownMtcpEngineOnFork();

  //these next two are defined in dmtcpawareapi.cpp
  void userHookTrampoline_preCkpt();
  void userHookTrampoline_postCkpt(bool isRestart);
}

extern "C" {
  // Funtion declaration for functions defined in libmtcp.so.
  void *mtcp_prepare_for_clone(int (*fn) (void *arg), void *child_stack,
                               int *flags, void *arg, int *parent_tidptr,
                               struct user_desc *newtls, int **child_tidptr);
  void mtcp_thread_start(void *threadv);
  void mtcp_threadiszombie (void);
  void mtcp_kill_ckpthread (void);
}

#endif
