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

#ifndef DMTCPAWARE_H
#define DMTCPAWARE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _DmtcpCoordinatorStatus {
  int numProcesses; //number of processes connected to dmtcp_coordinator
  int isRunning;    //1 if all processes in the computation are in a running state
} DmtcpCoordinatorStatus;

/**
 * Returns 1 if executing under dmtcp_checkpoint, 0 otherwise
 */
int dmtcpIsEnabled();

/**
 * Send a command to the dmtcp_coordinator as if it were typed on the console
 *
 * Can only be called if dmtcpIsEnabled()==1
 * Returns 1 if the command succeeds, < 0 otherwise
 */
int dmtcpRunCommand(char command);

/**
 * Gets the status of the computation according to coordinator
 */
DmtcpCoordinatorStatus dmtcpGetStatus();

//alias for ease of use
#define dmtcpRunCommandCheckpoint() dmtcpRunCommand('c') 

#ifdef __cplusplus
} //extern "C"
#endif

#endif //DMTCPAWARE_H
