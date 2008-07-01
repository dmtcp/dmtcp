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

/// Return value of dmtcpCheckpoint
#define DMTCP_AFTER_CHECKPOINT 1
/// Return value of dmtcpCheckpoint
#define DMTCP_AFTER_RESTART    2 

/// Returned when DMTCP is disabled, unless stated otherwise
#define DMTCP_ERROR_DISABLED -128

/// Pointer to a "void foo();" function
typedef void (*DmtcpFunctionPointer)(void);

/// Returned by dmtcpGetCoordinatorStatus()
typedef struct _DmtcpCoordinatorStatus {

  /// Number of processes connected to dmtcp_coordinator
  int numProcesses;

  /// 1 if all processes connected to dmtcp_coordinator are in a running state
  int isRunning;

} DmtcpCoordinatorStatus;

/// Returned by dmtcpGetLocalStatus()
typedef struct _DmtcpLocalStatus {

  /// The number of times this process has been checkpointed (excludes restarts)
  int numCheckpoints;

  /// The number of times this process has been restarted
  int numRestarts;

  /// Filename of (large) .mtcp checkpoint file (memory/threads) for this process
  const char* checkpointFilenameMtcp; 

  /// Filename of (tiny) .dmtcp checkpoint file (connection table) for this process
  const char* checkpointFilenameDmtcp;

  /// The DMTCP cluster-wide unique process identifier for this process.
  /// Format is "HostHash-PID-Timestamp"
  const char* uniquePidStr;

} DmtcpLocalStatus;


/**
 * Returns 1 if executing under dmtcp_checkpoint, 0 otherwise
 */
int dmtcpIsEnabled();

/**
 * Checkpoint the entire distributed computation, block until checkpoint is
 * complete.
 * - returns DMTCP_AFTER_CHECKPOINT if the checkpoint succeeded.
 * - returns DMTCP_AFTER_RESTART    after a restart.
 * - returns <=0 on error.
 */
int dmtcpCheckpoint();

/**
 * Prevent a checkpoint from starting until dmtcpDelayCheckpointsUnlock() is
 * called.
 * - Has (recursive) lock semantics, only one thread may acquire it at time.
 * - Only prevents checkpoints locally, remote processes may be suspended.
 *   Thus, send or recv to another checkpointed process may create deadlock.
 * - Returns 1 on success, <=0 on error
 */
int dmtcpDelayCheckpointsLock();

/**
 * Re-allow checkpoints, opposite of dmtcpDelayCheckpointsLock().
 * - Returns 1 on success, <=0 on error
 */
int dmtcpDelayCheckpointsUnlock();

/**
 * Sets the hook functions that DMTCP calls when it checkpoints/restarts. 
 * - These functions are called from the DMTCP thread while all user threads
 *   are suspended.
 * - First preCheckpoint() is called, then either postCheckpoint() or
 *   postRestart() is called.
 * - Set to NULL to disable.
 * - Returns 1 on success, <=0 on error
 */
int dmtcpInstallHooks( DmtcpFunctionPointer preCheckpoint
                      , DmtcpFunctionPointer postCheckpoint
                      , DmtcpFunctionPointer postRestart);

/**
 * Gets the coordinator-specific status of DMTCP.
 * - Calling this function invalidates older DmtcpCoordinatorStatus structures.
 * - Returns NULL on error.
 */
const DmtcpCoordinatorStatus* dmtcpGetCoordinatorStatus();

/**
 * Gets the local-node-specific status of DMTCP.
 * - Calling this function invalidates older DmtcpLocalStatus structures.
 * - Returns NULL on error.
 */
const DmtcpLocalStatus* dmtcpGetLocalStatus();

/**
 * Send a command to the dmtcp_coordinator as if it were typed on the console.
 * - Returns 1 if command was sent and well-formed, <= 0 otherwise.
 */
int dmtcpRunCommand(char command);

#ifdef __cplusplus
} //extern "C"
#endif

#endif //DMTCPAWARE_H
