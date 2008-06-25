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
#include "dmtcpworker.h"
#include "dmtcpmessagetypes.h"
#include "syscallwrappers.h"
#include <string>



static const dmtcp::DmtcpMessage * exampleMessage = NULL;
static int result[sizeof(exampleMessage->params)/sizeof(int)];

extern "C" int dmtcpIsEnabled() { return 1; }

extern "C" int dmtcpRunCommand(char command){
  _dmtcp_lock();
  dmtcp::DmtcpWorker worker(false);
  worker.useNormalCoordinatorFd();
  worker.connectAndSendUserCommand(command, result);
  _dmtcp_unlock();
  return result[0]>=0;
}

extern "C"  DmtcpCoordinatorStatus dmtcpGetStatus(){
  DmtcpCoordinatorStatus tmp;
  _dmtcp_lock();
  dmtcp::DmtcpWorker worker(false);
  worker.useNormalCoordinatorFd();
  worker.connectAndSendUserCommand('s', result);
  tmp.numProcesses = result[0];
  tmp.isRunning = result[1];
  _dmtcp_unlock();
  return tmp;
}

extern "C" const char* dmtcpGetCheckpointFilenameMtcp(){
  static std::string str;
  str=dmtcp::UniquePid::checkpointFilename();
  return str.c_str();
}

extern "C" const char* dmtcpGetCheckpointFilenameDmtcp(){
  static std::string str;
  str=dmtcp::UniquePid::dmtcpCheckpointFilename();
  return str.c_str();
}

extern "C" const char* dmtcpGetUniquePid(){
  static std::string str;
  str=dmtcp::UniquePid::ThisProcess().toString();
  return str.c_str();
}

