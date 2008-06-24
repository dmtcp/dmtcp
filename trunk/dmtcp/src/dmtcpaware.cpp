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

#include <stdio.h>

#include "dmtcpaware.h"

// this is a dummy library that is only called if dmtcp is *NOT* enabled
// the versions in dmtcpapi.cpp will be called if dmtcp is enabled


extern "C" int dmtcpIsEnabled() { return 0; }

extern "C" int dmtcpRunCommand(char command){
  fprintf(stderr, "dmtcpRunCommand: ERROR, program is not running under dmtcp_checkpoint.\n");
  return -128;
}

extern "C"  DmtcpCoordinatorStatus dmtcpGetStatus(){
  fprintf(stderr, "dmtcpGetStatus: ERROR, program is not running under dmtcp_checkpoint.\n");
  DmtcpCoordinatorStatus tmp;
  tmp.numProcesses = -1;
  tmp.isRunning = -1;
  return tmp;
}
