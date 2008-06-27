/***************************************************************************
 *   Copyright (C) 2008 by Jason Ansel                                     *
 *   jansel@ccs.neu.edu                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "dmtcpaware.h"

#include <stdio.h>
#include <string.h>

//
// this is a dummy library that is only called if dmtcp is *NOT* enabled
//
// the versions in dmtcpapi.cpp will be called if dmtcp is enabled
//

#ifdef DMTCPAWARE_NO_WARNINGS
# define WARN_NO_DMTCP_MSG ""
# define WARN_NO_DMTCP
#else
# define WARN_NO_DMTCP_MSG "%s: ERROR, program is not running under dmtcp_checkpoint.\n"
# define WARN_NO_DMTCP fprintf(stderr, WARN_NO_DMTCP_MSG , __FUNCTION__)
#endif

#ifndef EXTERNC
# define EXTERNC extern "C"
#endif

EXTERNC int dmtcpIsEnabled() { 
  return 0;
}

EXTERNC int dmtcpCheckpointBlocking(){
  WARN_NO_DMTCP;
  return -128;
}

EXTERNC int dmtcpRunCommand(char command){
  WARN_NO_DMTCP;
  return -128;
}

EXTERNC const DmtcpCoordinatorStatus* dmtcpGetCoordinatorStatus(){
  WARN_NO_DMTCP;
  return NULL;
}

EXTERNC const DmtcpLocalStatus* dmtcpGetLocalStatus(){
  WARN_NO_DMTCP;
  return NULL;
}

EXTERNC int dmtcpInstallHooks( DmtcpFunctionPointer preCheckpoint
                              , DmtcpFunctionPointer postCheckpoint
                              , DmtcpFunctionPointer postRestart){
  WARN_NO_DMTCP;
  return -128;
}

EXTERNC int dmtcpDelayCheckpointsLock(){
  WARN_NO_DMTCP;
  return -128;
}

EXTERNC int dmtcpDelayCheckpointsUnlock(){
  WARN_NO_DMTCP;
  return -128;
}


